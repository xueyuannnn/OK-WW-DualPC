//! Controller movement, held-state mutation and report ordering.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn controller_input_failure(&self, reason: f64) -> Result<[f64; 3]> {
        self.action(Op::ControllerInputFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_controller_input(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::ControllerMove => {
                let xy = self.call(Op::ControllerInputCoordinates, &[])?;
                if xy[0].abs() > 32767. || xy[1].abs() > 32767. {
                    return self.controller_input_failure(0.);
                }
                if self.flag(Op::ControllerInputLocal, &[])? {
                    self.action(Op::ControllerInputRelativeMouse, &[])?;
                    self.action(Op::ControllerInputSend, &[])?;
                } else {
                    self.action(Op::ControllerInputNetMove, &[])?;
                }
            }
            Method::ControllerAbsolute => {
                if !self.flag(Op::ControllerInputLocal, &[])? {
                    return self.controller_input_failure(1.);
                }
                let size = self.call(Op::ControllerInputDimensions, &[])?;
                let point = self.call(Op::ControllerInputCoordinates, &[])?;
                if size[0] < 1.
                    || size[1] < 1.
                    || point[0] < 0.
                    || point[1] < 0.
                    || point[0] >= size[0]
                    || point[1] >= size[1]
                {
                    return self.controller_input_failure(2.);
                }
                if self.flag(Op::ControllerInputHasCaptureTarget, &[])? {
                    self.action(Op::ControllerInputCaptureRect, &[])?;
                } else if self.flag(Op::ControllerInputHasTarget, &[])? {
                    self.action(Op::ControllerInputTargetRect, &[])?;
                } else if self.flag(Op::ControllerInputDryRun, &[])? {
                    self.action(Op::ControllerInputImageRect, &[])?;
                } else {
                    self.action(Op::ControllerInputForegroundRect, &[])?;
                }
                self.action(Op::ControllerInputMapPoint, &[])?;
                if self.flag(Op::ControllerInputHasTarget, &[])?
                    && !self.flag(Op::ControllerInputTargetContainsPoint, &[])?
                {
                    return self.controller_input_failure(3.);
                }
                self.action(
                    if self.flag(Op::ControllerInputDryRun, &[])? {
                        Op::ControllerInputUseImageDesktop
                    } else {
                        Op::ControllerInputVirtualDesktop
                    },
                    &[],
                )?;
                self.action(Op::ControllerInputNormalizePoint, &[])?;
                self.action(Op::ControllerInputAbsoluteMouse, &[])?;
                self.action(Op::ControllerInputSend, &[])?;
            }
            Method::ControllerButton => {
                let index = self.scalar(Op::ControllerInputButtonIndex, &[])? as i32;
                if index < 0 {
                    return self.controller_input_failure(4.);
                }
                let down = self.flag(Op::ControllerInputDown, &[])?;
                if self.flag(Op::ControllerInputLocal, &[])? {
                    if !down && !self.flag(Op::ControllerInputButtonHeld, &[])? {
                        return Ok([0.; 3]);
                    }
                    self.action(Op::ControllerInputButtonMouse, &[])?;
                    if index >= 3 {
                        self.action(Op::ControllerInputButtonData, &[])?;
                    }
                    if down {
                        self.action(Op::ControllerInputSetButton, &[])?;
                    }
                    self.action(Op::ControllerInputSendReleasing, &[])?;
                    if !down {
                        self.action(Op::ControllerInputClearButton, &[])?;
                    }
                } else {
                    self.action(
                        if down {
                            Op::ControllerInputSetButton
                        } else {
                            Op::ControllerInputClearButton
                        },
                        &[],
                    )?;
                    self.action(
                        match index {
                            0 => Op::ControllerInputNetLeft,
                            2 => Op::ControllerInputNetMiddle,
                            _ => Op::ControllerInputNetRight,
                        },
                        &[],
                    )?;
                }
            }
            Method::ControllerKey => {
                let code = self.scalar(Op::ControllerInputCode, &[])? as i32;
                if !(1..=255).contains(&code) {
                    return self.controller_input_failure(5.);
                }
                let modifier = (0xe0..=0xe7).contains(&code);
                let down = self.flag(Op::ControllerInputDown, &[])?;
                if self.flag(Op::ControllerInputLocal, &[])? {
                    let held = self.flag(
                        if modifier {
                            Op::ControllerInputModifierHeld
                        } else {
                            Op::ControllerInputKeyHeld
                        },
                        &[],
                    )?;
                    if !down && !held {
                        return Ok([0.; 3]);
                    }
                    self.action(Op::ControllerInputHid, &[])?;
                    if down {
                        self.action(
                            if modifier {
                                Op::ControllerInputSetModifier
                            } else {
                                Op::ControllerInputInsertKey
                            },
                            &[],
                        )?;
                    }
                    self.action(Op::ControllerInputSendReleasing, &[])?;
                    if !down {
                        self.action(
                            if modifier {
                                Op::ControllerInputClearModifier
                            } else {
                                Op::ControllerInputEraseKey
                            },
                            &[],
                        )?;
                    }
                } else {
                    if modifier {
                        self.action(
                            if down {
                                Op::ControllerInputSetModifier
                            } else {
                                Op::ControllerInputClearModifier
                            },
                            &[],
                        )?;
                    } else if down {
                        if self.scalar(Op::ControllerInputKeyCount, &[])? >= 10.
                            && !self.flag(Op::ControllerInputKeyHeld, &[])?
                        {
                            return self.controller_input_failure(6.);
                        }
                        self.action(Op::ControllerInputInsertKey, &[])?;
                    } else {
                        self.action(Op::ControllerInputEraseKey, &[])?;
                    }
                    self.action(Op::ControllerInputKeyboardReport, &[])?;
                }
            }
            Method::ControllerWheel => {
                let mut steps = self.scalar(Op::ControllerInputSteps, &[])? as i32;
                if i64::from(steps).abs() > 10000 {
                    return self.controller_input_failure(7.);
                }
                if self.flag(Op::ControllerInputLocal, &[])? {
                    self.action(
                        Op::ControllerInputWheelMouse,
                        &[steps.wrapping_mul(120) as f64],
                    )?;
                    self.action(Op::ControllerInputSend, &[])?;
                } else {
                    while steps != 0 {
                        let chunk = steps.clamp(-127, 127);
                        self.action(Op::ControllerInputNetWheel, &[chunk as f64])?;
                        steps -= chunk;
                    }
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
