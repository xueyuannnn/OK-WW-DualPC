//! Team mode changes, drafts and package import/export ordering.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn team_mutation_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::TeamMutationFailure, &[code])?;
        Err(HOST_ERROR)
    }
    fn team_mode_valid(&self) -> Result {
        if !self.flag(Op::TeamMutationModeImport, &[])?
            && !self.flag(Op::TeamMutationModeBuiltin, &[])?
        {
            return self.team_mutation_fail(0.);
        }
        Ok(())
    }
    pub(in crate::services) fn run_team_mutations(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::TeamStoreSetMode => {
                self.team_mode_valid()?;
                self.action(Op::TeamMutationReadMembers, &[])?;
                self.action(Op::TeamMutationLoadConfig, &[])?;
                if self.flag(Op::TeamMutationConfigEmpty, &[])? {
                    return self.team_mutation_fail(1.);
                }
                if self.flag(Op::TeamMutationPythonTeam, &[])? {
                    self.action(Op::TeamMutationPythonMode, &[])?;
                    return Ok([0.; 3]);
                }
                if self.flag(Op::TeamMutationModeImport, &[])? {
                    if !self.flag(Op::TeamMutationModuleForMembers, &[])? {
                        return self.team_mutation_fail(2.);
                    }
                    self.action(Op::TeamMutationVerifyMembers, &[])?;
                }
                self.action(Op::TeamMutationAssignMode, &[])?;
                self.action(Op::TeamMutationSaveConfig, &[])?;
            }
            Method::TeamStoreCreateTeam => {
                self.action(Op::TeamMutationReadMembers, &[])?;
                self.action(Op::TeamMutationLoadConfig, &[])?;
                if !self.flag(Op::TeamMutationConfigEmpty, &[])? {
                    return Ok([0.; 3]);
                }
                self.action(Op::TeamMutationDefaultConfig, &[])?;
                self.action(Op::TeamMutationSaveConfig, &[])?;
            }
            Method::TeamStoreSetAllModes => {
                self.team_mode_valid()?;
                self.action(Op::TeamMutationReadCatalog, &[])?;
                let n = self.scalar(Op::TeamMutationCatalogCount, &[])? as usize;
                let mut python = false;
                for i in 0..n {
                    python |= self.flag(Op::TeamMutationCatalogItemPython, &[i as f64])?;
                }
                if python {
                    self.action(Op::TeamMutationPythonAllModes, &[])?;
                }
                self.action(Op::TeamMutationReadCatalog, &[])?;
                let n = self.scalar(Op::TeamMutationCatalogCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::TeamMutationSelectCatalogItem, &[i as f64])?;
                    self.action(Op::TeamMutationReadItemMembers, &[])?;
                    if self.flag(Op::TeamMutationPythonTeam, &[])?
                        || !self.flag(Op::TeamMutationModuleForMembers, &[])?
                    {
                        continue;
                    }
                    if self.flag(Op::TeamMutationModeImport, &[])? {
                        self.action(Op::TeamMutationVerifyMembers, &[])?;
                    }
                    self.action(Op::TeamMutationAppendBeforeConfig, &[])?;
                }
                let n = self.scalar(Op::TeamMutationBeforeCount, &[])? as usize;
                let mut changed = 0;
                let save = (|| {
                    for i in 0..n {
                        self.action(Op::TeamMutationSelectBeforeConfig, &[i as f64])?;
                        self.action(Op::TeamMutationAssignMode, &[])?;
                        self.action(Op::TeamMutationSaveConfig, &[])?;
                        changed += 1;
                    }
                    Ok(())
                })();
                if let Err(error) = save {
                    self.action(Op::ClearException, &[])?;
                    for i in 0..changed {
                        self.action(Op::TeamMutationRestoreBeforeConfig, &[i as f64])?;
                    }
                    self.action(Op::RethrowHandled, &[])?;
                    return Err(error);
                }
            }
            Method::TeamStoreRemoveTeam => {
                self.action(Op::TeamMutationReadMembers, &[])?;
                self.action(Op::TeamMutationLoadConfig, &[])?;
                if self.flag(Op::TeamMutationConfigEmpty, &[])? {
                    return self.team_mutation_fail(1.);
                }
                self.action(Op::TeamMutationRemovalSource, &[])?;
                self.action(Op::TeamMutationRemovalDestination, &[])?;
                if !self.flag(Op::TeamMutationRemovalSourceWithinRoot, &[])?
                    || !self.flag(Op::TeamMutationRemovalDestinationWithinRoot, &[])?
                {
                    return self.team_mutation_fail(3.);
                }
                if !self.flag(Op::TeamMutationCreateDeletedDirectory, &[])?
                    || !self.flag(Op::TeamMutationMoveToDeleted, &[])?
                {
                    return self.team_mutation_fail(4.);
                }
                self.action(Op::TeamMutationReturnRemovalDestination, &[])?;
            }
            Method::TeamStoreCode => {
                self.action(Op::TeamMutationReadMembers, &[])?;
                self.action(Op::TeamMutationComputeKey, &[])?;
                if !self.flag(Op::TeamMutationHasModule, &[])? {
                    self.action(Op::TeamMutationBuiltinCodeMessage, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::TeamMutationReadModuleSources, &[])?;
                if !self.flag(Op::TeamMutationSourceExists, &[])?
                    || !self.flag(Op::TeamMutationSourceSafe, &[])?
                {
                    return self.team_mutation_fail(5.);
                }
                self.action(Op::TeamMutationDraftPath, &[])?;
                if self.flag(Op::TeamMutationDraftExists, &[])? {
                    self.action(Op::TeamMutationReadDraft, &[])?;
                } else {
                    self.action(Op::TeamMutationReadBundledSource, &[])?;
                }
                self.action(Op::TeamMutationReadDraftFlag, &[])?;
                self.action(Op::TeamMutationReturnCode, &[])?;
            }
            Method::TeamStoreSaveDraft => {
                self.action(Op::TeamMutationReadMembers, &[])?;
                self.action(Op::TeamMutationComputeKey, &[])?;
                if self.flag(Op::TeamMutationPythonTeam, &[])?
                    && self.flag(Op::TeamMutationPythonSourceSuffix, &[])?
                {
                    self.action(Op::TeamMutationReadPythonMember, &[])?;
                    if !self.flag(Op::TeamMutationPythonMemberInTeam, &[])? {
                        return self.team_mutation_fail(6.);
                    }
                    self.action(Op::TeamMutationPythonSave, &[])?;
                    return Ok([0.; 3]);
                }
                if !self.flag(Op::TeamMutationHasModule, &[])?
                    || !self.flag(Op::TeamMutationModuleContainsSource, &[])?
                    || !self.flag(Op::TeamMutationSourceSafe, &[])?
                {
                    return self.team_mutation_fail(5.);
                }
                if self.scalar(Op::TeamMutationUtf8Size, &[])? > 8. * 1024. * 1024. {
                    return self.team_mutation_fail(7.);
                }
                self.action(Op::TeamMutationSaveDraftBytes, &[])?;
            }
            Method::TeamStoreExportTeam => {
                self.action(Op::TeamMutationReadMembers, &[])?;
                self.action(Op::TeamMutationComputeKey, &[])?;
                if self.flag(Op::TeamMutationPythonTeam, &[])? {
                    self.action(Op::TeamMutationPythonExport, &[])?;
                    return Ok([0.; 3]);
                }
                if !self.flag(Op::TeamMutationHasModule, &[])? {
                    return self.team_mutation_fail(8.);
                }
                self.action(Op::TeamMutationCopyModule, &[])?;
                self.action(Op::TeamMutationVerifyModule, &[])?;
                self.action(Op::TeamMutationReadBinary, &[])?;
                self.action(Op::TeamMutationWriteExportPackage, &[])?;
            }
            Method::TeamStoreImportTeam => {
                if self.flag(Op::TeamMutationZipPath, &[])? {
                    self.action(Op::TeamMutationPythonImport, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::TeamMutationReadPackageBytes, &[])?;
                self.action(Op::TeamMutationParsePackage, &[])?;
                if !self.flag(Op::TeamMutationPackageFormat, &[])?
                    || self.scalar(Op::TeamMutationPackageAbi, &[])? != 1.
                {
                    return self.team_mutation_fail(9.);
                }
                self.action(Op::TeamMutationReadPackageManifest, &[])?;
                self.action(Op::TeamMutationReadManifestMembers, &[])?;
                self.action(Op::TeamMutationDecodeBinary, &[])?;
                if self.flag(Op::TeamMutationBinaryEmpty, &[])?
                    || self.scalar(Op::TeamMutationBinarySize, &[])? > 64. * 1024. * 1024.
                    || !self.flag(Op::TeamMutationBinaryDigestMatches, &[])?
                {
                    return self.team_mutation_fail(10.);
                }
                self.action(Op::TeamMutationTargetPath, &[])?;
                if !self.flag(Op::TeamMutationTargetExists, &[])? {
                    self.action(Op::TeamMutationSaveBinary, &[])?;
                } else if !self.flag(Op::TeamMutationTargetBytesMatch, &[])? {
                    return self.team_mutation_fail(11.);
                }
                self.action(Op::TeamMutationLoadTargetModule, &[])?;
                if !self.flag(Op::TeamMutationManifestMatches, &[])? {
                    return self.team_mutation_fail(12.);
                }
                self.action(Op::TeamMutationReceiptPath, &[])?;
                self.action(Op::TeamMutationOriginalFiles, &[])?;
                let n = self.scalar(Op::TeamMutationOriginalFileCount, &[])? as usize;
                let mut any = false;
                for i in 0..n {
                    any |= self.flag(Op::TeamMutationOriginalFileExists, &[i as f64])?;
                }
                if any {
                    self.action(Op::TeamMutationVerifyModule, &[])?;
                }
                self.action(Op::TeamMutationLoadConfig, &[])?;
                if self.flag(Op::TeamMutationConfigEmpty, &[])? {
                    self.action(Op::TeamMutationMetadataConfig, &[])?;
                }
                self.action(Op::TeamMutationWriteReceipt, &[])?;
                self.action(Op::TeamMutationRegistryPath, &[])?;
                if self.flag(Op::TeamMutationRegistryExists, &[])? {
                    self.action(Op::TeamMutationReadRegistry, &[])?;
                }
                self.action(Op::TeamMutationRegisterTarget, &[])?;
                self.action(Op::TeamMutationWriteRegistry, &[])?;
                self.action(Op::TeamMutationActivateModule, &[])?;
                self.action(Op::TeamMutationAssignImportMode, &[])?;
                self.action(Op::TeamMutationSaveConfig, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
