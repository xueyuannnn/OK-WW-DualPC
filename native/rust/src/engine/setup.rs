use super::*;
impl Host {
    pub(super) fn notify(&self) -> EngineResult {
        self.action(Op::LoadNotificationConfig, &[])?;
        if self.flag(Op::SystemNotification, &[])? {
            self.action(Op::EmitNotification, &[])?;
        }
        self.action(Op::SubmitNotification, &[])
    }
    pub(super) fn post_status(&self) -> EngineResult {
        if self.flag(Op::GenerationMatches, &[])? {
            let foreground = self.flag(Op::Foreground, &[])?;
            self.action(Op::BuildStatus, &[foreground as u8 as f64])?;
            self.action(Op::EmitStatus, &[])?;
        }
        Ok(())
    }
    pub(super) fn load(&self) -> EngineResult {
        self.action(Op::Preload, &[])?;
        self.action(Op::LogReady, &[])?;
        if self.flag(Op::TasksDirectory, &[])? || self.flag(Op::ImportsDirectory, &[])? {
            self.action(Op::SetLoadingScripts, &[1.])?;
            self.action(Op::ScanScripts, &[])?;
        } else {
            self.action(Op::EmitReady, &[])?;
        }
        Ok(())
    }
    pub(super) fn begin_preload(&self) -> EngineResult {
        if self.flag(Op::ModelJoinable, &[])? {
            return Ok(());
        }
        self.action(Op::SpawnModel, &[])
    }
    pub(super) fn load_models(&self) -> EngineResult {
        self.action(Op::StartModelTimer, &[])?;
        let result = (|| {
            self.action(Op::CreateTemplates, &[])?;
            self.action(Op::DefaultModelPath, &[])?;
            if !self.flag(Op::ModelExists, &[])? {
                self.action(Op::FallbackModelPath, &[])?;
            }
            self.action(Op::CreateOcr, &[])?;
            self.action(Op::CreateEcho, &[])?;
            self.action(Op::LogModelsReady, &[])
        })();
        if let Err(error) = result {
            if self.call(Op::ExceptionKind, &[])? == 5. {
                return Err(error);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::SetModelError, &[])?;
            self.action(Op::LogModelError, &[])?;
        }
        self.action(Op::SetModelsReady, &[1.])
    }
}
