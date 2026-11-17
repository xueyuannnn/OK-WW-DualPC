use super::*;
impl Host {
    fn trigger_settings(&self) -> EngineResult<bool> {
        if !self.flag(Op::RequestConfigure, &[])? {
            return Ok(false);
        }
        let count = self.call(Op::ScriptsCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectScriptEntry, &[index as f64])?;
            // The old predicate reads the fields array before checking the id.
            self.action(Op::LoadEntryFields, &[])?;
            if self.flag(Op::ScriptValueMatches, &[])? && self.flag(Op::ScriptTrigger, &[])? {
                let fields = self.call(Op::ScriptFieldsCount, &[])? as usize;
                for field in 0..fields {
                    self.action(Op::SelectScriptField, &[field as f64])?;
                    if self.flag(Op::ScriptFieldMatches, &[])? {
                        return Ok(true);
                    }
                }
            }
        }
        Ok(false)
    }
    fn script_command_body(&self) -> EngineResult {
        if self.flag(Op::ScriptsBusy, &[])? {
            return self.action(Op::ScheduleScriptRetry, &[]);
        }
        let live =
            self.flag(Op::RequestConfigure, &[])? && self.call(Op::RequestKeyKind, &[])? == 0.;
        let settings = self.trigger_settings()?;
        if (!self.flag(Op::Paused, &[])?
            || !self.flag(Op::WorkerFinished, &[])?
            || self.flag(Op::HasPending, &[])?)
            && !live
            && !settings
        {
            return self.fail(11.);
        }
        if self.flag(Op::RequestConfigure, &[])? {
            return self.script_configure();
        }
        self.action(Op::SetScriptsBusy, &[1.])?;
        self.action(Op::BuildScriptManageCommand, &[])?;
        self.action(Op::SpawnScriptManage, &[])
    }
    pub(super) fn script_command(&self) -> EngineResult {
        if let Err(error) = self.script_command_body() {
            if self.call(Op::ExceptionKind, &[])? == 5. {
                return Err(error);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::ReplyError, &[])?;
            self.action(Op::EmitReply, &[])?;
        }
        Ok(())
    }
    pub(super) fn script_configure(&self) -> EngineResult {
        if self.flag(Op::ScriptStateExists, &[])? {
            self.action(Op::ReadScriptState, &[])?;
        } else {
            self.action(Op::EmptyScriptState, &[])?;
        }
        self.action(Op::InitializeScriptChanges, &[])?;
        let mut found = false;
        let count = self.call(Op::ChangedScriptCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectChangedScript, &[index as f64])?;
            if !self.flag(Op::ScriptIdentMatches, &[])? {
                continue;
            }
            found = true;
            let key = self.call(Op::RequestKeyKind, &[])?;
            if key == 0. || key == 1. {
                if !self.flag(Op::RequestValueBool, &[])? {
                    return self.fail(12.);
                }
                self.action(Op::SetScriptBoolean, &[key])?;
                if key == 0. && self.flag(Op::ScriptFlagExists, &[])? {
                    self.action(Op::StoreScriptFlagValue, &[])?;
                }
            } else if key == 2. {
                if !self.flag(Op::RequestValueNumber, &[])? {
                    return self.fail(13.);
                }
                let value = self.call(Op::RequestValueDouble, &[])?;
                let integer = self.call(Op::RequestValueInt, &[])?;
                if value != integer || !(1. ..=1_000_000.).contains(&integer) {
                    return self.fail(13.);
                }
                self.action(Op::SetScriptRepeat, &[])?;
            } else {
                self.action(Op::LoadEntryFields, &[])?;
                let mut field_found = false;
                let fields = self.call(Op::ScriptFieldsCount, &[])? as usize;
                for field in 0..fields {
                    self.action(Op::SelectScriptField, &[field as f64])?;
                    if !self.flag(Op::ScriptFieldMatches, &[])? {
                        continue;
                    }
                    field_found = true;
                    self.action(Op::SetFieldValue, &[])?;
                    self.action(Op::StoreField, &[field as f64])?;
                }
                if !field_found {
                    return self.fail(14.);
                }
                self.action(Op::StoreScriptValues, &[])?;
            }
            self.action(Op::StoreChangedScript, &[index as f64])?;
        }
        if !found {
            return self.fail(15.);
        }
        self.action(Op::StoreScriptSettings, &[])?;
        self.action(Op::WriteScriptState, &[])?;
        self.action(Op::CommitChangedScripts, &[])?;
        self.action(Op::WriteScriptCatalog, &[])?;
        self.action(Op::ResultCatalog, &[])?;
        self.action(Op::EmitCatalogChanged, &[])?;
        self.action(Op::ReplySuccess, &[])?;
        self.action(Op::EmitReply, &[])
    }
    pub(super) fn script_reply(&self) -> EngineResult {
        self.action(Op::DeleteWatcher, &[])?;
        self.action(Op::SetScriptsBusy, &[0.])?;
        if self.flag(Op::ResponseOk, &[])? {
            self.action(Op::AdoptResponseScripts, &[])?;
            self.action(Op::AdoptResponseTemplates, &[])?;
            self.action(Op::ResultCatalog, &[])?;
            self.action(Op::ResultSelectedScript, &[])?;
            self.action(Op::ResultSnippet, &[])?;
            self.action(Op::EmitCatalogChanged, &[])?;
            self.action(Op::ReplySuccess, &[])?;
            self.action(Op::EmitReply, &[])?;
        } else {
            self.action(Op::ReplyResponseError, &[])?;
        }
        if self.flag(Op::LoadingScripts, &[])? {
            self.action(Op::SetLoadingScripts, &[0.])?;
            self.action(Op::EmitReady, &[])?;
        }
        Ok(())
    }
    pub(super) fn script_manage(&self) -> EngineResult {
        if let Err(error) = self.action(Op::ScriptManage, &[]) {
            if self.call(Op::ExceptionKind, &[])? == 5. {
                return Err(error);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::ReplyError, &[])?;
        }
        Ok(())
    }
}
