#pragma once
#include "reference_engine_lifecycle.hpp"
#include "reference_engine_command.hpp"
namespace okww::runtime::test {
struct CommandProbe {
    using O=rust::EngineOp;
    int scene,command,native=0,revision=0;
    bool pendingNonempty,foreground,cancel=false,pending,paused,finished,exitAfter=true,replyOk=false,replySent=false;
    int limit=37;
    QString error;
    QJsonArray events;
    CommandProbe(int value,int cmd):scene(value),command(cmd),pendingNonempty((value&1)!=0),foreground((value&2)!=0),
        pending((value&4)!=0),paused((value&8)!=0),finished((value&16)!=0){}
    void event(O op,double value=0){events.append(QJsonArray{int(op),value});}
    void failure(int point){if((scene/16)%7==point){if(scene&128)throw 123;throw std::runtime_error("command failure "+std::to_string(point));}}
    void invoke(O op,const double *a,double *out,const std::exception_ptr &failureError={}){
        switch(op){
        case O::RequestCommand:out[0]=command;event(op);break;
        case O::PendingTaskNonempty:out[0]=pendingNonempty;break;
        case O::PendingTaskEmpty:pendingNonempty=false;pending=true;event(op);break;
        case O::PendingExitFalse:exitAfter=false;event(op);break;
        case O::PendingLimitZero:limit=0;event(op);break;
        case O::Foreground:out[0]=foreground;break;
        case O::SetForeground:foreground=a[0]!=0;event(op,a[0]);break;
        case O::SetCancelForeground:cancel=a[0]!=0;event(op,a[0]);break;
        case O::Paused:out[0]=paused;break;
        case O::Pause:paused=true;pending=false;foreground=false;event(op);failure(0);break;
        case O::TeamBusy:out[0]=(scene&1)!=0;break;
        case O::WorkerFinished:out[0]=finished;break;
        case O::WorkerJoinable:out[0]=(scene&32)!=0;break;
        case O::HasPending:out[0]=pending;break;
        case O::TaskIsScript:out[0]=(scene&32)!=0;break;
        case O::GroupIsScript:out[0]=(scene&64)!=0;break;
        case O::RequestEnabled:out[0]=(scene&64)!=0;break;
        case O::ProfileWasEnabled:out[0]=(scene&8)!=0;break;
        case O::RequestAutoCombat:out[0]=(scene&128)!=0;break;
        case O::NativeCount:out[0]=3;break;
        case O::NativeSelect:native=int(a[0]);event(op,a[0]);break;
        case O::NativeMatches:out[0]=native==scene%4;event(op,a[0]);break;
        case O::IncrementCombatRevision:++revision;event(op);break;
        case O::ResultCatalog:event(op);failure(1);break;
        case O::ResultStatus:event(op,a[0]);failure(2);break;
        case O::SetProfile:event(op,a[0]);failure(3);break;
        case O::ReplySuccess:replyOk=true;event(op);failure(4);break;
        case O::SwitchTeam:event(op);failure(5);break;
        case O::StartRequest:event(op);failure(6);break;
        case O::SetTrigger:case O::ResetProfile:case O::ScriptCommand:case O::ScriptConfigureTask:
        case O::ScriptResetGroup:case O::ScriptToggleTask:case O::JoinWorker:case O::TeamCode:
        case O::TeamEditor:case O::TeamExport:case O::TeamCreate:case O::TeamDelete:case O::TeamSave:
        case O::TeamImport:case O::InvalidateParty:event(op);break;
        case O::Error:event(op,a[0]);throw std::runtime_error("command error "+std::to_string(int(a[0])));
        case O::ReplyError:try{std::rethrow_exception(failureError);}catch(const std::exception &e){error=errorText(e);}
            replyOk=false;event(op);break;
        case O::EmitReply:replySent=true;event(op);break;
        default:throw std::runtime_error("unexpected command operation");
        }
    }
    double value(O op,double a=0){double out[3]{};invoke(op,&a,out);return out[0];}
    bool flag(O op,double a=0){return value(op,a)!=0;}
    void action(O op,double a=0){value(op,a);}
    void replyError(std::exception_ptr e){double out[3]{};invoke(O::ReplyError,nullptr,out,e);}
    QJsonObject trace()const{return {{"events",events},{"pending_nonempty",pendingNonempty},{"pending",pending},
        {"foreground",foreground},{"cancel",cancel},{"paused",paused},{"exit_after",exitAfter},{"limit",limit},
        {"reply_ok",replyOk},{"reply_sent",replySent},{"reply_error",error},{"revision",revision}};}
};
inline QJsonObject commandTrace(int scene,int command,bool migrated){
    CommandProbe probe(scene,command);QString error;
    try{
        if(migrated)rust::engineRun(rust::EngineMethod::Command,[&](rust::EngineOp op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else reference_engine::command(probe);
    }catch(const std::exception &e){error=errorText(e);}catch(...){error="non-standard exception";}
    auto result=probe.trace();result["error"]=error;return result;
}
}
