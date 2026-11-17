use crate::services::*;
impl Host {
    fn daily_echo_objective(&self) -> Result {
        if self.scalar(Op::DailyRunPoints, &[])? < 0. || !self.flag(Op::DailyRunEchoEnabled, &[])? {
            return Ok(());
        }
        while self.scalar(Op::DailyEchoAttempts, &[])? < 3.
            && !self.flag(Op::DailyRunReady, &[])?
            && self.flag(Op::DailyRunNeedsEcho, &[])?
        {
            self.action(Op::IncrementDailyEchoAttempts, &[])?;
            self.action(Op::ResetDailyEchoCaptured, &[])?;
            if !self.flag(Op::StageDailyEchoCapture, &[])? {
                break;
            }
            self.action(Op::StageDailyRunReward, &[])?;
            if !self.flag(Op::DailyEchoCaptured, &[])? {
                break;
            }
        }
        Ok(())
    }
    fn daily_run(&self, enhanced: bool) -> Result {
        self.action(Op::CreateDailyTaskScope, &[enhanced as u8 as f64])?;
        self.action(Op::InitializeDailyRun, &[])?;
        let retries = if enhanced {
            (self.scalar(Op::DailyRunConfiguredRetries, &[])? as i32).clamp(1, 10)
        } else {
            1
        };
        self.action(Op::StoreDailyRunRetries, &[retries as f64])?;
        self.action(Op::ReadDailySelectedExtras, &[])?;
        let mut extras = Vec::new();
        if self.flag(Op::DailyRunExtraSelected, &[0.])? {
            extras.push(0);
        }
        if enhanced {
            let kinds = if self.flag(Op::DailyRunForgeryFirst, &[])? {
                [2, 1, 3]
            } else {
                [1, 2, 3]
            };
            for kind in kinds {
                if self.scalar(Op::DailyRunQuota, &[(kind - 1) as f64])? > 0. {
                    extras.push(kind);
                }
            }
        }
        if self.flag(Op::DailyRunExtraSelected, &[4.])? {
            extras.push(4);
        }
        extras.extend([5, 6]);
        for kind in [7, 8, 9] {
            if self.flag(Op::DailyRunExtraSelected, &[kind as f64])? {
                extras.push(kind);
            }
        }
        for kind in &extras {
            self.action(Op::AppendDailyRunExtra, &[*kind as f64])?;
        }
        for (step, estimate) in [(0., 30.), (1., 20.), (2., 420.), (3., 15.)] {
            self.action(Op::AppendDailyRunStep, &[step, estimate])?;
        }
        for (index, kind) in extras.iter().enumerate() {
            let estimate = match kind {
                5 => 15.,
                6 => 25.,
                7 => 1200.,
                4 => self.scalar(Op::DailyRunNestTargetCount, &[])?.max(1.) * 180.,
                1..=3 => 300.,
                _ => 0.,
            };
            self.action(Op::AppendDailyExtraStep, &[index as f64, estimate])?;
        }
        self.action(Op::BeginDailyRunWorkflow, &[])?;
        self.action(Op::DailyEnterGameRunning, &[])?;
        self.action(Op::ResetDailyLoggedIn, &[])?;
        self.action(Op::NavEnsureMain, &[180.])?;
        self.action(Op::DailyEnterGameCompleted, &[])?;
        if !self.flag(Op::StageDailyRunInitialClaim, &[])? {
            self.action(Op::DailyRunFailure, &[0.])?;
        }
        self.action(Op::ResetDailyEchoAttempts, &[])?;
        let already_done = self.flag(Op::DailyRunReady, &[])?;
        self.action(Op::DailyGoalStart, &[already_done as u8 as f64])?;
        if self.scalar(Op::DailyRunPoints, &[])? >= 80. {
            self.daily_echo_objective()?;
        }
        if !self.flag(Op::DailyRunReady, &[])? {
            self.action(Op::StageDailyRunStamina, &[])?;
        }
        self.action(Op::StageDailyRunCheck, &[])?;
        self.daily_echo_objective()?;
        self.action(Op::DailyRewardRunning, &[])?;
        let claimed = self.flag(Op::StageDailyRunReward, &[])?;
        // Preserve short circuiting: ready is only inspected after a successful stage.
        let complete = claimed && self.flag(Op::DailyRunReady, &[])?;
        self.action(Op::DailyRewardFinished, &[complete as u8 as f64])?;
        if !self.flag(Op::DailyRunReady, &[])? {
            self.action(Op::DailyGoalFailed, &[])?;
            let echo_enabled = self.flag(Op::DailyRunEchoEnabled, &[])?;
            let [stamina, known, _] = self.call(Op::DailyRunStamina, &[])?;
            if known == 0. {
                self.action(Op::DailyMissingStaminaDetail, &[])?;
            } else if stamina < 180. {
                self.action(Op::DailyStaminaDetail, &[])?;
            }
            if self.flag(Op::DailyRunNeedsEcho, &[])? {
                self.action(Op::DailyEchoDetail, &[echo_enabled as u8 as f64])?;
            }
            if self.flag(Op::HasDailyDiagnostic, &[])? {
                self.action(Op::EmitDailyRunDiagnostic, &[echo_enabled as u8 as f64])?;
            }
            self.action(Op::DailyRunFailure, &[1.])?;
        }
        if !already_done {
            self.action(Op::DailyGoalCompleted, &[])?;
        }
        self.action(Op::ValidateDailyRun, &[])?;
        self.action(Op::StoreDailyExtrasTotal, &[extras.len() as f64])?;
        for (index, kind) in extras.iter().enumerate() {
            self.action(Op::StoreDailyExtrasDone, &[index as f64])?;
            self.action(Op::ReportDailyExtra, &[index as f64, index as f64])?;
            let retry_navigation = ![0, 8, 9].contains(kind);
            self.action(
                Op::StageDailyExtra,
                &[index as f64, retry_navigation as u8 as f64],
            )?;
            self.action(Op::ReportDailyExtra, &[(index + 1) as f64, index as f64])?;
        }
        self.action(Op::StoreDailyExtrasDone, &[extras.len() as f64])?;
        if self.flag(Op::DailyRunHasFailures, &[])? {
            self.action(Op::DailyRunFailure, &[2.])?;
        }
        self.action(Op::DailyRunCompleteStatus, &[])
    }
    pub(in crate::services) fn run_daily_main(
        &self,
        method: Method,
        a: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::DailyRun => self.daily_run(a[0] != 0.)?,
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
