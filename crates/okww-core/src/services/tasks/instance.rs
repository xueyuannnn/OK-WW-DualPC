use crate::services::*;
impl Host {
    fn restart_dialog(&self) -> Result<f64> {
        self.action(Op::JoinDialog, &[])?;
        self.action(Op::FindCancelLine, &[])?;
        if self.flag(Op::DialogResource, &[])?
            && (self.flag(Op::DialogConfirm, &[])? || self.flag(Op::CancelLineExists, &[])?)
        {
            self.action(Op::AdoptCancelLine, &[])?;
            return Ok(1.);
        }
        if self.flag(Op::FindRestartLine, &[])? {
            return Ok(2.);
        }
        if self.flag(Op::DialogRestart, &[])?
            && self.flag(Op::DialogConfirm, &[])?
            && self.flag(Op::FindConfirmLine, &[])?
        {
            return Ok(3.);
        }
        Ok(0.)
    }
    fn exit_context(&self) -> Result<bool> {
        self.action(Op::JoinDialog, &[])?;
        Ok(self.flag(Op::DialogExit, &[])?
            && self.flag(Op::DialogExitConfirm, &[])?
            && !self.flag(Op::DialogForbidden, &[])?)
    }
    fn exit_confirmation(&self, width: f64) -> Result {
        if !self.flag(Op::ExitContext, &[])? {
            return Ok(());
        }
        let count = self.scalar(Op::DialogLineCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::IsExitConfirmLine, &[index as f64])?
                && self.scalar(Op::DialogCenterX, &[index as f64])? > width * 0.5
                && (!self.flag(Op::SelectedLineExists, &[])?
                    || self.scalar(Op::DialogLineX, &[index as f64])?
                        > self.scalar(Op::SelectedLineX, &[])?)
            {
                self.action(Op::StoreSelectedLine, &[index as f64])?;
            }
        }
        Ok(())
    }
    fn restart_boss(&self) -> Result {
        let mut tracker = crate::farming::RestartState::default();
        for _ in 0..120 {
            self.action(Op::Refresh, &[])?;
            if self.flag(Op::CombatIntroVisible, &[])? {
                self.action(Op::CloseCombatIntro, &[])?;
                continue;
            }
            let kind = self.scalar(Op::ReadRestartDialog, &[])? as i32;
            let hud = self.flag(Op::InTeam, &[])?;
            let action = tracker.step(kind, hud, hud && self.flag(Op::NavInRealm, &[])?);
            if action == 4 {
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::RestartCompleteInfo, &[])?;
                }
                return Ok(());
            }
            if action == 5 {
                break;
            }
            if action == 3 {
                if self.flag(Op::RestartButtonExists, &[])? {
                    self.action(Op::ClickRestartButton, &[if kind == 1 { 0.3 } else { 0. }])?;
                } else {
                    self.action(Op::Key, &[0., 0.3])?;
                }
                if kind != 1 {
                    continue;
                }
            } else if action == 2 {
                self.action(Op::Key, &[0., 0.5])?;
            }
            self.action(Op::Sleep, &[0.5])?;
        }
        self.fail(200.)
    }
    fn completed_nest(&self) -> Result<bool> {
        let count = self.scalar(Op::ReadCompleteText, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::LineIsComplete, &[index as f64])? {
                return Ok(true);
            }
        }
        Ok(false)
    }
    fn outside_nest(&self) -> Result<bool> {
        Ok(self.flag(Op::NavInWorld, &[])? && !self.completed_nest()?)
    }
    fn leave_nest(&self, send_escape: bool) -> Result {
        if self.outside_nest()? {
            return Ok(());
        }
        let mut escape = !send_escape;
        for _ in 0..4 {
            self.action(Op::Refresh, &[])?;
            if self.outside_nest()? {
                return Ok(());
            }
            self.action(Op::ReadNestExitDialog, &[])?;
            self.action(Op::ReadExitConfirmation, &[])?;
            if !self.flag(Op::ExitButtonExists, &[])?
                && self.flag(Op::ReadExitContext, &[])?
                && self.flag(Op::FindNestConfirmFeature, &[0.6])?
            {
                self.action(Op::FeatureAsExitButton, &[])?;
            }
            if self.flag(Op::ExitButtonExists, &[])? {
                self.action(Op::ClickExitButton, &[1.])?;
            } else {
                let mut prompt = false;
                if self.flag(Op::FindExitPrompt, &[0.7])? {
                    let count = self.scalar(Op::ReadExitPromptText, &[])? as usize;
                    for index in 0..count {
                        prompt |= self.flag(Op::LineIsExit, &[index as f64])?;
                    }
                }
                if prompt {
                    self.action(Op::Key, &[6., 1.])?;
                } else {
                    let count = self.scalar(Op::ReadNestExitLines, &[])? as usize;
                    let mut found = false;
                    for index in 0..count {
                        if self.flag(Op::LineIsExit, &[index as f64])? {
                            self.action(Op::SelectNestExitLine, &[index as f64])?;
                            found = true;
                            break;
                        }
                    }
                    if found {
                        self.action(Op::ClickExitButton, &[1.])?;
                    } else {
                        if self.flag(Op::InTeam, &[])? && !escape {
                            self.action(Op::Key, &[0., 1.])?;
                            escape = true;
                        } else {
                            self.action(Op::Sleep, &[1.])?;
                        }
                        continue;
                    }
                }
            }
            if self.wait_for(|| self.outside_nest(), 4.)? {
                return Ok(());
            }
        }
        if !self.wait_for(|| self.outside_nest(), 45.)? {
            return self.fail(201.);
        }
        Ok(())
    }
    pub(in crate::services) fn run_instance(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let mut value = 0.;
        match method {
            Method::CombineDialog => {
                let count = self.scalar(Op::DialogLineCount, &[])? as usize;
                for index in 0..count {
                    self.action(Op::AppendCleanLine, &[index as f64])?;
                }
            }
            Method::ExactDialog => {
                let count = self.scalar(Op::DialogLineCount, &[])? as usize;
                for index in 0..count {
                    if self.flag(Op::ExactLineMatch, &[index as f64])? {
                        self.action(Op::StoreSelectedLine, &[index as f64])?;
                        break;
                    }
                }
            }
            Method::RestartDialog => value = self.restart_dialog()?,
            Method::NestExitText => value = self.flag(Op::MatchExitText, &[])? as u8 as f64,
            Method::NestCompleteText => value = self.flag(Op::MatchCompleteText, &[])? as u8 as f64,
            Method::NestExitContext => value = self.exit_context()? as u8 as f64,
            Method::NestExitConfirmation => self.exit_confirmation(a[0])?,
            Method::RestartBoss => self.restart_boss()?,
            Method::CompletedNest => value = self.completed_nest()? as u8 as f64,
            Method::LeaveNest => self.leave_nest(a[0] != 0.)?,
            _ => return Err(INVALID_ACTION),
        }
        Ok([value, 0., 0.])
    }
}
