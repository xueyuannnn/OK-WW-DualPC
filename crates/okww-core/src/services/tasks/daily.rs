use crate::services::*;
impl Host {
    fn daily_progress(&self) -> Result {
        let count = self.scalar(Op::DailyObjectiveCount, &[])? as usize;
        for index in 0..count {
            if !self.flag(Op::DailyEchoObjective, &[index as f64])? {
                continue;
            }
            let mut needed = true;
            for candidate in 0..count {
                if candidate != index {
                    let [gap, height, _] = self.call(
                        Op::DailyObjectiveGeometry,
                        &[candidate as f64, index as f64],
                    )?;
                    if gap.abs() > height * 1.1 {
                        continue;
                    }
                }
                if self.flag(Op::DailyObjectiveCompleted, &[candidate as f64])? {
                    needed = false;
                }
                if self.flag(Op::DailyObjectiveCounter, &[candidate as f64])? {
                    let [current, maximum, _] = self.call(Op::DailyObjectiveCounterValues, &[])?;
                    if maximum > 0. && maximum <= 20. {
                        needed = current < maximum;
                    }
                }
            }
            self.action(Op::StoreDailyEchoNeeded, &[needed as u8 as f64])?;
            break;
        }
        for index in 0..count {
            if !self.flag(Op::DailyStaminaObjective, &[index as f64])? {
                continue;
            }
            let [value, ok, _] = self.call(Op::DailyStaminaObjectiveValue, &[])?;
            if ok != 0. && value <= 180. {
                self.action(Op::StoreDailyStaminaProgress, &[value])?;
                break;
            }
        }
        let count = self.scalar(Op::DailyPointsLineCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::DailyPointMatches, &[index as f64])? {
                self.action(Op::InsertDailyPoint, &[])?;
            }
        }
        if self.scalar(Op::DailyUniquePointsCount, &[])? == 1. {
            self.action(Op::StoreDailyUniquePoint, &[])?;
        }
        Ok(())
    }
    fn observe_daily(&self) -> Result {
        self.action(Op::ReadDailyHeading, &[])?;
        self.action(Op::ReadDailyObjectives, &[])?;
        self.action(Op::ReadDailyStrip, &[])?;
        self.action(Op::PrepareDailyCounterAreas, &[])?;
        let headings = self.scalar(Op::DailyHeadingCount, &[])? as usize;
        for index in 0..headings {
            self.action(Op::AppendDailyHeadingJson, &[index as f64])?;
        }
        let objectives = self.scalar(Op::DailyObjectiveCount, &[])? as usize;
        for index in 0..objectives {
            self.action(Op::AppendDailyObjectiveJson, &[index as f64])?;
        }
        let count = self.scalar(Op::DailyStripCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::AppendDailyStripJson, &[index as f64])?;
            if self.flag(Op::DailyCounterContains, &[index as f64])? {
                self.action(Op::AppendDailyCounterLine, &[index as f64])?;
            }
            if self.flag(Op::DailyLabelContains, &[index as f64])? {
                self.action(Op::AppendDailyLabelLine, &[index as f64])?;
            }
        }
        let confirmed = self.flag(Op::DailyHeadingMatches, &[])?
            && self.flag(Op::DailyCounterLabelMatches, &[])?;
        self.action(Op::ReadDailyProgress, &[confirmed as u8 as f64])?;
        if self.flag(Op::HasDailyObservation, &[])? {
            self.action(Op::StoreDailyObservation, &[confirmed as u8 as f64])?;
        }
        Ok(())
    }
    fn daily_status(&self) -> Result {
        if self.flag(Op::HasInfo, &[])? {
            self.action(Op::DailyStatusInfo, &[])?;
        }
        Ok(())
    }
    fn daily_stage(&self, retry_navigation: bool) -> Result<bool> {
        self.action(Op::DailyWorkflowRunning, &[])?;
        self.action(Op::DailyStageStatus, &[])?;
        let mut attempt = 0;
        while attempt < self.scalar(Op::DailyRetries, &[])? as i32 {
            let operation = (|| {
                self.action(Op::DailyStageOperation, &[])?;
                self.action(Op::DailyWorkflowCompleted, &[])
            })();
            if operation.is_ok() {
                return Ok(true);
            }
            let kind = self.scalar(Op::ExceptionKind, &[])? as i32;
            if kind == 1 {
                return Err(HOST_ERROR);
            }
            if kind == 8 {
                self.action(Op::ClearException, &[])?;
                self.action(Op::DailyExecutionFailed, &[])?;
                self.action(Op::RethrowHandled, &[])?;
                return Err(HOST_ERROR);
            }
            if !(2..=6).contains(&kind) {
                return Err(HOST_ERROR);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::DailyExceptionMessage, &[])?;
            if self.flag(Op::HasInfo, &[])? {
                self.action(Op::DailyStageErrorInfo, &[])?;
            }
            if kind != 2
                || !retry_navigation
                || attempt + 1 >= self.scalar(Op::DailyRetries, &[])? as i32
            {
                self.action(Op::DailyWorkflowFailed, &[])?;
                self.action(Op::AppendDailyFailedStage, &[])?;
                if self.flag(Op::HasNotify, &[])? {
                    self.action(Op::NotifyDailyFailedStage, &[])?;
                }
                self.action(Op::NavEnsureMain, &[180.])?;
                return Ok(false);
            }
            self.action(Op::NavEnsureMain, &[180.])?;
            attempt += 1;
        }
        Ok(false)
    }
    pub(in crate::services) fn run_daily(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::DailyCountKey => {
                return Ok([
                    if a[0] == 0. {
                        0.
                    } else if a[0] == 1. {
                        1.
                    } else {
                        2.
                    },
                    0.,
                    0.,
                ])
            }
            Method::DailyReady => return Ok([(a[0] >= 100.) as u8 as f64, 0., 0.]),
            Method::DailyProgress => self.daily_progress()?,
            Method::ObserveDailyProgress => self.observe_daily()?,
            Method::DailyDomain => {
                if self.flag(Op::DailyForgeryFirst, &[])? {
                    return Ok([1., 0., 0.]);
                }
                return Ok([
                    match self.scalar(Op::DailyPreferredDomain, &[])? as i32 {
                        0 => 1.,
                        1 => 2.,
                        _ => 0.,
                    },
                    0.,
                    0.,
                ]);
            }
            Method::DailyStatus => self.daily_status()?,
            Method::DailyStage => return Ok([self.daily_stage(a[0] != 0.)? as u8 as f64, 0., 0.]),
            Method::DailyObservationFailure => {
                if self.flag(Op::HasDailyDiagnostic, &[])? {
                    self.action(Op::EmitDailyObservationFailure, &[])?;
                }
                self.fail(600.)?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
