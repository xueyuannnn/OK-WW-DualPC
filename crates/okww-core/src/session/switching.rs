use super::*;
impl Host {
    fn choose_next(&self, intro: bool, low_con: bool) -> SessionResult<bool> {
        self.action(Op::ClearCandidates, &[])?;
        let count = self.call(Op::PartySize, &[])? as usize;
        for index in 0..count {
            let args = [index as f64, intro as u8 as f64, low_con as u8 as f64];
            let overridden = if self.flag(Op::PythonMacro, &[])? {
                self.values(Op::PythonPriority, &args)?
            } else if self.flag(Op::MacroExists, &[])? {
                self.values(Op::NativeMacroPriority, &args)?
            } else {
                [0.; 3]
            };
            let priority = if overridden[0] != 0. {
                overridden[1]
            } else {
                self.call(Op::BuiltinPriority, &args)?
            };
            self.action(Op::AppendCandidate, &[index as f64, priority])?;
        }
        let index = self.call(Op::ChooseDecision, &[intro as u8 as f64])?;
        self.action(Op::LogSwitch, &[])?;
        let same = self.flag(Op::DecisionMatchesPrevious, &[])?;
        self.action(Op::DisposeCandidates, &[])?;
        if same {
            self.action(Op::ClearTarget, &[])?;
            return Ok(false);
        }
        self.action(Op::SetTarget, &[index])?;
        Ok(true)
    }
    pub(super) fn switch_next(&self, free_intro: bool, low_con: bool) -> SessionResult {
        self.action(Op::UpdatePortraits, &[])?;
        let mut intro = free_intro;
        if !intro {
            let mut concerto = self.call(Op::PreviousCon, &[])?;
            if concerto > 0.8 && concerto != 1. {
                self.action(Op::Sleep, &[0.05])?;
                self.action(Op::NextFrame, &[])?;
                concerto = self.call(Op::PreviousCon, &[])?;
            }
            intro = concerto == 1.;
        }
        if !self.choose_next(intro, low_con)? {
            return self.action(Op::PreviousNormalAttack, &[0.2]);
        }
        self.action(Op::ApplyIntro, &[intro as u8 as f64])?;
        let start = self.now()?;
        let mut last_click = 0.;
        while self.now()? - start <= 10. {
            if !self.flag(Op::TargetSkipCheck, &[intro as u8 as f64])? {
                self.action(Op::CheckCombat, &[])?;
            } else {
                self.action(Op::Watchdog, &[])?;
            }
            let hud = self.values(Op::ReadHud, &[])?;
            if hud[2] == self.call(Op::PreviousIndex, &[])? {
                self.action(Op::UpdatePortraits, &[])?;
                let refreshed = intro || self.flag(Op::PreviousConFull, &[])?;
                if refreshed != intro {
                    intro = refreshed;
                    if !self.choose_next(intro, low_con)? {
                        return self.action(Op::PreviousNormalAttack, &[0.2]);
                    }
                }
                self.action(Op::ApplyIntro, &[intro as u8 as f64])?;
                if intro && self.flag(Op::PreviousCheckF, &[])? {
                    self.action(Op::BreakF, &[])?;
                }
            }
            if self.flag(Op::TargetWaitSwitch, &[])? {
                self.action(Op::ClickDefault, &[])?;
                self.action(Op::Sleep, &[0.1])?;
                continue;
            }
            if self.now()? - last_click > 0.1 {
                last_click = self.now()?;
                self.action(Op::TapTargetIndex, &[])?;
                self.action(Op::Sleep, &[0.001])?;
                self.action(Op::ClickDefault, &[])?;
                self.action(Op::Sleep, &[0.001])?;
            }
            let hud = self.values(Op::ReadHud, &[])?;
            if hud[0] == 0. {
                self.action(Op::Watchdog, &[])?;
                return self.fail(14.);
            }
            if hud[2] == self.call(Op::TargetIndex, &[])? {
                self.set_bool(Bool::InLiberation, false)?;
                if !intro && self.flag(Op::PreviousCheckF, &[])? {
                    self.action(Op::BreakF, &[])?;
                }
                self.action(Op::PreviousSwitchOut, &[intro as u8 as f64])?;
                self.action(Op::SetTargetCurrent, &[])?;
                self.action(Op::SetTargetSwitchIn, &[self.now()?])?;
                if intro {
                    self.action(Op::FreezeSwitchIntro, &[])?;
                    self.action(Op::SetPreviousOutro, &[self.now()?])?;
                }
                if self.flag(Op::HasSwitchPost, &[])? {
                    self.action(Op::RunSwitchPost, &[intro as u8 as f64])?;
                }
                self.action(Op::PublishParty, &[])?;
                return Ok(());
            }
            self.action(Op::NextFrame, &[])?;
        }
        self.fail(13.)
    }
}
