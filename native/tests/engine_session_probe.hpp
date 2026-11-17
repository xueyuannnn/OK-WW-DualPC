#pragma once
#include "engine_teams_probe.hpp"
#include "reference_engine_session.hpp"
namespace okww::runtime::test {
struct SessionProbe:TeamsProbe{
    bool warming=false,locked=false;
    int waits=0,checkpoints=0,scriptIndex=0;
    double time=10,lastProgress=-1;
    explicit SessionProbe(int scene):TeamsProbe(scene){}
    void invoke(O op,const double *a,double *out,const std::exception_ptr &error={}){
        switch(op){
        case O::AwakeActive:case O::Heartbeat:case O::ProgressTaskMatches:case O::InfoIsStage:out[0]=(scene&1)!=0;break;
        case O::EmergencyAvailable:case O::DeviceErrorEmpty:case O::ProgressUnchanged:case O::UserTemplatesExist:out[0]=(scene&2)!=0;break;
        case O::SelectedIsScript:case O::CaptureConnected:case O::PatchContainsCurrent:case O::FrameAvailable:out[0]=(scene&4)!=0;break;
        case O::ModelErrorEmpty:out[0]=(scene&8)!=0;break;
        case O::BoxConnected:case O::PatchContainsFlow:case O::SelectedEmpty:out[0]=(scene&8)!=0;break;
        case O::GateActive:case O::TaskRunning:case O::GenerationMatches:case O::ProgressStageUnchanged:out[0]=(scene&16)!=0;break;
        case O::HasControlPoll:out[0]=(scene&32)!=0;break;
        case O::CancelForeground:out[0]=(scene&64)!=0;break;
        case O::SetWarming:warming=a[0]!=0;event(op,a[0]);break;
        case O::Checkpoint:++checkpoints;event(op);if(checkpoints==scene%7)throw Cancelled();break;
        case O::ModelsReady:out[0]=waits>=scene%4;break;
        case O::Wait:++waits;time+=a[0];event(op,a[0]);if(waits==2&&scene%5==0)throw Cancelled();break;
        case O::ThrowModelError:throw std::runtime_error("model initialization failed");
        case O::LockLifecycle:locked=true;event(op);break;
        case O::UnlockLifecycle:locked=false;event(op);break;
        case O::BackendArm:event(op,a[0]);failure(2);break;
        case O::ThrowCancelled:throw Cancelled();
        case O::ThrowInputFailure:event(op,a[0]);throw std::runtime_error("input failure "+std::to_string(int(a[0])));
        case O::LimitSeconds:{const double values[]{0,-1,10,9,100};out[0]=values[scene%5];break;}
        case O::Now:out[0]=time;break;
        case O::RunBegan:out[0]=0;break;
        case O::LastProgress:{const double values[]{-1,9.89,9.9,9.91,11};out[0]=values[scene%5];break;}
        case O::ProgressPatchCount:out[0]=(scene/4)%7;break;
        case O::ProgressTotal:out[0]=(scene&32)?10:-1;break;
        case O::ProgressCurrent:out[0]=scene%13;break;
        case O::SelectProgressTask:case O::MergeProgressEntry:case O::StoreProgressTime:case O::LogEmergency:event(op,a[0]);break;
        case O::SnapshotScriptCount:out[0]=(scene/8)%5;break;
        case O::SelectSnapshotScript:scriptIndex=int(a[0]);event(op,a[0]);break;
        case O::ScriptTrigger:out[0]=scriptIndex%2==0;break;
        case O::ScriptInterval:{const double values[]{-.1,0,.01,.05,.07,10,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()};out[0]=values[(scene+scriptIndex)%8];break;}
        case O::AppendRunnableScript:events.append(QJsonArray{int(op),a[0],a[1]});break;
        case O::DispatchBuiltin:out[0]=(scene&2)!=0;event(op);break;
        case O::LogAwakeFailure:case O::PostStatusSelected:case O::LogPreparation:case O::CreatePreparation:
        case O::ValidateTeams:case O::LogPreparationDone:case O::ReadDeviceState:case O::PollControls:
        case O::ResetProgressTask:case O::InitializeProgressPatch:case O::CommitProgress:case O::QueueProgress:
        case O::EmitProgress:case O::EmitCombat:case O::LogInfo:case O::PublishStageInfo:case O::PublishDetailInfo:
        case O::AddUserTemplates:case O::StoreCustomScript:case O::LogScriptLoaded:event(op);break;
        default:TeamsProbe::invoke(op,a,out,error);break;
        }
    }
    double value(O op,double a=0){double out[3]{};invoke(op,&a,out);return out[0];}
    bool flag(O op,double a=0){return value(op,a)!=0;}
    void action(O op,double a=0){value(op,a);}
    void appendRunnable(bool trigger,double interval){const double a[]{double(trigger),interval};double out[3]{};invoke(O::AppendRunnableScript,a,out);}
};
inline QJsonObject sessionTrace(int scene,int method,bool migrated){
    SessionProbe probe(scene);QString error;double value=0;
    try{
        if(migrated)value=rust::engineRun(rust::EngineMethod(method),[&](rust::EngineOp op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else switch(method){case 27:reference_engine::prepare(probe);break;case 28:reference_engine::health(probe);break;
            case 29:reference_engine::progress(probe);break;case 30:reference_engine::progressDelivery(probe);break;
            case 31:reference_engine::combatDelivery(probe);break;case 32:reference_engine::infoProgress(probe);break;
            case 33:reference_engine::addTemplates(probe);break;case 34:value=reference_engine::dispatchBuiltin(probe);break;
            case 35:reference_engine::registerScripts(probe);break;case 36:reference_engine::requireFrame(probe);break;}
    }catch(const std::exception &e){error=errorText(e);}catch(...){error="non-standard exception";}
    auto result=probe.trace();result["error"]=error;result["value"]=value;result["locked"]=probe.locked;
    result["warming"]=probe.warming;result["waits"]=probe.waits;result["checkpoints"]=probe.checkpoints;return result;
}
}
