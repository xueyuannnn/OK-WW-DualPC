#pragma once
#include "engine_scripts_probe.hpp"
#include "reference_engine_teams.hpp"
namespace okww::runtime::test {
struct TeamsProbe:ScriptsProbe{
    bool teamBusy=false,inCombat=true,retrying=true,quit=false;
    explicit TeamsProbe(int scene):ScriptsProbe(scene),teamBusy((scene&1)!=0){}
    void invoke(O op,const double *a,double *out,const std::exception_ptr &error={}){
        switch(op){
        case O::TeamBusy:out[0]=teamBusy;break;
        case O::SetTeamBusy:teamBusy=a[0]!=0;event(op,a[0]);break;
        case O::ScriptCatalogExists:case O::ModelExists:out[0]=(scene&2)!=0;break;
        case O::NotificationsIdle:out[0]=(scene&4)!=0;break;
        case O::ExitElapsed:{const double elapsed[]{0,179999,180000,180001};out[0]=elapsed[(scene/16)%4];break;}
        case O::SetTaskInCombat:inCombat=a[0]!=0;event(op,a[0]);break;
        case O::SetRetryingStep:retrying=a[0]!=0;event(op,a[0]);break;
        case O::BindLaunchTimer:case O::BindExitTimer:event(op,a[0]);break;
        case O::ReadInitialScripts:event(op);failure(0);break;
        case O::ReadInitialTemplates:event(op);failure(1);break;
        case O::CreateNotifications:event(op);failure(2);break;
        case O::SnapshotTeams:event(op);failure(3);break;
        case O::SpawnTeamSwitch:event(op);failure(4);break;
        case O::SetTeamMode:case O::SetAllTeamModes:event(op);failure(5);break;
        case O::ReplyData:event(op);failure(6);break;
        case O::EmitQuit:quit=true;event(op);break;
        case O::RequestCommand:out[0]=(scene&2)?16:17;event(op);break;
        case O::ReplyOk:replyOk=true;event(op);break;
        case O::AppendBusinessTasks:case O::DefaultModelPath:case O::FallbackModelPath:case O::ExitTimerStop:
        case O::EmitPartyInvalidated:case O::ReplyRequestId:case O::ResultTeamElapsed:case O::StartTeamTimer:
        case O::ReplyElapsed:event(op);break;
        default:ScriptsProbe::invoke(op,a,out,error);break;
        }
    }
    double value(O op,double a=0){double out[3]{};invoke(op,&a,out);return out[0];}
    bool flag(O op,double a=0){return value(op,a)!=0;}
    void action(O op,double a=0){value(op,a);}
    void replyError(std::exception_ptr e){double out[3]{};invoke(O::ReplyError,nullptr,out,e);}
};
inline QJsonObject teamsTrace(int scene,int method,bool migrated){
    TeamsProbe probe(scene);QString error;double value=0;
    try{
        if(migrated)value=rust::engineRun(rust::EngineMethod(method),[&](rust::EngineOp op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else switch(method){case 20:reference_engine::initialize(probe);break;case 21:reference_engine::exitPoll(probe);break;
            case 22:reference_engine::invalidateParty(probe);break;case 23:reference_engine::switchTeam(probe);break;
            case 24:reference_engine::teamReply(probe);break;case 25:reference_engine::teamWork(probe);break;case 26:value=reference_engine::idle(probe);break;}
    }catch(const std::exception &e){error=errorText(e);}catch(...){error="non-standard exception";}
    auto result=probe.trace();result["error"]=error;result["value"]=value;result["team_busy"]=probe.teamBusy;
    result["combat"]=probe.inCombat;result["retrying"]=probe.retrying;result["quit"]=probe.quit;return result;
}
}
