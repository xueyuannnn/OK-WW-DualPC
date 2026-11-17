//! Team identity and file policies. Qt containers retain their original ordering.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn team_store_fail<T>(&self, code: f64) -> Result<T> {
        self.action(Op::TeamStoreFailure, &[code])?;
        Err(HOST_ERROR)
    }
    pub(in crate::services) fn run_team_store(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::TeamStoreBytes => {
                self.action(Op::TeamStoreCreateReadFile, &[])?;
                if !self.flag(Op::TeamStoreOpenReadFile, &[])?
                    || self.scalar(Op::TeamStoreReadFileSize, &[])? > args[0]
                {
                    return self.team_store_fail(0.);
                }
                self.action(Op::TeamStoreReadAllBytes, &[])?;
            }
            Method::TeamStoreSaveBytes => {
                if !self.flag(Op::TeamStoreMakeFileDirectory, &[])? {
                    return self.team_store_fail(1.);
                }
                self.action(Op::TeamStoreCreateSaveFile, &[])?;
                if !self.flag(Op::TeamStoreOpenSaveFile, &[])?
                    || !self.flag(Op::TeamStoreWriteAllBytes, &[])?
                    || !self.flag(Op::TeamStoreCommitBytes, &[])?
                {
                    return self.team_store_fail(2.);
                }
            }
            Method::TeamStoreCanonical => {
                self.action(Op::TeamStoreTrimName, &[])?;
                if self.flag(Op::TeamStoreShorekeeperAlias, &[])? {
                    self.action(Op::TeamStoreCanonicalShorekeeper, &[])?;
                }
                if self.flag(Op::TeamStoreLynaeAlias, &[])? {
                    self.action(Op::TeamStoreCanonicalLinnai, &[])?;
                }
            }
            Method::TeamStoreSafeSource => {
                self.action(Op::TeamStoreSplitPath, &[])?;
                let safe = !self.flag(Op::TeamStoreSourceEmpty, &[])?
                    && !self.flag(Op::TeamStoreSourceAbsolute, &[])?
                    && !self.flag(Op::TeamStoreSourceBackslash, &[])?
                    && !self.flag(Op::TeamStoreSourceColon, &[])?
                    && !self.flag(Op::TeamStoreSourceParent, &[])?
                    && !self.flag(Op::TeamStoreSourceCurrent, &[])?
                    && !self.flag(Op::TeamStoreSourceEmptyPart, &[])?
                    && self.flag(Op::TeamStoreSourceExtension, &[])?;
                return Ok([f64::from(safe), 0., 0.]);
            }
            Method::TeamStoreMembers => {
                let kind = self.scalar(Op::TeamStoreMembersValueKind, &[])? as i32;
                if kind == 0 {
                    let n = self.scalar(Op::TeamStoreMembersArrayCount, &[])? as usize;
                    for i in 0..n {
                        self.action(Op::TeamStoreAppendArrayMember, &[i as f64])?;
                    }
                } else if kind == 1 {
                    self.action(Op::TeamStoreSplitMembers, &[])?;
                    let n = self.scalar(Op::TeamStoreSplitMemberCount, &[])? as usize;
                    for i in 0..n {
                        self.action(Op::TeamStoreAppendStringMember, &[i as f64])?;
                    }
                }
                self.action(Op::TeamStoreLoadClasses, &[])?;
                let n = self.scalar(Op::TeamStoreMemberCount, &[])? as usize;
                for i in 0..n {
                    self.action(Op::TeamStoreSelectMember, &[i as f64])?;
                    if !self.flag(Op::TeamStoreClassSupported, &[])?
                        || self.flag(Op::TeamStoreMemberBase, &[])?
                        || self.flag(Op::TeamStoreMemberDuplicate, &[])?
                    {
                        return self.team_store_fail(3.);
                    }
                    self.action(Op::TeamStoreRememberMember, &[])?;
                }
                if n != 3 {
                    return self.team_store_fail(4.);
                }
                self.action(Op::TeamStoreSortMembers, &[])?;
            }
            Method::TeamStoreKey => {
                self.action(Op::TeamStoreSortMembers, &[])?;
                self.action(Op::TeamStoreJoinKey, &[])?;
            }
            Method::TeamStoreConfig => {
                self.action(Op::TeamStoreComputeConfigPath, &[])?;
                if self.flag(Op::TeamStoreConfigExists, &[])? {
                    self.action(Op::TeamStoreReadConfig, &[])?;
                }
            }
            Method::TeamStoreSaveConfig => {
                self.action(Op::TeamStoreAssignConfigTeam, &[])?;
                if !self.flag(Op::TeamStoreCreateConfigDirectory, &[])? {
                    return self.team_store_fail(5.);
                }
                self.action(Op::TeamStoreWriteConfig, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
