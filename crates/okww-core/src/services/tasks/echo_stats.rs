use crate::services::*;
impl Host {
    fn echo_normalize_stat(&self) -> Result {
        self.action(Op::SimplifyEchoProperty, &[])?;
        for (word, kind) in [(0., 0.), (1., 1.)] {
            if self.flag(Op::EchoPropertyContains, &[word])? {
                return self.action(Op::StoreCanonicalEchoProperty, &[kind]);
            }
        }
        for word in 2..5 {
            if self.flag(Op::EchoPropertyContains, &[word as f64])? {
                let percent = self.flag(Op::EchoValueHasPercent, &[])?;
                return self.action(
                    Op::StoreBaseEchoProperty,
                    &[(word - 2) as f64, percent as u8 as f64],
                );
            }
        }
        for word in 5..10 {
            if self.flag(Op::EchoPropertyContains, &[word as f64])? {
                return self.action(Op::StoreCanonicalEchoProperty, &[(word - 3) as f64]);
            }
        }
        Ok(())
    }
    fn echo_pair_stats(&self) -> Result {
        let count = self.scalar(Op::EchoStatLineCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::ReadEchoStatLine, &[index as f64])?;
            self.action(Op::SimplifyEchoStatLine, &[])?;
            if self.flag(Op::EchoStatPropertyMatches, &[])?
                && !self.flag(Op::EchoStatAuxiliary, &[])?
            {
                self.action(Op::TruncateEchoStatProperty, &[])?;
                self.action(Op::AppendEchoStatProperty, &[])?;
            }
            if self.flag(Op::EchoStatNumeric, &[])? {
                self.action(Op::AppendEchoStatValue, &[])?;
            }
        }
        let count = self.scalar(Op::EchoPropertyLineCount, &[])? as usize;
        for property in 0..count {
            self.action(Op::DefaultEchoStatValue, &[])?;
            let count = self.scalar(Op::EchoValueLineCount, &[])? as usize;
            if count > 0 {
                let mut closest = 0;
                for candidate in 1..count {
                    let a =
                        self.scalar(Op::EchoStatYDistance, &[candidate as f64, property as f64])?;
                    let b =
                        self.scalar(Op::EchoStatYDistance, &[closest as f64, property as f64])?;
                    if a < b {
                        closest = candidate;
                    }
                }
                self.action(Op::SelectEchoStatValue, &[closest as f64])?;
                self.action(Op::RemoveEchoStatValue, &[closest as f64])?;
            }
            self.action(Op::AppendPairedEchoStat, &[property as f64])?;
        }
        Ok(())
    }
    fn evaluate_echo_stats(&self) -> Result {
        let count = self.scalar(Op::EchoStatsCount, &[])? as i32;
        self.action(Op::StoreEchoDecisionCount, &[count as f64])?;
        self.action(Op::ReadValidEchoStats, &[])?;
        let mut rate = false;
        let mut damage = false;
        let mut first = false;
        let mut encountered = false;
        let mut rate_value = 0.;
        let mut damage_value = 0.;
        let mut invalid = 0;
        for index in 0..count {
            self.action(Op::NormalizeCurrentEchoStat, &[index as f64])?;
            let value = self.scalar(Op::ReadCurrentEchoStatNumber, &[index as f64])?;
            let kind = self.scalar(Op::CurrentEchoCritKind, &[])? as i32;
            let crit = kind == 0 || kind == 1;
            if self.flag(Op::EchoRuleAllBeforeCrit, &[])?
                && self.flag(Op::ValidEchoContains, &[0.])?
                && self.flag(Op::ValidEchoContains, &[1.])?
                && !encountered
            {
                if !crit && !self.flag(Op::ValidEchoContains, &[2.])? {
                    return self.action(Op::RejectEchoDecision, &[0.]);
                }
                if crit {
                    encountered = true;
                }
            }
            if kind == 0 {
                rate = true;
                rate_value += value;
            }
            if kind == 1 {
                damage = true;
                damage_value += value;
            }
            if crit && self.flag(Op::ValidEchoContains, &[2.])? && !first {
                first = true;
                let equivalent = if kind == 0 { value } else { value / 2. };
                if equivalent < self.scalar(Op::EchoFirstCritThreshold, &[])? {
                    return self.action(Op::RejectEchoDecision, &[1., kind as f64, value]);
                }
            }
            if !self.flag(Op::ValidEchoContains, &[2.])? {
                invalid += 1;
                self.action(Op::StoreEchoInvalidCount, &[invalid as f64])?;
            }
        }
        let remaining = 5 - count;
        let missing = (!rate) as i32 + (!damage) as i32;
        if self.flag(Op::EchoRuleRequiresBothCrit, &[])? && remaining < missing {
            return self.action(Op::RejectEchoDecision, &[2., missing as f64]);
        }
        if rate
            && damage
            && rate_value + damage_value / 2. < self.scalar(Op::EchoTotalCritThreshold, &[])?
        {
            return self.action(
                Op::RejectEchoDecision,
                &[3., rate_value + damage_value / 2.],
            );
        }
        if count == 1 && self.flag(Op::EchoRuleFirstValid, &[])? && invalid == 1 {
            return self.action(Op::RejectEchoDecision, &[4.]);
        }
        if 5 - invalid < self.scalar(Op::EchoValidThreshold, &[])? as i32 {
            return self.action(Op::RejectEchoDecision, &[5., (5 - invalid) as f64]);
        }
        Ok(())
    }
    pub(in crate::services) fn run_echo_stats(
        &self,
        method: Method,
        _a: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::EchoStatNumber => {
                self.action(Op::NormalizeEchoNumberPercent, &[])?;
                let [value, ok, _] = self.call(Op::ParseEchoStatNumber, &[])?;
                return Ok([
                    if ok != 0. && value.is_finite() {
                        value
                    } else {
                        0.
                    },
                    0.,
                    0.,
                ]);
            }
            Method::EchoFixedMain => {
                self.action(Op::SimplifyEchoMain, &[])?;
                for index in 0..4 {
                    self.action(Op::ReplaceEchoMainTerm, &[index as f64])?;
                }
            }
            Method::EchoNormalizeStat => self.echo_normalize_stat()?,
            Method::EchoPairStats => self.echo_pair_stats()?,
            Method::EchoEvaluateStats => self.evaluate_echo_stats()?,
            Method::EchoMergeResult => return self.call(Op::EchoMergeTextConfirmed, &[]),
            Method::EchoAwaitMerge => {
                let confirmed = Cell::new(false);
                if !self.wait_for_interval(
                    || {
                        if self.flag(Op::ReadEchoMergeResult, &[])? {
                            return Ok(true);
                        }
                        if !confirmed.get() {
                            confirmed.set(self.flag(Op::EchoMergeSkipConfirm, &[])?);
                        }
                        Ok(false)
                    },
                    12.,
                    0.2,
                )? {
                    self.action(Op::EchoStatsFailure, &[])?;
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
