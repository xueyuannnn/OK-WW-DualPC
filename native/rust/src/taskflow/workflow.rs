use super::*;
impl Host {
    fn w_set_number(&self, name: &str, value: f64) -> Result {
        self.query(15, name, &[value])?;
        Ok(())
    }
    fn w_number(&self, name: &str) -> Result<f64> {
        self.query(17, name, &[])
    }
    fn w_state(&self, state: &str) -> Result<bool> {
        Ok(self.query(13, state, &[])? != 0.)
    }
    fn w_request_state(&self, state: &str) -> Result<bool> {
        Ok(self.query(12, state, &[])? != 0.)
    }
    fn w_ignore(&self, result: Result) -> Result {
        if result.is_err() {
            self.action(Op::ClearError, &[])?;
        }
        Ok(())
    }
    pub(super) fn begin_workflow(&self) -> Result {
        self.action(Op::WorkflowReset, &[])?;
        let now = self.now()?;
        self.action(Op::WorkflowStart, &[now])?;
        self.action(Op::TimingReset, &[])?;
        self.w_ignore(self.action(Op::TimingRead, &[]))?;
        let count = self.value(Op::InputStepsCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::LoadStep, &[0., index as f64])?;
            self.query(14, "state\0pending", &[])?;
            self.w_set_number("elapsed_seconds", 0.)?;
            self.action(Op::LoadSamples, &[])?;
            let mut sum = 0.;
            let mut valid = 0;
            let samples = self.value(Op::SampleCount, &[])? as usize;
            for sample in 0..samples {
                let value = self.call(Op::Sample, &[sample as f64])?;
                if value[1] != 0. && value[0] > 0. && value[0] < 86400. {
                    sum += value[0];
                    valid += 1;
                }
            }
            if valid > 0 {
                self.w_set_number("estimate_seconds", sum / valid as f64)?;
            }
            self.query(
                14,
                if valid > 0 {
                    "estimate_source\0history"
                } else {
                    "estimate_source\0reference"
                },
                &[],
            )?;
            self.action(Op::AppendStep, &[])?;
        }
        self.action(Op::PublishWorkflow, &[])
    }
    pub(super) fn workflow_step(&self) -> Result {
        let mut valid = false;
        for state in ["running", "completed", "skipped", "failed"] {
            if self.w_request_state(state)? {
                valid = true;
                break;
            }
        }
        if !valid {
            return Ok(());
        }
        let count = self.value(Op::WorkflowCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::LoadStep, &[1., index as f64])?;
            if !self.flag(Op::StepMatchesId, &[])? {
                continue;
            }
            if self.flag(Op::StepMatchesState, &[])? {
                return Ok(());
            }
            let was_running = self.w_state("running")?;
            if self.w_request_state("running")? {
                self.w_set_number("started", self.now()?)?;
            } else if was_running {
                let elapsed = cpp_max_zero(self.now()? - self.w_number("started")?);
                self.w_set_number("elapsed_seconds", elapsed)?;
            }
            self.action(Op::StoreStepState, &[])?;
            if self.query(19, "detail", &[])? != 0. {
                self.action(Op::StoreDetail, &[])?;
            }
            self.action(Op::StoreWorkflowStep, &[index as f64])?;
            if self.w_request_state("completed")?
                && was_running
                && self.w_number("elapsed_seconds")? > 0.
                && self.flag(Op::HasTimingPath, &[])?
            {
                self.action(Op::LoadSamples, &[])?;
                self.action(Op::AppendSample, &[])?;
                while self.value(Op::SampleCount, &[])? > 12. {
                    self.action(Op::RemoveSample, &[])?;
                }
                self.action(Op::StoreSamples, &[])?;
                self.w_ignore(self.action(Op::TimingWrite, &[]))?;
            }
            self.action(Op::PublishWorkflow, &[])?;
            return Ok(());
        }
        Ok(())
    }
    pub(super) fn publish_workflow(&self) -> Result {
        if !self.flag(Op::HasProgress, &[])? {
            return Ok(());
        }
        self.action(Op::OutputReset, &[])?;
        let count = self.value(Op::WorkflowCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::LoadStep, &[2., index as f64])?;
            if self.w_state("running")? {
                let elapsed = cpp_max_zero(self.now()? - self.w_number("started")?);
                self.w_set_number("elapsed_seconds", elapsed)?;
            }
            self.query(18, "started", &[])?;
            self.action(Op::StoreOutputStep, &[index as f64])?;
        }
        let elapsed = cpp_max_zero(self.now()? - self.value(Op::WorkflowBegan, &[])?);
        self.action(Op::OutputPublish, &[elapsed])
    }
    pub(super) fn report_progress(&self, mut current: f64, mut total: f64) -> Result {
        if !self.flag(Op::HasProgress, &[])? {
            return Ok(());
        }
        if !current.is_finite() || !total.is_finite() || current < 0. || total <= 0. {
            current = -1.;
            total = -1.;
        }
        self.action(Op::ReportProgress, &[current, total])
    }
}
fn cpp_max_zero(value: f64) -> f64 {
    if value > 0. {
        value
    } else {
        0.
    }
}
