//! Physical keyboard event ownership and dedicated message-thread lifecycle.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_desktop_physical(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::PhysicalHook => {
                if self.flag(Op::PhysicalHookAction, &[])? {
                    self.action(Op::PhysicalHookRead, &[])?;
                    if self.scalar(Op::PhysicalHookUsage, &[])?
                        < self.scalar(Op::PhysicalHookCapacity, &[])?
                    {
                        let down = self.flag(Op::PhysicalHookKeyDown, &[])?
                            || self.flag(Op::PhysicalHookSysDown, &[])?;
                        if !self.flag(Op::PhysicalHookInjected, &[])? {
                            self.action(Op::PhysicalHookSetDown, &[f64::from(down)])?;
                            if down {
                                self.action(Op::PhysicalHookSetPhysicalPress, &[])?;
                            }
                        } else if down {
                            self.action(Op::PhysicalHookSetInjectedPress, &[])?;
                        }
                    }
                }
                self.action(Op::PhysicalHookCallNext, &[])?;
            }
            Method::PhysicalCreate => {
                if self.flag(Op::PhysicalClaimHook, &[])? {
                    self.action(Op::PhysicalFailure, &[0.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::PhysicalCreateThread, &[])?;
                while !self.flag(Op::PhysicalReady, &[])? {
                    self.action(Op::PhysicalWaitReady, &[])?;
                }
                if self.flag(Op::PhysicalFailed, &[])? {
                    self.action(Op::PhysicalJoinThread, &[])?;
                    self.action(Op::PhysicalClearActive, &[])?;
                    self.action(Op::PhysicalFailure, &[1.])?;
                    return Err(HOST_ERROR);
                }
            }
            Method::PhysicalDestroy => {
                if self.flag(Op::PhysicalHasThreadId, &[])? {
                    self.action(Op::PhysicalPostQuit, &[])?;
                }
                if self.flag(Op::PhysicalThreadJoinable, &[])? {
                    self.action(Op::PhysicalJoinThread, &[])?;
                }
                self.action(Op::PhysicalClearActive, &[])?;
            }
            Method::PhysicalWorker => {
                self.action(Op::PhysicalPeekMessage, &[])?;
                self.action(Op::PhysicalStoreThreadId, &[])?;
                for index in 0..256 {
                    self.action(Op::PhysicalResetDown, &[index as f64])?;
                }
                for index in 0..256 {
                    self.action(Op::PhysicalResetPress, &[index as f64])?;
                }
                for index in 0..256 {
                    self.action(Op::PhysicalResetInjected, &[index as f64])?;
                }
                self.action(Op::PhysicalInstallHook, &[])?;
                self.action(Op::PhysicalStoreFailed, &[])?;
                self.action(Op::PhysicalStoreReady, &[])?;
                if self.flag(Op::PhysicalHasHook, &[])? {
                    while self.scalar(Op::PhysicalGetMessage, &[])? > 0. {
                        self.action(Op::PhysicalTranslateMessage, &[])?;
                        self.action(Op::PhysicalDispatchMessage, &[])?;
                    }
                    self.action(Op::PhysicalUnhook, &[])?;
                }
            }
            Method::PhysicalDown | Method::PhysicalHotkey => {
                let vk = args[0];
                if !(0. ..256.).contains(&vk) {
                    return Ok([0.; 3]);
                }
                return self.call(
                    if matches!(method, Method::PhysicalDown) {
                        Op::PhysicalReadDown
                    } else {
                        Op::PhysicalReadLaterPress
                    },
                    &[vk],
                );
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
