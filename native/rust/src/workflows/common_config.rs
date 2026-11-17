//! Configuration merge, persistence and validation with the original field semantics.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn common_config_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::CommonConfigFailure, &[code])?;
        Err(HOST_ERROR)
    }
    fn config_number(&self, field: i32) -> Result<f64> {
        self.scalar(Op::CommonConfigNumber, &[f64::from(field)])
    }
    fn config_integer(&self, field: i32) -> Result<f64> {
        self.scalar(Op::CommonConfigInteger, &[f64::from(field)])
    }
    pub(super) fn run_common_config(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::CommonReadJson => {
                if !self.flag(Op::CommonJsonOpenRead, &[])? {
                    return self.common_config_fail(0.);
                }
                self.action(Op::CommonJsonReadBytes, &[])?;
                if self.flag(Op::CommonJsonHasBom, &[])? {
                    self.action(Op::CommonJsonRemoveBom, &[])?;
                }
                self.action(Op::CommonJsonParse, &[])?;
                if !self.flag(Op::CommonJsonParseOk, &[])?
                    || !self.flag(Op::CommonJsonObject, &[])?
                {
                    return self.common_config_fail(1.);
                }
                self.action(Op::CommonJsonResult, &[])?;
            }
            Method::CommonWriteJson => {
                self.action(Op::CommonJsonMakeDirectory, &[])?;
                self.action(Op::CommonJsonSaveFile, &[])?;
                self.action(Op::CommonJsonEncode, &[])?;
                if !self.flag(Op::CommonJsonOpenWrite, &[])?
                    || !self.flag(Op::CommonJsonWrite, &[])?
                    || !self.flag(Op::CommonJsonCommit, &[])?
                {
                    return self.common_config_fail(2.);
                }
            }
            Method::CommonConfigLoad => {
                self.action(Op::CommonConfigReadIncoming, &[])?;
                self.action(Op::CommonConfigIncomingBegin, &[])?;
                while self.flag(Op::CommonConfigIncomingMore, &[])? {
                    if self.flag(Op::CommonConfigIncomingObject, &[])? {
                        self.action(Op::CommonConfigExistingSub, &[])?;
                        self.action(Op::CommonConfigIncomingSub, &[])?;
                        self.action(Op::CommonConfigSubBegin, &[])?;
                        while self.flag(Op::CommonConfigSubMore, &[])? {
                            self.action(Op::CommonConfigSubAssign, &[])?;
                            self.action(Op::CommonConfigSubNext, &[])?;
                        }
                        self.action(Op::CommonConfigAssignSub, &[])?;
                        self.action(Op::CommonConfigDropIncomingSub, &[])?;
                    } else {
                        self.action(Op::CommonConfigAssignValue, &[])?;
                    }
                    self.action(Op::CommonConfigIncomingNext, &[])?;
                }
                self.action(Op::CommonConfigNormalize, &[])?;
            }
            Method::CommonConfigNormalize => {
                self.action(Op::CommonConfigReadBox, &[])?;
                if self.flag(Op::CommonConfigRetiredProtocol, &[])? {
                    self.action(Op::CommonConfigNetworkProtocol, &[])?;
                }
                for i in 0..5 {
                    self.action(Op::CommonConfigRemoveRetiredKey, &[f64::from(i)])?;
                }
                self.action(Op::CommonConfigStoreBox, &[])?;
                self.action(Op::CommonConfigReadCapture, &[])?;
                self.action(Op::CommonConfigRemoveAutoRegion, &[])?;
                if !self.flag(Op::CommonConfigCaptureIsDesktop, &[])? {
                    self.action(Op::CommonConfigSetNv12, &[])?;
                }
                self.action(Op::CommonConfigStoreCapture, &[])?;
            }
            Method::CommonConfigSave => {
                self.action(Op::CommonConfigValidateCapture, &[])?;
                self.action(Op::CommonConfigValidateBox, &[])?;
                self.action(Op::CommonConfigWrite, &[])?;
            }
            Method::CommonConfigGet => {
                self.action(Op::CommonConfigSplitPath, &[])?;
                let n = self.scalar(Op::CommonConfigPartCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::CommonConfigGetPart, &[i as f64])?;
                }
            }
            Method::CommonConfigSet => {
                self.action(Op::CommonConfigSplitPath, &[])?;
                if self.scalar(Op::CommonConfigPartCount, &[])? == 1. {
                    self.action(Op::CommonConfigSetTop, &[])?;
                } else {
                    self.action(Op::CommonConfigReadSetSub, &[])?;
                    self.action(Op::CommonConfigSetSecond, &[])?;
                    self.action(Op::CommonConfigStoreSetSub, &[])?;
                }
            }
            Method::CommonConfigValidateCapture => {
                let local = self.flag(Op::CommonConfigSourceDesktop, &[])?;
                if self.scalar(Op::CommonConfigSourceCode, &[])? < 0. {
                    return self.common_config_fail(3.);
                }
                if local
                    && (self.scalar(Op::CommonConfigLocalBackendCode, &[])? < 0.
                        || !self.config_number(6)?.is_finite()
                        || self.config_number(6)? != self.config_integer(6)?
                        || self.config_integer(6)? < 1.
                        || self.config_integer(6)? > 1000.)
                {
                    return self.common_config_fail(4.);
                }
                self.action(Op::CommonConfigReadLocalTarget, &[])?;
                if local
                    && self.scalar(Op::CommonConfigLocalBackendCode, &[])? == 1.
                    && !self.flag(Op::CommonConfigLocalTargetEmpty, &[])?
                    && self.scalar(Op::CommonConfigLocalTargetKind, &[])? < 0.
                {
                    return self.common_config_fail(5.);
                }
                if !local && self.scalar(Op::CommonConfigCardBackendCode, &[])? < 0. {
                    return self.common_config_fail(6.);
                }
                if !local
                    && (self.config_integer(0)? < 2.
                        || self.config_integer(1)? < 2.
                        || self.config_integer(0)? > 8192.
                        || self.config_integer(1)? > 8192.
                        || !self.config_number(2)?.is_finite()
                        || self.config_number(2)? <= 0.
                        || self.config_number(2)? > 1000.)
                {
                    return self.common_config_fail(7.);
                }
                if !local && !self.flag(Op::CommonConfigFormatNv12, &[])? {
                    return self.common_config_fail(8.);
                }
                if self.config_integer(3)? < 1280.
                    || self.config_integer(4)? < 720.
                    || self.config_integer(3)? > 8192.
                    || (self.config_number(3)? / self.config_number(4)? - 16. / 9.).abs() > 0.01
                {
                    return self.common_config_fail(9.);
                }
                if self.config_number(5)? <= 0. || self.config_number(5)? > 5. {
                    return self.common_config_fail(10.);
                }
            }
            Method::CommonConfigValidateBox => {
                if self.scalar(Op::CommonConfigProtocolCode, &[])? < 0. {
                    return self.common_config_fail(11.);
                }
                if self.scalar(Op::CommonConfigProtocolCode, &[])? == 1.
                    && self.flag(Op::CommonConfigTargetEnabled, &[])?
                    && !self.flag(Op::CommonConfigInputTargetWindow, &[])?
                {
                    return self.common_config_fail(12.);
                }
                if self.config_integer(7)? <= 0.
                    || self.config_integer(8)? <= 0.
                    || (self.scalar(Op::CommonConfigProtocolCode, &[])? != 1.
                        && (self.config_number(9)? <= 0.
                            || self.config_number(9)? > 5.
                            || self.config_number(10)? <= 0.
                            || self.config_number(11)? <= 0.))
                {
                    return self.common_config_fail(13.);
                }
                if !self.config_number(12)?.is_finite()
                    || !self.config_number(13)?.is_finite()
                    || self.config_number(12)? < 0.11
                    || self.config_number(12)? > 1.
                    || self.config_number(13)? < 0.1
                    || self.config_number(13)? > 0.5
                {
                    return self.common_config_fail(14.);
                }
                if self.flag(Op::CommonConfigDryRun, &[])? {
                    return Ok([0.; 3]);
                }
                if self.scalar(Op::CommonConfigProtocolCode, &[])? == 0.
                    && (!self.flag(Op::CommonConfigIpv4, &[])?
                        || self.config_integer(14)? < 1.
                        || self.config_integer(14)? > 65535.
                        || !self.flag(Op::CommonConfigUuidValid, &[])?)
                {
                    return self.common_config_fail(15.);
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
