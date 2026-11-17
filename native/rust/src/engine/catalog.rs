use super::*;
impl Host {
    pub(super) fn catalog(&self) -> EngineResult {
        self.action(Op::CatalogProfileLoad, &[])?;
        let count = self.call(Op::CatalogCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectCatalog, &[index as f64])?;
            let supported = self.native_available(1.)?;
            self.action(Op::CatalogSetSupported, &[supported as u8 as f64])?;
            if !supported {
                self.action(Op::CatalogUnsupportedDescription, &[])?;
            }
            if !supported {
                self.action(Op::CatalogUnsupportedName, &[])?;
            }
            self.action(Op::CatalogAppendTask, &[])?;
        }
        let scripts = self.call(Op::ScriptsCount, &[])? as usize;
        for index in 0..scripts {
            self.action(Op::SelectScriptEntry, &[index as f64])?;
            self.action(Op::ScriptRemoveSource, &[])?;
            self.action(Op::CatalogAppendScript, &[])?;
        }
        self.action(Op::CatalogStoreTasks, &[])?;
        self.action(Op::CatalogStoreScripts, &[])?;
        self.action(Op::CatalogStoreTemplates, &[])?;
        if self.flag(Op::TeamBusy, &[])? {
            self.action(Op::CatalogSnapshotTeams, &[])?;
        } else {
            self.action(Op::CatalogLiveTeams, &[])?;
        }
        Ok(())
    }
    pub(super) fn status(&self) -> EngineResult {
        self.action(Op::ProfileCatalogLoad, &[])?;
        let count = self.call(Op::CatalogCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectCatalog, &[index as f64])?;
            self.action(Op::AppendCatalogState, &[])?;
        }
        let scripts = self.call(Op::ScriptsCount, &[])? as usize;
        for index in 0..scripts {
            self.action(Op::SelectScriptEntry, &[index as f64])?;
            self.action(Op::AppendScriptState, &[])?;
        }
        self.action(Op::BuildStatusResult, &[])
    }
}
