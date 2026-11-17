#pragma once
#include "reference_engine_lifecycle.hpp"
namespace okww::runtime::reference_engine {
template<class H> void prepare(H &h){
    using O=rust::EngineOp;
    if(!h.flag(O::AwakeActive))h.action(O::LogAwakeFailure);
    h.action(O::SetWarming,1);h.action(O::PostStatusSelected);h.action(O::LogPreparation);
    h.action(O::LogEmergency,h.flag(O::EmergencyAvailable));h.action(O::CreatePreparation);h.action(O::Checkpoint);
    if(!h.flag(O::SelectedIsScript))h.action(O::ValidateTeams);
    h.action(O::Checkpoint);while(!h.flag(O::ModelsReady))h.action(O::Wait,.02);
    if(!h.flag(O::ModelErrorEmpty))h.action(O::ThrowModelError);
    h.action(O::Checkpoint);h.action(O::LockLifecycle);
    try{h.action(O::Checkpoint);h.action(O::BackendArm,1);}catch(...){h.action(O::UnlockLifecycle);throw;}
    h.action(O::UnlockLifecycle);h.action(O::SetWarming,0);h.action(O::LogPreparationDone);h.action(O::PostStatusSelected);
}
template<class H> void health(H &h){
    using O=rust::EngineOp;
    if(!h.flag(O::Heartbeat)){
        if(!h.flag(O::GateActive))h.action(O::ThrowCancelled);
        h.action(O::ReadDeviceState);
        const int message=h.flag(O::DeviceErrorEmpty)?(h.flag(O::CaptureConnected)&&h.flag(O::BoxConnected)?0:1):2;
        h.action(O::ThrowInputFailure,message);
    }
    if(h.flag(O::TaskRunning)&&h.flag(O::Foreground)&&h.flag(O::CancelForeground))h.action(O::ThrowCancelled);
    if(h.flag(O::TaskRunning)&&h.flag(O::HasControlPoll))h.action(O::PollControls);
    const double limit=h.value(O::LimitSeconds);
    if(limit>0&&h.value(O::Now)-h.value(O::RunBegan)>=limit)h.action(O::Error,18);
}
template<class H> void progress(H &h){
    using O=rust::EngineOp;
    h.action(O::SelectProgressTask,h.flag(O::SelectedEmpty));
    if(!h.flag(O::ProgressTaskMatches))h.action(O::ResetProgressTask);
    h.action(O::InitializeProgressPatch);const int count=int(h.value(O::ProgressPatchCount));
    for(int i=0;i<count;++i)h.action(O::MergeProgressEntry,i);
    if(h.flag(O::ProgressUnchanged))return;
    h.action(O::CommitProgress);
    const bool final=h.flag(O::PatchContainsCurrent)&&h.value(O::ProgressTotal)>0&&h.value(O::ProgressCurrent)>=h.value(O::ProgressTotal);
    if(!final&&!h.flag(O::PatchContainsFlow)&&h.flag(O::ProgressStageUnchanged)&&h.value(O::Now)-h.value(O::LastProgress)<.1)return;
    h.action(O::StoreProgressTime,h.value(O::Now));h.action(O::QueueProgress);
}
template<class H> void progressDelivery(H &h){using O=rust::EngineOp;if(h.flag(O::GenerationMatches)&&!h.flag(O::Paused)&&h.flag(O::GateActive))h.action(O::EmitProgress);}
template<class H> void combatDelivery(H &h){using O=rust::EngineOp;if(h.flag(O::GenerationMatches)&&!h.flag(O::Paused))h.action(O::EmitCombat);}
template<class H> void infoProgress(H &h){using O=rust::EngineOp;h.action(O::LogInfo);if(h.flag(O::InfoIsStage))h.action(O::PublishStageInfo);else h.action(O::PublishDetailInfo);}
template<class H> void addTemplates(H &h){using O=rust::EngineOp;if(h.flag(O::UserTemplatesExist))h.action(O::AddUserTemplates);}
template<class H> double dispatchBuiltin(H &h){using O=rust::EngineOp;if(!nativeAvailable(h,3))h.action(O::Error,19);return h.value(O::DispatchBuiltin);}
template<class H> void registerScripts(H &h){
    using O=rust::EngineOp;const int count=int(h.value(O::SnapshotScriptCount));
    for(int i=0;i<count;++i){
        h.action(O::SelectSnapshotScript,i);h.action(O::StoreCustomScript);h.action(O::LogScriptLoaded);
        const bool trigger=h.flag(O::ScriptTrigger);const double interval=std::max(.05,h.value(O::ScriptInterval));
        h.appendRunnable(trigger,interval);
    }
}
template<class H> void requireFrame(H &h){using O=rust::EngineOp;if(!h.flag(O::FrameAvailable))h.action(O::Error,20);}
}
