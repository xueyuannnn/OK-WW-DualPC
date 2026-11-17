use super::*;
impl Host {
    pub(super) fn fail(&self, reason: f64) -> EngineResult {
        self.action(Op::Error, &[reason])?;
        Err(HOST_ERROR)
    }
    pub(super) fn pause(&self) -> EngineResult {
        self.action(Op::LockLifecycle, &[])?;
        let result = (|| {
            self.action(Op::ClearPending, &[])?;
            self.action(Op::LaunchTimerStop, &[])?;
            self.action(Op::StopGate, &[])?;
            self.action(Op::GenerationZero, &[])?;
            self.action(Op::SetPaused, &[1.])?;
            self.action(Op::SetForeground, &[0.])?;
            self.action(Op::SetCancelForeground, &[0.])?;
            if let Err(error) = self.action(Op::BackendArm, &[0.]) {
                if self.call(Op::ExceptionKind, &[])? == 5. {
                    return Err(error);
                }
                self.action(Op::ClearException, &[])?;
                self.action(Op::LogException, &[])?;
            }
            Ok(())
        })();
        let unlock = self.action(Op::UnlockLifecycle, &[]);
        result.and(unlock)
    }
    pub(super) fn native_available(&self, source: f64) -> EngineResult<bool> {
        let count = self.call(Op::NativeCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::NativeSelect, &[index as f64])?;
            if self.flag(Op::NativeMatches, &[source])? {
                return Ok(true);
            }
        }
        Ok(false)
    }
    pub(super) fn start(&self) -> EngineResult {
        if self.flag(Op::TeamBusy, &[])? {
            return self.fail(0.);
        }
        if self.flag(Op::ScriptsBusy, &[])? {
            return self.fail(1.);
        }
        self.action(Op::ExitTimerStop, &[])?;
        let limit = self.call(Op::LimitOption, &[])?;
        if !limit.is_finite() || !(0. ..=604800.).contains(&limit) {
            return self.fail(2.);
        }
        let mut script = false;
        let count = self.call(Op::ScriptsCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectScriptEntry, &[index as f64])?;
            if self.flag(Op::ScriptMatches, &[])? {
                script = true;
                break;
            }
        }
        if script && !self.flag(Op::ScriptSupported, &[])? {
            return self.fail(3.);
        }
        if self.flag(Op::HasRequestedTask, &[])? && !script && !self.native_available(0.)? {
            return self.fail(4.);
        }
        if !self.flag(Op::HasRequestedTask, &[])? {
            self.action(Op::ProfileCatalogLoad, &[])?;
            let count = self.call(Op::CatalogCount, &[])? as usize;
            for index in 0..count {
                self.action(Op::SelectCatalog, &[index as f64])?;
                if self.flag(Op::CatalogTrigger, &[])?
                    && self.flag(Op::CatalogEnabled, &[])?
                    && !self.flag(Op::CatalogRemoteReset, &[])?
                    && !self.native_available(1.)?
                {
                    return self.fail(5.);
                }
            }
        }
        self.action(Op::Pause, &[])?;
        self.action(Op::Preload, &[])?;
        self.action(Op::PendingTaskSet, &[])?;
        self.action(
            Op::SetForeground,
            &[self.flag(Op::HasRequestedTask, &[])? as u8 as f64],
        )?;
        self.action(Op::PendingOptionsSet, &[])?;
        self.action(Op::PendingSnapshotSet, &[])?;
        self.action(Op::TryLaunch, &[])?;
        if self.flag(Op::HasPending, &[])? {
            self.action(Op::LaunchTimerStart, &[])?;
        }
        Ok(())
    }
    pub(super) fn try_launch(&self) -> EngineResult {
        if !self.flag(Op::HasPending, &[])? || !self.flag(Op::WorkerFinished, &[])? {
            return Ok(());
        }
        if self.flag(Op::WorkerJoinable, &[])? {
            self.action(Op::JoinWorker, &[])?;
        }
        self.action(Op::CopyLaunchTask, &[])?;
        self.action(Op::ClearPending, &[])?;
        self.action(Op::LaunchTimerStop, &[])?;
        self.action(Op::SetPaused, &[0.])?;
        self.action(Op::StartGate, &[])?;
        self.action(Op::GenerationEpoch, &[])?;
        self.action(Op::SetWorkerFinished, &[0.])?;
        self.action(Op::ScriptFlagsClear, &[])?;
        let count = self.call(Op::PendingScriptsCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::PendingSelectScript, &[index as f64])?;
            self.action(Op::StoreScriptFlag, &[])?;
        }
        if let Err(error) = self.action(Op::SpawnWorker, &[]) {
            let _ = self.action(Op::SetWorkerFinished, &[1.]);
            let _ = self.action(Op::StopGate, &[]);
            let _ = self.action(Op::SetPaused, &[1.]);
            return Err(error);
        }
        Ok(())
    }
    pub(super) fn destroy(&self) -> EngineResult {
        self.action(Op::Pause, &[])?;
        if self.flag(Op::WorkerJoinable, &[])? {
            self.action(Op::JoinWorker, &[])?;
        }
        if self.flag(Op::ModelJoinable, &[])? {
            self.action(Op::JoinModel, &[])?;
        }
        self.action(Op::NotificationsStop, &[])
    }
}
