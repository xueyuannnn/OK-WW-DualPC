#pragma once
#include "task_context_replay.hpp"
namespace okww::runtime::test {
template<class Context> QJsonObject taskImageTrace(int scene){
    const int mode=scene/32,variant=scene%32;
    const auto root=QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath("../..");
    QTemporaryDir directory;Templates templates(root+"/assets/coco_annotations.json");
    Profile profile(directory.path(),root+"/native/resources/catalog.json");
    profile.set("Monthly Card Config","Check Monthly Card",false);
    ContextClock clock;ExecutionGate gate;const auto epoch=gate.start();int checks=0,reads=0,sources=0,predicates=0;
    Execution execution(gate,epoch,clock,[&]{++checks;});QJsonArray events;
    Input input(Config::defaults(),{640,360},true,[&](QJsonObject command){command["time"]=clock.time;events.append(command);return QJsonObject{};},execution);
    const QSize size=mode>=8?QSize(640,360):QSize(32,24);
    cv::Mat source(size.height(),size.width(),CV_8UC3,cv::Scalar(17,83,199));
    if(mode>=8&&variant%4<3){
        const QStringList names{"gray_teleport","remove_custom","fast_travel_custom"};const auto name=names[variant%4];
        auto pattern=templates.pattern(name,size);if(pattern.channels()==1)cv::cvtColor(pattern,pattern,cv::COLOR_GRAY2BGR);
        const auto box=templates.box(name,size);pattern.copyTo(source(cv::Rect(box.x(),box.y(),box.width(),box.height())));
    }
    Context context(execution,input,templates,profile,[&]{++sources;return source;},[&](const cv::Mat &image,QRect area,double threshold){
        ++reads;const auto sum=cv::sum(image);events.append(QJsonObject{{"reader",reads},{"width",image.cols},{"height",image.rows},{"channels",image.channels()},
            {"area",QJsonArray{area.x(),area.y(),area.width(),area.height()}},{"threshold",threshold},{"sum",QJsonArray{sum[0],sum[1],sum[2]}}});
        if(variant==13)throw std::runtime_error("reader failed");if(variant==14)gate.stop();
        const QVector<TextLine> values{{"测试",{-3,2,5,7},.9},{"B",{7,-4,0,1},.8},{"C",{100,50,2,0},.7}};
        return values.mid(0,variant%4);
    });
    context.frame=source;context.task="AutoCombatTask";
    if(variant&1)context.taskOverrides={{"Auto Target",false},{"null",QJsonValue()},{"no_config",17}};
    QJsonObject output;QString error;
    const auto lines=[&](const QVector<TextLine> &values){QJsonArray rows;for(const auto &line:values)rows.append(QJsonObject{{"text",line.text},{"box",QJsonArray{line.box.x(),line.box.y(),line.box.width(),line.box.height()}},{"confidence",line.confidence}});output["lines"]=rows;};
    try{
        if(variant==12)gate.stop();
        switch(mode){
        case 0:lines(context.text(QRect(1,2,10,12),.65));break;
        case 1:lines(context.text(QRect(1,2,10,12),.7,Filter(variant%7)));break;
        case 2:{
            const double scales[]{-1,0,.5,1,1.5,2,3,8,8.1,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),1e-20};
            const QRect areas[]{{-2,-3,20,15},{200,200,10,10},{0,0,0,20},{0,0,32,24}};
            lines(context.textScaled(areas[(variant/3)%4],.65,scales[variant%12]));break;
        }
        case 3:case 4:case 5:{
            const double waits[]{0,.1,-1,std::numeric_limits<double>::quiet_NaN()};const double after=waits[variant%4];
            if(mode==3)context.click(QPointF(31.5,10.5),after);
            else if(mode==4)context.clickRelative(.803,.196,after,(variant&1)!=0);
            else context.key("f",after,.02);
            break;
        }
        case 6:{const QStringList keys{"Auto Target","_enabled","no_config","null"};output["value"]=context.setting(keys[variant%4],-7);break;}
        case 7:output["wait"]=context.wait([&]{++predicates;if((variant&8)&&predicates==2)gate.stop();return predicates>variant%4;},.2,.03);break;
        case 8:case 9:{
            if(variant&8)context.frame={};
            const auto found=mode==8?context.find("gray_teleport"):context.findAny({"gray_teleport","remove_custom","fast_travel_custom"});
            if(found)output["match"]=QJsonObject{{"name",found->name},{"box",QJsonArray{found->box.x(),found->box.y(),found->box.width(),found->box.height()}},{"confidence",found->confidence}};
            else output["match"]=QJsonValue();
            break;
        }
        }
    }catch(const std::exception &e){error=errorText(e);}
    output["error"]=error;output["events"]=events;output["checks"]=checks;output["reads"]=reads;output["sources"]=sources;
    output["predicates"]=predicates;output["time"]=clock.time;output["frame_version"]=double(context.frameVersion);return output;
}
}
