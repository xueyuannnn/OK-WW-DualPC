#pragma once
#include "reference_engine_lifecycle.hpp"
namespace okww::runtime::test {
struct LifecycleProbe{
    using O=rust::EngineOp;
    int scenario,requested,script=0,catalog=0,native=0,pendingScript=0,launchTask=-9;
    uint64_t epoch,generation;
    bool locked=false,gate=true,paused=false,foreground=true,cancelled=true,pending=true,
        launchTimer=true,exitTimer=true,workerFinished=true,workerJoinable=true,modelJoinable=true,
        armed=true,optionsCopied=false,snapshotCopied=false,preloaded=false,notificationsStopped=false;
    QJsonArray events;
    QJsonObject flags{{"old",true}};
    explicit LifecycleProbe(int input):scenario(input),requested((input/4)%8-1),epoch((1ULL<<60)+input),generation(epoch){
        pending=(scenario&8)!=0;workerFinished=(scenario&4)!=0;workerJoinable=(scenario&16)!=0;modelJoinable=(scenario&32)!=0;
    }
    void event(O op,double value=0){events.append(QJsonArray{int(op),value});}
    void invoke(O op,const double *a,double *out,const std::exception_ptr &error={}){
        switch(op){
            case O::LockLifecycle:if(locked)throw std::runtime_error("duplicate lock");locked=true;event(op);break;
            case O::UnlockLifecycle:if(!locked)throw std::runtime_error("missing lock");locked=false;event(op);break;
            case O::ClearPending:pending=false;event(op);break;
            case O::LaunchTimerStop:launchTimer=false;event(op);break;
            case O::StopGate:gate=false;event(op);break;
            case O::GenerationZero:generation=0;event(op);break;
            case O::SetPaused:paused=a[0]!=0;event(op,a[0]);break;
            case O::SetForeground:foreground=a[0]!=0;event(op,a[0]);break;
            case O::SetCancelForeground:cancelled=a[0]!=0;event(op,a[0]);break;
            case O::BackendArm:
                armed=a[0]!=0;event(op,a[0]);if(scenario%7==1)throw std::runtime_error("arm failed");if(scenario%7==2)throw 123;break;
            case O::LogException:
                if(scenario%19==0)throw std::runtime_error("log failed");
                try{std::rethrow_exception(error);}catch(const std::exception &e){events.append(QJsonArray{int(op),errorText(e)});}break;
            case O::TeamBusy:out[0]=(scenario&1)!=0;break;
            case O::ScriptsBusy:out[0]=(scenario&2)!=0;break;
            case O::ExitTimerStop:exitTimer=false;event(op);break;
            case O::LimitOption:{const double values[]{-1,0,1,604800,604800.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()};out[0]=values[scenario%7];break;}
            case O::ScriptsCount:out[0]=2;break;
            case O::SelectScriptEntry:script=int(a[0]);event(op,a[0]);break;
            case O::ScriptMatches:out[0]=script+3==requested;break;
            case O::ScriptSupported:out[0]=(scenario&(script?64:32))!=0;break;
            case O::HasRequestedTask:out[0]=requested>=0;break;
            case O::NativeCount:out[0]=3;break;
            case O::NativeSelect:native=int(a[0]);event(op,a[0]);break;
            case O::NativeMatches:out[0]=native==(a[0]==0?requested:catalog==0?0:catalog==1?6:7);break;
            case O::ProfileCatalogLoad:event(op);break;
            case O::CatalogCount:out[0]=3;break;
            case O::SelectCatalog:catalog=int(a[0]);event(op,a[0]);break;
            case O::CatalogTrigger:out[0]=catalog!=1 || (scenario&128)!=0;break;
            case O::CatalogEnabled:out[0]=(scenario&(1<<(catalog+4)))!=0;break;
            case O::CatalogRemoteReset:out[0]=catalog==2;break;
            case O::Error:throw std::runtime_error("start error "+std::to_string(int(a[0])));
            case O::Pause:pending=false;launchTimer=false;gate=false;generation=0;paused=true;foreground=cancelled=false;event(op);break;
            case O::Preload:preloaded=true;event(op);break;
            case O::PendingTaskSet:pending=true;event(op);break;
            case O::PendingOptionsSet:optionsCopied=true;event(op);break;
            case O::PendingSnapshotSet:snapshotCopied=true;event(op);break;
            case O::TryLaunch:if(scenario&256)pending=false;event(op);break;
            case O::HasPending:out[0]=pending;break;
            case O::LaunchTimerStart:launchTimer=true;event(op);break;
            case O::WorkerFinished:out[0]=workerFinished;break;
            case O::WorkerJoinable:out[0]=workerJoinable;break;
            case O::JoinWorker:workerJoinable=false;event(op);break;
            case O::CopyLaunchTask:launchTask=requested;event(op);break;
            case O::StartGate:gate=true;++epoch;event(op);break;
            case O::GenerationEpoch:generation=epoch;event(op);break;
            case O::SetWorkerFinished:workerFinished=a[0]!=0;event(op,a[0]);break;
            case O::ScriptFlagsClear:flags={};event(op);break;
            case O::PendingScriptsCount:out[0]=3;break;
            case O::PendingSelectScript:pendingScript=int(a[0]);event(op,a[0]);break;
            case O::StoreScriptFlag:
                if(scenario%23==0 && pendingScript==1)throw std::runtime_error("flag allocation failed");
                flags[QString::number(pendingScript)]=(scenario&(1<<pendingScript))!=0;event(op,pendingScript);break;
            case O::SpawnWorker:event(op);if(scenario%17==0)throw std::runtime_error("spawn failed");workerJoinable=true;break;
            case O::ModelJoinable:out[0]=modelJoinable;break;
            case O::JoinModel:modelJoinable=false;event(op);break;
            case O::NotificationsStop:notificationsStopped=true;event(op);break;
            default:throw std::runtime_error("unexpected lifecycle operation");
        }
    }
    double value(O op,double arg=0){double out[3]{};invoke(op,&arg,out);return out[0];}
    bool flag(O op,double arg=0){return value(op,arg)!=0;}
    void action(O op,double arg=0){value(op,arg);}
    void logException(std::exception_ptr error){double out[3]{};invoke(O::LogException,nullptr,out,error);}
    QJsonObject trace()const{
        return {{"events",events},{"flags",flags},{"epoch",QString::number(qulonglong(epoch))},{"generation",QString::number(qulonglong(generation))},
            {"locked",locked},{"gate",gate},{"paused",paused},{"foreground",foreground},{"cancelled",cancelled},{"pending",pending},
            {"launch_timer",launchTimer},{"exit_timer",exitTimer},{"worker_finished",workerFinished},{"worker_joinable",workerJoinable},
            {"model_joinable",modelJoinable},{"armed",armed},{"options_copied",optionsCopied},{"snapshot_copied",snapshotCopied},
            {"preloaded",preloaded},{"notifications_stopped",notificationsStopped},{"launch_task",launchTask}};
    }
};
inline QJsonObject lifecycleTrace(int scenario,int method,bool rust){
    LifecycleProbe probe(scenario);QString error;
    try{
        if(rust)rust::engineRun(rust::EngineMethod(method),[&](rust::EngineOp op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else switch(method){case 4:reference_engine::pause(probe);break;case 5:reference_engine::start(probe);break;
            case 6:reference_engine::tryLaunch(probe);break;case 7:reference_engine::destroy(probe);break;}
    }catch(const std::exception &e){error=errorText(e);}catch(...){error="non-standard exception";}
    auto result=probe.trace();result["error"]=error;return result;
}
}
