#pragma once
#include "reference_task_context.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime::test {
struct ContextClock final:Clock{
    double time=0;std::function<void()> tick;
    double now() const override{return time;}
    void sleep(double seconds) override{time+=seconds;if(tick)tick();}
};
template<class Context> QJsonObject taskContextTrace(int scenario){
    const auto root=QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath("../..");
    QTemporaryDir directory;Templates templates(root+"/assets/coco_annotations.json");
    Profile profile(directory.path(),root+"/native/resources/catalog.json");
    const int mode=scenario/16,variant=scenario%16;
    profile.set("Monthly Card Config","Check Monthly Card",mode>=2 && variant!=7);
    profile.set("Monthly Card Config","Monthly Card Time",12);
    ContextClock clock;ExecutionGate gate;const auto epoch=gate.start();Execution execution(gate,epoch,clock);
    QJsonArray events;int reads=0,statusReads=0,stage=0;
    Config config=Config::defaults();
    Input input(config,{1920,1080},true,[&](QJsonObject command){
        command["time"]=clock.time;events.append(command);
        if(command["op"]=="button" && command["name"]=="left" && !command["down"].toBool())++stage;
        return QJsonObject{};
    },execution);
    cv::Mat frame(1080,1920,CV_8UC3,cv::Scalar(17,23,31));
    if(mode==2 || mode==3 || mode==8){
        const int hudMask=mode==2?variant%8:6;
        const QStringList names{"char_1_text","char_2_text","char_3_text"};
        for(int i=0;i<3;++i)if(hudMask&(1<<i)){
            const auto area=templates.box(names[i],{1920,1080});auto pattern=templates.pattern(names[i],{1920,1080});
            if(pattern.channels()==1)cv::cvtColor(pattern,pattern,cv::COLOR_GRAY2BGR);
            pattern.copyTo(frame(cv::Rect(area.x(),area.y(),area.width(),area.height())));
        }
    }
    const auto paint=[&](cv::Mat &image,const QStringList &names){
        for(const auto &name:names){const auto area=templates.box(name,{1920,1080});auto pattern=templates.pattern(name,{1920,1080});
            if(pattern.channels()==1)cv::cvtColor(pattern,pattern,cv::COLOR_GRAY2BGR);
            pattern.copyTo(image(cv::Rect(area.x(),area.y(),area.width(),area.height())));}
    };
    cv::Mat post=frame.clone(),confirmFrame=frame.clone();
    if(mode==4){
        if(!(variant&1))paint(frame,{"confirm_btn_hcenter_vcenter"});
        if(!(variant&2))paint(frame,{"skip_dialog_check"});
        post=frame;
    }
    if(mode==5){
        const QStringList names{"fast_travel_custom","gray_teleport","remove_custom"};
        if(variant%4<3)paint(frame,{names[variant%4]});
        paint(confirmFrame,{"confirm_btn_hcenter_vcenter"});
        paint(post,{"char_2_text","char_3_text"});
    }
    if(mode==6 && variant%8==0)paint(frame,{"login_close"});
    if(mode==6 && variant%8==7)paint(frame,{"switch_account"});
    if(mode==7 && !(variant&1)){paint(frame,{"gray_teleport"});paint(post,{"char_2_text","char_3_text"});}
    if(mode==8){
        if(!(variant&1))paint(frame,{"pick_up_f_hcenter_vcenter"});
        if(variant&2)paint(frame,{"dialog_3_dots"});
        profile.set("AutoPickTask","Pick Up Black List",QJsonArray{variant&4?"垃圾":"保留"});
        profile.set("AutoPickTask","Pick Up White List",QJsonArray{variant&4?"垃圾":"保留"});
    }
    if(mode==9){
        if(variant%4==0)paint(frame,{"skip_dialog"});
        if(variant%4==1)paint(frame,{"skip_dialog_new"});
        if(variant%4==2)paint(frame,{"message","message_dialog"});
        if(variant%4==3){
            const cv::Point points[]{{960,1010},{974,1025},{960,1040},{946,1025}};
            cv::fillConvexPoly(frame,points,4,cv::Scalar(245,245,245));
            cv::rectangle(frame,{800,800,200,25},cv::Scalar(30,170,245),-1);
            profile.set("AutoDialogTask","继续普通NPC对白",bool(variant&4));
        }
    }
    Context context(execution,input,templates,profile,[&]()->cv::Mat{
        ++reads;events.append(QJsonArray{"frame",reads,clock.time});
        if(mode==0){
            if(variant==3)throw std::runtime_error("frame source failed");
            if(variant==8 || (variant!=0 && clock.time<.25))return {};
        }
        if(mode==5 && stage>0)return variant%4==0 && stage<4?confirmFrame:post;
        if((mode==6 || mode==7) && stage>0)return post;
        if(mode==9 && stage>0)return post;
        return frame;
    },[&](const cv::Mat &,QRect area,double){
        events.append(QJsonArray{"ocr",stage,clock.time});
        if(mode==8){
            const auto f=templates.box("pick_up_f_hcenter_vcenter",{1920,1080});
            TextLine line{"垃圾",{pixelRound(f.x()+f.width()*5.3),pixelRound(f.y()+f.height()*1.5),f.width()*3,f.height()},.99};
            return area.contains(line.center().toPoint())?QVector<TextLine>{line}:QVector<TextLine>{};
        }
        if(mode==9){
            QVector<TextLine> lines;
            for(const auto &line:QVector<TextLine>{{"陌生人",{800,800,200,25},.99},{"你好",{400,900,400,30},.99}})
                if(area.contains(line.center().toPoint()))lines.append(line);
            return lines;
        }
        if(mode==6 || mode==7){
            QVector<TextLine> lines;
            const auto word=[&](QString text,QRect bounds){if(area.contains(QPointF(bounds.center()).toPoint()))lines.append({text,bounds,.99});};
            if(mode==7){if(stage==0)word(variant&2?"无关文字":"快速旅行",{1400,1000,200,20});return lines;}
            switch(variant%8){
                case 1:word("点击连接",{800,400,200,30});break;
                case 2:word("登录",{800,400,200,30});word("+86",{800,450,100,30});break;
                case 3:case 4:word("游戏即将重启",{800,200,200,30});if(variant%8==3)word("确认",{800,600,100,30});break;
                case 5:case 6:word("进入游戏",{1100,800,200,30});if(variant%8==6)word("登录",{1100,850,100,30});break;
                case 7:word("点击进入",{800,850,200,30});break;
                default:break;
            }
            return lines;
        }
        if(mode!=3 || variant==6)return QVector<TextLine>{};
        if(stage==0)return QVector<TextLine>{{"点击领取今日月相",{700,850,200,30},.99},{"剩余10天",{700,900,200,30},.99}};
        if(stage==1)return QVector<TextLine>{{"获得奖励",{700,200,200,30},.99},{"点击空白处",{700,900,200,30},.99}};
        return QVector<TextLine>{};
    });
    context.wallTime=[] {return QDateTime(QDate(2030,1,2),QTime(12,0));};
    if(mode==8)context.task="AutoPickTask";
    if(mode==9)context.task="AutoDialogTask";
    context.state->loggedIn=mode==2 && variant>=8;
    context.state->inCombat=mode==3 && variant==5;
    if(variant!=2)context.info=[&](QString title,QString message){
        events.append(QJsonArray{"info",title,message,clock.time});
        if(mode==0 && variant==4)throw std::runtime_error("info failed");
    };
    if(variant!=1)context.sourceRecovering=[&]{
        ++statusReads;events.append(QJsonArray{"recovering",statusReads,clock.time});
        if(mode==0 && variant==5)throw std::runtime_error("status failed");
        return mode==0 && variant!=9 && (variant==8 || clock.time<.25);
    };
    if(variant&1)context.sourceSequence=[&]{events.append(QJsonArray{"sequence",reads});return uint64_t(reads*2);};
    if(mode==0 && variant==6)clock.tick=[&]{if(clock.time>.12)gate.stop();};
    QJsonObject result;
    try{
        context.refresh();
        if(mode==1){
            const double seconds=variant%5==0?-.1:variant%5==1?std::numeric_limits<double>::quiet_NaN():variant%5==2?86401.:variant%5==3?0.:.15;
            context.sleep(seconds);
        }else if(mode==2){const auto party=context.party();result["party"]=QJsonArray{party.visible,party.current,party.size};}
        else if(mode==3){
            if(variant&1)result["monthly"]=context.monthlyCard();else context.monthlyWindow();
            context.monthlyWindow();
        }
        else if(mode==4)result["skip"]=context.skipConfirm();
        else if(mode==5)result["travel"]=context.travelButton();
        else if(mode==6)result["login"]=context.login();
        else if(mode==7)result["fast_travel"]=context.fastTravel();
        else if(mode==8)result["auto_pick"]=context.autoPick();
        else if(mode==9)result["auto_dialog"]=context.autoDialog();
    }catch(const std::exception &error){result["error"]=errorText(error);}
    result["events"]=events;result["time"]=clock.time;result["reads"]=reads;result["status_reads"]=statusReads;
    result["frame_version"]=double(context.frameVersion);result["has_frame"]=!context.frame.empty();
    result["logged_in"]=context.state->loggedIn;result["monthly_window"]=context.state->monthlyWindowHandled;
    result["idle"]=input.idle();result["epoch_active"]=gate.active(epoch);
    return result;
}
template<class Context> QJsonObject workflowTrace(int scenario){
    const auto root=QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath("../..");
    QTemporaryDir directory;Templates templates(root+"/assets/coco_annotations.json");
    Profile profile(directory.path(),root+"/native/resources/catalog.json");
    ContextClock clock;ExecutionGate gate;Execution execution(gate,gate.start(),clock);
    Input input(Config::defaults(),{1920,1080},true,[](QJsonObject){return QJsonObject{};},execution);
    Context context(execution,input,templates,profile,[]{return cv::Mat{};},[](const cv::Mat &,QRect,double){return QVector<TextLine>{};});
    context.task="TestFlow";
    QString timing=directory.path()+"/timing.json";
    if(scenario%6==0)timing={};
    else if(scenario%6==1){
        QJsonArray samples;for(int n=0;n<15;++n)samples.append(n+1);
        samples.append("2");samples.append(-1);samples.append(0);samples.append(86400);
        writeJson(timing,{{"TestFlow:a",samples},{"TestFlow:b",QJsonArray{1,2,3}}});
    }else if(scenario%6==2){QFile file(timing);file.open(QIODevice::WriteOnly);file.write("{");}
    else if(scenario%6==3)timing=directory.path()+"/missing/timing.json";
    else if(scenario%6==4)timing=directory.path();
    QJsonArray published;int callbacks=0;
    if(!(scenario&16))context.progress=[&](QJsonObject value){
        value.remove("flow_observed_at");published.append(value);
        if((scenario&8) && ++callbacks==3)throw std::runtime_error("progress callback failed");
    };
    QString error;
    try{
        context.beginWorkflow({QJsonObject{{"id","a"},{"name","步骤甲"},{"estimate_seconds",22}},
                               QJsonObject{{"id","b"},{"name","步骤乙"},{"estimate_seconds",30}}},timing);
        context.workflowStep("missing","running");context.workflowStep("a","invalid");
        context.workflowStep("a","running",QString(600,QChar(u'测')));
        context.workflowStep("a","running","duplicate");
        clock.time+=.5;context.reportProgress("测试",1,3,"项","正在运行");
        clock.time+=scenario&4?-.8:.5;
        context.workflowStep("a","completed");context.workflowStep("a","completed");
        for(int step=0;step<14;++step){
            context.workflowStep("b","running");clock.time+=.25;
            context.workflowStep("b","completed");
        }
        context.workflowStep("a","running");clock.time+=.1;context.workflowStep("a","failed","失败信息");
        context.workflowStep("a","skipped");
        context.reportProgress(QString(200,QChar(u'长')),-1,2,QString(32,QChar(u'项')),QString(700,QChar(u'文')));
        context.reportProgress("非数字",std::numeric_limits<double>::quiet_NaN(),10);
        context.reportProgress("无总数",3,0);
    }catch(const std::exception &failure){error=errorText(failure);}
    QJsonObject history;
    try{if(!timing.isEmpty() && QFileInfo(timing).isFile())history=readJson(timing);}catch(...){}
    return {{"published",published},{"history",history},{"time",clock.time},{"error",error}};
}
}
