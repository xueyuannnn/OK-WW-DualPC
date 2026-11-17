use super::{commands::Command, *};
impl Host {
    pub(super) fn initialize(&self) -> EngineResult {
        let cache = (|| {
            self.action(Op::ReadInitialScripts, &[])?;
            if self.flag(Op::ScriptCatalogExists, &[])? {
                self.action(Op::ReadInitialTemplates, &[])?;
            }
            Ok::<_, i32>(())
        })();
        if cache.is_err() {
            self.action(Op::ClearException, &[])?;
        }
        self.action(Op::AppendBusinessTasks, &[])?;
        self.action(Op::BindLaunchTimer, &[50.])?;
        self.action(Op::DefaultModelPath, &[])?;
        if !self.flag(Op::ModelExists, &[])? {
            self.action(Op::FallbackModelPath, &[])?;
        }
        self.action(Op::CreateNotifications, &[])?;
        self.action(Op::BindExitTimer, &[100.])
    }
    pub(super) fn exit_poll(&self) -> EngineResult {
        if !self.flag(Op::Paused, &[])? {
            return self.action(Op::ExitTimerStop, &[]);
        }
        if self.flag(Op::NotificationsIdle, &[])? || self.call(Op::ExitElapsed, &[])? > 180_000. {
            self.action(Op::ExitTimerStop, &[])?;
            self.action(Op::EmitQuit, &[])?;
        }
        Ok(())
    }
    pub(super) fn invalidate_party(&self) -> EngineResult {
        self.action(Op::SetTaskInCombat, &[0.])?;
        self.action(Op::SetRetryingStep, &[0.])?;
        self.action(Op::IncrementCombatRevision, &[])?;
        self.action(Op::EmitPartyInvalidated, &[])
    }
    pub(super) fn switch_team(&self) -> EngineResult {
        if self.flag(Op::TeamBusy, &[])? {
            return self.fail(6.);
        }
        self.team_edit_guard()?;
        self.action(Op::SnapshotTeams, &[])?;
        self.action(Op::SetTeamBusy, &[1.])?;
        self.action(Op::SpawnTeamSwitch, &[])
    }
    pub(super) fn team_reply(&self) -> EngineResult {
        self.action(Op::DeleteWatcher, &[])?;
        self.action(Op::SetTeamBusy, &[0.])?;
        self.action(Op::InvalidateParty, &[])?;
        self.action(Op::ReplyRequestId, &[])?;
        let result = (|| {
            self.action(Op::ResultCatalog, &[])?;
            self.action(Op::ResultTeamElapsed, &[])?;
            self.action(Op::EmitCatalogChanged, &[])?;
            self.action(Op::ReplyData, &[])
        })();
        if let Err(error) = result {
            if self.call(Op::ExceptionKind, &[])? == 5. {
                return Err(error);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::ReplyError, &[])?;
        }
        self.action(Op::EmitReply, &[])
    }
    pub(super) fn team_work(&self) -> EngineResult {
        self.action(Op::StartTeamTimer, &[])?;
        let result = (|| {
            if Command::try_from(self.call(Op::RequestCommand, &[])? as i32)? == Command::TeamMode {
                self.action(Op::SetTeamMode, &[])?;
            } else {
                self.action(Op::SetAllTeamModes, &[])?;
            }
            self.action(Op::ReplyOk, &[])
        })();
        if let Err(error) = result {
            if self.call(Op::ExceptionKind, &[])? == 5. {
                return Err(error);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::ReplyError, &[])?;
        }
        self.action(Op::ReplyElapsed, &[])
    }
    pub(super) fn idle(&self) -> EngineResult<bool> {
        Ok(self.flag(Op::Paused, &[])?
            && self.flag(Op::WorkerFinished, &[])?
            && !self.flag(Op::TeamBusy, &[])?)
    }
}
