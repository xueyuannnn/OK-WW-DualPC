use super::*;
impl Host {
    pub(super) fn load_party(&self) -> SessionResult<bool> {
        let hud = self.values(Op::ReadHud, &[])?;
        if hud[0] == 0. {
            return Ok(false);
        }
        if self.flag(Op::PartyScanMatches, &[])? && self.call(Op::PartySize, &[])? == hud[1] {
            self.sync_current_members(hud[2])?;
            self.action(Op::PublishParty, &[])?;
            return Ok(self.call(Op::PartySize, &[])? >= 2.);
        }
        self.action(Op::StorePartyScan, &[])?;
        let count = hud[1] as usize;
        for index in 0..count {
            let previous = (index as f64) < self.call(Op::PartySize, &[])?;
            self.action(Op::ReadIdentity, &[index as f64, previous as u8 as f64])?;
            if self.flag(Op::ObservedIdentityExists, &[])?
                && !self.flag(Op::ObservedIdentitySupported, &[])?
            {
                self.action(Op::UnsupportedCharacter, &[index as f64])?;
                return Err(HOST_ERROR);
            }
            self.action(Op::AppendIdentity, &[])?;
        }
        let mut changed = self.call(Op::PartySize, &[])? != hud[1];
        let identities = self.call(Op::IdentityCount, &[])? as usize;
        for index in 0..identities {
            let index = index as f64;
            if self.flag(Op::IdentityExists, &[index])?
                && (index >= self.call(Op::PartySize, &[])?
                    || !self.flag(Op::MemberExists, &[index])?
                    || !self.flag(Op::MemberMatchesIdentity, &[index])?)
            {
                changed = true;
            }
        }
        if changed {
            self.action(Op::DropMacro, &[])?;
            self.action(Op::ClearSelectedTeam, &[])?;
        }
        self.action(Op::ResizeParty, &[hud[1]])?;
        self.action(Op::ResizeDefinitions, &[hud[1]])?;
        for index in 0..count {
            let index = index as f64;
            if self.flag(Op::IdentityExists, &[index])? {
                self.action(Op::CopyIdentityDefinition, &[index])?;
                if !self.flag(Op::MemberExists, &[index])?
                    || !self.flag(Op::MemberMatchesIdentity, &[index])?
                {
                    self.action(Op::CreateIdentifiedMember, &[index])?;
                }
                self.action(Op::StoreIdentityDefinition, &[index])?;
            } else if !self.flag(Op::MemberExists, &[index])? {
                self.action(Op::CreateUnknownMember, &[index])?;
            }
            self.action(Op::ResetMember, &[index])?;
            self.action(
                Op::SetMemberCurrent,
                &[index, (index == hud[2]) as u8 as f64],
            )?;
        }
        if self.flag(Op::TeamsExist, &[])? {
            self.action(Op::ReadTeamSelection, &[])?;
        } else {
            self.action(Op::EmptyTeamSelection, &[])?;
        }
        if !self.flag(Op::TeamSelectionMatches, &[])? {
            self.action(Op::DropMacro, &[])?;
            self.action(Op::StoreTeamSelection, &[])?;
            self.action(Op::ClearBridgeInfo, &[])?;
            let python = self.flag(Op::SelectionPython, &[])?;
            self.action(Op::PublishPartyState, &[python as u8 as f64])?;
            if self.flag(Op::TeamsExist, &[])? {
                self.action(Op::CreateSelectedMacro, &[])?;
            }
        }
        if self.flag(Op::MacroExists, &[])? {
            self.action(Op::MacroReset, &[])?;
        }
        self.set_time(Time::CombatStart, self.now()?)?;
        self.action(Op::PublishParty, &[])?;
        Ok(self.call(Op::PartySize, &[])? >= 2.)
    }
    pub(super) fn publish_party(&self) -> SessionResult {
        if !self.flag(Op::HasCombatInfo, &[])? {
            return Ok(());
        }
        let hud = self.values(Op::ReadHud, &[])?;
        let count = self.call(Op::PartySize, &[])? as usize;
        for index in 0..count {
            let current = hud[0] != 0. && self.call(Op::MemberIndex, &[index as f64])? == hud[2];
            self.action(Op::AppendPartyMember, &[index as f64, current as u8 as f64])?;
        }
        let mode = if self.flag(Op::SelectedPython, &[])? {
            1.
        } else if self.flag(Op::SelectedTeamEmpty, &[])? {
            0.
        } else {
            2.
        };
        self.action(Op::BuildPartyIdentity, &[mode, hud[0]])?;
        self.action(Op::BuildPartyKey, &[])?;
        self.action(Op::InitializePartyReport, &[])?;
        if self.flag(Op::SelectedPython, &[])? {
            self.action(Op::AppendPartyBridge, &[])?;
        }
        if self.flag(Op::PartyReportMatches, &[])?
            || (self.flag(Op::PartyKeyMatches, &[])?
                && self.now()? - self.time(Time::LastPartyReport)? < 0.25)
        {
            return Ok(());
        }
        self.action(Op::StorePartyKey, &[])?;
        self.action(Op::StorePartyReport, &[])?;
        self.set_time(Time::LastPartyReport, self.now()?)?;
        self.action(Op::EmitPartyInfo, &[])
    }
}
