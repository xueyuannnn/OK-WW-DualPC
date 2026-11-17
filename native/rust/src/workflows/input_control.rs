//! Input timing, ownership, recovery and segmentation. The host only transports
//! packets and owns Qt strings, sets and geometry values.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};

fn maximum(a: f64, b: f64) -> f64 {
    if a < b {
        b
    } else {
        a
    }
}
impl Host {
    fn input_number(&self, key: f64) -> Result<f64> {
        self.scalar(Op::InputConfigNumber, &[key])
    }
    fn input_fail<T>(&self, reason: f64) -> Result<T> {
        self.action(Op::InputFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    fn input_round(&self, value: f64) -> Result<f64> {
        self.scalar(Op::PixelRound, &[value])
    }
    fn input_guard<T>(&self, result: Result<T>) -> Result<T> {
        if result.is_err() {
            self.action(Op::InputRelease, &[])?;
        }
        result
    }
    fn input_transmit(&self) -> Result {
        let began = self.scalar(Op::Now, &[])?;
        let mut recovering = false;
        let mut reported = -1.;
        let mut dismissed_at = -1.;
        let mut dismissals = 0;
        loop {
            self.action(Op::Checkpoint, &[])?;
            let attempt = (|| {
                self.action(Op::InputTransportCommand, &[])?;
                if recovering && self.flag(Op::InputHasRecovery, &[])? {
                    self.action(Op::InputRecoveryReady, &[])?;
                }
                Ok(())
            })();
            match attempt {
                Ok(()) => return Ok(()),
                Err(error) => {
                    if !self.flag(Op::InputClassifyBlocked, &[])? {
                        return Err(error);
                    }
                    self.action(Op::ClearException, &[])?;
                    if !recovering {
                        self.action(Op::InputRelease, &[])?;
                        recovering = true;
                    }
                    let elapsed = self.scalar(Op::Now, &[])? - began;
                    if self.flag(Op::InputHasRecovery, &[])?
                        && (reported < 0. || elapsed - reported >= 1.)
                    {
                        self.action(Op::InputRecoveryNotice, &[elapsed])?;
                        reported = elapsed;
                    }
                    if self.flag(Op::InputDismissBlocked, &[])?
                        && dismissals < 3
                        && (dismissed_at < 0. || elapsed - dismissed_at >= 1.)
                    {
                        dismissals += 1;
                        dismissed_at = elapsed;
                        let result = (|| {
                            self.action(Op::InputTransportEscape, &[1.])?;
                            self.action(Op::ExecutionWait, &[0.05])?;
                            self.action(Op::InputTransportEscape, &[0.])
                        })();
                        if let Err(error) = result {
                            let blocked = self.flag(Op::InputIsBlocked, &[])?;
                            self.action(Op::InputRelease, &[])?;
                            if !blocked {
                                return Err(error);
                            }
                            self.action(Op::ClearException, &[])?;
                        }
                    }
                    if elapsed >= 120. {
                        return self.input_fail(0.);
                    }
                    self.action(Op::ExecutionWait, &[0.1])?;
                }
            }
        }
    }
    fn input_counts(&self, x: f64, y: f64, precise: bool) -> Result {
        let local = self.flag(Op::InputLocal, &[])?;
        let chunk = if local {
            32767.
        } else {
            let size = self.input_number(0.)?;
            if precise {
                size.min(127.)
            } else {
                size
            }
        };
        if chunk <= 0. {
            return self.input_fail(1.);
        }
        let interval = if local {
            0.
        } else {
            let value = self.input_number(1.)?;
            if precise {
                maximum(value, 0.008)
            } else {
                value
            }
        };
        let steps = maximum(1., ((x.abs().max(y.abs()) + chunk - 1.) / chunk).trunc());
        if steps > 100000. {
            return self.input_fail(2.);
        }
        let (mut sent_x, mut sent_y) = (0., 0.);
        for i in 1..=steps as i32 {
            let tx = self.input_round(x * f64::from(i) / steps)?;
            let ty = self.input_round(y * f64::from(i) / steps)?;
            self.action(Op::InputSendMove, &[tx - sent_x, ty - sent_y])?;
            (sent_x, sent_y) = (tx, ty);
            if interval != 0. && f64::from(i) < steps {
                self.action(Op::ExecutionWait, &[interval])?;
            }
        }
        Ok(())
    }
    pub(super) fn run_input_control(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        let mut out = [0.; 3];
        match method {
            Method::InputTransmit => self.input_transmit()?,
            Method::InputSend => {
                self.action(Op::Checkpoint, &[])?;
                self.action(Op::InputTransmitNested, &[])?;
            }
            Method::InputWorkshopCommand => {
                let kind = self.scalar(Op::InputCommandKind, &[])? as i32;
                if kind < 0 {
                    return self.input_fail(3.);
                }
                if kind == 0 {
                    self.action(Op::InputRelease, &[])?;
                } else {
                    self.action(Op::Checkpoint, &[])?;
                    if kind == 2
                        && self.flag(Op::InputCommandDown, &[])?
                        && self.flag(Op::InputButtonsEmpty, &[])?
                    {
                        self.action(Op::InputSettle, &[])?;
                    }
                    self.action(Op::InputTransmitNested, &[])?;
                    if kind == 3 {
                        self.action(Op::InputForgetPosition, &[])?;
                    }
                    if kind == 1 {
                        self.action(Op::InputTrackCommandKey, &[])?;
                    } else if kind == 2 {
                        self.action(Op::InputTrackCommandButton, &[])?;
                    }
                }
            }
            Method::InputKeyDown => {
                let code = self.scalar(Op::InputKeyCode, &[])?;
                self.action(Op::InputSendKey, &[code, 1.])?;
                self.action(Op::InputTrackKey, &[code, 1.])?;
            }
            Method::InputKeyUp | Method::InputButtonUp => {
                // The original key lookup precedes the protected transport.
                // An invalid key must not release unrelated held input.
                let code = if matches!(method, Method::InputKeyUp) {
                    self.scalar(Op::InputKeyCode, &[])?
                } else {
                    0.
                };
                let result = (|| {
                    if matches!(method, Method::InputKeyUp) {
                        self.action(Op::InputTransportKey, &[code, 0.])?;
                        self.action(Op::InputTrackKey, &[code, 0.])?;
                    } else {
                        self.action(Op::InputTransportButton, &[0.])?;
                        self.action(Op::InputTrackButton, &[0.])?;
                    }
                    Ok(())
                })();
                self.input_guard(result)?;
            }
            Method::InputKey => {
                let code = self.scalar(Op::InputKeyCode, &[])?;
                let held = self.flag(Op::InputKeyHeld, &[code])?;
                let result = (|| {
                    self.action(Op::InputKeyDownNested, &[])?;
                    self.action(Op::ExecutionWait, &[maximum(0., args[0])])?;
                    if !held {
                        self.action(Op::InputKeyUpNested, &[])?;
                    }
                    Ok(())
                })();
                self.input_guard(result)?;
            }
            Method::InputButtonDown => {
                if !self.flag(Op::InputButtonValid, &[])? {
                    return self.input_fail(4.);
                }
                if self.flag(Op::InputButtonsEmpty, &[])? {
                    self.action(Op::InputSettle, &[])?;
                }
                self.action(Op::InputSendButton, &[1.])?;
                self.action(Op::InputTrackButton, &[1.])?;
            }
            Method::InputSettle => {
                let ready = self.scalar(Op::InputPositionReadyAt, &[])?;
                if ready >= 0. {
                    self.action(
                        Op::ExecutionWait,
                        &[maximum(0., ready - self.scalar(Op::Now, &[])?)],
                    )?;
                }
            }
            Method::InputCounts => self.input_counts(args[0], args[1], args[2] != 0.)?,
            Method::InputMove => {
                self.action(Op::Checkpoint, &[])?;
                let target = self.call(Op::InputScreenPoint, &[])?;
                let result = (|| {
                    if self.flag(Op::InputAbsolute, &[])? {
                        self.action(Op::InputSendAbsolute, &target[..2])?;
                    } else {
                        let mut origin = [0.; 3];
                        if self.flag(Op::InputButtonsEmpty, &[])? {
                            let home = self.input_number(2.)?;
                            self.action(Op::InputSendMove, &[-home, -home])?;
                            self.action(Op::ExecutionWait, &[self.input_number(3.)?])?;
                        } else {
                            if !self.flag(Op::InputHasPosition, &[])? {
                                return self.input_fail(5.);
                            }
                            origin = self.call(Op::InputOriginPoint, &[])?;
                        }
                        let x =
                            self.input_round((target[0] - origin[0]) * self.input_number(4.)?)?;
                        let y =
                            self.input_round((target[1] - origin[1]) * self.input_number(5.)?)?;
                        self.action(
                            Op::InputCountsNested,
                            &[x, y, self.scalar(Op::InputButtonsEmpty, &[])?],
                        )?;
                    }
                    self.action(Op::InputStorePosition, &[])?;
                    let ready = self.scalar(Op::Now, &[])? + self.input_number(6.)?;
                    self.action(Op::InputSetReadyAt, &[ready])
                })();
                self.input_guard(result)?;
            }
            Method::InputRelative => {
                let result = (|| {
                    let local = self.flag(Op::InputLocal, &[])?;
                    let x = self
                        .input_round(args[0] * if local { 1. } else { self.input_number(4.)? })?;
                    let y = self
                        .input_round(args[1] * if local { 1. } else { self.input_number(5.)? })?;
                    self.action(Op::InputCountsNested, &[x, y, 0.])?;
                    self.action(Op::InputForgetPosition, &[])
                })();
                self.input_guard(result)?;
            }
            Method::InputClick => {
                let result = (|| {
                    let point = self.flag(Op::InputHasRequestedPoint, &[])?;
                    if point {
                        self.action(Op::InputMoveNested, &[])?;
                    }
                    self.action(Op::InputButtonDownNested, &[])?;
                    self.action(
                        Op::ExecutionWait,
                        &[maximum(if point { 0.04 } else { 0.02 }, args[0])],
                    )?;
                    self.action(Op::InputButtonUpNested, &[])
                })();
                self.input_guard(result)?;
            }
            Method::InputScroll => {
                if self.flag(Op::InputHasRequestedPoint, &[])? {
                    self.action(Op::InputMoveNested, &[])?;
                }
                self.action(Op::InputSettle, &[])?;
                self.action(Op::InputSendWheel, &[args[0]])?;
            }
            Method::InputSwipe => {
                if !args[0].is_finite() || args[0] < 0. || args[0] > 60000. {
                    return self.input_fail(6.);
                }
                let result = (|| {
                    self.action(Op::InputMoveNested, &[])?;
                    self.action(Op::InputButtonDownNested, &[])?;
                    let steps = maximum(1., self.input_round(args[0] / 10.)?);
                    for i in 1..=steps as i32 {
                        self.action(Op::InputMoveInterpolated, &[f64::from(i) / steps])?;
                        self.action(Op::ExecutionWait, &[args[0] / steps / 1000.])?;
                    }
                    self.action(Op::InputButtonUpNested, &[])?;
                    self.action(Op::ExecutionWait, &[maximum(0., args[1])])
                })();
                self.input_guard(result)?;
            }
            Method::InputText => {
                let count = self.scalar(Op::InputTextLength, &[])? as usize;
                for i in 0..count {
                    self.action(Op::InputPrepareStroke, &[i as f64])?;
                }
                let result = (|| {
                    for i in 0..count {
                        let shift = self.flag(Op::InputLoadStroke, &[i as f64])?;
                        let held = self.flag(Op::InputKeyHeld, &[225.])?;
                        if shift {
                            self.action(Op::InputShiftDown, &[])?;
                        }
                        self.action(Op::InputStroke, &[])?;
                        if shift && !held {
                            self.action(Op::InputShiftUp, &[])?;
                        }
                    }
                    Ok(())
                })();
                self.input_guard(result)?;
            }
            Method::InputScreenPoint => {
                let raw = self.call(Op::InputRawSize, &[])?;
                let point = self.call(Op::InputRequestedPoint, &[])?;
                let output = self.call(Op::InputOutputSize, &[])?;
                if raw[0] <= 0.
                    || raw[1] <= 0.
                    || point[0] < 0.
                    || point[1] < 0.
                    || point[0] >= output[0]
                    || point[1] >= output[1]
                    || !point[0].is_finite()
                    || !point[1].is_finite()
                {
                    return self.input_fail(7.);
                }
                let n = self.scalar(Op::InputCropCount, &[])?;
                if n != 0. {
                    if n != 4. {
                        return self.input_fail(8.);
                    }
                    self.action(Op::InputLoadCrop, &[])?;
                    if !self.flag(Op::InputCropValid, &[])? {
                        return self.input_fail(9.);
                    }
                }
                let desktop = self.call(Op::InputDesktopSize, &[])?;
                if desktop[0] <= 0. || desktop[1] <= 0. {
                    return self.input_fail(10.);
                }
                let crop = self.call(Op::InputCropGeometry, &[])?;
                let size = self.call(Op::InputCropSize, &[])?;
                out[0] = (desktop[0] - 1.).min(self.input_round(
                    (crop[0] + point[0] * size[0] / output[0]) * desktop[0] / raw[0],
                )?);
                out[1] = (desktop[1] - 1.).min(self.input_round(
                    (crop[1] + point[1] * size[1] / output[1]) * desktop[1] / raw[1],
                )?);
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(out)
    }
}
