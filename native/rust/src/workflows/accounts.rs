use super::*;
impl Host {
    fn read_accounts(&self) -> Result {
        self.action(Op::ReadAccountText, &[])?;
        let count = self.scalar(Op::AccountTextCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::AccountTextMatches, &[index as f64])? {
                self.action(Op::AppendAccountText, &[index as f64])?;
            }
        }
        self.action(Op::SortAccountText, &[])
    }
    fn current_account(&self) -> Result {
        if !self.wait_for_interval(
            || {
                self.action(Op::ReadAccounts, &[])?;
                Ok(self.scalar(Op::AccountLineCount, &[])? == 1.)
            },
            60.,
            0.4,
        )? {
            self.action(Op::AccountFailure, &[1.])?;
        }
        self.action(Op::SelectFirstAccountLine, &[])
    }
    fn logout_account(&self) -> Result {
        self.action(Op::NavEnsureMain, &[100.])?;
        self.action(Op::AccountEscape, &[1.5])?;
        if !self.wait_for(|| self.flag(Op::AccountSettingsFound, &[]), 10.)? {
            self.action(Op::AccountFailure, &[2.])?;
        }
        self.action(Op::ClickRelative, &[0.04, 0.96, 1.])?;
        self.action(Op::ConfirmAccountLogout, &[])?;
        self.action(Op::ResetAccountLoggedIn, &[])?;
        self.action(Op::ReadCurrentAccount, &[])
    }
    fn choose_account(&self) -> Result {
        self.action(Op::ReadCurrentAccount, &[])?;
        self.action(Op::ClickAccountLine, &[0.8])?;
        let pages = (self.scalar(Op::AccountPageLimit, &[])? as i32).clamp(1, 40);
        for _ in 0..pages {
            self.action(Op::ReadAccounts, &[])?;
            if self.scalar(Op::AccountLineCount, &[])? == 0. {
                self.action(Op::AccountFailure, &[3.])?;
            }
            self.action(Op::ReadNextAccount, &[])?;
            if self.flag(Op::HasSelectedAccount, &[])? {
                break;
            }
            self.action(Op::ClearAccountNames, &[])?;
            let count = self.scalar(Op::AccountLineCount, &[])? as usize;
            for index in 0..count {
                self.action(Op::AppendNormalizedAccountName, &[index as f64])?;
            }
            self.action(Op::JoinAccountNames, &[])?;
            if self.flag(Op::AccountFingerprintUnchanged, &[])? {
                self.action(Op::AccountEscape, &[0.3])?;
                self.action(Op::ClearSelectedAccount, &[])?;
                return Ok(());
            }
            self.action(Op::StoreAccountFingerprint, &[])?;
            self.action(Op::ScrollAccountList, &[])?;
            self.action(Op::Sleep, &[0.5])?;
        }
        if !self.flag(Op::HasSelectedAccount, &[])? {
            self.action(Op::AccountFailure, &[4.])?;
        }
        self.action(Op::ClickSelectedAccount, &[0.8])?;
        self.action(Op::ReadExpectedAccount, &[])?;
        if !self.wait_for_interval(
            || {
                self.action(Op::ReadAccounts, &[])?;
                Ok(self.scalar(Op::AccountLineCount, &[])? == 1.
                    && self.flag(Op::AccountSelectionMatches, &[])?)
            },
            8.,
            0.3,
        )? {
            self.action(Op::AccountFailure, &[5.])?;
        }
        Ok(())
    }
    fn run_accounts(&self) -> Result {
        self.action(Op::CreateAccountCycle, &[])?;
        let maximum = (self.scalar(Op::MaximumAccountCount, &[])? as i32).clamp(1, 100);
        self.action(Op::RunAccountDaily, &[])?;
        self.action(Op::LogoutAccount, &[])?;
        self.action(Op::CompleteCurrentAccount, &[])?;
        loop {
            if self.flag(Op::HasInfo, &[])? {
                self.action(Op::AccountCountInfo, &[])?;
            }
            self.action(Op::ChooseAccount, &[])?;
            if !self.flag(Op::HasSelectedAccount, &[])? {
                return Ok(());
            }
            if self.scalar(Op::AccountCycleCount, &[])? >= maximum as f64 {
                self.action(Op::AccountFailure, &[6.])?;
            }
            self.action(Op::ReadAccountLogin, &[])?;
            if !self.flag(Op::HasAccountLogin, &[])? {
                self.action(Op::AccountFailure, &[7.])?;
            }
            self.action(Op::ClickAccountLogin, &[])?;
            self.action(Op::ResetAccountLoggedIn, &[])?;
            self.action(Op::NavEnsureMain, &[180.])?;
            self.action(Op::RunAccountDaily, &[])?;
            self.action(Op::CompleteSelectedAccount, &[])?;
            self.action(Op::LogoutAccount, &[])?;
            self.action(Op::ReadCurrentAccount, &[])?;
            if !self.flag(Op::LoggedOutAccountMatches, &[])? {
                self.action(Op::AccountFailure, &[8.])?;
            }
        }
    }
    pub(super) fn run_account_method(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::NormalizeAccount => {
                self.action(Op::LowerTrimAccount, &[])?;
                self.action(Op::RemoveAccountWhitespace, &[])?;
                self.action(Op::ReplaceAccountZeros, &[])?;
                self.action(Op::ReplaceAccountDomain, &[])?;
            }
            Method::CompleteAccount => {
                self.action(Op::NormalizeCycleAccount, &[])?;
                if self.flag(Op::NormalizedAccountEmpty, &[])? {
                    self.action(Op::AccountFailure, &[0.])?;
                }
                self.action(Op::InsertCompletedAccount, &[])?;
            }
            Method::AccountCompleted => {
                self.action(Op::NormalizeCycleAccount, &[])?;
                return self.call(Op::CycleContainsAccount, &[]);
            }
            Method::NextAccount => {
                for index in 0..a[0] as usize {
                    if !self.flag(Op::NextAccountNameEmpty, &[index as f64])?
                        && !self.flag(Op::NextAccountCompleted, &[index as f64])?
                    {
                        return Ok([index as f64, 0., 0.]);
                    }
                }
                return Ok([-1., 0., 0.]);
            }
            Method::ReadAccounts => self.read_accounts()?,
            Method::CurrentAccount => self.current_account()?,
            Method::LogoutAccount => self.logout_account()?,
            Method::ChooseAccount => self.choose_account()?,
            Method::RunAccounts => self.run_accounts()?,
            Method::AccountCount => return self.call(Op::AccountCycleCount, &[]),
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
