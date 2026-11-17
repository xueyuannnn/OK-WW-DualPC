//! Team catalogue, source fingerprint reuse and runtime selection.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn team_catalog_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::TeamCatalogFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(super) fn run_team_catalog(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::TeamStoreCatalog => {
                self.action(Op::TeamCatalogScanDirectories, &[])?;
                let n = self.scalar(Op::TeamCatalogDirectoryCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::TeamCatalogSelectDirectory, &[i as f64])?;
                    if !self.flag(Op::TeamCatalogConfigExists, &[])? {
                        continue;
                    }
                    self.action(Op::TeamCatalogReadConfig, &[])?;
                    self.action(Op::TeamCatalogReadMembers, &[])?;
                    self.action(Op::TeamCatalogComputeKey, &[])?;
                    self.action(Op::TeamCatalogClearLabels, &[])?;
                    let members = self.scalar(Op::TeamCatalogMemberCount, &[])? as usize;
                    for j in 0..members {
                        self.action(Op::TeamCatalogAppendLabel, &[j as f64])?;
                    }
                    self.action(Op::TeamCatalogCreateItem, &[])?;
                    if self.flag(Op::TeamCatalogIsPython, &[])? {
                        self.action(Op::TeamCatalogMarkPython, &[])?;
                        for j in 0..members {
                            self.action(Op::TeamCatalogAppendPythonSource, &[j as f64])?;
                        }
                        self.action(Op::TeamCatalogAssignPythonSources, &[])?;
                        self.action(Op::TeamCatalogFindPython, &[])?;
                        if self.flag(Op::TeamCatalogPythonMissing, &[])? {
                            self.action(Op::TeamCatalogSetAvailable, &[0.])?;
                            self.action(Op::TeamCatalogPythonMissingError, &[])?;
                        } else {
                            self.action(Op::TeamCatalogSetAvailable, &[1.])?;
                        }
                    } else if self.flag(Op::TeamCatalogHasModule, &[])? {
                        self.action(Op::TeamCatalogAssignModuleSources, &[])?;
                        let verified = (|| {
                            self.action(Op::TeamCatalogVerifyModule, &[])?;
                            self.action(Op::TeamCatalogSetAvailable, &[1.])
                        })();
                        if let Err(error) = verified {
                            if self.scalar(Op::ExceptionKind, &[])? == 7. {
                                return Err(error);
                            }
                            self.action(Op::ClearException, &[])?;
                            self.action(Op::TeamCatalogSetAvailable, &[0.])?;
                            self.action(Op::TeamCatalogSetVerificationError, &[])?;
                        }
                    } else {
                        self.action(Op::TeamCatalogSetAvailable, &[0.])?;
                        self.action(Op::TeamCatalogModuleMissingError, &[])?;
                    }
                    self.action(Op::TeamCatalogAppendItem, &[])?;
                }
            }
            Method::TeamStoreValidateEnabled => {
                self.action(Op::TeamCatalogLoadItems, &[])?;
                self.action(Op::TeamCatalogCreateHash, &[])?;
                let n = self.scalar(Op::TeamCatalogItemCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::TeamCatalogSelectItem, &[i as f64])?;
                    if self.flag(Op::TeamCatalogItemImportMode, &[])?
                        && !self.flag(Op::TeamCatalogItemAvailable, &[])?
                    {
                        return self.team_catalog_fail(0.);
                    }
                    if !self.flag(Op::TeamCatalogItemPython, &[])?
                        || !self.flag(Op::TeamCatalogItemImportMode, &[])?
                    {
                        continue;
                    }
                    self.action(Op::TeamCatalogAppendValidationTeam, &[])?;
                    self.action(Op::TeamCatalogComputeValidationDirectory, &[])?;
                    self.action(Op::TeamCatalogHashMemberKey, &[])?;
                    self.action(Op::TeamCatalogHashConfig, &[])?;
                    self.action(Op::TeamCatalogCreatePythonIterator, &[])?;
                    while self.flag(Op::TeamCatalogPythonIteratorHasNext, &[])? {
                        self.action(Op::TeamCatalogAppendPythonPath, &[])?;
                    }
                    self.action(Op::TeamCatalogSortPythonPaths, &[])?;
                    let files = self.scalar(Op::TeamCatalogPythonPathCount, &[])? as usize;
                    for j in 0..files {
                        self.action(Op::TeamCatalogSelectPythonPath, &[j as f64])?;
                        self.action(Op::TeamCatalogHashRelativePath, &[])?;
                        self.action(Op::TeamCatalogHashPythonBytes, &[])?;
                    }
                    self.action(Op::TeamCatalogDropPythonIterator, &[])?;
                }
                self.action(Op::TeamCatalogFinishFingerprint, &[])?;
                self.action(Op::TeamCatalogLock, &[])?;
                let unchanged = self.flag(Op::TeamCatalogFingerprintMatches, &[])?;
                self.action(Op::TeamCatalogUnlock, &[])?;
                if unchanged {
                    return Ok([0.; 3]);
                }
                if self.scalar(Op::TeamCatalogValidationTeamCount, &[])? > 0. {
                    self.action(Op::TeamCatalogValidatePython, &[])?;
                }
                self.action(Op::TeamCatalogLock, &[])?;
                self.action(Op::TeamCatalogCommitFingerprint, &[])?;
                self.action(Op::TeamCatalogUnlock, &[])?;
            }
            Method::TeamStoreSelection => {
                self.action(Op::TeamCatalogLoadParty, &[])?;
                let n = self.scalar(Op::TeamCatalogPartyCount, &[])? as usize;
                for i in 0..n {
                    if self.flag(Op::TeamCatalogPartyMemberPresent, &[i as f64])? {
                        self.action(Op::TeamCatalogAppendPartyMember, &[i as f64])?;
                    }
                }
                if self.scalar(Op::TeamCatalogMemberCount, &[])? != 3.
                    || self.flag(Op::TeamCatalogContainsBaseChar, &[])?
                {
                    return Ok([0.; 3]);
                }
                self.action(Op::TeamCatalogLock, &[])?;
                self.action(Op::TeamCatalogLoadSelectedConfig, &[])?;
                if !self.flag(Op::TeamCatalogConfigImportMode, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::TeamCatalogComputeKey, &[])?;
                if self.flag(Op::TeamCatalogIsPython, &[])? {
                    self.action(Op::TeamCatalogReturnPythonKey, &[])?;
                    return Ok([0.; 3]);
                }
                if !self.flag(Op::TeamCatalogHasModule, &[])? {
                    return self.team_catalog_fail(1.);
                }
                self.action(Op::TeamCatalogVerifyModule, &[])?;
                self.action(Op::TeamCatalogReturnNativeKey, &[])?;
            }
            Method::TeamStoreCreate => {
                if self.flag(Op::TeamCatalogSelectionEmpty, &[])? {
                    return Ok([0.; 3]);
                }
                if self.flag(Op::TeamCatalogSelectionPython, &[])? {
                    self.action(Op::TeamCatalogCreatePythonMacro, &[])?;
                    self.action(Op::TeamCatalogConfigureMacro, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::TeamCatalogLock, &[])?;
                if !self.flag(Op::TeamCatalogHasModule, &[])? {
                    return self.team_catalog_fail(2.);
                }
                self.action(Op::TeamCatalogCopyModule, &[])?;
                self.action(Op::TeamCatalogUnlock, &[])?;
                self.action(Op::TeamCatalogCreateNativeMacro, &[])?;
                if !self.flag(Op::TeamCatalogMacroPresent, &[])? {
                    return self.team_catalog_fail(3.);
                }
                self.action(Op::TeamCatalogConfigureMacro, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
