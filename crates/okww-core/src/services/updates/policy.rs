//! Release filtering, checksum selection and update metadata policies.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn update_policy_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::UpdatePolicyFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_update_policy(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::UpdateTrustedDownload => {
                if !self.flag(Op::UpdateUrlHttps, &[])?
                    || !self.flag(Op::UpdateUrlUserInfoEmpty, &[])?
                {
                    return Ok([0.; 3]);
                }
                let port = self.scalar(Op::UpdateUrlPort, &[])?;
                if port != -1. && self.scalar(Op::UpdateUrlPort, &[])? != 443. {
                    return Ok([0.; 3]);
                }
                return self.call(Op::UpdateUrlTrustedHost, &[]);
            }
            Method::UpdateNewerReleases => {
                self.action(Op::UpdateParseInstalledVersion, &[])?;
                if !self.flag(Op::UpdateInstalledValid, &[])? {
                    return self.update_policy_fail(0.);
                }
                let n = self.scalar(Op::UpdateReleaseCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::UpdateSelectRelease, &[i as f64])?;
                    self.action(Op::UpdateReadTag, &[])?;
                    self.action(Op::UpdateParseTagVersion, &[])?;
                    if !self.flag(Op::UpdateTagValid, &[])?
                        || self.flag(Op::UpdateReleaseDraft, &[])?
                        || self.flag(Op::UpdatePublicationEmpty, &[])?
                        || (args[0] == 0.
                            && (self.flag(Op::UpdateReleasePreview, &[])?
                                || self.flag(Op::UpdateVersionHasPreview, &[])?))
                        || self.scalar(Op::UpdateCompareInstalled, &[])? <= 0.
                        || self.flag(Op::UpdateTagSeen, &[])?
                    {
                        continue;
                    }
                    self.action(Op::UpdateCreateRelease, &[])?;
                    let preview = self.flag(Op::UpdateReleasePreview, &[])?
                        || self.flag(Op::UpdateVersionHasPreview, &[])?;
                    self.action(Op::UpdateAssignPreview, &[f64::from(preview)])?;
                    self.action(Op::UpdateAssetBaseUrl, &[])?;
                    let n = self.scalar(Op::UpdateAssetCount, &[])? as usize;
                    for j in 0..n {
                        self.action(Op::UpdateSelectAsset, &[j as f64])?;
                        if !self.flag(Op::UpdateAssetUploaded, &[])? {
                            continue;
                        }
                        self.action(Op::UpdateReadAssetName, &[])?;
                        self.action(Op::UpdateReadAssetUrl, &[])?;
                        if !self.flag(Op::UpdateAssetUrlMatches, &[])? {
                            continue;
                        }
                        if self.flag(Op::UpdateAssetPackageName, &[])? {
                            self.action(Op::UpdateAssignPackageUrl, &[])?;
                            self.action(Op::UpdateAssignPackageSize, &[])?;
                            self.action(Op::UpdateReadAssetDigest, &[])?;
                            if self.flag(Op::UpdateDigestSha256Prefix, &[])? {
                                self.action(Op::UpdateAssignPackageDigest, &[])?;
                            }
                        } else if self.flag(Op::UpdateAssetChecksumsName, &[])? {
                            self.action(Op::UpdateAssignChecksumsUrl, &[])?;
                        }
                    }
                    self.action(Op::UpdateRememberTag, &[])?;
                    self.action(Op::UpdateAppendRelease, &[])?;
                }
                self.action(Op::UpdateSortReleases, &[])?;
            }
            Method::UpdateSummary => {
                self.action(Op::UpdateSplitSummary, &[])?;
                let n = self.scalar(Op::UpdateSummaryLineCount, &[])? as usize;
                let mut selected = 0;
                for i in 0..n {
                    self.action(Op::UpdateSelectSummaryLine, &[i as f64])?;
                    self.action(Op::UpdateTrimSummaryLine, &[])?;
                    if self.flag(Op::UpdateSummaryLineEmpty, &[])? {
                        continue;
                    }
                    let mut skip = false;
                    for j in 0..6 {
                        if self.flag(Op::UpdateSummaryIgnoredPrefix, &[f64::from(j)])? {
                            skip = true;
                            break;
                        }
                    }
                    if skip {
                        continue;
                    }
                    self.action(Op::UpdateAppendSummaryLine, &[240.])?;
                    selected += 1;
                    if selected == 6 {
                        break;
                    }
                }
                if selected == 0 {
                    self.action(Op::UpdateReturnEmptySummary, &[])?;
                } else {
                    self.action(Op::UpdateJoinSummary, &[])?;
                }
            }
            Method::UpdatePackageChecksum => {
                self.action(Op::UpdateCreateChecksumPattern, &[])?;
                self.action(Op::UpdateSplitChecksumText, &[])?;
                let n = self.scalar(Op::UpdateChecksumLineCount, &[])? as usize;
                let mut found = false;
                for i in 0..n {
                    self.action(Op::UpdateMatchChecksumLine, &[i as f64])?;
                    if !self.flag(Op::UpdateChecksumLineMatches, &[])? {
                        continue;
                    }
                    if found {
                        return self.update_policy_fail(1.);
                    }
                    self.action(Op::UpdateAssignChecksum, &[])?;
                    found = true;
                }
                if !found {
                    return self.update_policy_fail(2.);
                }
            }
            Method::UpdateInstallationBusy => {
                self.action(Op::UpdateOpenInstallLock, &[])?;
                if self.flag(Op::UpdateInstallLockOpened, &[])? {
                    self.action(Op::UpdateCloseInstallLock, &[])?;
                    return Ok([0.; 3]);
                }
                let error = self.scalar(Op::UpdateInstallLockError, &[])? as u32;
                return Ok([f64::from(matches!(error, 5 | 32 | 33)), 0., 0.]);
            }
            Method::UpdateReleaseInstallable => {
                let installable = !self.flag(Op::UpdatePackageUrlEmpty, &[])?
                    && !self.flag(Op::UpdateChecksumsUrlEmpty, &[])?
                    && args[0] > 0.;
                return Ok([f64::from(installable), 0., 0.]);
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
