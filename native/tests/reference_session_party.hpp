#pragma once
#include "reference_session_recovery.hpp"
namespace okww::runtime::reference_session {
template<class H> bool loadParty(H &h){
    using O=rust::SessionOp;const auto hud=h.observe(O::ReadHud);
    if(!hud[0])return false;
    if(h.flag(O::PartyScanMatches)&&h.value(O::PartySize)==hud[1]){
        syncCurrent(h,hud[2]);h.action(O::PublishParty);return h.value(O::PartySize)>=2;
    }
    h.action(O::StorePartyScan);
    for(int i=0;i<int(hud[1]);++i){
        h.pair(O::ReadIdentity,i,i<h.value(O::PartySize));
        if(h.flag(O::ObservedIdentityExists)&&!h.flag(O::ObservedIdentitySupported))h.action(O::UnsupportedCharacter,i);
        h.action(O::AppendIdentity);
    }
    bool changed=h.value(O::PartySize)!=hud[1];const int identities=int(h.value(O::IdentityCount));
    for(int i=0;i<identities;++i)if(h.flag(O::IdentityExists,i)&&(i>=h.value(O::PartySize)||!h.flag(O::MemberExists,i)||!h.flag(O::MemberMatchesIdentity,i)))changed=true;
    if(changed){h.action(O::DropMacro);h.action(O::ClearSelectedTeam);}
    h.action(O::ResizeParty,hud[1]);h.action(O::ResizeDefinitions,hud[1]);
    for(int i=0;i<int(hud[1]);++i){
        if(h.flag(O::IdentityExists,i)){
            h.action(O::CopyIdentityDefinition,i);
            if(!h.flag(O::MemberExists,i)||!h.flag(O::MemberMatchesIdentity,i))h.action(O::CreateIdentifiedMember,i);
            h.action(O::StoreIdentityDefinition,i);
        }else if(!h.flag(O::MemberExists,i))h.action(O::CreateUnknownMember,i);
        h.action(O::ResetMember,i);h.pair(O::SetMemberCurrent,i,i==hud[2]);
    }
    if(h.flag(O::TeamsExist))h.action(O::ReadTeamSelection);else h.action(O::EmptyTeamSelection);
    if(!h.flag(O::TeamSelectionMatches)){
        h.action(O::DropMacro);h.action(O::StoreTeamSelection);h.action(O::ClearBridgeInfo);
        h.action(O::PublishPartyState,h.flag(O::SelectionPython));if(h.flag(O::TeamsExist))h.action(O::CreateSelectedMacro);
    }
    if(h.flag(O::MacroExists))h.action(O::MacroReset);
    h.set(Time::CombatStart,h.now());h.action(O::PublishParty);return h.value(O::PartySize)>=2;
}
template<class H> void publishParty(H &h){
    using O=rust::SessionOp;if(!h.flag(O::HasCombatInfo))return;
    const auto hud=h.observe(O::ReadHud);const int count=int(h.value(O::PartySize));
    for(int i=0;i<count;++i)h.pair(O::AppendPartyMember,i,hud[0]&&h.value(O::MemberIndex,i)==hud[2]);
    const int mode=h.flag(O::SelectedPython)?1:h.flag(O::SelectedTeamEmpty)?0:2;
    h.pair(O::BuildPartyIdentity,mode,hud[0]);h.action(O::BuildPartyKey);h.action(O::InitializePartyReport);
    if(h.flag(O::SelectedPython))h.action(O::AppendPartyBridge);
    if(h.flag(O::PartyReportMatches)||(h.flag(O::PartyKeyMatches)&&h.now()-h.get(Time::LastPartyReport)<.25))return;
    h.action(O::StorePartyKey);h.action(O::StorePartyReport);h.set(Time::LastPartyReport,h.now());h.action(O::EmitPartyInfo);
}
}
