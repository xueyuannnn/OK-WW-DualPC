#pragma once
#include "reference_session_detection.hpp"
#include "runtime/combat_session.hpp"
namespace okww::runtime::reference_session {
template<class H> bool restoreCondition(H &h){
    using O=rust::SessionOp;h.action(O::Watchdog);if(h.flag(O::InTeam))return true;
    if(!h.flag(O::ResultChecked)||h.now()-h.value(O::ResultCheckedAt)>=.5){
        h.action(O::SetResultChecked,1);h.action(O::SetResultCheckedAt,h.now());const int result=int(h.value(O::ChallengeResult));
        if(result)h.action(O::Throw,result==2?10:11);
    }
    return false;
}
template<class H> bool combatActive(H &h){
    using O=rust::SessionOp;if(h.get(Bool::InLiberation))return true;
    if(h.get(Bool::Engaged)){
        if(h.flag(O::CheckedVersionMatches))return true;
        if(!h.flag(O::InTeam)){
            h.action(O::ReleaseInput);h.action(O::Log,2);
            if(!h.pair(O::WaitHudRestore,15,.1))h.action(O::Throw,12);h.action(O::Log,3);
        }
        h.action(O::CheckBreak);const bool current=h.flag(O::CurrentExists),python=h.flag(O::PythonMacro);
        if(current&&(python?h.flag(O::PythonSkipCombatCheck):h.flag(O::BuiltinSkipCombatCheck)))return true;
        if(h.flag(O::HasOnCombatCheck)&&!h.flag(O::OnCombatCheck)){h.action(O::ResetCombat);return false;}
        if(h.flag(O::TargetLocked)){h.action(O::SetCheckedVersionFrame);return true;}
        if(h.flag(O::HasEndCondition)&&h.flag(O::EndCondition)){h.action(O::ResetCombat);return false;}
        if(h.flag(O::TargetEnemy)){h.action(O::SetCheckedVersionFrame);return true;}
        if(h.flag(O::MonthlyCard))return true;
        h.action(O::ResetCombat);return false;
    }
    if(!h.flag(O::LoadParty))return false;
    const bool target=h.flag(O::TargetLocked),autoTarget=!h.flag(O::AutoTask)||h.flag(O::AutoTargetSetting);
    const bool boss=h.flag(O::BossFeatures);
    if(!target&&!(autoTarget&&(h.flag(O::SessionHealthBar)||boss)))return false;
    if(!target&&!h.flag(O::TargetEnemy))return false;
    h.set(Bool::HasLevitator,h.flag(O::LevitatorFeature));h.set(Bool::Engaged,true);h.set(Bool::ContextCombat,true);return true;
}
template<class H> void releaseReset(H &h){using O=rust::SessionOp;h.action(O::ReleaseInput);h.action(O::ResetCombat);}
template<class H> bool runAutoCombat(H &h){
    using O=rust::SessionOp;if(!h.flag(O::InTeam))return false;
    const bool earth=h.flag(O::EarthFeature),exit=h.flag(O::RealmExitFeature),world=earth&&!exit,realm=exit&&!earth;
    h.set(Bool::OpenWorld,!realm);h.set(Bool::UseLiberation,h.flag(O::UseLiberationSetting)||!world);
    bool ran=false,switched=false;
    try{
        while(h.flag(O::CombatActive)){
            ran=true;if(!switched){h.action(O::SwitchHealer);switched=true;}
            h.action(O::PerformCurrent);h.action(O::NextFrame);
        }
    }catch(const CharacterDead &){releaseReset(h);throw;}
    catch(const CombatInterrupted &){releaseReset(h);h.logException(std::current_exception());return ran;}
    catch(const CombatEnded &){h.logException(std::current_exception());}
    catch(...){releaseReset(h);throw;}
    h.action(O::ReleaseInput);
    try{
        if(ran){
            if(h.flag(O::AutoEnabled)){
                if(h.flag(O::MacroExists))h.action(O::MacroCombatEnd);
                if(!h.flag(O::MacroExists)||!h.flag(O::SelectedPython))if(h.flag(O::CurrentExists))h.action(O::ActorCombatEnd);
            }
            const int count=int(h.value(O::PartySize));for(int i=0;i<count;++i)h.action(O::ResetMember,i);
            if(h.flag(O::AutoEnabled))h.action(O::SwitchHealer);
        }
    }catch(...){releaseReset(h);throw;}
    h.action(O::ResetCombat);return ran;
}
template<class H> double battleRun(H &h,int method){
    switch(method){case 26:return combatActive(h);case 27:return restoreCondition(h);case 28:return runAutoCombat(h);
        case 29:h.action(rust::SessionOp::Watchdog);return h.flag(rust::SessionOp::CombatActive);}
    return 0;
}
}
