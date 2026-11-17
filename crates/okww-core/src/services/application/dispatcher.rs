use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_dispatcher(
        &self,
        method: Method,
        a: &[f64],
    ) -> Result<[f64; 3]> {
        let mut result = [0.; 3];
        match method {
            Method::BusinessTasks => {
                let count = self.scalar(Op::BuiltinTaskCount, &[])? as usize;
                for i in 0..count {
                    let kind = self.scalar(Op::LoadBuiltinTask, &[i as f64])? as i32;
                    self.action(
                        Op::AppendBuiltinTask,
                        &[f64::from(matches!(kind, 0 | 1)), 0.1],
                    )?;
                }
            }
            Method::DispatchBusinessTask => {
                match a[0] as i32 {
                    0 => return Ok(result),
                    1 => match self.scalar(Op::RunAutomaticCombat, &[]) {
                        Ok(value) => {
                            result[0] = value;
                            return Ok(result);
                        }
                        Err(error) => {
                            if !self.flag(Op::TaskErrorCharacterDead, &[])? {
                                return Err(error);
                            }
                            self.action(Op::ClearException, &[])?;
                            let enabled = self.flag(Op::TaskDeathRecoveryEnabled, &[])?;
                            let limit =
                                (self.scalar(Op::TaskDeathRecoveryLimit, &[])? as i32).clamp(1, 10);
                            let now = self.scalar(Op::Now, &[])?;
                            if now - self.scalar(Op::TaskLastDeathRecovery, &[])? > 300. {
                                self.action(Op::TaskSetDeathRecoveries, &[0.])?;
                            }
                            if !enabled {
                                self.action(Op::RethrowHandled, &[])?;
                                return Err(HOST_ERROR);
                            }
                            let count = self.scalar(Op::TaskDeathRecoveryCount, &[])? + 1.;
                            self.action(Op::TaskSetDeathRecoveries, &[count])?;
                            if count > f64::from(limit) {
                                self.action(Op::RethrowHandled, &[])?;
                                return Err(HOST_ERROR);
                            }
                            self.action(Op::TaskRecoverStartingPopup, &[])?;
                            self.action(Op::TaskStoreRecoveryTime, &[self.scalar(Op::Now, &[])?])?;
                            if self.flag(Op::TaskHasInfo, &[])? {
                                self.action(Op::TaskReportCombatRecovery, &[])?;
                            }
                            result[0] = 1.;
                            return Ok(result);
                        }
                    },
                    2 => self.action(Op::DispatchMultiAccountDaily, &[])?,
                    3 => self.action(
                        Op::DispatchDaily,
                        &[self.scalar(Op::TaskEnhancedSuffix, &[])?],
                    )?,
                    4 => self.action(
                        Op::DispatchEchoFarm,
                        &[self.scalar(Op::TaskEnhancedSuffix, &[])?],
                    )?,
                    5 => self.action(Op::DispatchNightmare, &[])?,
                    6..=8 => self.action(
                        Op::DispatchDomain,
                        &[a[0] - 6., self.scalar(Op::TaskEnhancedSuffix, &[])?],
                    )?,
                    9 => self.action(Op::DispatchGarden, &[])?,
                    10 => self.action(Op::DispatchMergeEcho, &[])?,
                    11 => self.action(Op::DispatchEnhanceEcho, &[])?,
                    12 => self.action(Op::DispatchChangeEcho, &[])?,
                    _ => {
                        self.action(Op::UnknownBusinessTask, &[])?;
                        return Err(HOST_ERROR);
                    }
                }
                result[0] = 1.;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(result)
    }
}
