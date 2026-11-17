//! Bounded diagnostic log copying and the original best-effort report write.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_diagnostics(&self, method: Method) -> Result<[f64; 3]> {
        if !matches!(method, Method::SaveDiagnostic) {
            return Err(INVALID_ACTION);
        }
        self.action(Op::DiagnosticMetadata, &[])?;
        if self.flag(Op::DiagnosticMemoryStatus, &[])? {
            self.action(Op::DiagnosticMemoryFields, &[])?;
        }
        let saved = (|| {
            if !self.flag(Op::DiagnosticMakeDirectory, &[])? {
                self.action(Op::DiagnosticFailure, &[0.])?;
                return Err(HOST_ERROR);
            }
            for i in 0..2 {
                self.action(Op::DiagnosticCreateInput, &[f64::from(i)])?;
                if !self.flag(Op::DiagnosticOpenInput, &[])? {
                    self.action(Op::DiagnosticDropInput, &[])?;
                    continue;
                }
                self.action(Op::DiagnosticSeekTail, &[])?;
                self.action(Op::DiagnosticCreateOutput, &[f64::from(i)])?;
                self.action(Op::DiagnosticReadTail, &[65536.])?;
                if !self.flag(Op::DiagnosticOpenOutput, &[])?
                    || !self.flag(Op::DiagnosticWriteTail, &[])?
                    || !self.flag(Op::DiagnosticCommitOutput, &[])?
                {
                    self.action(Op::DiagnosticFailure, &[1.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::DiagnosticDropLog, &[])?;
            }
            self.action(Op::DiagnosticWriteReport, &[])?;
            Ok(())
        })();
        if let Err(error) = saved {
            if self.scalar(Op::ExceptionKind, &[])? != 6. {
                return Err(error);
            }
            self.action(Op::ClearException, &[])?;
            self.action(Op::DiagnosticDropLog, &[])?;
            self.action(Op::DiagnosticWriteErrorField, &[])?;
            self.action(Op::ClearException, &[])?;
        }
        Ok([0.; 3])
    }
}
