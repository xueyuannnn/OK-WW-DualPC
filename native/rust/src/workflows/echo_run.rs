use super::*;
impl Host {
    fn restore_echo_callbacks(&self) -> Result {
        self.action(Op::RestoreEchoCombatCheck, &[])?;
        self.action(Op::RestoreEchoCombatEnd, &[])?;
        self.action(Op::RestoreEchoTargetTimeout, &[])
    }
    fn echo_run(&self, enhanced: bool) -> Result<i32> {
        self.action(Op::StoreEchoEnhanced, &[enhanced as u8 as f64])?;
        if self.flag(Op::EchoSuppliedConfigEmpty, &[])? {
            self.action(Op::LoadEchoProfileConfig, &[enhanced as u8 as f64])?;
        } else {
            self.action(Op::CopyEchoSuppliedConfig, &[])?;
        }
        self.action(Op::CreateEchoTaskScope, &[enhanced as u8 as f64])?;
        self.action(Op::ResetEchoRunCounters, &[])?;
        let rotation = self.flag(Op::EchoRotationMode, &[])?;
        let [total, is_number, number] = self.call(Op::ReadEchoRunCount, &[])?;
        let total = total as i32;
        if is_number == 0. || number != total as f64 || !(0..=100000).contains(&total) {
            self.action(Op::EchoRunFailure, &[0.])?;
        }
        if total == 0
            && !(self.flag(Op::EchoEnhanced, &[])? && self.flag(Op::EchoMaterialMode, &[])?)
        {
            return Ok(0);
        }
        let mut per = 1;
        if rotation {
            if self.flag(Op::EchoMaterialMode, &[])? {
                self.action(Op::EchoRunFailure, &[1.])?;
            }
            let [rounds, is_number, number] = self.call(Op::ReadEchoRotationRounds, &[])?;
            per = rounds as i32;
            if total < 0
                || !(1..=100000).contains(&per)
                || (is_number != 0. && number != per as f64)
            {
                self.action(Op::EchoRunFailure, &[2.])?;
            }
            self.action(Op::ReadEchoRotationTargets, &[])?;
        }
        self.action(Op::CaptureEchoCallbacks, &[])?;
        self.action(Op::InstallEchoCombatCheck, &[])?;
        self.action(Op::InstallEchoCombatEnd, &[])?;
        let farming = (|| -> Result {
            self.action(Op::RecoverEchoStartingPopup, &[])?;
            self.action(Op::Sleep, &[0.5])?;
            if !rotation {
                self.action(Op::RunEchoSingle, &[])?;
            } else {
                let mut position = 0;
                while self.scalar(Op::EchoTotalCompleted, &[])? < (total as f64) {
                    let count = self.scalar(Op::EchoRotationTargetCount, &[])? as i32;
                    self.action(Op::SelectEchoRotationTarget, &[(position % count) as f64])?;
                    let rounds = per.min(total - self.scalar(Op::EchoTotalCompleted, &[])? as i32);
                    self.action(Op::ApplyEchoRotationTarget, &[rounds as f64])?;
                    if self.flag(Op::HasInfo, &[])? {
                        self.action(Op::EchoRotationInfo, &[rounds as f64])?;
                    }
                    if self.scalar(Op::RunEchoSingle, &[])? != rounds as f64 {
                        self.action(Op::EchoRunFailure, &[3.])?;
                    }
                    position += 1;
                }
            }
            Ok(())
        })();
        if farming.is_err() {
            self.action(Op::ClearException, &[])?;
            self.restore_echo_callbacks()?;
            self.action(Op::RethrowHandled, &[])?;
            return Err(HOST_ERROR);
        }
        self.restore_echo_callbacks()?;
        Ok(self.scalar(Op::EchoTotalCompleted, &[])? as i32)
    }
    pub(super) fn run_echo_root(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let value = match method {
            Method::EchoRun => self.echo_run(a[0] != 0.)? as f64,
            Method::EchoCombatCheck => {
                if !self.flag(Op::EchoRealm, &[])? && self.flag(Op::EchoPickDefault, &[])? {
                    self.action(Op::IncrementEchoDrops, &[])?;
                }
                self.action(Op::CheckEchoRealm, &[20.])?;
                1.
            }
            Method::EchoCombatEnd => {
                (!self.flag(Op::EchoHasHealthBar, &[])? && self.flag(Op::EchoDetectedDrops, &[])?)
                    as u8 as f64
            }
            _ => return Err(INVALID_ACTION),
        };
        Ok([value, 0., 0.])
    }
}
