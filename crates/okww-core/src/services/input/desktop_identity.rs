//! Saved desktop identities: preserve pinned handles, uniqueness and validation rules.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    pub(in crate::services) fn run_desktop_identity(&self, method: Method) -> Result<[f64; 3]> {
        match method {
            Method::DesktopIdentityCreate => {
                if self.flag(Op::DesktopIdentityIsMonitor, &[])? {
                    self.action(Op::DesktopIdentitySearchName, &[])?;
                    self.action(Op::DesktopIdentityEnumerateMonitors, &[])?;
                    self.action(Op::DesktopIdentityStoreMonitor, &[])?;
                    if !self.flag(Op::DesktopIdentityHasMonitor, &[])? {
                        self.action(Op::DesktopIdentityFailure, &[0.])?;
                        return Err(HOST_ERROR);
                    }
                } else if self.flag(Op::DesktopIdentityIsWindow, &[])? {
                    self.action(Op::DesktopIdentityReadPinned, &[])?;
                    if self.flag(Op::DesktopIdentityHasPinned, &[])? {
                        self.action(Op::DesktopIdentityReadPinnedProcess, &[])?;
                    }
                    if self.flag(Op::DesktopIdentityHasPinned, &[])?
                        && self.flag(Op::DesktopIdentityPinnedIsWindow, &[])?
                        && self.flag(Op::DesktopIdentityPinnedProcessMatches, &[])?
                        && self.flag(Op::DesktopIdentityPinnedClassMatches, &[])?
                        && self.flag(Op::DesktopIdentityPinnedExeMatches, &[])?
                    {
                        self.action(Op::DesktopIdentityStorePinnedWindow, &[])?;
                        self.action(Op::DesktopIdentityStorePinnedProcess, &[])?;
                        self.action(Op::DesktopIdentityValidateRect, &[])?;
                        return Ok([0.; 3]);
                    }
                    self.action(Op::DesktopIdentityListWindows, &[])?;
                    let count = self.scalar(Op::DesktopIdentityWindowCount, &[])? as usize;
                    for index in 0..count {
                        self.action(Op::DesktopIdentitySelectWindow, &[index as f64])?;
                        if !self.flag(Op::DesktopIdentityMatchesCandidate, &[])? {
                            self.action(Op::DesktopIdentityDropCandidate, &[])?;
                            continue;
                        }
                        self.action(Op::DesktopIdentityAppendCandidate, &[])?;
                        if self.flag(Op::DesktopIdentitySameHandle, &[])?
                            && self.flag(Op::DesktopIdentitySameProcess, &[])?
                        {
                            self.action(Op::DesktopIdentityKeepCandidate, &[])?;
                            self.action(Op::DesktopIdentityDropCandidate, &[])?;
                            break;
                        }
                        self.action(Op::DesktopIdentityDropCandidate, &[])?;
                    }
                    self.action(Op::DesktopIdentityDropWindows, &[])?;
                    if self.scalar(Op::DesktopIdentityCandidateCount, &[])? != 1. {
                        self.action(Op::DesktopIdentityFailure, &[1.])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::DesktopIdentityUniqueCandidate, &[])?;
                    self.action(Op::DesktopIdentityStoreCandidateWindow, &[])?;
                    self.action(Op::DesktopIdentityStoreCandidateProcess, &[])?;
                    self.action(Op::DesktopIdentityDropCandidate, &[])?;
                    self.action(Op::DesktopIdentityDropCandidates, &[])?;
                } else {
                    self.action(Op::DesktopIdentityFailure, &[2.])?;
                    return Err(HOST_ERROR);
                }
                self.action(Op::DesktopIdentityValidateRect, &[])?;
            }
            Method::DesktopIdentityMonitor => {
                if self.flag(Op::DesktopIdentityMonitorInfo, &[])?
                    && self.flag(Op::DesktopIdentityMonitorNameMatches, &[])?
                {
                    self.action(Op::DesktopIdentityMonitorSelect, &[])?;
                }
            }
            Method::DesktopIdentityRect => {
                if self.flag(Op::DesktopIdentityRectHasWindow, &[])? {
                    self.action(Op::DesktopIdentityRectReadProcess, &[])?;
                    if !self.flag(Op::DesktopIdentityRectIsWindow, &[])?
                        || !self.flag(Op::DesktopIdentityRectProcessMatches, &[])?
                        || !self.flag(Op::DesktopIdentityRectClassMatches, &[])?
                    {
                        self.action(Op::DesktopIdentityFailure, &[3.])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::DesktopIdentityRectClient, &[])?;
                } else {
                    if !self.flag(Op::DesktopIdentityRectHasMonitor, &[])?
                        || !self.flag(Op::DesktopIdentityRectMonitorInfo, &[])?
                    {
                        self.action(Op::DesktopIdentityFailure, &[4.])?;
                        return Err(HOST_ERROR);
                    }
                    self.action(Op::DesktopIdentityRectMonitor, &[])?;
                }
            }
            Method::DesktopIdentityForeground => {
                return Ok([
                    f64::from(
                        self.flag(Op::DesktopIdentityRectHasWindow, &[])?
                            && self.flag(Op::DesktopIdentityRectForeground, &[])?,
                    ),
                    0.,
                    0.,
                ]);
            }
            Method::DesktopIdentityMatch => {
                return Ok([
                    f64::from(
                        self.flag(Op::DesktopIdentityCandidateExeMatches, &[])?
                            && self.flag(Op::DesktopIdentityCandidateTitleMatches, &[])?
                            && self.flag(Op::DesktopIdentityCandidateClassMatches, &[])?,
                    ),
                    0.,
                    0.,
                ]);
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
