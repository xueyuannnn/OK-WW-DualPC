use super::*;
impl Host {
    fn transform_echo_lines(&self, fixed: bool) -> Result {
        let count = self.scalar(Op::EchoOperationLineCount, &[])? as usize;
        for index in 0..count {
            self.action(
                if fixed {
                    Op::FixEchoOperationLine
                } else {
                    Op::SimplifyEchoOperationLine
                },
                &[index as f64],
            )?;
        }
        Ok(())
    }
    fn echo_wait_text(&self, timeout: f64) -> Result {
        self.wait_for_interval(
            || {
                self.action(Op::ReadEchoWaitText, &[])?;
                self.transform_echo_lines(true)?;
                self.flag(Op::MatchEchoWaitText, &[])
            },
            timeout,
            0.1,
        )?;
        Ok(())
    }
    fn echo_cultivate(&self) -> Result {
        self.action(Op::ReadEchoOperationText, &[0.82, 0.86, 0.97, 0.96])?;
        self.transform_echo_lines(false)?;
        self.action(Op::MatchEchoCultivate, &[])
    }
    fn echo_zero_level(&self, change: bool) -> Result<bool> {
        self.action(
            Op::ReadEchoOperationText,
            if change {
                &[0.66, 0.48, 0.77, 0.56]
            } else {
                &[0.65, 0.35, 1., 0.57]
            },
        )?;
        self.transform_echo_lines(false)?;
        self.flag(Op::MatchEchoZeroLevel, &[])
    }
    fn echo_enter_cultivate(&self) -> Result {
        if !self.flag(Op::ReadEchoCultivate, &[])? {
            self.action(Op::EchoOperationFailure, &[0.])?;
        }
        let end = self.scalar(Op::Now, &[])? + 5.;
        loop {
            if !self.flag(Op::ReadEchoCultivate, &[])? {
                return Ok(());
            }
            self.action(Op::ClickEchoOperationText, &[0.5])?;
            let again = self.scalar(Op::Now, &[])? < end;
            if !again {
                break;
            }
        }
        self.action(Op::EchoOperationFailure, &[1.])
    }
    fn echo_back_to_bag(&self) -> Result {
        if !self.wait_for(
            || {
                if self.flag(Op::ReadEchoCultivate, &[])? {
                    return Ok(true);
                }
                self.action(Op::EchoOperationEscape, &[1.])?;
                Ok(false)
            },
            10.,
        )? {
            self.action(Op::EchoOperationFailure, &[2.])?;
        }
        Ok(())
    }
    fn echo_mark(&self, lock: bool) -> Result {
        self.action(Op::PrepareEchoMarkBox, &[lock as u8 as f64])?;
        if !self.wait_for(
            || {
                self.action(Op::ReadEchoMarkYes, &[])?;
                self.action(Op::ReadEchoMarkNo, &[])?;
                if !self.flag(Op::HasEchoMarkYes, &[])? && !self.flag(Op::HasEchoMarkNo, &[])? {
                    self.action(Op::EchoOperationFailure, &[if lock { 3. } else { 4. }])?;
                }
                if self.flag(Op::HasEchoMarkYes, &[])?
                    && (!self.flag(Op::HasEchoMarkNo, &[])?
                        || self.scalar(Op::EchoMarkYesConfidence, &[])?
                            >= self.scalar(Op::EchoMarkNoConfidence, &[])?)
                {
                    return Ok(true);
                }
                self.action(Op::PressEchoMarkKey, &[lock as u8 as f64])?;
                Ok(false)
            },
            5.,
        )? {
            self.action(Op::EchoOperationFailure, &[if lock { 5. } else { 6. }])?;
        }
        Ok(())
    }
    fn echo_screenshot(&self) -> Result {
        self.action(Op::SanitizeEchoScreenshotReason, &[])?;
        self.action(Op::PrepareEchoScreenshotDirectory, &[])?;
        if !self.flag(Op::CreateEchoScreenshotDirectory, &[])? {
            self.action(Op::EchoOperationFailure, &[7.])?;
        }
        self.action(Op::PrepareEchoScreenshotCrop, &[])?;
        if !self.flag(Op::EncodeEchoScreenshot, &[])? {
            self.action(Op::EchoOperationFailure, &[8.])?;
        }
        self.action(Op::CreateEchoScreenshotFile, &[])?;
        if !self.flag(Op::OpenEchoScreenshotFile, &[])? {
            self.action(Op::EchoOperationFailure, &[9.])?;
        }
        let written = self.scalar(Op::WriteEchoScreenshot, &[])?;
        if written != self.scalar(Op::EchoScreenshotSize, &[])? {
            self.action(Op::EchoOperationFailure, &[9.])?;
        }
        if !self.flag(Op::CommitEchoScreenshot, &[])? {
            self.action(Op::EchoOperationFailure, &[9.])?;
        }
        Ok(())
    }
    fn echo_upgrade(&self) -> Result {
        let mut saw = false;
        let until = self.scalar(Op::Now, &[])? + 5.;
        while self.scalar(Op::Now, &[])? < until {
            if self.flag(Op::ReadEchoStageAdd, &[])? {
                saw = true;
                self.action(Op::ClickEchoOperationText, &[0.3])?;
            } else if saw {
                break;
            }
        }
        if !saw {
            self.action(Op::EchoOperationFailure, &[10.])?;
        }
        self.action(Op::ReadEchoEnhanceButton, &[])?;
        if !self.flag(Op::HasEchoOperationText, &[])? {
            self.action(Op::EchoOperationFailure, &[11.])?;
        }
        self.action(Op::Sleep, &[0.1])?;
        // A material commit is sent once; result uncertainty never retries it.
        self.action(Op::ClickEchoOperationText, &[1.5])?;
        for _ in 0..12 {
            self.action(Op::ReadEchoUpgradeResult, &[])?;
            if !self.flag(Op::HasEchoOperationText, &[])? {
                return Ok(());
            }
            if self.flag(Op::EchoUpgradeDoNotAsk, &[])? {
                if !self.flag(Op::EchoOperationSkipConfirm, &[])? {
                    self.action(Op::EchoOperationFailure, &[12.])?;
                }
            } else {
                self.action(Op::ClickEchoOperationText, &[1.])?;
            }
        }
        self.action(Op::EchoOperationFailure, &[13.])
    }
    fn echo_enhance(&self) -> Result<i32> {
        self.action(Op::CreateEchoOperationScope, &[0.])?;
        let language = self.scalar(Op::EchoOperationLanguage, &[])? as i32;
        if language != 0 && language != 1 {
            self.action(Op::EchoOperationFailure, &[14.])?;
        }
        self.action(Op::LoadEchoOperationConfig, &[])?;
        let mut success = 0;
        let mut failed = 0;
        for _ in 0..3000 {
            self.action(Op::Refresh, &[])?;
            if !self.flag(Op::ReadEchoCultivate, &[])? {
                self.action(Op::EchoOperationFailure, &[0.])?;
            }
            self.action(Op::ClickRelative, &[0.13, 0.21, 0.5])?;
            if !self.flag(Op::ReadEchoZeroLevel, &[0.])? {
                self.action(Op::ClickRelative, &[0.35, 0.917, 0.5])?;
                self.action(Op::ClickRelative, &[0.35, 0.917, 0.5])?;
                self.action(Op::ClickRelative, &[0.13, 0.21, 0.5])?;
                if !self.flag(Op::ReadEchoZeroLevel, &[0.])? {
                    return Ok(success);
                }
            }
            self.action(Op::EnterEchoCultivate, &[])?;
            for count in 1..=5 {
                self.action(Op::UpgradeEchoItem, &[])?;
                let verified = self.wait_for_interval(
                    || {
                        self.action(Op::ReadEnhancedEchoStats, &[])?;
                        let length = self.scalar(Op::EnhancedEchoStatCount, &[])? as i32;
                        if length != count {
                            return Ok(false);
                        }
                        for index in 0..length {
                            let value = self.scalar(Op::EnhancedEchoStatNumber, &[index as f64])?;
                            if value > 0. {
                                continue;
                            }
                            return Ok(false);
                        }
                        Ok(true)
                    },
                    4.,
                    0.2,
                )?;
                if !verified {
                    self.action(Op::EchoOperationFailure, &[15.])?;
                }
                self.action(Op::EvaluateEnhancedEcho, &[])?;
                if !self.flag(Op::EnhancedEchoKeep, &[])? {
                    self.action(Op::MarkEchoItem, &[0.])?;
                    failed += 1;
                    self.action(Op::ScreenshotRejectedEcho, &[failed as f64])?;
                    if self.flag(Op::HasInfo, &[])? {
                        self.action(Op::EchoFailedInfo, &[failed as f64])?;
                    }
                    self.action(Op::BackEchoToBag, &[])?;
                    break;
                }
                if count == 5 {
                    self.action(Op::MarkEchoItem, &[1.])?;
                    success += 1;
                    self.action(Op::ScreenshotAcceptedEcho, &[success as f64])?;
                    if self.flag(Op::HasInfo, &[])? {
                        self.action(Op::EchoSuccessInfo, &[success as f64])?;
                    }
                    if self.flag(Op::EchoPauseAfterSuccess, &[])? {
                        if self.flag(Op::HasNotify, &[])? {
                            self.action(Op::NotifyEchoSuccess, &[])?;
                        }
                        return Ok(success);
                    }
                    self.action(Op::BackEchoToBag, &[])?;
                }
            }
        }
        self.action(Op::EchoOperationFailure, &[16.])?;
        Err(HOST_ERROR)
    }
    fn echo_change(&self) -> Result<i32> {
        self.action(Op::CreateEchoOperationScope, &[1.])?;
        if self.scalar(Op::EchoOperationLanguage, &[])? != 0. {
            self.action(Op::EchoOperationFailure, &[17.])?;
        }
        self.action(Op::ReadEchoTargetAttribute, &[])?;
        if self.scalar(Op::EchoTargetAttributeKind, &[])? < 0. {
            self.action(Op::EchoOperationFailure, &[18.])?;
        }
        let mut success = 0;
        for _ in 0..3000 {
            self.action(Op::Refresh, &[])?;
            if !self.flag(Op::ReadEchoCultivate, &[])? {
                self.action(Op::EchoOperationFailure, &[0.])?;
            }
            if !self.flag(Op::ReadEchoZeroLevel, &[1.])? {
                return Ok(success);
            }
            self.action(Op::EnterEchoCultivate, &[])?;
            self.action(Op::PrepareEchoOperationScreen, &[])?;
            if !self.flag(Op::ReadEchoEnhanceHeading, &[])? {
                self.action(Op::EchoOperationFailure, &[19.])?;
            }
            self.action(Op::Sleep, &[0.5])?;
            self.action(Op::ReadEchoCurrentMain, &[])?;
            let count = self.scalar(Op::EchoOperationLineCount, &[])? as usize;
            if count == 0 {
                self.action(Op::EchoOperationFailure, &[20.])?;
            }
            for index in 0..count {
                if self.flag(Op::EchoCurrentMainIsTarget, &[index as f64])? {
                    self.action(Op::EchoOperationFailure, &[21.])?;
                }
            }
            self.action(Op::ClickRelative, &[0.04, 0.41, 0.])?;
            if !self.flag(Op::ReadEchoMainAttributeHeading, &[])? {
                self.action(Op::EchoOperationFailure, &[22.])?;
            }
            self.action(Op::Sleep, &[0.8])?;
            self.action(Op::ClickRelative, &[0.52, 0.71, 0.])?;
            if !self.flag(Op::ReadEchoTargetOption, &[])? {
                self.action(Op::EchoOperationFailure, &[23.])?;
            }
            self.action(Op::Sleep, &[0.1])?;
            self.action(Op::ClickEchoOperationText, &[0.5])?;
            if !self.flag(Op::ReadEchoAttributeConfirm, &[])? {
                self.action(Op::EchoOperationFailure, &[24.])?;
            }
            self.action(Op::ClickEchoOperationText, &[2.])?;
            if !self.flag(Op::ReadEchoReconstruct, &[])? {
                self.action(Op::EchoOperationFailure, &[25.])?;
            }
            self.action(Op::ClickEchoOperationText, &[0.5])?;
            if !self.flag(Op::ReadEchoReconstructResult, &[])? {
                self.action(Op::EchoOperationFailure, &[26.])?;
            }
            self.action(Op::BackEchoToBag, &[])?;
            if self.flag(Op::HasInfo, &[])? {
                success += 1;
                self.action(Op::EchoSuccessInfo, &[success as f64])?;
            } else {
                success += 1;
            }
        }
        self.action(Op::EchoOperationFailure, &[27.])?;
        Err(HOST_ERROR)
    }
    fn echo_merge(&self) -> Result<i32> {
        self.action(Op::CreateEchoOperationScope, &[2.])?;
        self.action(Op::EnsureEchoOperationMain, &[])?;
        self.action(Op::OpenEchoBag, &[])?;
        if !self.wait_for(|| Ok(!self.flag(Op::InTeam, &[])?), 5.)? {
            self.action(Op::EchoOperationFailure, &[28.])?;
        }
        self.action(Op::Sleep, &[3.])?;
        if !self.flag(Op::EchoMergePrompt, &[])? {
            if self.flag(Op::HasInfo, &[])? {
                self.action(Op::EchoMergeNotEnoughInfo, &[])?;
            }
            self.action(Op::EnsureEchoOperationMain, &[])?;
            return Ok(0);
        }
        self.action(Op::ConfirmEchoMergeDialog, &[])?;
        self.action(Op::Sleep, &[2.])?;
        self.action(Op::ClickRelative, &[0.034, 0.293, 1.])?;
        self.action(Op::ClickRelative, &[0.580, 0.911, 4.])?;
        self.action(Op::ClickEchoCentered, &[0.602, 0.124, 0.5])?;
        self.action(Op::ClickEchoCentered, &[0.520, 0.904, 2.])?;
        self.action(Op::ClickRelative, &[0.041, 0.918, 1.])?;
        self.action(Op::ClickRelative, &[0.826, 0.840, 0.5])?;
        self.action(Op::ClickRelative, &[0.717, 0.204, 0.5])?;
        self.action(Op::ClickRelative, &[0.041, 0.918, 0.5])?;
        let mut batches = 0;
        while batches < 100 {
            self.action(Op::ClickRelative, &[0.310, 0.915, 0.5])?;
            if !self.wait_for(|| self.flag(Op::ReadEchoSelectAll, &[]), 5.)? {
                self.action(Op::EchoOperationFailure, &[29.])?;
            }
            self.action(Op::ClickEchoSelectAll, &[1.])?;
            if !self.flag(Op::EchoMergeFullSelection, &[])? {
                break;
            }
            self.action(Op::ClickRelative, &[0.782, 0.910, 0.])?;
            self.action(Op::WaitEchoMergeResult, &[])?;
            self.action(Op::Sleep, &[3.])?;
            self.action(Op::ClickEchoCentered, &[0.496, 0.972, 1.])?;
            batches += 1;
        }
        self.action(Op::EnsureEchoOperationMain, &[])?;
        Ok(batches)
    }
    pub(super) fn run_echo_operations(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let value = match method {
            Method::EchoWaitText => {
                self.echo_wait_text(a[0])?;
                0.
            }
            Method::EchoCultivate => {
                self.echo_cultivate()?;
                0.
            }
            Method::EchoZeroLevel => self.echo_zero_level(a[0] != 0.)? as u8 as f64,
            Method::EchoEnterCultivate => {
                self.echo_enter_cultivate()?;
                0.
            }
            Method::EchoBackToBag => {
                self.echo_back_to_bag()?;
                0.
            }
            Method::EchoMark => {
                self.echo_mark(a[0] != 0.)?;
                0.
            }
            Method::EchoScreenshot => {
                self.echo_screenshot()?;
                0.
            }
            Method::EchoUpgrade => {
                self.echo_upgrade()?;
                0.
            }
            Method::EchoEnhance => self.echo_enhance()? as f64,
            Method::EchoChange => self.echo_change()? as f64,
            Method::EchoMerge => self.echo_merge()? as f64,
            _ => return Err(INVALID_ACTION),
        };
        Ok([value, 0., 0.])
    }
}
