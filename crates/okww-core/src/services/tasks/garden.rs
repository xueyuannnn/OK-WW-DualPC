use crate::services::*;
impl Host {
    fn garden_turn(&self) -> Result<[f64; 3]> {
        let count = self.scalar(Op::ReadGardenTurnText, &[])? as usize;
        for index in 0..count {
            self.action(Op::AppendGardenTurnText, &[index as f64])?;
        }
        self.action(Op::ParseGardenStage, &[])?;
        self.action(Op::ParseGardenDay, &[])?;
        if !self.flag(Op::GardenStageExists, &[])? || !self.flag(Op::GardenDayExists, &[])? {
            return Ok([0.; 3]);
        }
        let values = self.call(Op::GardenTurnValues, &[])?;
        Ok([1., values[0], values[1]])
    }
    fn blessing_controls(&self) -> Result<[f64; 3]> {
        let count = self.scalar(Op::ReadBlessingText, &[])? as usize;
        self.action(Op::FindBlessingButton, &[])?;
        for index in 0..count {
            self.action(Op::AppendBlessingText, &[index as f64])?;
        }
        if self.flag(Op::ParseBlessingUses, &[])? {
            return Ok([1., self.scalar(Op::BlessingUsesValue, &[])?, 0.]);
        }
        Ok([0.; 3])
    }
    fn use_blessing(&self, open: bool) -> Result<bool> {
        self.action(Op::ReadGardenTurn, &[])?;
        if !open {
            if !self.flag(Op::GardenCurrentTurnExists, &[])?
                || self.flag(Op::GardenCheckedTurnMatches, &[])?
            {
                return Ok(false);
            }
            let attempts = self.scalar(Op::GardenTurnAttempts, &[])?;
            if attempts >= 3. {
                return Ok(false);
            }
            self.action(Op::SetGardenTurnAttempts, &[attempts + 1.])?;
            self.action(Op::ClickRelative, &[0.18, 0.785, 0.3])?;
            self.action(Op::Refresh, &[])?;
        }
        self.action(Op::ReadBlessingControls, &[])?;
        if self.flag(Op::BlessingButtonExists, &[])?
            && self.flag(Op::BlessingUsesExists, &[])?
            && self.scalar(Op::CurrentBlessingUses, &[])? > 0.
        {
            self.action(Op::ClickBlessingButton, &[0.3])?;
            self.action(Op::Refresh, &[])?;
            self.action(Op::ReadAfterBlessingControls, &[])?;
            if !self.flag(Op::AfterBlessingButtonExists, &[])?
                || (self.flag(Op::AfterBlessingUsesExists, &[])?
                    && self.scalar(Op::AfterBlessingUses, &[])?
                        < self.scalar(Op::CurrentBlessingUses, &[])?)
            {
                self.action(Op::StoreGardenCheckedTurn, &[])?;
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::BlessingUsedInfo, &[])?;
                }
            }
        } else {
            if self.flag(Op::BlessingUsesExists, &[])?
                && self.scalar(Op::CurrentBlessingUses, &[])? == 0.
            {
                self.action(Op::StoreGardenCheckedTurn, &[])?;
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::BlessingEmptyInfo, &[])?;
                }
            }
            self.action(Op::ClickRelative, &[0.5, 0.8, 0.2])?;
        }
        self.action(Op::SetBlessingOpen, &[0.])?;
        Ok(true)
    }
    fn maximize_speed(&self) -> Result<bool> {
        if self.scalar(Op::Now, &[])? - self.scalar(Op::SpeedChecked, &[])? < 30. {
            return Ok(false);
        }
        let count = self.scalar(Op::ReadGardenSpeedText, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectGardenSpeedLine, &[index as f64])?;
            if self.flag(Op::GardenSpeedMax, &[])? {
                self.action(Op::SetSpeedChecked, &[self.scalar(Op::Now, &[])?])?;
                self.action(Op::SetSpeedAttempts, &[0.])?;
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::GardenMaxSpeedInfo, &[])?;
                }
                return Ok(false);
            }
            if self.flag(Op::GardenSpeedMatches, &[])? {
                let attempts = self.scalar(Op::SpeedAttempts, &[])?;
                if attempts >= 6. {
                    self.action(Op::SetSpeedChecked, &[self.scalar(Op::Now, &[])?])?;
                    self.action(Op::SetSpeedAttempts, &[0.])?;
                    return Ok(false);
                }
                self.action(Op::ClickGardenSpeedLine, &[index as f64, 0.3])?;
                self.action(
                    Op::SetSpeedAttempts,
                    &[self.scalar(Op::SpeedAttempts, &[])? + 1.],
                )?;
                return Ok(true);
            }
        }
        Ok(false)
    }
    fn garden_best_feature(&self) -> Result {
        if !self.flag(Op::InitialBlessingChecked, &[])? {
            self.action(Op::SetInitialBlessingChecked, &[1.])?;
            if self.flag(Op::ReadInitialBlessingButton, &[])? {
                self.action(Op::SetBlessingOpen, &[1.])?;
            }
        }
        if self.flag(Op::BlessingOpen, &[])? {
            self.action(Op::UseGardenBlessing, &[1.])?;
            return Ok(());
        }
        if self.flag(Op::ReadModernGardenReward, &[])? {
            if self.flag(Op::ModernGardenChoice, &[])? {
                self.action(Op::Sleep, &[0.3])?;
                self.action(Op::Refresh, &[])?;
                self.action(Op::ReloadGardenRewardResult, &[])?;
            } else {
                self.action(Op::StoreModernGardenReward, &[])?;
            }
            return Ok(());
        }
        let names = self.scalar(Op::ReadGardenTemplateNames, &[])? as usize;
        for index in 0..names {
            if !self.flag(Op::GardenTemplateName, &[index as f64])? {
                continue;
            }
            let kind = self.scalar(Op::GardenTemplateKind, &[index as f64])?;
            if kind == 0. || kind == 1. {
                continue;
            }
            self.action(
                Op::AppendGardenTemplateMatches,
                &[index as f64, if kind == 2. { 0.4 } else { 0.002 }],
            )?;
        }
        let count = self.scalar(Op::GardenFeatureMatchCount, &[])? as usize;
        for priority in 0..2 {
            for index in 0..count {
                if self.flag(Op::GardenFeaturePriority, &[index as f64, priority as f64])?
                    && (!self.flag(Op::GardenFeatureTargetExists, &[])?
                        || self.scalar(Op::GardenFeatureConfidence, &[index as f64])?
                            > self.scalar(Op::GardenTargetConfidence, &[])?)
                {
                    self.action(Op::SelectGardenFeatureTarget, &[index as f64])?;
                }
            }
            if self.flag(Op::GardenFeatureTargetExists, &[])? {
                break;
            }
        }
        if !self.flag(Op::GardenFeatureTargetExists, &[])? {
            for index in 0..count {
                if !self.flag(Op::GardenFeatureTargetExists, &[])?
                    || self.scalar(Op::GardenFeatureConfidence, &[index as f64])?
                        > self.scalar(Op::GardenTargetConfidence, &[])?
                {
                    self.action(Op::SelectGardenFeatureTarget, &[index as f64])?;
                }
            }
        }
        if self.flag(Op::GardenFeatureTargetExists, &[])? {
            let kind = self.scalar(Op::GardenFeatureTargetKind, &[])?;
            if kind == 0. || kind == 1. {
                self.action(Op::OpenGardenShop, &[])?;
                return Ok(());
            }
            if kind == 2.
                && (self.flag(Op::MaximizeGardenSpeed, &[])?
                    || self.flag(Op::UseGardenBlessing, &[0.])?)
            {
                return Ok(());
            }
        }
        self.action(Op::StoreGardenFeatureTarget, &[])
    }
    fn garden_active(&self) -> Result<bool> {
        self.action(Op::Refresh, &[])?;
        if self.flag(Op::GardenActiveFeatures, &[0.85])? {
            return Ok(true);
        }
        if self.flag(Op::GardenActiveReward, &[])? {
            return Ok(true);
        }
        if self.flag(Op::GardenActiveBlessing, &[])? {
            self.action(Op::SetBlessingOpen, &[1.])?;
            return Ok(true);
        }
        let count = self.scalar(Op::ReadGardenActiveText, &[])? as usize;
        for index in 0..count {
            self.action(Op::AppendGardenActiveText, &[index as f64])?;
        }
        Ok(self.flag(Op::GardenActiveHeading, &[])? && self.flag(Op::GardenActiveStage, &[])?)
    }
    fn garden_loop(&self) -> Result {
        self.action(Op::ResetGardenCheckedTurn, &[])?;
        self.action(Op::ClearGardenTurnAttempts, &[])?;
        self.action(Op::SetInitialBlessingChecked, &[0.])?;
        self.action(Op::SetSpeedChecked, &[-1e12])?;
        let deadline = self.scalar(Op::Now, &[])? + 3600.;
        let mut last_action = self.scalar(Op::Now, &[])?;
        loop {
            if self.scalar(Op::Now, &[])? > deadline
                || self.scalar(Op::Now, &[])? - last_action > 180.
            {
                return self.fail(501.);
            }
            self.action(Op::Sleep, &[0.1])?;
            self.action(Op::ReadBestGardenFeature, &[])?;
            self.action(Op::Sleep, &[0.2])?;
            if self.flag(Op::BestGardenFeatureExists, &[])? {
                last_action = self.scalar(Op::Now, &[])?;
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::BestGardenFeatureInfo, &[])?;
                }
                let kind = self.scalar(Op::BestGardenFeatureKind, &[])?;
                if kind == 0. {
                    self.action(Op::Sleep, &[1.])?;
                    if self.flag(Op::FindGardenGold, &[0.9])?
                        || self.flag(Op::FindGardenPurple, &[0.9])?
                    {
                        self.action(Op::ClickGardenRewardTier, &[1.])?;
                    } else {
                        self.action(Op::ClickRelative, &[0.5, 0.2, 1.])?;
                    }
                    self.action(Op::ClickGardenFixedFeature, &[0., 1.])?;
                    continue;
                }
                if kind == 1. {
                    if self.scalar(Op::ReadGardenNotInterested, &[0.4])? > 0. {
                        self.action(Op::ClickLastGardenNotInterested, &[1.])?;
                    }
                    self.action(Op::ClickGardenFixedFeature, &[1., 1.])?;
                    continue;
                }
                if kind == 2. {
                    self.action(Op::ClickRelative, &[965. / 1920., 860. / 1080., 2.])?;
                    self.action(Op::ClickRelative, &[700. / 1920., 666. / 1080., 2.])?;
                    self.action(Op::ClickRelative, &[1600. / 1920., 900. / 1080., 2.])?;
                }
                self.action(Op::ClickBestGardenFeature, &[1.])?;
            } else {
                let finish = self.call(Op::ReadGardenFinishButtons, &[])?;
                if finish[0] != 0. && finish[1] != 0. {
                    self.action(Op::Sleep, &[2.])?;
                    self.action(Op::ClickGardenBack, &[1.])?;
                    if self.wait_for(|| self.flag(Op::FindGardenStart, &[]), 5.)? {
                        self.action(Op::Key, &[0., 1.])?;
                    }
                    return Ok(());
                }
                self.action(Op::Sleep, &[0.2])?;
            }
        }
    }
    fn open_weekly(&self) -> Result {
        let mut opened = false;
        for attempt in 0..2 {
            if opened {
                break;
            }
            self.action(Op::OpenGardenWeeklyBook, &[])?;
            self.action(Op::Sleep, &[0.5])?;
            self.action(Op::ClickRelative, &[0.343, 0.129, 0.5])?;
            opened =
                self.wait_for_interval(|| self.flag(Op::ReadGardenWeeklyProgress, &[]), 6., 0.3)?;
            if !opened && self.flag(Op::HasInfo, &[])? {
                self.action(Op::GardenWeeklyRetryInfo, &[(attempt + 1) as f64])?;
            }
        }
        if !opened {
            self.action(Op::InvokeGardenWeeklyFailure, &[0.])?;
            return Err(HOST_ERROR);
        }
        self.action(Op::ClickRelative, &[0.927, 0.893, 3.])?;
        self.action(Op::ClickRelative, &[0.927, 0.893, 2.])
    }
    fn garden_run_loop(&self) -> Result {
        let retries = if self.flag(Op::RecoveryEnabled, &[])? {
            (self.scalar(Op::RecoveryLimit, &[])? as i32).clamp(1, 10)
        } else {
            0
        };
        let mut attempt = 0;
        loop {
            if self.action(Op::RunGardenLoop, &[]).is_ok() {
                return Ok(());
            }
            if self.scalar(Op::ExceptionKind, &[])? != 2. || attempt >= retries {
                return Err(HOST_ERROR);
            }
            self.action(Op::ClearException, &[])?;
            if self.flag(Op::HasInfo, &[])? {
                self.action(
                    Op::GardenLoopRetryInfo,
                    &[(attempt + 1) as f64, retries as f64],
                )?;
            }
            self.action(Op::Sleep, &[0.5])?;
            attempt += 1;
        }
    }
    fn garden_run(&self) -> Result {
        self.action(Op::GardenTaskScope, &[])?;
        if self.flag(Op::IsGardenActive, &[])? {
            self.action(Op::RunGardenLoopWithRetries, &[])?;
        }
        self.action(Op::NavEnsureMain, &[30.])?;
        for _round in 0..50 {
            self.action(Op::OpenGardenWeekly, &[])?;
            self.wait_for(|| self.flag(Op::ReadGardenWeeklyProgress, &[]), 5.)?;
            if !self.flag(Op::GardenProgressExists, &[])? {
                self.action(Op::InvokeGardenWeeklyFailure, &[1.])?;
                return Err(HOST_ERROR);
            }
            let progress = self.scalar(Op::GardenProgressValue, &[])?;
            if self.flag(Op::HasInfo, &[])? {
                self.action(Op::GardenPointsInfo, &[progress])?;
            }
            if progress >= 6000. {
                return Ok(());
            }
            self.action(Op::ClickRelative, &[0.246, 0.486, 1.])?;
            self.action(Op::RunGardenLoopWithRetries, &[])?;
        }
        self.fail(502.)
    }
    pub(in crate::services) fn run_garden(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let mut value = 0.;
        match method {
            Method::GardenTurn => return self.garden_turn(),
            Method::GardenBlessingControls => return self.blessing_controls(),
            Method::GardenUseBlessing => value = self.use_blessing(a[0] != 0.)? as u8 as f64,
            Method::GardenMaxSpeed => value = self.maximize_speed()? as u8 as f64,
            Method::GardenBestFeature => self.garden_best_feature()?,
            Method::GardenActive => value = self.garden_active()? as u8 as f64,
            Method::GardenLoop => self.garden_loop()?,
            Method::GardenOpenWeekly => self.open_weekly()?,
            Method::GardenWeeklyFailure => {
                if self.flag(Op::HasGardenDiagnostic, &[])? {
                    self.action(Op::EmitGardenWeeklyDiagnostic, &[])?;
                }
                self.fail(503.)?;
            }
            Method::GardenRunLoop => self.garden_run_loop()?,
            Method::GardenRun => self.garden_run()?,
            _ => return Err(INVALID_ACTION),
        }
        Ok([value, 0., 0.])
    }
}
