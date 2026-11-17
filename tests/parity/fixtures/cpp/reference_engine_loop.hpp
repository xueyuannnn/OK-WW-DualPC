#pragma once
#include "runtime/engine_abi.hpp"
namespace okww::runtime::reference_engine {
// Frozen scheduling policy from the pre-Rust Engine::run. Field/SDK accesses
// are expressed through the same observation interface; control remains C++.
// The original source block and its hash are retained in migration validation.
// Original UTF-8 LF SHA256: 2dd8259cab9101bca116f8817e53a686343becfbb401d5f85858697c4f57982a
template<class Host> void resume(Host &h){
    using O=rust::EngineOp;
    h.action(O::ReleaseInput);h.action(O::LockLifecycle);
    try{
        if(!h.flag(O::GateActive))h.action(O::ThrowCancelled);
        h.action(O::SetForeground,0);h.action(O::SetCancelForeground,0);
    }catch(...){h.action(O::UnlockLifecycle);throw;}
    h.action(O::UnlockLifecycle);
    h.action(O::ClearSelected);h.action(O::ClearActiveTask);h.action(O::ClearContextTask);
    h.action(O::ClearOverrides);h.action(O::ClearContextCombat);h.action(O::ResetRunner);
    h.action(O::CombatPending);h.action(O::ClearLimit);h.action(O::ClearNext);h.action(O::PostStatus);
}
template<class Host> void cancel(Host &h){
    using O=rust::EngineOp;
    h.action(O::SetStoppedFailure);h.action(O::RecordResult,0);h.action(O::SetResultRecorded,1);
    resume(h);h.action(O::LogStopped);
}
template<class Host> void run(Host &h){
    using O=rust::EngineOp;double lastStatus=0;
    while(true){
        h.action(O::Checkpoint);
        if(h.flag(O::HasSelected) && h.flag(O::CancelForeground))cancel(h);
        h.action(O::Refresh);bool handled=false;
        const auto count=int(h.value(O::TaskCount));
        for(int index=0;index<count;++index){
            h.action(O::SelectTask,index);
            if(h.flag(O::HasSelected) && !h.flag(O::TaskSelected))continue;
            if(!h.flag(O::HasSelected) && !h.flag(O::TaskTrigger))continue;
            if(!h.flag(O::HasSelected) && (h.flag(O::TaskCustom)?!h.flag(O::CustomTrigger)||!h.flag(O::ScriptEnabled):!h.flag(O::ProfileEnabled,0)))continue;
            if(h.value(O::Now)<h.value(O::NextTime))continue;
            h.action(O::SetTaskContext);h.action(O::SetTaskRunning,1);
            try{handled=h.flag(O::RunTask);}
            catch(const Cancelled &){
                if(!h.flag(O::GateActive)||!h.flag(O::CancelForeground))throw;
                h.action(O::SetTaskRunning,0);cancel(h);break;
            }catch(const CombatEnded &){
                if(!h.flag(O::TaskAutoCombat)||h.flag(O::ProfileEnabled,1))throw;
                h.action(O::ReleaseInput);h.logException(std::current_exception());handled=false;
            }
            h.action(O::SetTaskRunning,0);
            const auto next=h.value(O::Now)+h.value(O::TaskInterval);h.action(O::SetNextTime,next);
            if(!h.flag(O::TaskTrigger)){
                h.action(O::Checkpoint);h.action(O::ReleaseInput);
                const bool exit=h.flag(O::ExitOption)||(!h.flag(O::TaskCustom)&&h.flag(O::ExitProfile));
                h.action(O::SetExitAfter,exit);h.action(O::RecordResult,1);h.action(O::SetResultRecorded,1);h.action(O::TaskDone);
                h.action(O::CompletionNameReset);
                const auto names=int(h.value(O::CatalogCount));
                for(int name=0;name<names;++name){h.action(O::SelectCatalog,name);if(h.flag(O::CatalogSelected))h.action(O::CompletionNameSet);}
                h.action(O::NotifyDone);
                if(exit){h.action(O::SetCompleted,1);h.action(O::StopGate);break;}
                resume(h);h.action(O::LogCompleted);break;
            }
            if(handled)break;
        }
        if(h.value(O::Now)-lastStatus>.25){h.action(O::PostStatusSelected);lastStatus=h.value(O::Now);}
        h.action(O::Wait,std::clamp(h.value(O::TriggerInterval),1.,60000.)/1000.);
    }
}
template<class Host> bool scriptIterations(Host &h){
    using O=rust::EngineOp;
    const bool trigger=h.flag(O::ScriptTrigger);
    const int repeat=std::clamp(int(h.value(O::ScriptRepeat)),1,1000000);
    const bool continuous=!trigger&&h.flag(O::ScriptContinuous);
    bool handled=false;
    for(uint64_t iteration=0;trigger?iteration<1:(continuous||iteration<uint64_t(repeat));++iteration){
        h.action(O::Checkpoint);
        if(!trigger)h.scriptProgress(iteration,repeat,continuous);
        if(!trigger&&h.flag(O::HasInfo))h.scriptInfo(iteration+1,repeat,continuous);
        try{handled=h.flag(O::ScriptRun,trigger)||handled;}
        catch(const ScriptDisabled &){if(h.flag(O::HasInfo))h.action(O::ScriptDisabledInfo);return false;}
    }
    if(!trigger&&!continuous)h.action(O::ScriptComplete,repeat);
    return handled;
}
}
