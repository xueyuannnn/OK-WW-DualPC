//! Read-only builtin excerpts and editable imported-source selection.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn editor_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::TeamEditorFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(super) fn run_team_editor(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::TeamEditorBuiltinText => {
                self.action(Op::TeamEditorCreateResourceFile, &[])?;
                if !self.flag(Op::TeamEditorOpenResource, &[])? {
                    return self.editor_fail(0.);
                }
                self.action(Op::TeamEditorReadResource, &[])?;
            }
            Method::TeamEditorDefinition => {
                if args[0] < 0. {
                    return self.editor_fail(1.);
                }
                self.action(Op::TeamEditorCreateTokenIterator, &[])?;
                let mut depth = 0;
                let mut opened = false;
                while self.flag(Op::TeamEditorTokenHasNext, &[])? {
                    self.action(Op::TeamEditorSelectToken, &[])?;
                    if self.flag(Op::TeamEditorTokenOpening, &[])? {
                        opened = true;
                        depth += 1;
                    } else if self.flag(Op::TeamEditorTokenClosing, &[])? && opened {
                        depth -= 1;
                        if depth == 0 {
                            self.action(Op::TeamEditorDefinitionEnd, &[])?;
                            if self.flag(Op::TeamEditorEndBeforeText, &[])?
                                && self.flag(Op::TeamEditorEndSemicolon, &[])?
                            {
                                self.action(Op::TeamEditorIncludeSemicolon, &[])?;
                            }
                            self.action(Op::TeamEditorReturnDefinition, &[])?;
                            return Ok([0.; 3]);
                        }
                    }
                }
                return self.editor_fail(2.);
            }
            Method::TeamEditorBuiltin => {
                self.action(Op::TeamEditorCreateDeclaration, &[])?;
                self.action(Op::TeamEditorListBuiltinFiles, &[])?;
                let n = self.scalar(Op::TeamEditorBuiltinFileCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::TeamEditorSelectBuiltinFile, &[i as f64])?;
                    if self.flag(Op::TeamEditorDeclarationMatches, &[])? {
                        self.action(Op::TeamEditorChoosePrimary, &[])?;
                        break;
                    }
                }
                let common = self.flag(Op::TeamEditorCommonCharacter, &[])?;
                if common {
                    self.action(Op::TeamEditorChooseCommonPrimary, &[])?;
                }
                if self.flag(Op::TeamEditorPrimaryEmpty, &[])? {
                    return self.editor_fail(3.);
                }
                self.action(Op::TeamEditorInitialChoices, &[])?;
                for i in 0..2 {
                    if !self.flag(Op::TeamEditorCommonChoicePresent, &[f64::from(i)])? {
                        self.action(Op::TeamEditorAppendCommonChoice, &[f64::from(i)])?;
                    }
                }
                self.action(Op::TeamEditorLoadRustChoices, &[])?;
                let rust_count = self.scalar(Op::TeamEditorRustChoiceCount, &[])? as usize;
                for i in 0..rust_count {
                    self.action(Op::TeamEditorAppendRustChoice, &[i as f64])?;
                }
                if self.flag(Op::TeamEditorRequestedEmpty, &[])? {
                    self.action(Op::TeamEditorUsePrimary, &[])?;
                } else {
                    self.action(Op::TeamEditorUseRequested, &[])?;
                }
                if !self.flag(Op::TeamEditorSourceInChoices, &[])? {
                    return self.editor_fail(4.);
                }
                if self.flag(Op::TeamEditorSourceFull, &[])? {
                    self.action(Op::TeamEditorStripFullPrefix, &[])?;
                } else {
                    self.action(Op::TeamEditorUseSourceFile, &[])?;
                }
                self.action(Op::TeamEditorLoadSelectedText, &[])?;
                if self.flag(Op::TeamEditorSourcePrimary, &[])? {
                    self.action(Op::TeamEditorDeclarationOffset, &[])?;
                }
                if common && self.flag(Op::TeamEditorSourcePrimary, &[])? {
                    self.action(Op::TeamEditorCommonOffset, &[])?;
                }
                if self.flag(Op::TeamEditorSourcePrimary, &[])? {
                    self.action(Op::TeamEditorWrapExcerpt, &[])?;
                    self.action(Op::TeamEditorResetOffset, &[])?;
                }
                let n = self.scalar(Op::TeamEditorChoiceCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::TeamEditorSelectChoice, &[i as f64])?;
                    if self.flag(Op::TeamEditorChoiceFull, &[])? {
                        self.action(Op::TeamEditorLabelFullChoice, &[])?;
                    } else if self.flag(Op::TeamEditorChoicePrimary, &[])? {
                        self.action(Op::TeamEditorLabelPrimaryChoice, &[])?;
                    } else {
                        self.action(Op::TeamEditorLabelPlainChoice, &[])?;
                    }
                    self.action(Op::TeamEditorAppendChoice, &[])?;
                }
                let focus = if self.flag(Op::TeamEditorOffsetNegative, &[])? {
                    1.
                } else {
                    self.scalar(Op::TeamEditorFocusLine, &[])? + 1.
                };
                self.action(Op::TeamEditorReturnBuiltin, &[f64::from(common), focus])?;
            }
            Method::TeamEditorPrimaryImported => {
                if self.flag(Op::TeamEditorHsinCorePresent, &[])? {
                    self.action(Op::TeamEditorReturnHsinCore, &[])?;
                    return Ok([0.; 3]);
                }
                self.action(Op::TeamEditorLoadSourceKeys, &[])?;
                let n = self.scalar(Op::TeamEditorSourceKeyCount, &[])? as usize;
                for i in 0..n {
                    if self.flag(Op::TeamEditorSourceKeyCpp, &[i as f64])? {
                        self.action(Op::TeamEditorReturnSourceKey, &[i as f64])?;
                        return Ok([0.; 3]);
                    }
                }
                return self.editor_fail(5.);
            }
            Method::TeamStoreEditor => {
                self.action(Op::TeamEditorReadMembers, &[])?;
                if !self.flag(Op::TeamEditorMemberInTeam, &[])? {
                    return self.editor_fail(6.);
                }
                self.action(Op::TeamEditorLoadTeamConfig, &[])?;
                if self.flag(Op::TeamEditorTeamConfigEmpty, &[])? {
                    return self.editor_fail(7.);
                }
                if !self.flag(Op::TeamEditorViewActive, &[])?
                    && !self.flag(Op::TeamEditorViewImport, &[])?
                {
                    return self.editor_fail(8.);
                }
                self.action(Op::TeamEditorReadMode, &[])?;
                if self.flag(Op::TeamEditorPythonTeam, &[])?
                    && (self.flag(Op::TeamEditorViewImport, &[])?
                        || self.flag(Op::TeamEditorModeImport, &[])?)
                {
                    self.action(Op::TeamEditorPythonSourceName, &[])?;
                    if !self.flag(Op::TeamEditorRequestedEmpty, &[])?
                        && !self.flag(Op::TeamEditorRequestedMatchesPython, &[])?
                    {
                        return self.editor_fail(9.);
                    }
                    self.action(Op::TeamEditorCreatePythonFile, &[])?;
                    if !self.flag(Op::TeamEditorOpenPythonFile, &[])?
                        || self.scalar(Op::TeamEditorPythonFileSize, &[])? > 8. * 1024. * 1024.
                    {
                        return self.editor_fail(10.);
                    }
                    self.action(Op::TeamEditorReadPythonCode, &[])?;
                    self.action(Op::TeamEditorDropPythonFile, &[])?;
                } else if self.flag(Op::TeamEditorViewImport, &[])?
                    || self.flag(Op::TeamEditorModeImport, &[])?
                {
                    self.action(Op::TeamEditorTeamKey, &[])?;
                    if !self.flag(Op::TeamEditorModulePresent, &[])? {
                        return self.editor_fail(11.);
                    }
                    self.action(Op::TeamEditorLoadModuleSources, &[])?;
                    if self.flag(Op::TeamEditorRequestedEmpty, &[])? {
                        self.action(Op::TeamEditorUsePrimaryImported, &[])?;
                    } else {
                        self.action(Op::TeamEditorUseRequested, &[])?;
                    }
                    self.action(Op::TeamEditorReadModuleCode, &[])?;
                    self.action(Op::TeamEditorComputePrimaryImported, &[])?;
                    self.action(Op::TeamEditorAppendPrimaryImportedChoice, &[])?;
                    self.action(Op::TeamEditorLoadSourceKeys, &[])?;
                    let n = self.scalar(Op::TeamEditorSourceKeyCount, &[])? as usize;
                    for i in 0..n {
                        if !self.flag(Op::TeamEditorSourceKeyPrimary, &[i as f64])? {
                            self.action(Op::TeamEditorAppendImportedChoice, &[i as f64])?;
                        }
                    }
                    self.action(Op::TeamEditorAssignImportedMetadata, &[])?;
                } else {
                    self.action(Op::TeamEditorReadBuiltinCode, &[])?;
                }
                self.action(Op::TeamEditorAssignCharacter, &[])?;
                self.action(Op::TeamEditorAssignActiveMode, &[])?;
                let view_only = self.flag(Op::TeamEditorViewImport, &[])?
                    && !self.flag(Op::TeamEditorModeImport, &[])?;
                self.action(Op::TeamEditorAssignViewOnly, &[f64::from(view_only)])?;
                if view_only {
                    self.action(Op::TeamEditorSetReadOnly, &[])?;
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
