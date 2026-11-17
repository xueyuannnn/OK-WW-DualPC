#pragma once
#include "reference_session_switch.hpp"
namespace okww::runtime::reference_session {
template<class H> void freeHeal(H &h,bool atHealPoint){
    using O=rust::SessionOp;h.action(O::CreateNavigation);const bool realm=h.flag(O::InRealm);
    if(realm)h.action(O::ExitRealm,120);if(!realm||!atHealPoint)h.action(O::HealAtBeacon);
}
template<class H> void recoverStartingPopup(H &h,bool atHealPoint){using O=rust::SessionOp;if(!h.flag(O::DeathDialog))return;h.action(O::ResetCombat);h.action(O::CloseRevivalDialog);freeHeal(h,atHealPoint);h.action(O::Log,5);}
template<class H> void recoverDead(H &h,bool atHealPoint){
    using O=rust::SessionOp;h.action(O::ReleaseInput);h.action(O::ResetCombat);h.set(Time::Deadline,0);h.action(O::CloseRevivalDialog);
    if(h.flag(O::HasRecovery))h.action(O::CustomRecovery);else freeHeal(h,atHealPoint);
    h.action(O::ResetCombat);h.action(O::Log,4);h.action(O::Throw,18);
}
template<class H> bool closeRevivalDialog(H &h){
    using O=rust::SessionOp;if(!h.flag(O::DeathDialog))return false;h.action(O::ReleaseInput);
    return guarded(h,Bool::CheckingDialog,true,[&]{return guarded(h,Bool::SkipCombatCheck,true,[&]{
        h.action(O::ContextEscape,.5);if(h.flag(O::WaitDeathClosed,2))return true;
        h.closePopup(.803,.196,.5);if(!h.flag(O::WaitDeathClosed,2))h.action(O::Throw,19);return true;
    });});
}
template<class H> bool combatOnce(H &h,std::span<const double> a){
    using O=rust::SessionOp;if(!std::isfinite(a[0])||a[0]<0||!std::isfinite(a[5])||a[5]<=0||a[5]>7200)h.action(O::Throw,20);
    return guarded(h,Bool::CombatScope,true,[&]{return guarded(h,Bool::SkipCombatCheck,false,[&]{
        h.set(Bool::OpenWorld,false);h.set(Bool::UseLiberation,a[4]!=0);h.set(Time::LastDeathCheck,-1);
        try{
            if(h.flag(O::DeathDialog))h.action(O::Throw,5);
            const auto until=h.now()+a[0];bool found=false;
            do{h.action(O::Watchdog);if(h.flag(O::CombatActive)){found=true;break;}
                if(a[2])h.action(O::ClickMiddle,.5);h.action(O::ContextSleep,.02);
            }while(h.now()<until);
            if(!found){if(a[1])h.action(O::Throw,15);h.action(O::ResetCombat);return false;}
            h.set(Time::Deadline,h.now()+a[5]);const double count=h.value(O::GetCount,0)+1;h.pair(O::SetCount,0,count);
            if(h.flag(O::HasInfo))h.action(O::CombatCountInfo,count);h.action(O::SwitchHealer);
            int interruptions=0;const int retries=h.flag(O::ResumeInterrupted)?std::clamp(int(h.value(O::RecoveryRetryLimit)),1,10):0;
            for(;;){
                try{while(h.flag(O::CombatActive)){h.action(O::Watchdog);h.action(O::PerformCurrent);h.action(O::NextFrame);}break;}
                catch(const CharacterDead &){throw;}
                catch(const CombatInterrupted &){
                    const auto error=std::current_exception();h.action(O::ReleaseInput);h.set(Bool::InLiberation,false);h.logException(error);
                    if(++interruptions>retries)h.interruptionLimit(error);
                    if(!h.flag(O::WaitTeamOrDeath,5))h.action(O::Throw,17);
                    h.action(O::Watchdog);if(h.flag(O::DeathDialog))h.action(O::Throw,5);h.logRetry(interruptions,retries);
                }catch(const CombatEnded &){h.logException(std::current_exception());break;}
            }
            if(h.flag(O::DeathDialog))h.action(O::Throw,5);h.action(O::ReleaseInput);
            if(h.flag(O::MacroExists))h.action(O::MacroCombatEnd);
            if(!h.flag(O::MacroExists)||!h.flag(O::SelectedPython))if(h.flag(O::CurrentExists))h.action(O::ActorCombatEnd);
            const int members=int(h.value(O::PartySize));for(int i=0;i<members;++i)h.action(O::ResetMember,i);
            h.action(O::SwitchHealer);if(h.flag(O::DeathDialog))h.action(O::Throw,5);
            h.set(Time::Deadline,0);h.action(O::ResetCombat);h.action(O::WaitTeamAfterCombat,10);return true;
        }catch(const Cancelled &){h.action(O::ReleaseInput);h.set(Time::Deadline,0);h.action(O::ResetCombat);throw;}
        catch(const CharacterDead &){h.action(O::RecoverDead);}
        catch(...){h.action(O::ReleaseInput);h.set(Time::Deadline,0);h.action(O::ResetCombat);if(h.flag(O::DeathDialog))h.action(O::RecoverDead);throw;}
        return false;
    });});
}
template<class H> double recoveryRun(H &h,int method,std::span<const double> a){
    using O=rust::SessionOp;
    switch(method){case 31:recoverStartingPopup(h,a[0]!=0);break;case 32:return closeRevivalDialog(h);case 33:return combatOnce(h,a);
        case 34:recoverDead(h,a[0]!=0);break;case 35:return !h.flag(O::DeathDialog);case 36:return h.flag(O::InTeam)||h.flag(O::DeathDialog);}
    return 0;
}
}
