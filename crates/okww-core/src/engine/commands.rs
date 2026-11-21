use super::*;
include!(concat!(env!("OUT_DIR"), "/commands_abi.rs"));

impl Host {
    pub(super) fn team_edit_guard(&self) -> EngineResult {
        if !self.flag(Op::Paused, &[])?
            || !self.flag(Op::WorkerFinished, &[])?
            || self.flag(Op::HasPending, &[])?
        {
            return self.fail(7.);
        }
        if self.flag(Op::WorkerJoinable, &[])? {
            self.action(Op::JoinWorker, &[])?;
        }
        Ok(())
    }
    fn command_body(&self) -> EngineResult<bool> {
        let command = Command::try_from(self.call(Op::RequestCommand, &[])? as i32)?;
        match command {
            Command::TeamMode | Command::TeamAllMode | Command::TeamImport => {
                self.action(Op::SwitchTeam, &[])?;
                return Ok(false);
            }
            Command::Script => {
                self.action(Op::ScriptCommand, &[])?;
                return Ok(false);
            }
            Command::Catalog => self.action(Op::ResultCatalog, &[])?,
            Command::Pause | Command::Shutdown => {
                self.action(Op::Pause, &[])?;
                self.action(Op::ResultStatus, &[0.])?;
            }
            Command::StopTask => {
                if self.flag(Op::PendingTaskNonempty, &[])? {
                    self.action(Op::PendingTaskEmpty, &[])?;
                    self.action(Op::PendingExitFalse, &[])?;
                    self.action(Op::PendingLimitZero, &[])?;
                    self.action(Op::SetForeground, &[0.])?;
                } else if self.flag(Op::Foreground, &[])? && !self.flag(Op::Paused, &[])? {
                    self.action(Op::SetCancelForeground, &[1.])?;
                }
                self.action(Op::ResultStatus, &[0.])?;
            }
            Command::Start => {
                self.action(Op::StartRequest, &[])?;
                self.action(Op::ResultStatus, &[1.])?;
            }
            Command::TaskConfig | Command::GlobalConfig => {
                if self.flag(Op::TaskIsScript, &[])? {
                    self.action(Op::ScriptConfigureTask, &[])?;
                    return Ok(false);
                }
                self.action(
                    Op::SetProfile,
                    &[(command == Command::TaskConfig) as u8 as f64],
                )?;
                self.action(Op::ResultCatalog, &[])?;
            }
            Command::ResetConfig => {
                if self.flag(Op::GroupIsScript, &[])? {
                    self.action(Op::ScriptResetGroup, &[])?;
                    return Ok(false);
                }
                self.action(Op::ResetProfile, &[])?;
                self.action(Op::ResultCatalog, &[])?;
            }
            Command::Trigger => {
                if self.flag(Op::TaskIsScript, &[])? {
                    self.action(Op::ScriptToggleTask, &[])?;
                    return Ok(false);
                }
                if self.flag(Op::RequestEnabled, &[])? && !self.native_available(2.)? {
                    return self.fail(8.).map(|_| false);
                }
                let previous = self.flag(Op::ProfileWasEnabled, &[])?;
                self.action(Op::SetTrigger, &[])?;
                if self.flag(Op::RequestAutoCombat, &[])?
                    && !previous
                    && self.flag(Op::RequestEnabled, &[])?
                {
                    self.action(Op::IncrementCombatRevision, &[])?;
                }
                self.action(Op::ResultStatus, &[0.])?;
            }
            Command::TeamCode
            | Command::TeamEditor
            | Command::TeamExport
            | Command::TeamCreate
            | Command::TeamDelete
            | Command::TeamSave
            | Command::Team => {
                if self.flag(Op::TeamBusy, &[])? {
                    return self.fail(6.).map(|_| false);
                }
                match command {
                    Command::TeamCode => self.action(Op::TeamCode, &[])?,
                    Command::TeamEditor => self.action(Op::TeamEditor, &[])?,
                    Command::TeamExport => self.action(Op::TeamExport, &[])?,
                    _ => {
                        self.team_edit_guard()?;
                        match command {
                            Command::TeamCreate => self.action(Op::TeamCreate, &[])?,
                            Command::TeamDelete => self.action(Op::TeamDelete, &[])?,
                            Command::TeamSave => self.action(Op::TeamSave, &[])?,
                            _ => return self.fail(9.).map(|_| false),
                        }
                        self.action(Op::InvalidateParty, &[])?;
                        self.action(Op::ResultCatalog, &[])?;
                    }
                }
            }
            Command::Unknown => return self.fail(10.).map(|_| false),
        }
        self.action(Op::ReplySuccess, &[])?;
        Ok(true)
    }
    pub(super) fn command(&self) -> EngineResult {
        match self.command_body() {
            Ok(false) => return Ok(()),
            Ok(true) => {}
            Err(error) => {
                if self.call(Op::ExceptionKind, &[])? == 5. {
                    return Err(error);
                }
                self.action(Op::ClearException, &[])?;
                self.action(Op::ReplyError, &[])?;
            }
        }
        self.action(Op::EmitReply, &[])
    }
}

pub fn parse(value: &[u8]) -> i32 {
    Command::parse(value) as i32
}

pub fn script_key(value: &[u8]) -> i32 {
    match value {
        b"_enabled" => 0,
        b"_continuous" => 1,
        b"_repeat" => 2,
        _ => 3,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn routing_preserves_exact_names_and_prefixes() {
        assert_eq!(Command::parse(b"team_mode"), Command::TeamMode);
        assert_eq!(Command::parse(b"team_future"), Command::Team);
        assert_eq!(Command::parse(b"script_configure"), Command::Script);
        assert_eq!(Command::parse(b"script"), Command::Unknown);
        assert_eq!(Command::parse(b"START"), Command::Unknown);
        assert_eq!(Command::parse(b"start\0"), Command::Unknown);
    }
}
