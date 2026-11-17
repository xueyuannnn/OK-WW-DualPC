#pragma once
#include "runtime/engine_abi.hpp"
namespace okww::runtime::reference_engine {
// Command routing frozen from the pre-Rust implementation. SDK expressions
// are replaced by observations; branch order and catch/return boundaries remain.
template<class H> void command(H &h) {
    using O=rust::EngineOp;
    try {
        const auto op=int(h.value(O::RequestCommand));
        if(op==16||op==17){h.action(O::SwitchTeam);return;}
        if(op==18){h.action(O::ScriptCommand);return;}
        if(op==0)h.action(O::ResultCatalog);
        else if(op==1||op==2){h.action(O::Pause);h.action(O::ResultStatus,0);}
        else if(op==3){
            if(h.flag(O::PendingTaskNonempty)){
                h.action(O::PendingTaskEmpty);h.action(O::PendingExitFalse);h.action(O::PendingLimitZero);h.action(O::SetForeground,0);
            }else if(h.flag(O::Foreground)&&!h.flag(O::Paused))h.action(O::SetCancelForeground,1);
            h.action(O::ResultStatus,0);
        }else if(op==4){h.action(O::StartRequest);h.action(O::ResultStatus,1);}
        else if(op==5||op==6){
            if(h.flag(O::TaskIsScript)){h.action(O::ScriptConfigureTask);return;}
            h.action(O::SetProfile,op==5);h.action(O::ResultCatalog);
        }else if(op==7){
            if(h.flag(O::GroupIsScript)){h.action(O::ScriptResetGroup);return;}
            h.action(O::ResetProfile);h.action(O::ResultCatalog);
        }else if(op==8){
            if(h.flag(O::TaskIsScript)){h.action(O::ScriptToggleTask);return;}
            if(h.flag(O::RequestEnabled)&&!nativeAvailable(h,2))h.action(O::Error,8);
            const bool previous=h.flag(O::ProfileWasEnabled);
            h.action(O::SetTrigger);
            if(h.flag(O::RequestAutoCombat)&&!previous&&h.flag(O::RequestEnabled))h.action(O::IncrementCombatRevision);
            h.action(O::ResultStatus,0);
        }else if((op>=9&&op<=15)||op==19){
            if(h.flag(O::TeamBusy))h.action(O::Error,6);
            if(op==9)h.action(O::TeamCode);
            else if(op==10)h.action(O::TeamEditor);
            else if(op==11)h.action(O::TeamExport);
            else {
                if(!h.flag(O::Paused)||!h.flag(O::WorkerFinished)||h.flag(O::HasPending))h.action(O::Error,7);
                if(h.flag(O::WorkerJoinable))h.action(O::JoinWorker);
                if(op==12)h.action(O::TeamCreate);
                else if(op==13)h.action(O::TeamDelete);
                else if(op==14)h.action(O::TeamSave);
                else if(op==15)h.action(O::TeamImport);
                else h.action(O::Error,9);
                h.action(O::InvalidateParty);h.action(O::ResultCatalog);
            }
        }else h.action(O::Error,10);
        h.action(O::ReplySuccess);
    }catch(const std::exception &){h.replyError(std::current_exception());}
    h.action(O::EmitReply);
}
}
