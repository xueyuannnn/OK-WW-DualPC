#include "runtime/character.hpp"
#include "reference_gauges.hpp"
#include "reference_forms.hpp"
#include "reference_spectro.hpp"
#include "reference_gauge_analysis.hpp"
#include "rotation_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
class ReadyPartner final : public Character {
    bool ready_;
  public:
    ReadyPartner(CombatHost &host,bool ready):Character(host,CharacterState{}),ready_(ready) {}
    bool partnerReady() override {return ready_;}
};
std::unique_ptr<Character> original(const QString &name,CombatHost &host,CharacterState state) {
    if(name=="Zhezhi")return std::make_unique<reference::Zhezhi>(host,std::move(state));
    if(name=="Lupa")return std::make_unique<reference::Lupa>(host,std::move(state));
    if(name=="Ciaccona")return std::make_unique<reference::Ciaccona>(host,std::move(state));
    if(name=="Hiyuki")return std::make_unique<reference::Hiyuki>(host,std::move(state));
    if(name=="Changli")return std::make_unique<reference::Changli>(host,std::move(state));
    if(name=="Rover")return std::make_unique<reference::Rover>(host,std::move(state));
    if(name=="Cartethyia")return std::make_unique<reference::Cartethyia>(host,std::move(state));
    if(name=="Carlotta")return std::make_unique<reference::Carlotta>(host,std::move(state));
    if(name=="Camellya")return std::make_unique<reference::Camellya>(host,std::move(state));
    if(name=="Phoebe")return std::make_unique<reference::Phoebe>(host,std::move(state));
    if(name=="Zani")return std::make_unique<reference::Zani>(host,std::move(state));
    fail("Unknown reference character");
}
QJsonObject replay(const QString &name,int scenario,bool rust) {
    QJsonObject config{{"ready",QJsonArray{bool(scenario&1),bool(scenario&2),bool(scenario&4)}},
                       {"forte",bool(scenario&8)}, {"forte_white",.4}, {"gauge",scenario%4},
                       {"con",scenario&16?1.:.4}, {"stop_at",125}, {"teammate","Other"},
                       {"levitator",bool(scenario&32)}, {"flying",true}, {"land_at",102},
                       {"features",QJsonObject{{"lupa_wolf_icon2",QJsonArray{100.4,101.4}},
                                                {"hiyuki_lib_forte",QJsonArray{100.2,102.}},
                                                {"has_target@box_target_enemy_long",QJsonArray{100.,104.}},
                                                {"has_target@target_box_long2",QJsonArray{100.,105.}}}}};
    if(scenario&64)config["cancel_at"]=100.35;
    config["element"]=scenario%6;
    if(name=="Carlotta"){
        config["task_id"]=scenario&32?"FarmEchoTask":"AutoCombatTask";
        if(scenario&16)config["party"]=QJsonArray{QJsonObject{{"class","Zhezhi"},{"name","char_zhezhi"},{"role",1},{"buff_at",98},{"switch_at",99}}};
    }
    if(name=="Cartethyia"){
        config["features"]=QJsonObject{{"forte_cartethyia_sword1",bool(scenario&1)},
            {"forte_cartethyia_sword2",bool(scenario&2)},{"forte_cartethyia_sword3",bool(scenario&4)},
            {"lib_cartethyia_big",bool(scenario&8)}};
    }
    if(name=="Ciaccona")config["party"]=QJsonArray{
        QJsonObject{{"class",scenario&32?"Cartethyia":"Phoebe"},{"name","char_partner"},{"role",1},{"buff_at",98},{"switch_at",99}},
        QJsonObject{{"class","Verina"},{"role",2},{"buff_at",98},{"switch_at",99}}};
    if(name=="Phoebe" && (scenario&32))config["party"]=QJsonArray{
        QJsonObject{{"class","Zani"},{"name","char_zani"},{"role",1},{"buff_at",98},{"switch_at",99}}};
    if(name=="Zani" && (scenario&32))config["party"]=QJsonArray{
        QJsonObject{{"class","Phoebe"},{"name","char_phoebe"},{"role",1},{"buff_at",98},{"switch_at",99}}};
    test::RotationReplay host(config);
    CharacterState state;state.name=name;state.className=name;state.index=0;state.current=true;state.role=Role(scenario%3);
    auto actor=rust?makeCharacter(name,host,state):original(name,host,state);
    host.character=actor.get();
    ReadyPartner partner(host,scenario&32);
    actor->setPartner(&partner);
    QString error;
    try {
        for(int step=0;step<3;++step) {
            if(step==2)actor->resetState();
            actor->state.current=true;
            actor->state.hasIntro=(step%2)==0;
            actor->state.hasSubDpsIntro=true;
            host.observe(config);
            actor->perform();
            host.event("priority",QString::number(actor->switchPriority(host.teammate(),true,false)));
            host.event("outro",actor->inOutro()?"1":"0");
            if(step==1)actor->combatEnd();
            actor->switchOut(step==0);
            host.sleep(.3);
        }
        actor->combatEnd();
    }catch(const std::exception &failure){error=errorText(failure);}
    QStringList keys(host.heldKeys.begin(),host.heldKeys.end());keys.sort();
    QJsonArray freezes;
    for(const auto &entry:host.freezes.entries())freezes.append(QJsonArray{entry.start,entry.duration,entry.threshold});
    return {{"events",host.events},{"time",host.now()},{"error",error},{"held",host.held},
            {"keys",QJsonArray::fromStringList(keys)},{"liberation",host.inLiberation},{"skip",host.skipCombatCheck},
            {"last_resonance",actor->state.lastResonance},{"last_echo",actor->state.lastEcho},
            {"last_liberation",actor->state.lastLiberation},{"last_switch",actor->state.lastSwitch},
            {"intro",actor->state.hasIntro},{"sub_intro",actor->state.hasSubDpsIntro},{"con",actor->state.currentCon},
            {"intro_motion",actor->introMotion},{"freezes",freezes},
            {"role",int(actor->state.role)},{"element",actor->state.ringIndex}};
}
bool equal(const QJsonValue &a,const QJsonValue &b) {
    if(a.type()!=b.type())return false;
    if(a.isDouble())return std::abs(a.toDouble()-b.toDouble())<1e-9;
    if(a.isArray()) {
        const auto x=a.toArray(),y=b.toArray();if(x.size()!=y.size())return false;
        for(int i=0;i<x.size();++i)if(!equal(x[i],y[i]))return false;
        return true;
    }
    if(a.isObject()) {
        const auto x=a.toObject(),y=b.toObject();if(x.keys()!=y.keys())return false;
        for(auto it=x.begin();it!=x.end();++it)if(!equal(it.value(),y[it.key()]))return false;
        return true;
    }
    return a==b;
}
}
class RustCharacterTests final : public QObject {
    Q_OBJECT
  private slots:
    void gaugePixelsMatchOriginal_data(){
        QTest::addColumn<int>("scenario");
        for(int scenario=0;scenario<640;++scenario)QTest::newRow(qPrintable(QString::number(scenario)))<<scenario;
    }
    void gaugePixelsMatchOriginal(){
        QFETCH(int,scenario);
        const int widths[]{0,8,63,64,65,96,131,256,527,546,736,1024};
        const int width=widths[scenario%12],height=1+scenario%11;
        cv::Mat storage(height,width+17,CV_8UC3,cv::Scalar(0,0,0));
        auto image=storage(cv::Rect(3,0,width,height));
        uint32_t random=uint32_t(scenario+1)*2654435761U;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            random=random*1664525U+1013904223U;
            const bool white=scenario%5==0?(x%12<5):scenario%5==1?(x<width/2 && x%9<4):scenario%5==2?((random>>24)>120):scenario%5==3?false:true;
            image.at<cv::Vec3b>(y,x)=white?cv::Vec3b(245,245,245):cv::Vec3b(0,0,0);
        }
        const ColorRange color{{240,240,240},{255,255,255}};
        QCOMPARE(gaugeColorRatio(image,color),reference_gauge::gaugeColorRatio(image,color));
        QCOMPARE(stripedGaugeRatio(image,color),reference_gauge::stripedGaugeRatio(image,color));
        cv::Mat gray;
        if(!image.empty())cv::cvtColor(image,gray,cv::COLOR_BGR2GRAY);
        if(scenario%7==0 && !gray.empty())gray.setTo(127);
        QCOMPARE(stripeRegion(gray),reference_gauge::stripeRegion(gray));
        for(int segments=0;segments<=5;++segments)
            QCOMPARE(phoebeGaugeValue(image,color,segments,9,11,25),reference_gauge::phoebeGaugeValue(image,color,segments,9,11,25));
        for(bool nonzero:{false,true})
            QCOMPARE(gaugeRingRatio(image,color,.395,.496,nonzero),reference_gauge::gaugeRingRatio(image,color,.395,.496,nonzero));
        const double value=double((scenario*7919)%20001-10000)/10000.;
        for(int precision=-2;precision<=6;++precision)
            QCOMPARE(gaugeRoundHalfUp(value,precision),reference_gauge::gaugeRoundHalfUp(value,precision));
    }
    void gaugeActorsMatchFrozenCppIncludingPartnerAndLifecycle_data() {
        QTest::addColumn<QString>("name");QTest::addColumn<int>("scenario");
        for(const auto &name:QStringList{"Zhezhi","Lupa","Ciaccona","Hiyuki","Changli","Rover","Cartethyia","Carlotta","Camellya","Phoebe","Zani"})
            for(int scenario=0;scenario<128;++scenario)
                QTest::newRow(qPrintable(name+"-"+QString::number(scenario)))<<name<<scenario;
    }
    void gaugeActorsMatchFrozenCppIncludingPartnerAndLifecycle() {
        QFETCH(QString,name);QFETCH(int,scenario);
        const auto reference=replay(name,scenario,false),actual=replay(name,scenario,true);
        QVERIFY2(equal(reference,actual),qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",reference},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
};
QTEST_GUILESS_MAIN(RustCharacterTests)
#include "test_rust_characters.moc"
