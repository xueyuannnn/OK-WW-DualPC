use super::*;
impl Host {
    pub(super) fn prepare(&self) -> EngineResult {
        if !self.flag(Op::AwakeActive, &[])? {
            self.action(Op::LogAwakeFailure, &[])?;
        }
        self.action(Op::SetWarming, &[1.])?;
        self.action(Op::PostStatusSelected, &[])?;
        self.action(Op::LogPreparation, &[])?;
        let reserve = self.flag(Op::EmergencyAvailable, &[])?;
        self.action(Op::LogEmergency, &[reserve as u8 as f64])?;
        self.action(Op::CreatePreparation, &[])?;
        self.action(Op::Checkpoint, &[])?;
        if !self.flag(Op::SelectedIsScript, &[])? {
            self.action(Op::ValidateTeams, &[])?;
        }
        self.action(Op::Checkpoint, &[])?;
        while !self.flag(Op::ModelsReady, &[])? {
            self.action(Op::Wait, &[0.02])?;
        }
        if !self.flag(Op::ModelErrorEmpty, &[])? {
            self.action(Op::ThrowModelError, &[])?;
            return Err(HOST_ERROR);
        }
        self.action(Op::Checkpoint, &[])?;
        self.action(Op::LockLifecycle, &[])?;
        let result = (|| {
            self.action(Op::Checkpoint, &[])?;
            self.action(Op::BackendArm, &[1.])
        })();
        let unlock = self.action(Op::UnlockLifecycle, &[]);
        result.and(unlock)?;
        self.action(Op::SetWarming, &[0.])?;
        self.action(Op::LogPreparationDone, &[])?;
        self.action(Op::PostStatusSelected, &[])
    }
    pub(super) fn health(&self) -> EngineResult {
        if !self.flag(Op::Heartbeat, &[])? {
            if !self.flag(Op::GateActive, &[])? {
                self.action(Op::ThrowCancelled, &[])?;
                return Err(HOST_ERROR);
            }
            self.action(Op::ReadDeviceState, &[])?;
            let message = if self.flag(Op::DeviceErrorEmpty, &[])? {
                if self.flag(Op::CaptureConnected, &[])? && self.flag(Op::BoxConnected, &[])? {
                    0.
                } else {
                    1.
                }
            } else {
                2.
            };
            self.action(Op::ThrowInputFailure, &[message])?;
            return Err(HOST_ERROR);
        }
        if self.flag(Op::TaskRunning, &[])?
            && self.flag(Op::Foreground, &[])?
            && self.flag(Op::CancelForeground, &[])?
        {
            self.action(Op::ThrowCancelled, &[])?;
            return Err(HOST_ERROR);
        }
        if self.flag(Op::TaskRunning, &[])? && self.flag(Op::HasControlPoll, &[])? {
            self.action(Op::PollControls, &[])?;
        }
        let limit = self.call(Op::LimitSeconds, &[])?;
        if limit > 0. && self.now()? - self.call(Op::RunBegan, &[])? >= limit {
            self.fail(18.)?;
        }
        Ok(())
    }
    pub(super) fn progress(&self) -> EngineResult {
        let selected = self.flag(Op::SelectedEmpty, &[])?;
        self.action(Op::SelectProgressTask, &[selected as u8 as f64])?;
        if !self.flag(Op::ProgressTaskMatches, &[])? {
            self.action(Op::ResetProgressTask, &[])?;
        }
        self.action(Op::InitializeProgressPatch, &[])?;
        let count = self.call(Op::ProgressPatchCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::MergeProgressEntry, &[index as f64])?;
        }
        if self.flag(Op::ProgressUnchanged, &[])? {
            return Ok(());
        }
        self.action(Op::CommitProgress, &[])?;
        let final_update = self.flag(Op::PatchContainsCurrent, &[])?
            && self.call(Op::ProgressTotal, &[])? > 0.
            && self.call(Op::ProgressCurrent, &[])? >= self.call(Op::ProgressTotal, &[])?;
        if !final_update
            && !self.flag(Op::PatchContainsFlow, &[])?
            && self.flag(Op::ProgressStageUnchanged, &[])?
            && self.now()? - self.call(Op::LastProgress, &[])? < 0.1
        {
            return Ok(());
        }
        let time = self.now()?;
        self.action(Op::StoreProgressTime, &[time])?;
        self.action(Op::QueueProgress, &[])
    }
    pub(super) fn progress_delivery(&self) -> EngineResult {
        if self.flag(Op::GenerationMatches, &[])?
            && !self.flag(Op::Paused, &[])?
            && self.flag(Op::GateActive, &[])?
        {
            self.action(Op::EmitProgress, &[])?;
        }
        Ok(())
    }
    pub(super) fn combat_delivery(&self) -> EngineResult {
        if self.flag(Op::GenerationMatches, &[])? && !self.flag(Op::Paused, &[])? {
            self.action(Op::EmitCombat, &[])?;
        }
        Ok(())
    }
    pub(super) fn info_progress(&self) -> EngineResult {
        self.action(Op::LogInfo, &[])?;
        if self.flag(Op::InfoIsStage, &[])? {
            self.action(Op::PublishStageInfo, &[])?;
        } else {
            self.action(Op::PublishDetailInfo, &[])?;
        }
        Ok(())
    }
    pub(super) fn add_templates(&self) -> EngineResult {
        if self.flag(Op::UserTemplatesExist, &[])? {
            self.action(Op::AddUserTemplates, &[])?;
        }
        Ok(())
    }
    pub(super) fn dispatch_builtin(&self) -> EngineResult<f64> {
        if !self.native_available(3.)? {
            self.fail(19.)?;
        }
        self.call(Op::DispatchBuiltin, &[])
    }
    pub(super) fn register_scripts(&self) -> EngineResult {
        let count = self.call(Op::SnapshotScriptCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectSnapshotScript, &[index as f64])?;
            self.action(Op::StoreCustomScript, &[])?;
            self.action(Op::LogScriptLoaded, &[])?;
            let trigger = self.flag(Op::ScriptTrigger, &[])?;
            let interval = self.call(Op::ScriptInterval, &[])?.max(0.05);
            self.action(Op::AppendRunnableScript, &[trigger as u8 as f64, interval])?;
        }
        Ok(())
    }
}
