//! Boundary validation and atomic application of Python character state.
use super::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn workshop_state_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::WorkshopStateFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(super) fn run_workshop_state(&self, method: Method, args: &[f64]) -> Result<[f64; 3]> {
        match method {
            Method::WorkshopFinite => {
                if !self.flag(Op::WorkshopValueDouble, &[])? {
                    return self.workshop_state_fail(0.);
                }
                let number = self.scalar(Op::WorkshopValueNumber, &[])?;
                if !number.is_finite() {
                    return self.workshop_state_fail(0.);
                }
                return Ok([number, 0., 0.]);
            }
            Method::WorkshopBoolean => {
                if !self.flag(Op::WorkshopValueBool, &[])? {
                    return self.workshop_state_fail(1.);
                }
                return self.call(Op::WorkshopValueBoolean, &[]);
            }
            Method::WorkshopPercentile => {
                let n = self.scalar(Op::WorkshopSampleCount, &[])? as usize;
                if n == 0 {
                    return Ok([0.; 3]);
                }
                self.action(Op::WorkshopSortSamples, &[])?;
                return self.call(
                    Op::WorkshopSampleAt,
                    &[((n as f64 * args[0]).ceil() - 1.).min((n - 1) as f64)],
                );
            }
            Method::WorkshopCombatState => {
                let now = self.scalar(Op::WorkshopHostNow, &[])?;
                self.action(Op::WorkshopLoadParty, &[])?;
                let n = self.scalar(Op::WorkshopPartyCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::WorkshopSelectCharacter, &[i as f64])?;
                    self.action(Op::WorkshopCharacterSnapshot, &[])?;
                    for j in 0..9 {
                        let time = self.scalar(Op::WorkshopCharacterTime, &[f64::from(j)])?;
                        self.action(
                            Op::WorkshopSnapshotTime,
                            &[f64::from(j), if time < 0. { -1. } else { now - time }],
                        )?;
                    }
                    self.action(Op::WorkshopAppendCharacter, &[])?;
                }
                let n = self.scalar(Op::WorkshopFreezeCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::WorkshopAppendFreezeSnapshot, &[i as f64, now])?;
                }
                let start = self.scalar(Op::WorkshopCombatStart, &[])?;
                self.action(
                    Op::WorkshopReturnCombatState,
                    &[if start < 0. { -1. } else { now - start }],
                )?;
            }
            Method::WorkshopApplyState => {
                self.action(Op::WorkshopLoadParty, &[])?;
                self.action(Op::WorkshopLoadCharacterValues, &[])?;
                let n = self.scalar(Op::WorkshopCharacterValueCount, &[])? as usize;
                let party = self.scalar(Op::WorkshopPartyCount, &[])? as usize;
                if n != party {
                    return self.workshop_state_fail(2.);
                }
                let now = self.scalar(Op::WorkshopHostNow, &[])?;
                for i in 0..n {
                    self.action(Op::WorkshopSelectCharacterValue, &[i as f64])?;
                    let index = self.scalar(Op::WorkshopReadFiniteIndex, &[])?;
                    let mut found = false;
                    for j in 0..party {
                        if self.scalar(Op::WorkshopPartyIndex, &[j as f64])? == index {
                            self.action(Op::WorkshopSelectCharacter, &[j as f64])?;
                            found = true;
                            break;
                        }
                    }
                    if !found || self.flag(Op::WorkshopIndexSeen, &[index])? {
                        return self.workshop_state_fail(3.);
                    }
                    self.action(Op::WorkshopMarkIndex, &[index])?;
                    self.action(Op::WorkshopCopyCharacterState, &[])?;
                    self.action(Op::WorkshopReadRole, &[])?;
                    if !self.flag(Op::WorkshopRoleValid, &[])? {
                        return self.workshop_state_fail(4.);
                    }
                    self.action(Op::WorkshopAssignRole, &[])?;
                    for j in 0..3 {
                        self.action(Op::WorkshopReadStateFlag, &[f64::from(j)])?;
                    }
                    let con = self.scalar(Op::WorkshopReadConcerto, &[])?;
                    if !(0. ..=1.).contains(&con) {
                        return self.workshop_state_fail(5.);
                    }
                    let buff = self.scalar(Op::WorkshopReadBuffTime, &[])?;
                    if !(0. ..=86400.).contains(&buff) {
                        return self.workshop_state_fail(6.);
                    }
                    for j in 0..9 {
                        let age = self.scalar(Op::WorkshopReadStateAge, &[f64::from(j)])?;
                        self.action(
                            Op::WorkshopAssignStateTime,
                            &[f64::from(j), if age < 0. { -1. } else { now - age }],
                        )?;
                    }
                    self.action(Op::WorkshopPrepareCharacter, &[])?;
                    self.action(Op::WorkshopPrepareCheckFlag, &[])?;
                }
                self.action(Op::WorkshopLoadFreezeValues, &[])?;
                let n = self.scalar(Op::WorkshopFreezeValueCount, &[])? as usize;
                if n > 2048 {
                    return self.workshop_state_fail(7.);
                }
                for i in 0..n {
                    self.action(Op::WorkshopSelectFreezeValue, &[i as f64])?;
                    let age = self.scalar(Op::WorkshopReadFreezeNumber, &[0.])?;
                    let duration = self.scalar(Op::WorkshopReadFreezeNumber, &[1.])?;
                    let threshold = self.scalar(Op::WorkshopReadFreezeNumber, &[2.])?;
                    if !(0. ..=600.).contains(&duration) {
                        return self.workshop_state_fail(8.);
                    }
                    self.action(
                        Op::WorkshopPrepareFreeze,
                        &[now, now - age, duration, threshold],
                    )?;
                }
                let age = self.scalar(Op::WorkshopReadCombatAge, &[])?;
                self.action(Op::WorkshopReadInLiberation, &[])?;
                for i in 0..party {
                    self.action(Op::WorkshopCommitCharacter, &[i as f64])?;
                }
                self.action(Op::WorkshopCommitFreezes, &[])?;
                self.action(
                    Op::WorkshopCommitCombatStart,
                    &[if age < 0. { -1. } else { now - age }],
                )?;
                self.action(Op::WorkshopCommitInLiberation, &[])?;
            }
            Method::WorkshopValidateHandshake => {
                if !self.flag(Op::WorkshopHelloReady, &[])?
                    || self.scalar(Op::WorkshopHelloProtocol, &[])? != 2.
                    || self.scalar(Op::WorkshopHelloApi, &[])? != 1.
                {
                    return self.workshop_state_fail(9.);
                }
                self.action(Op::WorkshopReadCapabilities, &[])?;
                for i in 0..6 {
                    if !self.flag(Op::WorkshopHasCapability, &[f64::from(i)])? {
                        return self.workshop_state_fail(10.);
                    }
                }
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
