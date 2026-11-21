//! Port of upstream Lily at 46a334c5a8d782596ed17026843df5b81d87010c.
use super::*;

impl Host {
    pub(super) fn lily_liberation_ready(&self) -> ActionResult<bool> {
        Ok(!self.feature("lily_slumberland", 0.8, "")?
            && self.feature("lily_liberation", 0.8, "")?
            && self.truth(Op::Available, &[LIBERATION, 1., 1.])?)
    }

    fn lily_build(&self, concerto: Option<f64>) -> ActionResult<bool> {
        let start = self.now()?;
        let timeout = if concerto.is_some() { 3. } else { 10. };
        while self.elapsed(start)? < timeout {
            self.set(CYCLE_START, self.now()?)?;
            let ready = if let Some(target) = concerto {
                self.value(Op::Concerto, &[])? >= target
            } else {
                self.truth(Op::Forte, &[0.])? || self.truth(Op::Forte, &[2.])?
            };
            if ready {
                return Ok(true);
            }
            if self.virtual_action(&[VIRTUAL_RESONANCE, 0., 0., 0., 0., 0.8, 1.])?[0] == 0. {
                self.click(false, 0.1)?;
            }
            self.run(Method::CycleSleep as i32, &[0.1])?;
        }
        self.action(Op::LilyNotice, &[if concerto.is_some() { 1. } else { 0. }])?;
        Ok(false)
    }

    fn lily_heavy_channel(&self) -> ActionResult<bool> {
        Ok(!self.truth(Op::Forte, &[0.])? && self.feature("lily_heavy", 0.8, "")?)
    }

    fn lily_heavy(&self) -> ActionResult<bool> {
        self.action(Op::CheckCombat, &[])?;
        self.action(Op::Mouse, &[1.])?;
        let result = (|| {
            if !self.wait_with(
                || self.lily_heavy_channel(),
                2.,
                0.,
                || Ok(()),
                || self.action(Op::CheckCombat, &[]),
            )? {
                self.action(Op::LilyNotice, &[2.])?;
                return Ok(false);
            }
            self.wait_with(
                || Ok(!self.lily_heavy_channel()?),
                6.,
                0.15,
                || Ok(()),
                || self.action(Op::CheckCombat, &[]),
            )
        })();
        let released = self.action(Op::Mouse, &[0.]);
        let finished = match result {
            Ok(value) => {
                released?;
                value
            }
            Err(error) => {
                let _ = released;
                return Err(error);
            }
        };
        if finished {
            self.sleep(0.7, true)?;
        } else {
            self.action(Op::LilyNotice, &[3.])?;
        }
        Ok(finished)
    }

    pub(super) fn lily_perform(&self) -> ActionResult {
        if self.state(INTRO)? != 0. {
            self.normal_attacks(0.8, 0.1, 0., false, false)?;
        }
        if !self.feature("lily_slumberland", 0.8, "")? {
            self.virtual_action(&[VIRTUAL_ECHO, 0., 0., 0.])?;
            self.lily_build(None)?;
            if self.truth(Op::Forte, &[0.])? || self.truth(Op::Forte, &[2.])? {
                self.lily_heavy()?;
            }
            let can_liberate = self.flag(USE_LIBERATION)? && self.lily_liberation_ready()?;
            self.lily_build(Some(if can_liberate { 0.8 } else { 1. }))?;
            let liberated =
                self.virtual_action(&[VIRTUAL_LIBERATION, -1., 0., 0., 0., 1.])?[0] != 0.;
            if can_liberate && !liberated {
                self.lily_build(Some(1.))?;
            }
        }
        self.virtual_action(&[VIRTUAL_SWITCH, 0., 0.])?;
        Ok(())
    }
}
