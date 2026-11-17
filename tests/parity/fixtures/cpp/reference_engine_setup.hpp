#pragma once
#include "runtime/engine_abi.hpp"
namespace okww::runtime::reference_engine {
// Frozen Engine notify/status/load/model policies, with SDK observations.
// Original UTF-8 LF SHA256: 2dd8259cab9101bca116f8817e53a686343becfbb401d5f85858697c4f57982a
template<class H> void notify(H &h){
    using O=rust::EngineOp;h.action(O::LoadNotificationConfig);
    if(h.flag(O::SystemNotification))h.action(O::EmitNotification);
    h.action(O::SubmitNotification);
}
template<class H> void postStatus(H &h){
    using O=rust::EngineOp;
    if(h.flag(O::GenerationMatches)){h.action(O::BuildStatus,h.flag(O::Foreground));h.action(O::EmitStatus);}
}
template<class H> void load(H &h){
    using O=rust::EngineOp;h.action(O::Preload);h.action(O::LogReady);
    if(h.flag(O::TasksDirectory)||h.flag(O::ImportsDirectory)){h.action(O::SetLoadingScripts,1);h.action(O::ScanScripts);}
    else h.action(O::EmitReady);
}
template<class H> void beginPreload(H &h){
    using O=rust::EngineOp;if(h.flag(O::ModelJoinable))return;h.action(O::SpawnModel);
}
template<class H> void loadModels(H &h){
    using O=rust::EngineOp;h.action(O::StartModelTimer);
    try{
        h.action(O::CreateTemplates);h.action(O::DefaultModelPath);
        if(!h.flag(O::ModelExists))h.action(O::FallbackModelPath);
        h.action(O::CreateOcr);h.action(O::CreateEcho);h.action(O::LogModelsReady);
    }catch(const std::exception &){h.modelError(std::current_exception());h.action(O::LogModelError);}
    h.action(O::SetModelsReady,1);
}
}
