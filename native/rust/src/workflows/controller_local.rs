//! SendInput focus/foreign-modifier protection and input tagging.
use super::{Host, Op, Result, HOST_ERROR};
impl Host {
    pub(super) fn run_controller_local(&self) -> Result<[f64; 3]> {
        if !self.flag(Op::ControllerLocalConnected, &[])? {
            self.action(Op::ControllerLocalFailure, &[0.])?;
            return Err(HOST_ERROR);
        }
        let releasing = self.flag(Op::ControllerLocalReleasing, &[])?;
        let mut cancel_run_dialog = false;
        if !releasing
            && (!self.flag(Op::ControllerLocalDryRun, &[])?
                || self.flag(Op::ControllerLocalHasEnvironment, &[])?)
        {
            self.action(
                if self.flag(Op::ControllerLocalHasEnvironment, &[])? {
                    Op::ControllerLocalReadProvider
                } else {
                    Op::ControllerLocalReadSystem
                },
                &[],
            )?;
            self.action(Op::ControllerLocalCopyEnvironment, &[])?;
            let modifiers = self.scalar(Op::ControllerLocalEnvironmentModifiers, &[])? as i32;
            let held = self.scalar(Op::ControllerLocalHeldModifiers, &[])? as i32;
            let foreign = modifiers & !held & 0x88;
            if foreign != 0 {
                self.action(Op::ControllerLocalModifierLabels, &[])?;
                for index in 0..8 {
                    if foreign & (1 << index) != 0 {
                        self.action(Op::ControllerLocalAppendModifier, &[index as f64])?;
                    }
                }
                self.action(Op::ControllerLocalForeignModifierError, &[])?;
                return Err(HOST_ERROR);
            }
            let shell = self.flag(Op::ControllerLocalExplorer, &[])?;
            self.action(Op::ControllerLocalReadClass, &[])?;
            let run_dialog = shell
                && self.flag(Op::ControllerLocalDialogClass, &[])?
                && self.flag(Op::ControllerLocalRunTitle, &[])?;
            if run_dialog {
                let count = self.scalar(Op::ControllerLocalInputCount, &[])? as usize;
                cancel_run_dialog = count != 0;
                for index in 0..count {
                    let a = [index as f64];
                    if !self.flag(Op::ControllerLocalInputKeyboard, &a)?
                        || !self.flag(Op::ControllerLocalInputEscape, &a)?
                        || !self.flag(Op::ControllerLocalInputScanCode, &a)?
                    {
                        cancel_run_dialog = false;
                        break;
                    }
                }
            }
            if self.flag(Op::ControllerLocalOwnWindow, &[])?
                || (shell
                    && !cancel_run_dialog
                    && self.flag(Op::ControllerLocalExcludedClass, &[])?)
            {
                self.action(Op::ControllerLocalFocusError, &[f64::from(run_dialog)])?;
                return Err(HOST_ERROR);
            }
            self.action(Op::ControllerLocalDropEnvironmentScope, &[])?;
        }
        if self.flag(Op::ControllerLocalHasTarget, &[])? && !releasing && !cancel_run_dialog {
            self.action(Op::ControllerLocalTargetRect, &[])?;
            if !self.flag(Op::ControllerLocalTargetForeground, &[])? {
                self.action(Op::ControllerLocalTargetError, &[])?;
                return Err(HOST_ERROR);
            }
        }
        let count = self.scalar(Op::ControllerLocalInputCount, &[])? as usize;
        for index in 0..count {
            let a = [index as f64];
            if self.flag(Op::ControllerLocalInputMouse, &a)? {
                self.action(Op::ControllerLocalTagMouse, &a)?;
            }
            if self.flag(Op::ControllerLocalInputKeyboard, &a)? {
                self.action(Op::ControllerLocalTagKeyboard, &a)?;
            }
        }
        if self.flag(Op::ControllerLocalDryRun, &[])? {
            self.action(Op::ControllerLocalRecord, &[])?;
            return Ok([0.; 3]);
        }
        self.action(Op::ControllerLocalClock, &[])?;
        self.action(Op::ControllerLocalSendInputs, &[])?;
        if !self.flag(Op::ControllerLocalSendComplete, &[])? {
            self.action(Op::ControllerLocalFailure, &[1.])?;
            return Err(HOST_ERROR);
        }
        self.action(Op::ControllerLocalLatency, &[])?;
        Ok([0.; 3])
    }
}
