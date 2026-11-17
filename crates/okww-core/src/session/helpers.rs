use super::*;
impl Host {
    pub(super) fn snapshot(&self) -> SessionResult {
        self.action(Op::SnapshotPath, &[])?;
        if !self.flag(Op::MakeSnapshotDirectory, &[])? {
            return self.action(Op::Log, &[6.]);
        }
        self.action(Op::SanitizeSnapshotName, &[])?;
        if !self.flag(Op::EncodeSnapshot, &[])? {
            return Ok(());
        }
        self.action(Op::CreateSnapshotFile, &[])?;
        if !self.flag(Op::OpenSnapshot, &[])?
            || !self.flag(Op::WriteSnapshot, &[])?
            || !self.flag(Op::CommitSnapshot, &[])?
        {
            self.action(Op::Log, &[7.])?;
        }
        Ok(())
    }
    pub(super) fn characters(&self) -> SessionResult {
        let count = self.call(Op::PartySize, &[])? as usize;
        for index in 0..count {
            self.action(Op::AppendMemberPointer, &[index as f64])?;
        }
        Ok(())
    }
    pub(super) fn hotkey(&self, skill: f64) -> SessionResult {
        if skill == 0. || skill == 1. || skill == 2. {
            self.action(Op::ReadHotkey, &[skill])
        } else {
            self.action(Op::ExtraHotkey, &[])
        }
    }
    pub(super) fn feature(&self) -> SessionResult<bool> {
        if !self.flag(Op::FeatureBoxEmpty, &[])? {
            self.action(Op::FeatureSearchBox, &[])?;
        }
        self.flag(Op::FindFeature, &[])
    }
    pub(super) fn best_in_box(&self, threshold: f64) -> SessionResult {
        let mut score = threshold;
        let count = self.call(Op::CandidateNameCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::FindCandidate, &[index as f64])? {
                let confidence = self.call(Op::CandidateConfidence, &[])?;
                if confidence >= score {
                    self.action(Op::StoreBestCandidate, &[index as f64])?;
                    score = confidence;
                }
            }
        }
        Ok(())
    }
    pub(super) fn region_inside(args: &[f64]) -> bool {
        !(args[0] < 0. || args[1] < 0. || args[2] >= args[4] || args[3] >= args[5])
    }
    pub(super) fn concerto(&self) -> SessionResult<f64> {
        self.action(Op::CaptureConcertoState, &[])?;
        let percent = self.call(Op::DetectConcerto, &[])?;
        self.action(Op::RestoreConcertoCache, &[])?;
        let mut changed = false;
        for index in 0..6 {
            if self.call(Op::BeforeCalibrationArea, &[index as f64])?
                != self.call(Op::CurrentCalibrationArea, &[index as f64])?
            {
                changed = true;
                break;
            }
        }
        if changed {
            for index in 0..6 {
                let value = self.call(Op::CurrentCalibrationArea, &[index as f64])?;
                self.action(Op::StoreCalibrationArea, &[index as f64, value])?;
            }
            self.action(Op::WriteCalibration, &[])?;
        }
        Ok(percent)
    }
    pub(super) fn concerto_area_ratio(&self) -> SessionResult<f64> {
        if self.call(Op::CharacterRing, &[])? < 0. {
            self.action(Op::CallCharacterConcerto, &[])?;
        }
        let index = self.call(Op::CharacterRing, &[])?;
        if !(0. ..6.).contains(&index) || self.call(Op::CurrentCalibrationArea, &[index])? <= 0. {
            return Ok(0.);
        }
        self.action(Op::BuildConcertoRegion, &[])?;
        if !self.flag(Op::ConcertoRegionInside, &[])? {
            return Ok(0.);
        }
        let size = self.values(Op::FrameSize, &[])?;
        let minimum = 1500. / 3840. / 2160. * size[0] * size[1];
        let area = self.call(Op::CountConcertoRing, &[index, minimum])?;
        Ok(area / self.call(Op::CurrentCalibrationArea, &[index])?)
    }
    pub(super) fn workshop_task_config(&self) -> SessionResult {
        self.action(Op::ReadTaskConfig, &[])?;
        let count = self.call(Op::OverrideCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::MergeOverride, &[index as f64])?;
        }
        Ok(())
    }
}
