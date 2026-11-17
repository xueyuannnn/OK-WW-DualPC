#pragma once
#include "reference_engine_lifecycle.hpp"
namespace okww::runtime::reference_engine {
template<class H> void initialize(H &h){
    using O=rust::EngineOp;
    try{h.action(O::ReadInitialScripts);if(h.flag(O::ScriptCatalogExists))h.action(O::ReadInitialTemplates);}catch(...){}
    h.action(O::AppendBusinessTasks);h.action(O::BindLaunchTimer,50);h.action(O::DefaultModelPath);
    if(!h.flag(O::ModelExists))h.action(O::FallbackModelPath);
    h.action(O::CreateNotifications);h.action(O::BindExitTimer,100);
}
template<class H> void exitPoll(H &h){
    using O=rust::EngineOp;
    if(!h.flag(O::Paused)){h.action(O::ExitTimerStop);return;}
    if(h.flag(O::NotificationsIdle)||h.value(O::ExitElapsed)>180000){h.action(O::ExitTimerStop);h.action(O::EmitQuit);}
}
template<class H> void invalidateParty(H &h){
    using O=rust::EngineOp;h.action(O::SetTaskInCombat,0);h.action(O::SetRetryingStep,0);
    h.action(O::IncrementCombatRevision);h.action(O::EmitPartyInvalidated);
}
template<class H> void switchTeam(H &h){
    using O=rust::EngineOp;
    if(h.flag(O::TeamBusy))h.action(O::Error,6);
    if(!h.flag(O::Paused)||!h.flag(O::WorkerFinished)||h.flag(O::HasPending))h.action(O::Error,7);
    if(h.flag(O::WorkerJoinable))h.action(O::JoinWorker);
    h.action(O::SnapshotTeams);h.action(O::SetTeamBusy,1);h.action(O::SpawnTeamSwitch);
}
template<class H> void teamReply(H &h){
    using O=rust::EngineOp;
    h.action(O::DeleteWatcher);h.action(O::SetTeamBusy,0);h.action(O::InvalidateParty);h.action(O::ReplyRequestId);
    try{h.action(O::ResultCatalog);h.action(O::ResultTeamElapsed);h.action(O::EmitCatalogChanged);h.action(O::ReplyData);}
    catch(const std::exception &){h.replyError(std::current_exception());}
    h.action(O::EmitReply);
}
template<class H> void teamWork(H &h){
    using O=rust::EngineOp;
    h.action(O::StartTeamTimer);
    try{if(h.value(O::RequestCommand)==16)h.action(O::SetTeamMode);else h.action(O::SetAllTeamModes);h.action(O::ReplyOk);}
    catch(const std::exception &){h.replyError(std::current_exception());}
    h.action(O::ReplyElapsed);
}
template<class H> bool idle(H &h){using O=rust::EngineOp;return h.flag(O::Paused)&&h.flag(O::WorkerFinished)&&!h.flag(O::TeamBusy);}
}
