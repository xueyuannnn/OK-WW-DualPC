//! The Windows scheduler keeps its existing names, XML and rollback order.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn schedule_fail<T>(&self, reason: f64) -> Result<T> {
        self.action(Op::ScheduleFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    fn xml_begin(&self, tag: f64) -> Result {
        self.action(Op::ScheduleXmlBegin, &[tag])
    }
    fn xml_end(&self) -> Result {
        self.action(Op::ScheduleXmlEnd, &[])
    }
    fn xml_text(&self, tag: f64, source: f64, value: f64) -> Result {
        self.action(Op::ScheduleXmlText, &[tag, source, value])
    }
    fn schedule_xml(&self) -> Result {
        self.action(Op::ScheduleReadTaskCatalogue, &[])?;
        let mut found = false;
        let count = self.scalar(Op::ScheduleNativeTaskCount, &[])? as usize;
        for i in 0..count {
            self.action(Op::ScheduleLoadNativeTask, &[i as f64])?;
            if self.flag(Op::ScheduleEntryMatches, &[])?
                && !self.flag(Op::ScheduleEntryTrigger, &[])?
            {
                found = true;
            }
        }
        // Script catalogue errors still propagate when a builtin was found.
        self.action(Op::ScheduleReadScriptCatalogue, &[])?;
        let count = self.scalar(Op::ScheduleScriptTaskCount, &[])? as usize;
        for i in 0..count {
            self.action(Op::ScheduleLoadScriptTask, &[i as f64])?;
            if self.flag(Op::ScheduleEntryMatches, &[])?
                && !self.flag(Op::ScheduleEntryTrigger, &[])?
                && self.flag(Op::ScheduleEntrySupported, &[])?
            {
                found = true;
            }
        }
        if !found {
            return self.schedule_fail(7.);
        }
        let kind = self.scalar(Op::ScheduleKind, &[])? as i32;
        if kind < 0 {
            return self.schedule_fail(8.);
        }
        self.action(Op::ScheduleParseStart, &[])?;
        if !self.flag(Op::ScheduleStartValid, &[])? {
            return self.schedule_fail(9.);
        }
        let interval = self.scalar(
            Op::ScheduleReadNumber,
            &[0., 1., 1., if kind == 5 { 744. } else { 365. }],
        )?;
        let weekday = self.scalar(Op::ScheduleReadNumber, &[1., 1., 1., 7.])?;
        let monthday = self.scalar(Op::ScheduleReadNumber, &[2., 1., 1., 31.])?;
        let limit = self.scalar(Op::ScheduleReadNumber, &[3., 0., 0., 168.])?;
        self.action(Op::ScheduleCreateXmlWriter, &[])?;
        self.action(Op::ScheduleXmlDocument, &[])?;
        self.xml_begin(0.)?;
        self.action(Op::ScheduleXmlNamespace, &[])?;
        self.action(Op::ScheduleXmlAttribute, &[0.])?;
        self.xml_begin(1.)?;
        self.xml_text(2., 0., 0.)?;
        self.xml_text(3., 1., 0.)?;
        self.xml_end()?;
        self.xml_begin(4.)?;
        self.xml_begin(if matches!(kind, 0 | 5) { 5. } else { 6. })?;
        if kind == 5 {
            self.xml_begin(7.)?;
            self.xml_text(8., 2., interval)?;
            self.xml_text(9., 3., 0.)?;
            self.xml_end()?;
        }
        self.xml_text(10., 4., 0.)?;
        self.xml_text(11., 5., 0.)?;
        match kind {
            1 | 4 => {
                self.xml_begin(12.)?;
                self.xml_text(13., 6., if kind == 4 { interval } else { 1. })?;
                self.xml_end()?;
            }
            2 => {
                self.xml_begin(14.)?;
                self.xml_text(15., 6., 1.)?;
                self.xml_begin(16.)?;
                self.action(Op::ScheduleXmlWeekday, &[weekday - 1.])?;
                self.xml_end()?;
                self.xml_end()?;
            }
            3 => {
                self.xml_begin(17.)?;
                self.xml_begin(18.)?;
                self.xml_text(19., 6., monthday)?;
                self.xml_end()?;
                self.xml_begin(20.)?;
                for month in 0..12 {
                    self.action(Op::ScheduleXmlMonth, &[f64::from(month)])?;
                }
                self.xml_end()?;
                self.xml_end()?;
            }
            _ => {}
        }
        self.xml_end()?;
        self.xml_end()?;
        self.xml_begin(21.)?;
        self.xml_begin(22.)?;
        self.action(Op::ScheduleXmlAttribute, &[1.])?;
        self.xml_text(23., 7., 0.)?;
        self.xml_text(24., 8., 0.)?;
        self.xml_text(25., 9., 0.)?;
        self.xml_end()?;
        self.xml_end()?;
        self.xml_begin(26.)?;
        for (tag, source, value) in [
            (27., 10., 0.),
            (28., 3., 0.),
            (29., 3., 0.),
            (30., 3., 0.),
            (31., 5., 0.),
            (32., 3., 0.),
            (33., 5., 0.),
            (11., 11., 0.),
            (34., 3., 0.),
            (35., 12., 0.),
            (36., 6., 7.),
        ] {
            self.xml_text(tag, source, value)?;
        }
        self.xml_end()?;
        self.action(Op::ScheduleBuildArguments, &[limit * 3600.])?;
        if self.flag(Op::ScheduleExitAfter, &[])? {
            self.action(Op::ScheduleAppendExitArgument, &[])?;
        }
        let count = self.scalar(Op::ScheduleArgumentCount, &[])? as usize;
        for i in 0..count {
            self.action(Op::ScheduleQuoteArgument, &[i as f64])?;
        }
        self.xml_begin(37.)?;
        self.action(Op::ScheduleXmlAttribute, &[2.])?;
        self.xml_begin(38.)?;
        self.xml_text(39., 13., 0.)?;
        self.xml_text(40., 14., 0.)?;
        self.xml_text(41., 15., 0.)?;
        self.xml_end()?;
        self.xml_end()?;
        self.xml_end()?;
        self.action(Op::ScheduleXmlDocumentEnd, &[])
    }
    pub(in crate::services) fn run_scheduler(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let mut result = [0.; 3];
        match method {
            Method::ScheduleInitialize => {
                if self.flag(Op::ScheduleUserEmpty, &[])? {
                    self.action(Op::ScheduleFillCurrentUser, &[])?;
                }
                if !self.flag(Op::ScheduleHasCommand, &[])? {
                    self.action(Op::ScheduleFillDefaultCommand, &[])?;
                }
                self.action(Op::ScheduleBuildPrefix, &[])?;
            }
            Method::ScheduleCurrentUser => {
                if !self.flag(Op::ScheduleOpenToken, &[])? {
                    return self.schedule_fail(0.);
                }
                self.action(Op::ScheduleQueryTokenSize, &[])?;
                self.action(Op::ScheduleAllocateToken, &[])?;
                let read = self.flag(Op::ScheduleReadToken, &[])?;
                self.action(Op::ScheduleCloseToken, &[])?;
                if !read {
                    return self.schedule_fail(1.);
                }
                if !self.flag(Op::ScheduleConvertSid, &[])? {
                    return self.schedule_fail(2.);
                }
                self.action(Op::ScheduleReadSidString, &[])?;
                self.action(Op::ScheduleFreeSid, &[])?;
            }
            Method::ScheduleRunCommand => {
                self.action(Op::ScheduleHideCommandWindow, &[])?;
                self.action(Op::ScheduleStartCommand, &[])?;
                if !self.flag(Op::ScheduleCommandStarted, &[5000.])? {
                    return self.schedule_fail(3.);
                }
                if !self.flag(Op::ScheduleCommandFinished, &[10000.])? {
                    self.action(Op::ScheduleKillCommand, &[])?;
                    self.action(Op::ScheduleWaitCommand, &[1000.])?;
                    return self.schedule_fail(4.);
                }
                if !self.flag(Op::ScheduleCommandNormal, &[])?
                    || self.scalar(Op::ScheduleCommandExitCode, &[])? != 0.
                {
                    return self.schedule_fail(5.);
                }
                self.action(Op::ScheduleReadCommandOutput, &[])?;
            }
            Method::ScheduleQuoteArgument => {
                self.action(Op::ScheduleQuoteStart, &[])?;
                let length = self.scalar(Op::ScheduleQuoteLength, &[])? as usize;
                let mut slashes = 0;
                for i in 0..length {
                    let c = self.scalar(Op::ScheduleQuoteUnit, &[i as f64])? as u16;
                    if c == 92 {
                        slashes += 1;
                        continue;
                    }
                    self.action(
                        Op::ScheduleQuoteAppendUnit,
                        &[
                            i as f64,
                            f64::from(if c == 34 { slashes * 2 + 1 } else { slashes }),
                        ],
                    )?;
                    slashes = 0;
                }
                self.action(Op::ScheduleQuoteFinish, &[f64::from(slashes * 2)])?;
            }
            Method::ScheduleNumber => {
                let n = if self.flag(Op::ScheduleNumberUndefined, &[])? {
                    a[0]
                } else {
                    self.scalar(Op::ScheduleNumberValue, &[])?
                };
                if !n.is_finite() || n != n.floor() || n < a[1] || n > a[2] {
                    return self.schedule_fail(6.);
                }
                result[0] = n;
            }
            Method::ScheduleXml => self.schedule_xml()?,
            Method::ScheduleRegistry => {
                if self.flag(Op::ScheduleRegistryExists, &[])? {
                    self.action(Op::ScheduleReadRegistry, &[])?;
                }
            }
            Method::ScheduleSave => {
                self.action(Op::ScheduleMakeDirectory, &[])?;
                self.action(Op::ScheduleWriteRegistry, &[])?;
            }
            Method::ScheduleValidateName => {
                if !self.flag(Op::ScheduleNamePrefix, &[])?
                    || !self.flag(Op::ScheduleNameId, &[])?
                {
                    return self.schedule_fail(10.);
                }
            }
            Method::ScheduleList => {
                self.action(Op::ScheduleLoadRegistry, &[])?;
                let count = self.scalar(Op::ScheduleRegistryCount, &[])? as usize;
                for i in 0..count {
                    self.action(Op::ScheduleSelectEntry, &[i as f64])?;
                    self.action(Op::ScheduleValidateSelectedName, &[])?;
                    self.action(Op::ScheduleAppendEntry, &[])?;
                }
            }
            Method::ScheduleRegister => {
                self.action(Op::ScheduleCreateTemporaryDirectory, &[])?;
                if !self.flag(Op::ScheduleTemporaryDirectoryValid, &[])? {
                    return self.schedule_fail(11.);
                }
                self.action(Op::ScheduleTemporaryPath, &[])?;
                self.action(Op::ScheduleCreateFile, &[])?;
                self.action(Op::ScheduleEncodeXml, &[])?;
                if !self.flag(Op::ScheduleOpenFile, &[])?
                    || !self.flag(Op::ScheduleWriteFile, &[])?
                    || !self.flag(Op::ScheduleFlushFile, &[])?
                {
                    return self.schedule_fail(12.);
                }
                self.action(Op::ScheduleDestroyFile, &[])?;
                self.action(Op::ScheduleInvokeCreate, &[])?;
            }
            Method::ScheduleAdd => {
                self.action(Op::ScheduleSetNewEnabled, &[])?;
                self.action(Op::ScheduleBuildDefinition, &[])?;
                self.action(Op::ScheduleGenerateName, &[])?;
                self.action(Op::ScheduleRegisterDefinition, &[])?;
                self.action(Op::ScheduleLoadRegistry, &[])?;
                self.action(Op::ScheduleStoreSettings, &[])?;
                let saved = self.action(Op::ScheduleSaveRegistry, &[]);
                if saved.is_err() {
                    let _ = self.action(Op::ScheduleInvokeDelete, &[]);
                    saved?;
                }
                self.action(Op::ScheduleReturnName, &[])?;
            }
            Method::ScheduleUpdate => {
                self.action(Op::ScheduleValidateRequestedName, &[])?;
                self.action(Op::ScheduleLoadRegistry, &[])?;
                if !self.flag(Op::ScheduleRequestedExists, &[])? {
                    return self.schedule_fail(13.);
                }
                self.action(Op::ScheduleReadPrevious, &[])?;
                self.action(Op::ScheduleRemoveSettingsName, &[])?;
                self.action(Op::ScheduleRestoreEnabled, &[])?;
                self.action(Op::ScheduleBuildDefinition, &[])?;
                self.action(Op::ScheduleRegisterDefinition, &[])?;
                self.action(Op::ScheduleStoreSettings, &[])?;
                let saved = self.action(Op::ScheduleSaveRegistry, &[]);
                if saved.is_err() {
                    let _ = self.action(Op::ScheduleRollbackDefinition, &[]);
                    saved?;
                }
            }
            Method::ScheduleRemove => {
                self.action(Op::ScheduleValidateRequestedName, &[])?;
                self.action(Op::ScheduleLoadRegistry, &[])?;
                if !self.flag(Op::ScheduleRequestedExists, &[])? {
                    return self.schedule_fail(13.);
                }
                self.action(Op::ScheduleInvokeDelete, &[])?;
                self.action(Op::ScheduleRemoveEntry, &[])?;
                self.action(Op::ScheduleSaveRegistry, &[])?;
            }
            Method::ScheduleSetEnabled => {
                self.action(Op::ScheduleValidateRequestedName, &[])?;
                self.action(Op::ScheduleLoadRegistry, &[])?;
                if !self.flag(Op::ScheduleRequestedExists, &[])? {
                    return self.schedule_fail(13.);
                }
                self.action(Op::ScheduleInvokeEnabled, &[a[0]])?;
                self.action(Op::ScheduleReadPrevious, &[])?;
                self.action(Op::ScheduleUpdateEnabled, &[a[0]])?;
                self.action(Op::ScheduleStorePrevious, &[])?;
                self.action(Op::ScheduleSaveRegistry, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(result)
    }
}
