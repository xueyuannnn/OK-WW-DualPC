#include "runtime/execution.hpp"
#include "runtime/input.hpp"
#include "runtime/combat_rules.hpp"
#include "reference_execution.hpp"
#include "reference_input.hpp"
#include "rotation_replay.hpp"
#include "task_context_replay.hpp"
#include "reference_task_text.hpp"
#include "task_image_replay.hpp"
#include <QtTest>
using namespace okww;
namespace {
bool originalCombatWait(runtime::CombatHost &host,const std::function<bool()> &condition,double timeout,
                        const std::function<void()> &post,double settle,const std::function<void()> &pre) {
    if(timeout<0 || !std::isfinite(timeout) || !std::isfinite(settle) || settle<0)fail("战斗等待时间无效");
    const double deadline=host.now()+(timeout==0?10:timeout);
    double settled=-1;
    do {
        if(pre)pre();
        host.nextFrame();
        if(condition()) {
            if(settle==0)return true;
            if(settled>=0 && host.now()-settled>settle)return true;
            if(settled<0)settled=host.now();
            continue;
        }
        settled=-1;
        if(post)post();
        if(host.now()>deadline)return false;
    }while(true);
}
QJsonObject combatWaitTrace(bool rust,int scenario) {
    runtime::test::RotationReplay host({{"stop_at",112},{"ready",QJsonArray{false,false,false}}});
    int conditions=0;
    const auto pre=[&]{host.event("pre","");if(scenario==8)throw std::runtime_error("pre failed");};
    const auto post=[&]{host.event("post","");if(scenario==9)throw std::runtime_error("post failed");host.click();};
    const auto condition=[&] {
        host.event("condition",QString::number(++conditions));
        if(scenario==10)throw std::runtime_error("condition failed");
        return scenario!=1 && conditions>2 && (scenario!=5 || conditions%3!=0);
    };
    const double timeout=scenario==2?0:scenario==3?-1:scenario==4?std::numeric_limits<double>::quiet_NaN():.4;
    const double settle=scenario==6?.05:scenario==5?.09:scenario==7?-.1:0;
    QJsonObject result;
    try{result["result"]=rust?host.wait(condition,timeout,post,settle,pre):originalCombatWait(host,condition,timeout,post,settle,pre);}
    catch(const std::exception &error){result["error"]=errorText(error);}
    result["events"]=host.events;result["time"]=host.now();return result;
}
struct TraceClock final : runtime::Clock, reference::Clock {
    double time=0;
    mutable QJsonArray events;
    std::function<void()> tick;
    double now() const override {events.append(QJsonArray{"now",time});return time;}
    void sleep(double seconds) override {events.append(QJsonArray{"sleep",seconds});time+=seconds;if(tick)tick();}
};
template<class Gate,class Execution> QJsonObject trace(int scenario) {
    TraceClock clock;Gate gate;const auto epoch=gate.start();int checks=0;
    Execution execution(gate,epoch,clock,[&] {
        clock.events.append(QJsonArray{"checkpoint",clock.time});++checks;
        if(scenario==6)throw std::runtime_error("health failure");
        if(scenario==17)gate.stop();
        if(scenario==18 && checks==1){gate.stop();gate.start();}
    });
    if(scenario==7)gate.stop();
    clock.tick=[&] {
        if(scenario==8 && clock.time>=.025)gate.stop();
        if(scenario==19)throw std::runtime_error("sleep failure");
    };
    const auto condition=[&] {
        clock.events.append(QJsonArray{"condition",clock.time});
        if(scenario==5)throw std::runtime_error("condition failure");
        return scenario==4 || scenario==16 || (scenario==2 && clock.time>=.065);
    };
    QJsonObject out;
    try {
        if(scenario==0)execution.wait(0);
        else if(scenario==1)execution.wait(.075);
        else if(scenario>=2 && scenario<=5)out["until"]=execution.until(condition,scenario==4?0:.075,.02);
        else if(scenario==9)execution.wait(-1);
        else if(scenario==10)execution.wait(std::numeric_limits<double>::quiet_NaN());
        else if(scenario==11)execution.wait(std::numeric_limits<double>::infinity());
        else if(scenario==12)execution.wait(86401);
        else if(scenario==13)out["until"]=execution.until(condition,-1);
        else if(scenario==14)out["until"]=execution.until(condition,1,0);
        else if(scenario==15 || scenario==16)out["until"]=execution.until(condition,1,std::numeric_limits<double>::quiet_NaN());
        else execution.wait(.1);
    } catch(const std::exception &error) {out["error"]=errorText(error);}
    out["events"]=clock.events;out["time"]=clock.time;out["active"]=gate.active(epoch);
    return out;
}
struct SocketTrace {
    int64_t time=0;
    QVector<QByteArray> replies;
    QJsonArray events;
    int sent=0,received=0,sendResult=16,waitResult=1;
    rust::TransportHost host() {
        return {this,
            [](void *value)->int64_t {auto &s=*static_cast<SocketTrace *>(value);s.events.append("now");return s.time;},
            [](void *value,const uint8_t *,size_t)->int32_t {auto &s=*static_cast<SocketTrace *>(value);++s.sent;s.events.append("send");return s.sendResult;},
            [](void *value,int64_t remaining)->int32_t {
                auto &s=*static_cast<SocketTrace *>(value);s.events.append(QJsonArray{"readable",double(remaining)});
                if(s.received<s.replies.size()){s.time+=1000;return s.waitResult;}
                s.time+=remaining;return 0;
            },
            [](void *value,uint8_t *bytes,size_t capacity)->int32_t {
                auto &s=*static_cast<SocketTrace *>(value);s.events.append("receive");
                const auto &reply=s.replies[s.received++];
                const auto size=std::min(capacity,size_t(reply.size()));memcpy(bytes,reply.constData(),size);return int32_t(size);
            }};
    }
};
int originalExchange(const rust::TransportHost &h,const QByteArray &packet,int64_t timeout,uint32_t seq,uint32_t command) {
    if(h.send(h.context,reinterpret_cast<const uint8_t *>(packet.constData()),size_t(packet.size()))!=packet.size())return -1;
    const auto deadline=h.now(h.context)+timeout;
    while(h.now(h.context)<deadline) {
        const auto remaining=std::max<int64_t>(1,deadline-h.now(h.context));
        if(h.readable(h.context,remaining)<=0)break;
        uint8_t bytes[2048];const auto size=h.receive(h.context,bytes,sizeof(bytes));
        if(size<16)continue;
        if(qFromLittleEndian<uint32_t>(bytes+8)==seq && qFromLittleEndian<uint32_t>(bytes+12)==command)return 1;
    }
    return 0;
}
}
class RustCoreTests : public QObject {
    Q_OBJECT
private slots:
    void taskImagesAndInputKeepFrozenContextBehavior_data(){
        QTest::addColumn<int>("scene");for(int scene=0;scene<320;++scene)QTest::newRow(qPrintable(QString::number(scene)))<<scene;
    }
    void taskImagesAndInputKeepFrozenContextBehavior(){
        QFETCH(int,scene);
        const auto expected=runtime::test::taskImageTrace<runtime::reference_context::TaskContext>(scene);
        const auto actual=runtime::test::taskImageTrace<runtime::TaskContext>(scene);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void taskTextRulesMatchOriginal_data(){
        QTest::addColumn<int>("scenario");
        for(int scenario=0;scenario<256;++scenario)QTest::newRow(qPrintable(QString::number(scenario)))<<scenario;
    }
    void taskTextRulesMatchOriginal(){
        QFETCH(int,scenario);
        const QStringList choices{"点击领取今日月相","點擊領取本日月卡","CLAIM Daily Lunite","Claim Today Subscription",
            "剩余10天","剩餘1天","remaining 12 days","2 days remaining","获得奖励","獲得物品：","Rewards Obtained",
            "点击空白处","點擊空白","click anywhere","tap anywhere","click any","where","+86","登录","开始游戏",""};
        QVector<runtime::TextLine> lines;
        for(int index=0;index<7;++index){
            auto text=choices[(scenario+index*7)%choices.size()];
            if(scenario&1)text=" \t"+text+"\n";
            if(scenario&2)text.insert(text.size()/2,QChar(0x00a0));
            lines.append({text,{index*10,index*11,100,30},.9});
        }
        QCOMPARE(runtime::monthlyTextConfirmed(lines),runtime::reference_task_text::monthlyTextConfirmed(lines));
        QCOMPARE(runtime::rewardTextConfirmed(lines),runtime::reference_task_text::rewardTextConfirmed(lines));
        for(const auto &pattern:QStringList{"登录|Log","\\+86","","^.*$","获得.*","["}){
            const auto trace=[&](bool rust){
                QJsonObject result;
                try{
                    const auto found=rust?runtime::matchingText(lines,pattern):runtime::reference_task_text::matchingText(lines,pattern);
                    result["found"]=bool(found);if(found){result["text"]=found->text;result["x"]=found->box.x();result["y"]=found->box.y();}
                }catch(const std::exception &error){result["error"]=errorText(error);}
                return result;
            };
            QCOMPARE(trace(true),trace(false));
        }
    }
    void monthlyTimeBoundariesMatchOriginal(){
        for(int hour=0;hour<=25;++hour)for(int seconds:{0,29,30,31,59,60,89,90,86369,86370,86399}){
            const auto now=QDateTime(QDate(2030,1,2),QTime(0,0)).addSecs(seconds);
            const auto trace=[&](bool rust){
                QJsonObject result;
                try{result["key"]=rust?runtime::monthlyWindowKey(now,hour):runtime::reference_task_text::monthlyWindowKey(now,hour);}
                catch(const std::exception &error){result["error"]=errorText(error);}return result;
            };
            QCOMPARE(trace(true),trace(false));
        }
        QVERIFY_EXCEPTION_THROWN(runtime::monthlyWindowKey({},4),std::runtime_error);
    }
    void continueMarkerPixelsMatchOriginal_data(){
        QTest::addColumn<int>("scenario");
        for(int scenario=0;scenario<128;++scenario)QTest::newRow(qPrintable(QString::number(scenario)))<<scenario;
    }
    void continueMarkerPixelsMatchOriginal(){
        QFETCH(int,scenario);
        const QSize sizes[]{{640,360},{1280,720},{1920,1080},{2560,1440},{1600,900},{1920,1200}};
        const auto size=sizes[scenario%6];cv::Mat storage(size.height()+4,size.width()+8,CV_8UC3,cv::Scalar(13,23,31));
        auto image=storage(cv::Rect(3,2,size.width(),size.height()));
        const int x=size.width()/2,y=runtime::pixelRound(size.height()*.95),width=6+scenario%53,height=6+(scenario*7)%43;
        const cv::Scalar color= scenario&1?cv::Scalar(180,180,244):scenario&2?cv::Scalar(180,180,245):cv::Scalar(179,179,179);
        if(scenario%3==0)cv::rectangle(image,{x-width/2,y-height/2,width,height},color,-1);
        else if(scenario%3==1){const cv::Point points[]{{x,y-height/2},{x+width/2,y+height/2},{x-width/2,y+height/2}};cv::fillConvexPoly(image,points,3,color);}
        else cv::circle(image,{x,y},width/2,color,-1);
        QCOMPARE(runtime::hasContinueMarker(image),runtime::reference_task_text::hasContinueMarker(image));
    }
    void workflowProgressAndHistoryMatchOriginal_data(){
        QTest::addColumn<int>("scenario");
        for(int scenario=0;scenario<32;++scenario)QTest::newRow(qPrintable(QString::number(scenario)))<<scenario;
    }
    void workflowProgressAndHistoryMatchOriginal(){
        QFETCH(int,scenario);
        const auto expected=runtime::test::workflowTrace<runtime::reference_context::TaskContext>(scenario);
        const auto actual=runtime::test::workflowTrace<runtime::TaskContext>(scenario);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void taskContextPreservesRecoveryMonthlyAndParty_data(){
        QTest::addColumn<int>("scenario");
        for(int scenario=0;scenario<160;++scenario)QTest::newRow(qPrintable(QString::number(scenario)))<<scenario;
    }
    void taskContextPreservesRecoveryMonthlyAndParty(){
        QFETCH(int,scenario);
        const auto expected=runtime::test::taskContextTrace<runtime::reference_context::TaskContext>(scenario);
        const auto actual=runtime::test::taskContextTrace<runtime::TaskContext>(scenario);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void combatWaitPreservesSettlePrePostAndExceptionOrder_data() {
        QTest::addColumn<int>("scenario");
        for(int i=0;i<11;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;
    }
    void combatWaitPreservesSettlePrePostAndExceptionOrder() {
        QFETCH(int,scenario);
        QCOMPARE(combatWaitTrace(true,scenario),combatWaitTrace(false,scenario));
    }
    void executionMatchesOriginalEventOrder_data() {
        QTest::addColumn<int>("scenario");
        for(int i=0;i<20;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;
    }
    void executionMatchesOriginalEventOrder() {
        QFETCH(int,scenario);
        QCOMPARE((trace<runtime::ExecutionGate,runtime::Execution>(scenario)),
                 (trace<reference::ExecutionGate,reference::Execution>(scenario)));
    }
    void keyMappingMatchesOriginalIncludingRejectedKeys() {
        QStringList keys{"f0","f13","f+1","f 1","f1 ","f01","f-1","\tf1","CTRL_R","RETURN","SHIFT_R","中文","K","ı","İ","😀",""," ","F１２"};
        for(int c=1;c<256;++c)keys.append(QString(QChar(c)));
        for(const auto &key:keys) {
            QJsonObject oldResult,newResult;
            try{oldResult["code"]=reference::keyCode(key);}catch(const std::exception &e){oldResult["error"]=errorText(e);}
            try{newResult["code"]=runtime::Input::keyCode(key);}catch(const std::exception &e){newResult["error"]=errorText(e);}
            QVERIFY2(oldResult==newResult,qPrintable(key+": "+QString::fromUtf8(QJsonDocument(newResult).toJson(QJsonDocument::Compact))));
        }
    }
    void freezeCopiesHaveIndependentRustOwnershipAndMovedSourcesRemainUsable() {
        runtime::FreezeTimeline first;first.add(10,2,2,.1);
        auto copy=first;first.clear();QCOMPARE(copy.entries().size(),1);
        runtime::FreezeTimeline moved(std::move(copy));QCOMPARE(moved.entries().size(),1);
        QCOMPARE(copy.entries().size(),0);QCOMPARE(copy.elapsed(10,1),9.);
        copy.add(10,3,1,.1);QCOMPARE(copy.entries().size(),1);
        first=copy;copy.clear();QCOMPARE(first.entries().size(),1);
        first=std::move(moved);QCOMPARE(first.entries().size(),1);QCOMPARE(moved.entries().size(),0);
    }
    void networkReportBytesMatchTheOriginalProtocol() {
        QRandomGenerator random(0xAF3C2828);
        for(int i=0;i<5000;++i) {
            const auto uuid=random.generate(),nonce=random.generate(),seq=random.generate(),command=random.generate();
            std::array<uint8_t,16> bytes{};
            rust::okww_rs_packet_header(uuid,nonce,seq,command,bytes.data());
            QCOMPARE(qFromLittleEndian<uint32_t>(bytes.data()),uuid);
            QCOMPARE(qFromLittleEndian<uint32_t>(bytes.data()+4),nonce);
            QCOMPARE(qFromLittleEndian<uint32_t>(bytes.data()+8),seq);
            QCOMPARE(qFromLittleEndian<uint32_t>(bytes.data()+12),command);
            QVERIFY(rust::okww_rs_matching_reply(bytes.data(),bytes.size(),seq,command));
            QVERIFY(!rust::okww_rs_matching_reply(bytes.data(),bytes.size()-1,seq,command));
            QVERIFY(!rust::okww_rs_matching_reply(bytes.data(),bytes.size(),seq+1,command));
        }
    }
    void transportPreservesSingleSendAndMatchingReplyOrder_data() {
        QTest::addColumn<int>("scenario");
        for(int i=0;i<7;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;
    }
    void transportPreservesSingleSendAndMatchingReplyOrder() {
        QFETCH(int,scenario);
        const auto reply=[](uint32_t seq,uint32_t command) {
            QByteArray packet(16,'\0');qToLittleEndian(seq,packet.data()+8);qToLittleEndian(command,packet.data()+12);return packet;
        };
        SocketTrace a;
        if(scenario==0)a.replies={reply(7,8)};
        if(scenario==1)a.replies={QByteArray(3,'x'),reply(6,8),reply(7,9),reply(7,8)};
        if(scenario==3)a.sendResult=-1;
        if(scenario==4)a.sendResult=12;
        if(scenario==5){a.replies={reply(7,8)};a.waitResult=-1;}
        if(scenario==6)a.replies={reply(7,9)};
        SocketTrace b=a;auto oldHost=a.host(),newHost=b.host();QByteArray packet(16,'x');
        const auto oldResult=originalExchange(oldHost,packet,10000,7,8);
        const auto newResult=rust::okww_rs_transport_exchange(&newHost,reinterpret_cast<const uint8_t *>(packet.constData()),size_t(packet.size()),10000,7,8);
        QCOMPARE(newResult,oldResult);QCOMPARE(b.events,a.events);QCOMPARE(b.sent,1);QCOMPARE(b.received,a.received);
    }
    void latencyPercentilesMatchTheOriginalNearestRanks() {
        QRandomGenerator random(57);
        for(int count=1;count<=128;++count) {
            std::vector<double> old;
            for(int i=0;i<count;++i)old.push_back(random.generateDouble()*30.);
            auto now=old;std::sort(old.begin(),old.end());double output[2]{};
            QVERIFY(rust::okww_rs_latency_percentiles(now.data(),now.size(),output));
            QCOMPARE(output[0],old[size_t(std::ceil(.5*count))-1]);
            QCOMPARE(output[1],old[size_t(std::ceil(.95*count))-1]);
        }
    }
};
QTEST_GUILESS_MAIN(RustCoreTests)
#include "test_rust_core.moc"
