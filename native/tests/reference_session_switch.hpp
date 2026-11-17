#pragma once
#include "reference_session_battle.hpp"
namespace okww::runtime::reference_session {
template<class H> bool chooseNext(H &h,bool intro,bool lowCon){
    using O=rust::SessionOp;h.action(O::ClearCandidates);const int count=int(h.value(O::PartySize));
    for(int i=0;i<count;++i){
        std::array<double,3> overridden{};
        if(h.flag(O::PythonMacro))overridden=h.priority(O::PythonPriority,i,intro,lowCon);
        else if(h.flag(O::MacroExists))overridden=h.priority(O::NativeMacroPriority,i,intro,lowCon);
        const auto priority=overridden[0]?overridden[1]:h.priority(O::BuiltinPriority,i,intro,lowCon)[0];
        h.pair(O::AppendCandidate,i,priority);
    }
    const auto index=h.value(O::ChooseDecision,intro);h.action(O::LogSwitch);
    const bool same=h.flag(O::DecisionMatchesPrevious);h.action(O::DisposeCandidates);
    if(same){h.action(O::ClearTarget);return false;}h.action(O::SetTarget,index);return true;
}
template<class H> void switchNext(H &h,bool freeIntro,bool lowCon){
    using O=rust::SessionOp;h.action(O::UpdatePortraits);bool intro=freeIntro;
    if(!intro){double con=h.value(O::PreviousCon);if(con>.8&&con!=1){h.action(O::Sleep,.05);h.action(O::NextFrame);con=h.value(O::PreviousCon);}intro=con==1;}
    if(!chooseNext(h,intro,lowCon)){h.action(O::PreviousNormalAttack,.2);return;}
    h.action(O::ApplyIntro,intro);const auto start=h.now();double lastClick=0;
    while(h.now()-start<=10){
        if(!h.flag(O::TargetSkipCheck,intro))h.action(O::CheckCombat);else h.action(O::Watchdog);
        auto hud=h.observe(O::ReadHud);
        if(hud[2]==h.value(O::PreviousIndex)){
            h.action(O::UpdatePortraits);const bool refreshed=intro||h.flag(O::PreviousConFull);
            if(refreshed!=intro){intro=refreshed;if(!chooseNext(h,intro,lowCon)){h.action(O::PreviousNormalAttack,.2);return;}}
            h.action(O::ApplyIntro,intro);if(intro&&h.flag(O::PreviousCheckF))h.action(O::BreakF);
        }
        if(h.flag(O::TargetWaitSwitch)){h.action(O::ClickDefault);h.action(O::Sleep,.1);continue;}
        if(h.now()-lastClick>.1){lastClick=h.now();h.action(O::TapTargetIndex);h.action(O::Sleep,.001);h.action(O::ClickDefault);h.action(O::Sleep,.001);}
        hud=h.observe(O::ReadHud);if(!hud[0]){h.action(O::Watchdog);h.action(O::Throw,14);}
        if(hud[2]==h.value(O::TargetIndex)){
            h.set(Bool::InLiberation,false);if(!intro&&h.flag(O::PreviousCheckF))h.action(O::BreakF);
            h.action(O::PreviousSwitchOut,intro);h.action(O::SetTargetCurrent);h.action(O::SetTargetSwitchIn,h.now());
            if(intro){h.action(O::FreezeSwitchIntro);h.action(O::SetPreviousOutro,h.now());}
            if(h.flag(O::HasSwitchPost))h.action(O::RunSwitchPost,intro);h.action(O::PublishParty);return;
        }
        h.action(O::NextFrame);
    }
    h.action(O::Throw,13);
}
}
