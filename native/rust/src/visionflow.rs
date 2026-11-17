mod bars;
mod detection;
// Recognition policies, separated from Qt, OpenCV and OCR SDK objects.
use std::ffi::c_void;
const HOST_ERROR: i32 = -1;
const INVALID_ACTION: i32 = -3;
include!(concat!(env!("OUT_DIR"), "/visionflow_abi.rs"));
type Result<T = ()> = std::result::Result<T, i32>;
#[repr(C)]
pub struct Host {
    pub context: *mut c_void,
    pub invoke: unsafe extern "C" fn(*mut c_void, i32, *const f64, usize, *mut f64) -> i32,
}
impl Host {
    fn call(&self, op: Op, args: &[f64]) -> Result<[f64; 3]> {
        let mut out = [0.; 3];
        // SAFETY: both buffers are borrowed for the synchronous SDK callback;
        // exceptions remain host-owned and cannot unwind through Rust.
        if unsafe {
            (self.invoke)(
                self.context,
                op as i32,
                args.as_ptr(),
                args.len(),
                out.as_mut_ptr(),
            )
        } == 1
        {
            Ok(out)
        } else {
            Err(HOST_ERROR)
        }
    }
    fn scalar(&self, op: Op, args: &[f64]) -> Result<f64> {
        Ok(self.call(op, args)?[0])
    }
    fn flag(&self, op: Op, args: &[f64]) -> Result<bool> {
        Ok(self.scalar(op, args)? != 0.)
    }
    fn action(&self, op: Op, args: &[f64]) -> Result {
        self.call(op, args)?;
        Ok(())
    }
    fn fail(&self, reason: f64) -> Result {
        self.action(Op::Throw, &[reason])?;
        Err(HOST_ERROR)
    }
    fn initialize(&self) -> Result {
        let count = self.scalar(Op::CharacterEntries, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectEntry, &[index as f64])?;
            self.action(Op::InitializeDefinition, &[])?;
            let templates = self.scalar(Op::TemplateEntries, &[])? as usize;
            for template in 0..templates {
                self.action(Op::AppendTemplate, &[template as f64])?;
            }
            if self.flag(Op::TemplatesEmpty, &[])? || self.flag(Op::ClassNameEmpty, &[])? {
                return self.fail(0.);
            }
            self.action(Op::AppendDefinition, &[])?;
        }
        Ok(())
    }
    fn party(&self) -> Result<[f64; 3]> {
        if !self.flag(Op::PartyVersionMatches, &[])? {
            self.action(Op::ReadParty, &[])?;
            self.action(Op::StorePartyVersion, &[])?;
        }
        self.call(Op::PartyData, &[])
    }
    fn best(&self, threshold: f64) -> Result {
        let count = self.scalar(Op::MatchNamesCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::FindMatch, &[index as f64, threshold])?
                && (!self.flag(Op::ResultExists, &[])?
                    || self.scalar(Op::CandidateConfidence, &[])?
                        > self.scalar(Op::ResultConfidence, &[])?)
            {
                self.action(Op::StoreMatch, &[])?;
            }
        }
        Ok(())
    }
    fn character(&self, index: f64) -> Result {
        if !(0. ..=2.).contains(&index) {
            return self.fail(1.);
        }
        self.action(Op::CharacterArea, &[index])?;
        if self.flag(Op::PreviousExists, &[])? && self.scalar(Op::PreviousConfidence, &[])? > 0.92 {
            let count = self.scalar(Op::DefinitionsCount, &[])? as usize;
            for definition in 0..count {
                self.action(Op::SelectDefinition, &[definition as f64])?;
                if self.flag(Op::DefinitionHasPrevious, &[])?
                    && self.flag(Op::FindDefinitionBest, &[0.6])?
                {
                    self.action(Op::BuildResultIdentity, &[])?;
                    return Ok(());
                }
            }
        }
        let count = self.scalar(Op::DefinitionsCount, &[])? as usize;
        for definition in 0..count {
            self.action(Op::SelectDefinition, &[definition as f64])?;
            if self.flag(Op::FindDefinitionBest, &[0.6])?
                && (!self.flag(Op::ResultIdentityExists, &[])?
                    || self.scalar(Op::CandidateConfidence, &[])?
                        > self.scalar(Op::IdentityConfidence, &[])?)
            {
                self.action(Op::BuildResultIdentity, &[])?;
            }
        }
        if !self.flag(Op::ResultIdentityExists, &[])?
            || self.scalar(Op::IdentityConfidence, &[])? < 0.75
        {
            let mut second = 0_f64;
            let count = self.scalar(Op::DefinitionsCount, &[])? as usize;
            for definition in 0..count {
                self.action(Op::SelectDefinition, &[definition as f64])?;
                let mut score = 0_f64;
                let templates = self.scalar(Op::TemplateCount, &[])? as usize;
                for template in 0..templates {
                    if self.flag(Op::FindSoftTemplate, &[template as f64, 0.75, 0.9])? {
                        score = score.max(self.scalar(Op::SoftConfidence, &[])?);
                    }
                }
                if self.flag(Op::SoftResultExists, &[])?
                    && score <= self.scalar(Op::SoftResultConfidence, &[])?
                {
                    second = second.max(score);
                } else if score > 0. {
                    if self.flag(Op::SoftResultExists, &[])? {
                        second = second.max(self.scalar(Op::SoftResultConfidence, &[])?);
                    }
                    self.action(Op::StoreSoftIdentity, &[score])?;
                }
            }
            if self.flag(Op::SoftResultExists, &[])?
                && self.scalar(Op::SoftResultConfidence, &[])? >= 0.82
                && self.scalar(Op::SoftResultConfidence, &[])? - second >= 0.04
                && (!self.flag(Op::ResultIdentityExists, &[])?
                    || self.scalar(Op::SoftResultConfidence, &[])?
                        > self.scalar(Op::IdentityConfidence, &[])?)
            {
                self.action(Op::AdoptSoftIdentity, &[])?;
            }
        }
        Ok(())
    }
    pub fn run(&self, method: i32, args: &[f64]) -> Result<[f64; 3]> {
        let method = Method::try_from(method)?;
        if args.len() != method.arg_count() {
            return Err(INVALID_ACTION);
        }
        match method {
            Method::SegmentedGauge => {
                if self.flag(Op::ImageEmpty, &[])? || args[0] < 1. {
                    return Ok([0.; 3]);
                }
                self.action(Op::MakeGaugeMask, &[])?;
                return self.call(Op::AnalyzeGauge, args);
            }
            Method::Initialize => self.initialize()?,
            Method::Party => return self.party(),
            Method::Best => self.best(args[0])?,
            Method::Character => self.character(args[0])?,
            Method::CdX => {
                return Ok([
                    1. - (1. - args[2]) * args[0].min(args[1] * 16. / 9.) / args[0],
                    0.,
                    0.,
                ])
            }
            Method::Cooldown => {
                if args[0] == 3. || !self.flag(Op::CooldownExists, &[args[1]])? {
                    return Ok([0.; 3]);
                }
                return Ok([
                    self.scalar(Op::CooldownSeconds, args)?
                        - self.scalar(Op::ObservedElapsed, args)?,
                    0.,
                    0.,
                ]);
            }
            Method::RunDialog => {
                return Ok([self.run_dialog()? as u8 as f64, 0., 0.]);
            }
            Method::LevelConfirmed => {
                return Ok([self.level_confirmed()? as u8 as f64, 0., 0.]);
            }
            Method::RefreshCooldowns => {
                self.refresh_cooldowns(args[0])?;
            }
            Method::Scaled => {
                self.scaled(args)?;
            }
            Method::Target => {
                return self.target(args[0] != 0., args[1] != 0.);
            }
            Method::Health => {
                return Ok([self.health(args[0] != 0.)? as u8 as f64, 0., 0.]);
            }
            Method::Death => {
                return Ok([self.death()? as u8 as f64, 0., 0.]);
            }
            Method::Challenge => {
                return self.challenge();
            }
            Method::Forte => {
                return Ok([self.forte(args[0])? as u8 as f64, 0., 0.]);
            }
            Method::White => {
                return Ok([self.white()?, 0., 0.]);
            }
            Method::Rectangles => {
                self.rectangles(args)?;
            }
            Method::ShiftedCandidates => {
                self.shifted_candidates()?;
            }
            Method::ShiftedHealth => {
                return Ok([self.shifted_health()? as u8 as f64, 0., 0.]);
            }
            Method::Reset => {
                self.action(Op::ClearCooldowns, &[])?;
                self.action(Op::ResetPartyVersion, &[])?;
                self.action(Op::ResetCooldownVersion, &[])?;
            }
        }
        Ok([0.; 3])
    }
}
