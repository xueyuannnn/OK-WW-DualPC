//! Configuration merge, validation and UI field visibility. Qt retains the
//! JSON values and their original comparison/iteration semantics.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn profile_fail<T>(&self, reason: f64) -> Result<T> {
        self.action(Op::ProfileFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    fn profile_sections(&self, group: impl Fn() -> Result) -> Result {
        for section in 0..2 {
            self.action(Op::ProfileSelectSection, &[f64::from(section)])?;
            let n = self.scalar(Op::ProfileGroupCount, &[])? as usize;
            for i in 0..n {
                self.action(Op::ProfileLoadGroup, &[i as f64])?;
                group()?;
            }
        }
        Ok(())
    }
    pub(in crate::services) fn run_profile(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::ProfileInitialize => {
                if self.flag(Op::ProfileOverridesExist, &[])? {
                    self.action(Op::ProfileReadOverrides, &[])?;
                    for section in 0..2 {
                        self.action(Op::ProfileSelectSection, &[f64::from(section)])?;
                        self.action(Op::ProfileClearGroups, &[])?;
                        let n = self.scalar(Op::ProfileGroupCount, &[])? as usize;
                        for i in 0..n {
                            self.action(Op::ProfileLoadGroup, &[i as f64])?;
                            self.action(Op::ProfileLoadPatch, &[])?;
                            if self.flag(Op::ProfilePatchNameString, &[])? && section != 0 {
                                self.action(Op::ProfilePatchName, &[])?;
                            }
                            if self.flag(Op::ProfilePatchDescriptionString, &[])? {
                                self.action(Op::ProfilePatchDescription, &[])?;
                            }
                            self.action(Op::ProfileReadPatchFields, &[])?;
                            self.action(Op::ProfileClearFields, &[])?;
                            let fields = self.scalar(Op::ProfileFieldCount, &[])? as usize;
                            for j in 0..fields {
                                self.action(Op::ProfileLoadField, &[j as f64])?;
                                if self.flag(Op::ProfileFieldRemoved, &[])? {
                                    continue;
                                }
                                let kind = self.scalar(Op::ProfileExitKeyKind, &[])?;
                                if kind == 1. {
                                    self.action(Op::ProfileExitTaskLabels, &[])?;
                                }
                                if kind == 2. {
                                    self.action(Op::ProfileExitErrorLabel, &[])?;
                                }
                                self.action(Op::ProfileTakeFieldChange, &[])?;
                                let changes = self.scalar(Op::ProfileChangeCount, &[])? as usize;
                                for k in 0..changes {
                                    self.action(Op::ProfileApplyFieldChange, &[k as f64])?;
                                }
                                self.action(Op::ProfileAppendField, &[])?;
                            }
                            self.action(Op::ProfileReadExtraKeys, &[])?;
                            let extras = self.scalar(Op::ProfileExtraCount, &[])? as usize;
                            for j in 0..extras {
                                self.action(Op::ProfileLoadExtraField, &[j as f64])?;
                                self.action(Op::ProfileAppendField, &[])?;
                            }
                            self.action(Op::ProfileStoreFields, &[])?;
                            self.action(Op::ProfileAppendGroup, &[])?;
                        }
                        self.action(Op::ProfileStoreSection, &[])?;
                    }
                }
                self.action(Op::ProfileMakeDirectory, &[])?;
                self.profile_sections(|| {
                    self.action(Op::ProfileLoadDefaultConfig, &[])?;
                    if self.flag(Op::ProfileSavedConfigExists, &[])? {
                        self.action(Op::ProfileReadSavedConfig, &[])?;
                        let n = self.scalar(Op::ProfileSavedKeyCount, &[])? as usize;
                        for i in 0..n {
                            self.action(Op::ProfileOverlaySavedKey, &[i as f64])?;
                        }
                    }
                    self.action(Op::ProfileStoreInitialConfig, &[])
                })?;
            }
            Method::ProfileFilename => {
                if self.flag(Op::ProfileIdEmpty, &[])?
                    || self.flag(Op::ProfileIdForwardSlash, &[])?
                    || self.flag(Op::ProfileIdBackwardSlash, &[])?
                    || self.flag(Op::ProfileIdParent, &[])?
                {
                    return self.profile_fail(0.);
                }
                self.action(Op::ProfileBuildFilename, &[])?;
            }
            Method::ProfileDefaults => {
                for section in 0..2 {
                    self.action(Op::ProfileSelectSection, &[f64::from(section)])?;
                    let n = self.scalar(Op::ProfileGroupCount, &[])? as usize;
                    for i in 0..n {
                        self.action(Op::ProfileLoadGroup, &[i as f64])?;
                        if !self.flag(Op::ProfileGroupMatchesRequest, &[])? {
                            continue;
                        }
                        let fields = self.scalar(Op::ProfileFieldCount, &[])? as usize;
                        for j in 0..fields {
                            self.action(Op::ProfileLoadField, &[j as f64])?;
                            self.action(Op::ProfileCopyDefaultField, &[])?;
                        }
                        if self.flag(Op::ProfileGroupHasTrigger, &[])? {
                            self.action(Op::ProfileCopyDefaultEnabled, &[])?;
                        }
                        return Ok([0.; 3]);
                    }
                }
                return self.profile_fail(1.);
            }
            Method::ProfileGroup => {
                self.action(Op::LockProfile, &[])?;
                if !self.flag(Op::ProfileValueGroupExists, &[])? {
                    return self.profile_fail(1.);
                }
                self.action(Op::ProfileReturnGroup, &[])?;
            }
            Method::ProfileValue => {
                self.action(Op::ProfileReadRequestedGroup, &[])?;
                if self.flag(Op::ProfileValueExists, &[])? {
                    self.action(Op::ProfileReturnValue, &[])?;
                } else {
                    self.action(Op::ProfileReturnFallback, &[])?;
                }
            }
            Method::ProfileSet => {
                self.action(Op::LockProfile, &[])?;
                self.action(Op::ProfileReadRequestedGroup, &[])?;
                // Search every matching group/field: the original last match
                // wins when a catalogue contains duplicate IDs or keys.
                self.profile_sections(|| {
                    if !self.flag(Op::ProfileGroupMatchesRequest, &[])? {
                        return Ok(());
                    }
                    let fields = self.scalar(Op::ProfileFieldCount, &[])? as usize;
                    for i in 0..fields {
                        self.action(Op::ProfileLoadField, &[i as f64])?;
                        if self.flag(Op::ProfileFieldMatchesRequest, &[])? {
                            self.action(Op::ProfileStoreSpec, &[])?;
                        }
                    }
                    Ok(())
                })?;
                if self.flag(Op::ProfileIsEnabledKey, &[])? {
                    if !self.flag(Op::ProfileInputBool, &[])? {
                        return self.profile_fail(2.);
                    }
                } else {
                    if self.flag(Op::ProfileSpecEmpty, &[])? {
                        return self.profile_fail(3.);
                    }
                    let kind = self.scalar(Op::ProfileSpecKind, &[])? as i32;
                    if kind == 1 && !self.flag(Op::ProfileInputBool, &[])? {
                        return self.profile_fail(4.);
                    }
                    if matches!(kind, 2 | 3) {
                        if !self.flag(Op::ProfileInputNumber, &[])? {
                            return self.profile_fail(5.);
                        }
                        let n = self.scalar(Op::ProfileInputDouble, &[])?;
                        if !n.is_finite() {
                            return self.profile_fail(5.);
                        }
                        if kind == 2 && n.floor() != n {
                            return self.profile_fail(6.);
                        }
                        if self.flag(Op::ProfileMinimumNumber, &[])?
                            && n < self.scalar(Op::ProfileMinimum, &[])?
                        {
                            return self.profile_fail(7.);
                        }
                        if self.flag(Op::ProfileMaximumNumber, &[])?
                            && n > self.scalar(Op::ProfileMaximum, &[])?
                        {
                            return self.profile_fail(8.);
                        }
                    }
                    if kind == 4 && !self.flag(Op::ProfileOptionContains, &[])? {
                        return self.profile_fail(9.);
                    }
                    if kind == 5 && !self.flag(Op::ProfileInputString, &[])? {
                        return self.profile_fail(10.);
                    }
                    if kind == 6 && !self.flag(Op::ProfileInputArray, &[])? {
                        return self.profile_fail(11.);
                    }
                }
                self.action(Op::ProfileSetConfigValue, &[])?;
                self.action(Op::ProfileWriteConfig, &[])?;
                self.action(Op::ProfileStoreConfig, &[])?;
            }
            Method::ProfileReset => {
                self.action(Op::LockProfile, &[])?;
                self.action(Op::ProfileReadRequestedGroup, &[])?;
                self.action(Op::ProfileReadInitialDefaults, &[])?;
                let count = self.scalar(Op::ProfileInitialKeyCount, &[])? as usize;
                for i in 0..count {
                    if !self.flag(Op::ProfileInitialKeyEnabled, &[i as f64])? {
                        self.action(Op::ProfileResetKey, &[i as f64])?;
                    }
                }
                self.action(Op::ProfileWriteConfig, &[])?;
                self.action(Op::ProfileStoreConfig, &[])?;
            }
            Method::ProfileCatalog => {
                self.action(Op::LockProfile, &[])?;
                self.action(Op::ProfileCopySchema, &[])?;
                for section in 0..2 {
                    self.action(Op::ProfileSelectSection, &[f64::from(section)])?;
                    self.action(Op::ProfileClearGroups, &[])?;
                    let count = self.scalar(Op::ProfileGroupCount, &[])? as usize;
                    for i in 0..count {
                        self.action(Op::ProfileLoadGroup, &[i as f64])?;
                        self.action(Op::ProfileReadCatalogConfig, &[])?;
                        self.action(Op::ProfileClearFields, &[])?;
                        let fields = self.scalar(Op::ProfileFieldCount, &[])? as usize;
                        for j in 0..fields {
                            self.action(Op::ProfileLoadField, &[j as f64])?;
                            self.action(Op::ProfileReadVisibility, &[])?;
                            let count = self.scalar(Op::ProfileVisibilityCount, &[])? as usize;
                            let mut visible = true;
                            for k in 0..count {
                                if !self.flag(Op::ProfileVisibilityMatches, &[k as f64])? {
                                    visible = false;
                                }
                            }
                            if !visible {
                                continue;
                            }
                            self.action(Op::ProfileFillCurrentValue, &[])?;
                            self.action(Op::ProfileAppendField, &[])?;
                        }
                        self.action(Op::ProfileStoreFields, &[])?;
                        if self.flag(Op::ProfileGroupHasTrigger, &[])? {
                            self.action(Op::ProfileFillCurrentEnabled, &[])?;
                        }
                        self.action(Op::ProfileAppendGroup, &[])?;
                    }
                    self.action(Op::ProfileStoreCatalogSection, &[])?;
                }
                self.action(Op::ProfileFillPreferences, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
