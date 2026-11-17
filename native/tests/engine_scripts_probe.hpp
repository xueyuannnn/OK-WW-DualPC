#pragma once
#include "engine_command_probe.hpp"
#include "reference_engine_scripts.hpp"
namespace okww::runtime::test {
struct ScriptsProbe:CommandProbe{
    int entry=0,field=0;
    bool configuring=false,busy=false,loading=false;
    explicit ScriptsProbe(int scene):CommandProbe(scene,0),busy((scene&1)!=0),loading((scene&2)!=0){}
    void invoke(O op,const double *a,double *out,const std::exception_ptr &error={}){
        switch(op){
        case O::ScriptsBusy:out[0]=busy;break;
        case O::RequestConfigure:out[0]=(scene&2)!=0;break;
        case O::RequestKeyKind:out[0]=(scene/4)%4;break;
        case O::ScriptsCount:case O::ChangedScriptCount:out[0]=(scene/8)%5;break;
        case O::SelectScriptEntry:case O::SelectChangedScript:entry=int(a[0]);event(op,a[0]);break;
        case O::ScriptValueMatches:case O::ScriptIdentMatches:out[0]=entry==scene%4||((scene&128)&&entry==2);break;
        case O::ScriptTrigger:out[0]=(scene&16)!=0;break;
        case O::ScriptFieldsCount:out[0]=(scene/16)%4;break;
        case O::SelectScriptField:field=int(a[0]);event(op,a[0]);break;
        case O::ScriptFieldMatches:out[0]=field==scene%3||((scene&256)&&field==2);break;
        case O::ScriptStateExists:out[0]=(scene&32)!=0;break;
        case O::RequestValueBool:out[0]=(scene&64)!=0;break;
        case O::ScriptFlagExists:out[0]=(scene&128)!=0;break;
        case O::RequestValueNumber:out[0]=(scene&64)!=0;break;
        case O::RequestValueDouble:{const double values[]{-1,0,1,2.5,1000000,1000001,1.5,9};out[0]=values[(scene/8)%8];break;}
        case O::RequestValueInt:{const int values[]{-1,0,1,0,1000000,1000001,0,9};out[0]=values[(scene/8)%8];break;}
        case O::InitializeScriptChanges:configuring=true;event(op);break;
        case O::SetScriptsBusy:busy=a[0]!=0;event(op,a[0]);break;
        case O::ResponseOk:out[0]=(scene&4)!=0;break;
        case O::LoadingScripts:out[0]=loading;break;
        case O::SetLoadingScripts:loading=a[0]!=0;event(op,a[0]);break;
        case O::SetScriptBoolean:case O::StoreField:case O::StoreChangedScript:event(op,a[0]);break;
        case O::ReadScriptState:case O::EmptyScriptState:case O::LoadEntryFields:case O::StoreScriptFlagValue:
        case O::SetScriptRepeat:case O::SetFieldValue:case O::StoreScriptValues:case O::StoreScriptSettings:
        case O::CommitChangedScripts:case O::WriteScriptCatalog:case O::EmitCatalogChanged:case O::ScheduleScriptRetry:
        case O::BuildScriptManageCommand:case O::DeleteWatcher:case O::AdoptResponseScripts:case O::AdoptResponseTemplates:
        case O::ResultSelectedScript:case O::ResultSnippet:case O::ReplyResponseError:case O::EmitReady:event(op);break;
        case O::WriteScriptState:event(op);failure(1);break;
        case O::SpawnScriptManage:event(op);failure(2);break;
        case O::ScriptManage:event(op);failure(3);break;
        default:CommandProbe::invoke(op,a,out,error);break;
        }
    }
    double value(O op,double a=0){double out[3]{};invoke(op,&a,out);return out[0];}
    bool flag(O op,double a=0){return value(op,a)!=0;}
    void action(O op,double a=0){value(op,a);}
    void replyError(std::exception_ptr e){double out[3]{};invoke(O::ReplyError,nullptr,out,e);}
};
inline QJsonObject scriptsTrace(int scene,int method,bool migrated){
    ScriptsProbe probe(scene);QString error;
    try{
        if(migrated)rust::engineRun(rust::EngineMethod(method),[&](rust::EngineOp op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else switch(method){case 16:reference_engine::scriptCommand(probe);break;case 17:reference_engine::scriptConfigure(probe);break;
            case 18:reference_engine::scriptReply(probe);break;case 19:reference_engine::scriptManage(probe);break;}
    }catch(const std::exception &e){error=errorText(e);}catch(...){error="non-standard exception";}
    auto result=probe.trace();result["error"]=error;result["busy"]=probe.busy;result["loading"]=probe.loading;result["configuring"]=probe.configuring;
    return result;
}
}
