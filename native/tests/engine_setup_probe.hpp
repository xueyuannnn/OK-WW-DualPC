#pragma once
#include "reference_engine_setup.hpp"
namespace okww::runtime::test{
struct SetupProbe{
    using O=rust::EngineOp;
    int scenario;
    bool config=false,submitted=false,notified=false,statusBuilt=false,statusEmitted=false,loading=false,
        scanned=false,ready=false,spawned=false,modelReady=false,templates=false,ocr=false,echo=false;
    int modelPath=0;
    QString modelFailure;
    QJsonArray events;
    explicit SetupProbe(int input):scenario(input){}
    void event(O op,double value=0){events.append(QJsonArray{int(op),value});}
    void failure(int point){if((scenario/8)%7==point){if(scenario&128)throw 123;throw std::runtime_error("setup failed "+std::to_string(point));}}
    void invoke(O op,const double *a,double *out,const std::exception_ptr &error={}){
        switch(op){
            case O::LoadNotificationConfig:config=true;event(op);break;
            case O::SystemNotification:out[0]=(scenario&1)!=0;break;
            case O::EmitNotification:notified=true;event(op);failure(1);break;
            case O::SubmitNotification:submitted=true;event(op);failure(2);break;
            case O::GenerationMatches:out[0]=(scenario&1)!=0;break;
            case O::Foreground:out[0]=(scenario&2)!=0;break;
            case O::BuildStatus:statusBuilt=true;event(op,a[0]);failure(1);break;
            case O::EmitStatus:statusEmitted=true;event(op);failure(2);break;
            case O::Preload:event(op);failure(1);break;
            case O::LogReady:event(op);failure(2);break;
            case O::TasksDirectory:out[0]=(scenario&1)!=0;break;
            case O::ImportsDirectory:out[0]=(scenario&2)!=0;break;
            case O::SetLoadingScripts:loading=a[0]!=0;event(op,a[0]);break;
            case O::ScanScripts:scanned=true;event(op);failure(3);break;
            case O::EmitReady:ready=true;event(op);failure(4);break;
            case O::ModelJoinable:out[0]=(scenario&1)!=0;break;
            case O::SpawnModel:spawned=true;event(op);failure(3);break;
            case O::StartModelTimer:event(op);failure(0);break;
            case O::CreateTemplates:templates=true;event(op);failure(1);break;
            case O::DefaultModelPath:modelPath=1;event(op);break;
            case O::ModelExists:out[0]=(scenario&1)!=0;break;
            case O::FallbackModelPath:modelPath=2;event(op);break;
            case O::CreateOcr:ocr=true;event(op);failure(2);break;
            case O::CreateEcho:echo=true;event(op);if((scenario/8)%7>=5)throw std::runtime_error("initial model load failed");failure(3);break;
            case O::LogModelsReady:event(op);failure(4);break;
            case O::SetModelError:
                try{std::rethrow_exception(error);}catch(const std::exception &e){modelFailure=errorText(e);}
                event(op);failure(5);break;
            case O::LogModelError:event(op);failure(6);break;
            case O::SetModelsReady:modelReady=a[0]!=0;event(op,a[0]);break;
            default:throw std::runtime_error("unexpected setup operation");
        }
    }
    double value(O op,double arg=0){double out[3]{};invoke(op,&arg,out);return out[0];}
    bool flag(O op,double arg=0){return value(op,arg)!=0;}
    void action(O op,double arg=0){value(op,arg);}
    void modelError(std::exception_ptr error){double out[3]{};invoke(O::SetModelError,nullptr,out,error);}
    QJsonObject trace()const{
        return {{"events",events},{"config",config},{"submitted",submitted},{"notified",notified},{"status_built",statusBuilt},
            {"status_emitted",statusEmitted},{"loading",loading},{"scanned",scanned},{"ready",ready},{"spawned",spawned},
            {"model_ready",modelReady},{"templates",templates},{"ocr",ocr},{"echo",echo},{"model_path",modelPath},{"model_failure",modelFailure}};
    }
};
inline QJsonObject setupTrace(int scenario,int method,bool rust){
    SetupProbe probe(scenario);QString error;
    try{
        if(rust)rust::engineRun(rust::EngineMethod(method),[&](rust::EngineOp op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else switch(method){case 8:reference_engine::notify(probe);break;case 9:reference_engine::postStatus(probe);break;
            case 10:reference_engine::load(probe);break;case 11:reference_engine::beginPreload(probe);break;case 12:reference_engine::loadModels(probe);break;}
    }catch(const std::exception &e){error=errorText(e);}catch(...){error="non-standard exception";}
    auto result=probe.trace();result["error"]=error;return result;
}
}
