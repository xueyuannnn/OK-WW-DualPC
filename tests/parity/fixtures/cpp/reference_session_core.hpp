#pragma once
#include "runtime/session_abi.hpp"
namespace okww::runtime::reference_session {
enum class Bool:int {
#define OKWW_SESSION_BOOL(name,id,field) name=id,
#define OKWW_SESSION_TIME(name,id,field)
#define OKWW_SESSION_COUNT(name,id,field)
#include "session_state.def"
#undef OKWW_SESSION_COUNT
#undef OKWW_SESSION_TIME
#undef OKWW_SESSION_BOOL
};
enum class Time:int {
#define OKWW_SESSION_BOOL(name,id,field)
#define OKWW_SESSION_TIME(name,id,field) name=id,
#define OKWW_SESSION_COUNT(name,id,field)
#include "session_state.def"
#undef OKWW_SESSION_COUNT
#undef OKWW_SESSION_TIME
#undef OKWW_SESSION_BOOL
};
// Frozen control flow from combat_session.cpp before migration. SDK accesses
// and native fields are observations; the callbacks perform no physical input.
template<class H> void initialize(H &h){using O=rust::SessionOp;if(h.flag(O::CalibrationExists)){h.action(O::ReadCalibration);for(int i=0;i<6;++i)h.action(O::CalibrationValue,i);}h.action(O::CreateConcerto);h.action(O::InitialRevision);}
template<class H> void pollControls(H &h){
    using O=rust::SessionOp;
    if(h.flag(O::AutoTask)&&!h.flag(O::AutoEnabled))h.action(O::Throw,0);
    if(h.flag(O::RevisionChanged)){h.action(O::StoreRevision);if(h.flag(O::AutoTask)){h.set(Time::MacroRearmAt,h.now());h.set(Bool::PendingMacroReset,true);}}
    if(h.get(Bool::PendingMacroReset)){
        if(h.get(Bool::MacroBusy))h.action(O::Throw,1);
        h.set(Bool::PendingMacroReset,false);if(h.flag(O::MacroExists))h.action(O::MacroReset);
    }
    const bool down=h.flag(O::PhysicalFExists)&&h.flag(O::PhysicalFRead);
    if(down&&!h.get(Bool::PhysicalFWasDown)&&!h.get(Bool::MacroBusy))h.set(Time::MacroRearmAt,h.now());
    h.set(Bool::PhysicalFWasDown,down);
}
template<class H> double current(H &h){using O=rust::SessionOp;const int count=int(h.value(O::PartySize));for(int i=0;i<count;++i)if(h.flag(O::MemberCurrent,i))return i;return -1;}
template<class H> bool interval(H &h,double seconds){
    using O=rust::SessionOp;if(seconds<=0)return true;
    if(h.now()-h.get(Time::LastInterval)<seconds){h.action(O::Wait,.001);h.action(O::Refresh);return false;}
    h.set(Time::LastInterval,h.now());return true;
}
template<class H> void tap(H &h,const double *a){using O=rust::SessionOp;if(!h.flag(O::Interval,a[1]))return;h.action(O::InputKey,a[2]);h.action(O::Refresh);if(a[0]>0)h.action(O::Sleep,a[0]);}
template<class H> void click(H &h,double between){using O=rust::SessionOp;if(!h.flag(O::Interval,between))return;h.action(O::InputClick,.01);h.action(O::Refresh);}
template<class H> void watchdog(H &h){
    using O=rust::SessionOp;h.action(O::Checkpoint);
    if(h.get(Time::Deadline)>0&&h.now()>h.get(Time::Deadline))h.action(O::Throw,3);
    if((!h.get(Bool::Engaged)&&!h.get(Bool::CombatScope)&&!h.get(Bool::MacroBusy))||h.get(Bool::CheckingDialog)||h.now()-h.get(Time::LastDeathCheck)<1)return;
    h.set(Time::LastDeathCheck,h.now());const auto saved=h.get(Bool::CheckingDialog);h.set(Bool::CheckingDialog,true);
    try{
        if(h.flag(O::ReadRunDialog)){
            h.action(O::ReleaseInput);h.action(O::Log,0);
            for(int attempt=0;attempt<3;++attempt){
                h.action(O::InputEscape,.05);h.action(O::Wait,.4);h.action(O::Refresh);
                if(!h.flag(O::ReadRunDialog)){h.action(O::Log,1);h.set(Bool::CheckingDialog,saved);return;}
            }
            h.action(O::Throw,4);
        }
        if(h.flag(O::DeathDialog)){h.action(O::ReleaseInput);h.action(O::Throw,5);}
        if(h.get(Bool::MacroBusy)&&!h.flag(O::HudVisible)){
            const int result=int(h.value(O::ChallengeResult));
            if(result){h.action(O::ReleaseInput);h.action(O::Throw,result==2?6:7);}
        }
    }catch(...){h.set(Bool::CheckingDialog,saved);throw;}
    h.set(Bool::CheckingDialog,saved);
}
template<class H> void nextFrame(H &h){using O=rust::SessionOp;h.action(O::Wait,.001);h.action(O::Refresh);h.action(O::Watchdog);}
template<class H> void sleep(H &h,double seconds){
    using O=rust::SessionOp;if(!std::isfinite(seconds)||seconds<0||seconds>86400)h.action(O::Throw,2);
    const double end=h.now()+seconds;
    do{h.action(O::Wait,std::min(.4,std::max(0.,end-h.now())));h.action(O::Refresh);h.action(O::Watchdog);
        if(!h.get(Bool::SkipCombatCheck)&&!h.get(Bool::Checking))h.action(O::CheckCombat);
    }while(h.now()<end);
}
template<class H> double cooldown(H &h,const double *a){
    using O=rust::SessionOp;if(!h.flag(O::CurrentExists))return 0;
    const auto index=h.value(O::CurrentIndex);h.action(O::RefreshCooldowns,index);
    return h.pair(O::Cooldown,a[0],a[1]<0?index:a[1]);
}
template<class H> bool available(H &h,const double *a){using O=rust::SessionOp;return (!a[2]||h.value(O::Highlighted,a[0])>0)&&(!a[3]||!h.pair(O::HasCd,a[0],a[1]));}
template<class H> bool healthBar(H &h){
    using O=rust::SessionOp;if(h.flag(O::HealthBarNative,h.get(Bool::Engaged)))return true;
    if(!h.flag(O::InTeam))return false;
    if(h.now()-h.get(Time::LastHealthCheck)>=.2){h.set(Bool::HealthCache,h.flag(O::ShiftedHealthBar));h.set(Time::LastHealthCheck,h.now());}
    return h.get(Bool::HealthCache);
}
template<class H> bool targetEnemy(H &h){
    using O=rust::SessionOp;if(h.get(Bool::Engaged)&&h.flag(O::InTeam)&&h.flag(O::SessionHealthBar))return true;
    if(h.flag(O::TargetLocked))return true;const auto start=h.now();
    while(h.now()-start<h.get(Time::TargetEnemyTimeout)){
        h.action(O::ClickMiddle,.2);if(h.flag(O::InTeam)&&h.flag(O::SessionHealthBar))return true;
        if(h.flag(O::TargetLocked))return true;h.action(O::NextFrame);
    }
    return false;
}
template<class H> void refreshObservation(H &h){using O=rust::SessionOp;h.action(O::Refresh);h.action(O::Watchdog);}
template<class H> void workshopCheckpoint(H &h){
    using O=rust::SessionOp;h.action(O::Checkpoint);
    if(h.get(Time::LastWorkshopObservation)<0||h.now()-h.get(Time::LastWorkshopObservation)>=.1){h.set(Time::LastWorkshopObservation,h.now());h.action(O::RefreshObservation);}
}
template<class H> double run(H &h,int method,std::span<const double> a){
    switch(method){case 0:initialize(h);break;case 1:pollControls(h);break;case 2:return current(h);case 3:return interval(h,a[0]);case 4:tap(h,a.data());break;
        case 5:click(h,a[0]);break;case 6:watchdog(h);break;case 7:nextFrame(h);break;case 8:sleep(h,a[0]);break;case 9:return cooldown(h,a.data());
        case 10:return available(h,a.data());case 11:return healthBar(h);case 12:return targetEnemy(h);case 13:refreshObservation(h);break;
        case 14:workshopCheckpoint(h);break;case 15:return cooldown(h,a.data())>.2;}
    return 0;
}
}
