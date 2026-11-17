use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn echo_model_fail<T>(&self, reason: f64) -> Result<T> {
        self.action(Op::EchoModelFailure, &[reason])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_echo_detection(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::EchoModelInitialize => {
                if !self.flag(Op::EchoModelExists, &[])? {
                    return self.echo_model_fail(0.);
                }
                self.action(Op::CompileEchoModel, &[])?;
                let shape = self.call(Op::EchoModelShape, &[])?;
                if shape[0] != 4. || shape[1] != 1. || shape[2] != 3. {
                    return self.echo_model_fail(1.);
                }
                let dimensions = self.call(Op::EchoModelDimensions, &[])?;
                if dimensions[0] != dimensions[1] || dimensions[0] < 32. || dimensions[0] > 4096. {
                    return self.echo_model_fail(1.);
                }
                self.action(Op::EchoModelStoreDimensions, &dimensions[..2])?;
                self.action(Op::CreateEchoRequest, &[])?;
            }
            Method::EchoModelDetect => {
                if self.flag(Op::EchoModelHasCheckpoint, &[])? {
                    self.action(Op::Checkpoint, &[])?;
                }
                let image = self.call(Op::EchoModelImage, &[])?;
                if image[0] != 0.
                    || image[1] == 0.
                    || !args[0].is_finite()
                    || args[0] < 0.
                    || args[0] > 1.
                {
                    return self.echo_model_fail(2.);
                }
                self.action(Op::LockEchoModel, &[])?;
                let source = self.call(Op::EchoModelImageSize, &[])?;
                let target = self.call(Op::EchoModelTargetSize, &[])?;
                let Some(layout) = crate::echo_detection::layout(
                    source[0] as i32,
                    source[1] as i32,
                    target[0] as i32,
                    target[1] as i32,
                ) else {
                    self.action(Op::EchoLayoutFailure, &[])?;
                    return Err(HOST_ERROR);
                };
                self.action(Op::StoreEchoLayout, &layout[..3])?;
                self.action(Op::StoreEchoPadding, &layout[3..])?;
                self.action(Op::ResizeEchoImage, &[])?;
                self.action(Op::PadEchoImage, &[])?;
                self.action(Op::CreateEchoTensor, &[])?;
                self.action(Op::FillEchoTensor, &[])?;
                self.action(Op::SetEchoTensor, &[])?;
                self.action(Op::InferEchoModel, &[])?;
                self.action(Op::ReadEchoTensor, &[])?;
                let shape = self.call(Op::EchoPredictionShape, &[])?;
                if shape[0] != 3.
                    || shape[1] != 1.
                    || shape[2] < 5.
                    || shape[2] > 1000.
                    || !self.flag(Op::EchoPredictionFloat, &[])?
                {
                    return self.echo_model_fail(3.);
                }
                self.action(Op::PostprocessEchoTensor, args)?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
