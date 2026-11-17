use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(super) fn run_inference(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::InferenceWait => {
                if args[0] == 0. {
                    self.action(Op::InferenceSynchronous, &[])?;
                } else {
                    self.action(Op::Checkpoint, &[])?;
                    self.action(Op::InferenceStartTimer, &[])?;
                    self.action(Op::InferenceStart, &[])?;
                    let result = (|| {
                        while !self.flag(Op::InferenceReady, &[20.])? {
                            self.action(Op::Checkpoint, &[])?;
                            if self.scalar(Op::InferenceElapsedMs, &[])? > 10000. {
                                self.action(Op::InferenceTimeout, &[])?;
                                return Err(HOST_ERROR);
                            }
                        }
                        self.action(Op::Checkpoint, &[])
                    })();
                    if result.is_err() {
                        // These two SDK cleanup failures were intentionally ignored.
                        // The host retains the first exception until we return.
                        let _ = self.action(Op::InferenceCancel, &[]);
                        let _ = self.action(Op::InferenceDrain, &[]);
                    }
                    result?;
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
