#pragma once
#include "runtime/engine_abi.hpp"
namespace okww::runtime::reference_engine {
template<class H> void scriptConfigure(H &h){
    using O=rust::EngineOp;
    if(h.flag(O::ScriptStateExists))h.action(O::ReadScriptState);else h.action(O::EmptyScriptState);
    h.action(O::InitializeScriptChanges);bool found=false;
    const int count=int(h.value(O::ChangedScriptCount));
    for(int i=0;i<count;++i){
        h.action(O::SelectChangedScript,i);if(!h.flag(O::ScriptIdentMatches))continue;found=true;
        const int key=int(h.value(O::RequestKeyKind));
        if(key==0||key==1){
            if(!h.flag(O::RequestValueBool))h.action(O::Error,12);
            h.action(O::SetScriptBoolean,key);
            if(key==0&&h.flag(O::ScriptFlagExists))h.action(O::StoreScriptFlagValue);
        }else if(key==2){
            if(!h.flag(O::RequestValueNumber)||h.value(O::RequestValueDouble)!=h.value(O::RequestValueInt)||
                h.value(O::RequestValueInt)<1||h.value(O::RequestValueInt)>1000000)h.action(O::Error,13);
            h.action(O::SetScriptRepeat);
        }else{
            h.action(O::LoadEntryFields);bool fieldFound=false;
            const int fields=int(h.value(O::ScriptFieldsCount));
            for(int f=0;f<fields;++f){
                h.action(O::SelectScriptField,f);if(!h.flag(O::ScriptFieldMatches))continue;
                fieldFound=true;h.action(O::SetFieldValue);h.action(O::StoreField,f);
            }
            if(!fieldFound)h.action(O::Error,14);
            h.action(O::StoreScriptValues);
        }
        h.action(O::StoreChangedScript,i);
    }
    if(!found)h.action(O::Error,15);
    h.action(O::StoreScriptSettings);h.action(O::WriteScriptState);h.action(O::CommitChangedScripts);h.action(O::WriteScriptCatalog);
    h.action(O::ResultCatalog);h.action(O::EmitCatalogChanged);h.action(O::ReplySuccess);h.action(O::EmitReply);
}
template<class H> void scriptCommand(H &h){
    using O=rust::EngineOp;
    try{
        if(h.flag(O::ScriptsBusy)){h.action(O::ScheduleScriptRetry);return;}
        const bool live=h.flag(O::RequestConfigure)&&h.value(O::RequestKeyKind)==0;
        bool settings=false;
        if(h.flag(O::RequestConfigure)){
            const int count=int(h.value(O::ScriptsCount));
            for(int i=0;i<count&&!settings;++i){
                h.action(O::SelectScriptEntry,i);h.action(O::LoadEntryFields);
                if(h.flag(O::ScriptValueMatches)&&h.flag(O::ScriptTrigger)){
                    const int fields=int(h.value(O::ScriptFieldsCount));
                    for(int f=0;f<fields;++f){h.action(O::SelectScriptField,f);if(h.flag(O::ScriptFieldMatches)){settings=true;break;}}
                }
            }
        }
        if((!h.flag(O::Paused)||!h.flag(O::WorkerFinished)||h.flag(O::HasPending))&&!live&&!settings)h.action(O::Error,11);
        if(h.flag(O::RequestConfigure)){scriptConfigure(h);return;}
        h.action(O::SetScriptsBusy,1);h.action(O::BuildScriptManageCommand);h.action(O::SpawnScriptManage);
    }catch(const std::exception &){h.replyError(std::current_exception());h.action(O::EmitReply);}
}
template<class H> void scriptReply(H &h){
    using O=rust::EngineOp;
    h.action(O::DeleteWatcher);h.action(O::SetScriptsBusy,0);
    if(h.flag(O::ResponseOk)){
        h.action(O::AdoptResponseScripts);h.action(O::AdoptResponseTemplates);h.action(O::ResultCatalog);
        h.action(O::ResultSelectedScript);h.action(O::ResultSnippet);h.action(O::EmitCatalogChanged);h.action(O::ReplySuccess);h.action(O::EmitReply);
    }else h.action(O::ReplyResponseError);
    if(h.flag(O::LoadingScripts)){h.action(O::SetLoadingScripts,0);h.action(O::EmitReady);}
}
template<class H> void scriptManage(H &h){
    using O=rust::EngineOp;
    try{h.action(O::ScriptManage);}catch(const std::exception &){h.replyError(std::current_exception());}
}
}
