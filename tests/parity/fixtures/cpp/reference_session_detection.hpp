#pragma once
#include "reference_session_core.hpp"
namespace okww::runtime::reference_session {
template<class H,class F> auto guarded(H &h,Bool field,bool value,F body){
    const auto previous=h.get(field);h.set(field,value);
    try{auto result=body();h.set(field,previous);return result;}catch(...){h.set(field,previous);throw;}
}
template<class H> bool targetLocked(H &h){
    using O=rust::SessionOp;const bool current=h.flag(O::CurrentExists),python=h.flag(O::PythonMacro);
    const bool shortTarget=current&&(python?h.flag(O::PythonShortTarget):h.flag(O::BuiltinShortTarget));
    auto target=h.observe(O::ObserveTarget,shortTarget);
    if(!target[0]||!target[1]||h.value(O::GetCount,1)!=0)return target[0]!=0;
    return guarded(h,Bool::Checking,true,[&]{
        h.action(O::Sleep,1);target=h.observe(O::ObserveTarget,shortTarget);
        if(!target[0]||!target[1])return target[0]!=0;
        h.action(O::TapEscape,2);h.action(O::TapEscape,1.5);h.pair(O::SetCount,1,1);return false;
    });
}
template<class H> bool checkBreak(H &h){
    using O=rust::SessionOp;if(h.get(Bool::CanBreak))return true;
    if(h.flag(O::FullBreakFeature)){h.set(Bool::CanBreak,true);return true;}
    if(!h.get(Bool::InLiberation)&&h.now()-h.get(Time::LastBreakCheck)>1){
        h.set(Time::LastBreakCheck,h.now());if(h.flag(O::BreakFeature)&&!h.flag(O::DialogInteract)){h.set(Bool::CanBreak,true);return true;}
    }
    return false;
}
template<class H> bool macroBreakPrompt(H &h){using O=rust::SessionOp;if(h.flag(O::VisibleBreakPrompt,.8))h.set(Bool::CanBreak,true);return h.get(Bool::CanBreak);}
template<class H> bool visibleBreakPrompt(H &h,double threshold){using O=rust::SessionOp;return h.flag(O::FindBreakPrompt,threshold)&&!h.flag(O::DialogInteract);}
template<class H> bool breakF(H &h,bool force){
    using O=rust::SessionOp;if(force)h.action(O::TapF,.05);
    if(!h.get(Bool::CanBreak)&&!h.flag(O::CheckBreak))return false;
    const auto start=h.now();
    while(h.now()-start<.5||(h.now()-start<5&&(h.get(Bool::CanBreak)||h.flag(O::CheckBreak)))){
        h.action(O::TapF,.1);h.action(O::ClickDefault);h.action(O::Sleep,.1);h.set(Bool::CanBreak,false);
    }
    return true;
}
template<class H> void checkCombat(H &h){
    using O=rust::SessionOp;h.action(O::Checkpoint);h.action(O::Watchdog);
    if(h.get(Bool::SkipCombatCheck)||h.get(Bool::Checking)||!h.get(Bool::Engaged))return;
    guarded(h,Bool::Checking,true,[&]{if(!h.flag(O::CombatActive))h.action(O::Throw,8);return 0;});
}
template<class H> void resetCombat(H &h){
    using O=rust::SessionOp;h.set(Bool::ContextCombat,false);h.pair(O::SetCount,1,0);
    h.set(Bool::CanBreak,false);h.set(Bool::HasLevitator,false);h.set(Bool::InLiberation,false);h.set(Bool::Engaged,false);
    h.action(O::ClearCheckedVersion);h.action(O::VisionReset);h.set(Time::LastHealthCheck,-1);h.set(Bool::HealthCache,false);
}
template<class H> void syncCurrent(H &h,double current){
    using O=rust::SessionOp;const int count=int(h.value(O::PartySize));
    for(int i=0;i<count;++i)h.pair(O::SetMemberCurrent,i,h.value(O::MemberIndex,i)==current);
}
template<class H> void performCurrent(H &h){
    using O=rust::SessionOp;const auto hud=h.observe(O::ReadHud);
    if(hud[0]&&hud[1]==h.value(O::PartySize)&&hud[2]>=0&&hud[2]<hud[1])syncCurrent(h,hud[2]);
    if(!h.flag(O::CurrentExists))h.action(O::Throw,9);
    h.action(O::SetActorPerform,h.now());if(!h.flag(O::MacroExists)||!h.flag(O::MacroPerform))h.action(O::ActorPerform);
    h.action(O::PublishParty);
}
template<class H> void updatePortraits(H &h){
    using O=rust::SessionOp;const int count=int(h.value(O::PartySize));
    for(int i=0;i<count;++i)if(!h.flag(O::MemberCurrent,i)&&h.value(O::MemberRing,i)>=0&&!h.flag(O::MemberLiberationReady,i)&&h.flag(O::FindLiberationPortrait,i))h.action(O::SetMemberLiberationReady,i);
}
template<class H> bool isHealer(H &h,double member){using O=rust::SessionOp;return h.flag(O::PythonMacro)?h.flag(O::PythonHealer,member):h.value(O::MemberRole,member)==2;}
template<class H> void switchHealer(H &h){
    using O=rust::SessionOp;if(!h.flag(O::HudVisible))return;
    const bool task=h.flag(O::TaskIdMatch,0)||h.flag(O::TaskIdMatch,1);
    const bool own=task||h.flag(O::AutoTask);
    const bool enabled=own?h.flag(O::HealerTaskSetting):h.flag(O::HealerGlobalSetting);
    if(!enabled)return;
    if(!h.flag(O::CurrentExists)||isHealer(h,-1))return;
    int healer=-1;const int count=int(h.value(O::PartySize));
    for(int i=0;i<count;++i)if(isHealer(h,i)){healer=i;break;}
    if(healer<0)return;
    guarded(h,Bool::SkipCombatCheck,true,[&]{
        const auto start=h.now();while(h.now()-start<6){
            const auto hud=h.observe(O::ReadHud);
            if(hud[0]&&hud[2]!=h.value(O::CurrentIndex)){syncCurrent(h,hud[2]);return 0;}
            h.action(O::TapMember,healer);h.action(O::Sleep,.2);
        }
        return 0;
    });
}
template<class H> double detectionRun(H &h,int method,std::span<const double> a){
    switch(method){case 16:return targetLocked(h);case 17:return checkBreak(h);case 18:return macroBreakPrompt(h);case 19:return visibleBreakPrompt(h,a[0]);
        case 20:return breakF(h,a[0]!=0);case 21:checkCombat(h);break;case 22:resetCombat(h);break;case 23:performCurrent(h);break;
        case 24:updatePortraits(h);break;case 25:switchHealer(h);break;}
    return 0;
}
}
