#pragma once
#include "runtime/engine_abi.hpp"
namespace okww::runtime::reference_engine {
// Frozen pre-Rust lifecycle policy, with SDK accesses adapted to observations.
// Original UTF-8 LF SHA256: 2dd8259cab9101bca116f8817e53a686343becfbb401d5f85858697c4f57982a
template<class H> void pause(H &h){
    using O=rust::EngineOp;h.action(O::LockLifecycle);
    try{
        h.action(O::ClearPending);h.action(O::LaunchTimerStop);h.action(O::StopGate);
        h.action(O::GenerationZero);h.action(O::SetPaused,1);h.action(O::SetForeground,0);h.action(O::SetCancelForeground,0);
        try{h.action(O::BackendArm,0);}catch(const std::exception &){h.logException(std::current_exception());}
    }catch(...){h.action(O::UnlockLifecycle);throw;}
    h.action(O::UnlockLifecycle);
}
template<class H> bool nativeAvailable(H &h,int source){
    using O=rust::EngineOp;const int count=int(h.value(O::NativeCount));
    for(int index=0;index<count;++index){h.action(O::NativeSelect,index);if(h.flag(O::NativeMatches,source))return true;}
    return false;
}
template<class H> void start(H &h){
    using O=rust::EngineOp;
    if(h.flag(O::TeamBusy))h.action(O::Error,0);
    if(h.flag(O::ScriptsBusy))h.action(O::Error,1);
    h.action(O::ExitTimerStop);const double limit=h.value(O::LimitOption);
    if(!std::isfinite(limit)||limit<0||limit>168*3600)h.action(O::Error,2);
    bool script=false;const int scripts=int(h.value(O::ScriptsCount));
    for(int index=0;index<scripts;++index){h.action(O::SelectScriptEntry,index);if(h.flag(O::ScriptMatches)){script=true;break;}}
    if(script&&!h.flag(O::ScriptSupported))h.action(O::Error,3);
    if(h.flag(O::HasRequestedTask)&&!script&&!nativeAvailable(h,0))h.action(O::Error,4);
    if(!h.flag(O::HasRequestedTask)){
        h.action(O::ProfileCatalogLoad);const int count=int(h.value(O::CatalogCount));
        for(int index=0;index<count;++index){
            h.action(O::SelectCatalog,index);
            if(h.flag(O::CatalogTrigger)&&h.flag(O::CatalogEnabled)&&!h.flag(O::CatalogRemoteReset)&&!nativeAvailable(h,1))h.action(O::Error,5);
        }
    }
    h.action(O::Pause);h.action(O::Preload);h.action(O::PendingTaskSet);h.action(O::SetForeground,h.flag(O::HasRequestedTask));
    h.action(O::PendingOptionsSet);h.action(O::PendingSnapshotSet);h.action(O::TryLaunch);
    if(h.flag(O::HasPending))h.action(O::LaunchTimerStart);
}
template<class H> void tryLaunch(H &h){
    using O=rust::EngineOp;
    if(!h.flag(O::HasPending)||!h.flag(O::WorkerFinished))return;
    if(h.flag(O::WorkerJoinable))h.action(O::JoinWorker);
    h.action(O::CopyLaunchTask);h.action(O::ClearPending);h.action(O::LaunchTimerStop);
    h.action(O::SetPaused,0);h.action(O::StartGate);h.action(O::GenerationEpoch);h.action(O::SetWorkerFinished,0);h.action(O::ScriptFlagsClear);
    const int scripts=int(h.value(O::PendingScriptsCount));
    for(int index=0;index<scripts;++index){h.action(O::PendingSelectScript,index);h.action(O::StoreScriptFlag);}
    try{h.action(O::SpawnWorker);}catch(...){h.action(O::SetWorkerFinished,1);h.action(O::StopGate);h.action(O::SetPaused,1);throw;}
}
template<class H> void destroy(H &h){
    using O=rust::EngineOp;h.action(O::Pause);
    if(h.flag(O::WorkerJoinable))h.action(O::JoinWorker);
    if(h.flag(O::ModelJoinable))h.action(O::JoinModel);
    h.action(O::NotificationsStop);
}
}
