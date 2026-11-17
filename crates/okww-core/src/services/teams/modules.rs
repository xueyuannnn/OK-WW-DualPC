//! Native module verification and lazy Python file-session selection.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn team_module_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::TeamModuleFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_team_modules(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::TeamStoreLoadModule => {
                self.action(Op::TeamModuleCreateLibrary, &[])?;
                self.action(Op::TeamModuleSetLoadHints, &[])?;
                if !self.flag(Op::TeamModuleLoadLibrary, &[])? {
                    return self.team_module_fail(0.);
                }
                self.action(Op::TeamModuleResolveRuntimeAbi, &[])?;
                if !self.flag(Op::TeamModuleRuntimeAbiPresent, &[])?
                    || self.scalar(Op::TeamModuleReadRuntimeAbi, &[])? != 2.
                {
                    return self.team_module_fail(1.);
                }
                self.action(Op::TeamModuleResolveManifest, &[])?;
                self.action(Op::TeamModuleResolveFactory, &[])?;
                if !self.flag(Op::TeamModuleManifestReaderPresent, &[])?
                    || !self.flag(Op::TeamModuleFactoryPresent, &[])?
                {
                    return self.team_module_fail(2.);
                }
                self.action(Op::TeamModuleReadManifestText, &[])?;
                if !self.flag(Op::TeamModuleManifestTextPresent, &[])? {
                    return self.team_module_fail(3.);
                }
                self.action(Op::TeamModuleParseManifest, &[])?;
                if !self.flag(Op::TeamModuleManifestObject, &[])? {
                    return self.team_module_fail(4.);
                }
                self.action(Op::TeamModuleAssignManifest, &[])?;
                if !self.flag(Op::TeamModuleFormatMatches, &[])?
                    || self.scalar(Op::TeamModuleManifestAbi, &[])? != 1.
                    || self.scalar(Op::TeamModuleOriginalSourceCount, &[])? != 3.
                {
                    return self.team_module_fail(5.);
                }
                self.action(Op::TeamModuleValidateMembers, &[])?;
                if self.flag(Op::TeamModuleIdentifierEmpty, &[])? {
                    return self.team_module_fail(6.);
                }
                self.action(Op::TeamModuleReturnLoaded, &[])?;
            }
            Method::TeamStoreInitialize => {
                self.action(Op::TeamModuleInitialDirectories, &[])?;
                if self.flag(Op::TeamModuleSourceProjectExists, &[])? {
                    self.action(Op::TeamModuleAppendBuildDirectory, &[])?;
                }
                self.action(Op::TeamModuleUniqueDirectories, &[])?;
                let n = self.scalar(Op::TeamModuleDirectoryCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::TeamModuleSelectDirectory, &[i as f64])?;
                    for j in 0..5 {
                        self.action(Op::TeamModuleBundledPath, &[f64::from(j)])?;
                        if !self.flag(Op::TeamModuleSelectedPathExists, &[])? {
                            continue;
                        }
                        self.action(Op::TeamModuleLoadSelected, &[])?;
                        self.action(Op::TeamModuleActivateBundled, &[])?;
                    }
                }
                self.action(Op::TeamModuleRegistryPath, &[])?;
                if self.flag(Op::TeamModuleRegistryExists, &[])? {
                    self.action(Op::TeamModuleReadRegistry, &[])?;
                    let n = self.scalar(Op::TeamModuleRegistryCount, &[])? as usize;
                    for i in 0..n {
                        self.action(Op::TeamModuleSelectRegistryEntry, &[i as f64])?;
                        if !self.flag(Op::TeamModuleRegistryFilenameValid, &[])? {
                            return self.team_module_fail(7.);
                        }
                        self.action(Op::TeamModuleRegisteredPath, &[])?;
                        if !self.flag(Op::TeamModuleRegisteredDigestMatches, &[])? {
                            return self.team_module_fail(8.);
                        }
                        self.action(Op::TeamModuleLoadSelected, &[])?;
                        if !self.flag(Op::TeamModuleRegistryTeamMatches, &[])? {
                            return self.team_module_fail(9.);
                        }
                        self.action(Op::TeamModuleActivateRegistered, &[])?;
                    }
                }
            }
            Method::TeamStorePythonTeam => {
                let n = self.scalar(Op::TeamModuleMemberCount, &[])? as usize;
                for i in 0..n {
                    if !self.flag(Op::TeamModulePythonMemberExists, &[i as f64])? {
                        return Ok([0.; 3]);
                    }
                }
                return Ok([1., 0., 0.]);
            }
            Method::TeamStorePythonCommand => {
                if !self.flag(Op::TeamModuleFileSessionPresent, &[])? {
                    self.action(Op::TeamModuleCreateFileSession, &[])?;
                }
                self.action(Op::TeamModuleRunPythonCommand, &[])?;
            }
            Method::TeamStoreVerifySources => {
                self.action(Op::TeamModuleLoadOriginalSources, &[])?;
                let n = self.scalar(Op::TeamModuleSourceCount, &[])? as usize;
                let mut present = 0;
                for i in 0..n {
                    self.action(Op::TeamModuleSelectOriginalSource, &[i as f64])?;
                    if !self.flag(Op::TeamModuleOriginalSourceFilename, &[])? {
                        return self.team_module_fail(10.);
                    }
                    self.action(Op::TeamModuleOriginalSourcePath, &[])?;
                    if self.flag(Op::TeamModuleSelectedPathExists, &[])? {
                        present += 1;
                        if !self.flag(Op::TeamModuleOriginalDigestMatches, &[])? {
                            return self.team_module_fail(11.);
                        }
                    }
                }
                if present == 3 {
                    return Ok([0.; 3]);
                }
                if present != 0 {
                    return self.team_module_fail(12.);
                }
                self.action(Op::TeamModuleReceiptPath, &[])?;
                if !self.flag(Op::TeamModuleReceiptExists, &[])? {
                    return self.team_module_fail(13.);
                }
                self.action(Op::TeamModuleReadReceipt, &[])?;
                if !self.flag(Op::TeamModuleReceiptIdMatches, &[])?
                    || !self.flag(Op::TeamModuleReceiptSourcesMatch, &[])?
                {
                    return self.team_module_fail(14.);
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
