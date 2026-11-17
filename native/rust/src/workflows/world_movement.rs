use super::*;
impl Host {
    fn world_walk_interact(&self, a: &[f64]) -> Result<bool> {
        if self.flag(Op::WorldFindRequestedInteract, &[])? {
            return Ok(true);
        }
        self.action(Op::WorldMiddleClick, &[0.01])?;
        self.action(Op::Sleep, &[0.2])?;
        if a[2] > 0. && self.flag(Op::WorldWalkInteractUntil, &[1., a[2], a[4], a[1], a[3]])? {
            return Ok(true);
        }
        self.flag(Op::WorldWalkInteractUntil, &[0., a[0], a[4], a[1], a[3]])
    }
    fn world_walk_echo(&self, timeout: f64, threshold: f64) -> Result<bool> {
        self.action(Op::CreateWorldMovement, &[])?;
        let end = self.scalar(Op::Now, &[])? + timeout;
        let mut no_echo = -1.;
        let mut moving = false;
        while self.scalar(Op::Now, &[])? < end {
            self.action(Op::Sleep, &[0.001])?;
            if self.flag(Op::WorldPickDefault, &[])? {
                self.action(Op::WorldMovementStop, &[])?;
                return Ok(true);
            }
            if self.flag(Op::WorldHasCombat, &[])? && self.flag(Op::WorldCombat, &[])? {
                self.action(Op::WorldMovementStop, &[])?;
                return Ok(false);
            }
            self.action(Op::WorldReadEchoes, &[threshold])?;
            if self.scalar(Op::WorldEchoCount, &[])? == 0. {
                if no_echo < 0. {
                    no_echo = self.scalar(Op::Now, &[])?;
                } else if self.scalar(Op::Now, &[])? - no_echo > 3. {
                    break;
                }
            } else {
                no_echo = -1.;
            }
            self.action(Op::WorldMoveEchoDirection, &[moving as u8 as f64])?;
            moving = true;
            if self.flag(Op::WorldHasUpdate, &[])? {
                self.action(Op::WorldUpdate, &[])?;
            }
        }
        self.action(Op::WorldMovementStop, &[])?;
        Ok(false)
    }
    fn world_find_echo(&self, a: &[f64]) -> Result<[f64; 3]> {
        if self.flag(Op::WorldPickEchoOnly, &[])? {
            self.action(Op::Sleep, &[0.5])?;
            return Ok([1., 1., 0.]);
        }
        let mut maximum = 0.;
        for index in 0..4 {
            if a[1] != 0. {
                self.action(Op::WorldCenterCamera, &[])?;
            }
            self.action(Op::WorldReadEchoes, &[a[3]])?;
            let count = self.scalar(Op::WorldEchoCount, &[])?;
            maximum = f64::max(maximum, count);
            if count != 0. {
                return Ok([
                    self.scalar(Op::WorldWalkEcho, &[a[2], 0.5])?,
                    (maximum > 1.) as u8 as f64,
                    0.,
                ]);
            }
            self.action(Op::WorldReadFrontBox, &[])?;
            if a[0] != 0. && self.scalar(Op::WorldFrontColor, &[])? > 0.02 {
                return Ok([
                    self.scalar(Op::WorldWalkEcho, &[8., 0.5])?,
                    (maximum > 1.) as u8 as f64,
                    0.,
                ]);
            }
            if a[1] == 0. && index == 0 {
                return Ok([0., (maximum > 1.) as u8 as f64, 0.]);
            }
            self.action(Op::WorldTapDirection, &[1., 0.5, 0.05])?;
        }
        self.action(Op::WorldCenterCamera, &[])?;
        Ok([0., (maximum > 1.) as u8 as f64, 0.])
    }
    fn world_walk_box(&self, a: &[f64]) -> Result<bool> {
        if !self.flag(Op::WorldHasFind, &[])? || !a[0].is_finite() || a[0] <= 0. || a[0] > 180. {
            self.action(Op::WorldMovementFailure, &[0.])?;
        }
        let deadline = self.scalar(Op::Now, &[])? + a[0];
        if self.flag(Op::WorldHasEnd, &[])?
            && !self.wait_for(
                || Ok(self.flag(Op::WorldEnd, &[])? || self.flag(Op::WorldProbeFind, &[])?),
                a[0],
            )?
        {
            return Ok(false);
        }
        self.action(Op::CreateWorldMovement, &[])?;
        let mut last = -1;
        let mut centered = false;
        let mut ended = false;
        let mut running = false;
        while self.scalar(Op::Now, &[])? < deadline {
            self.action(Op::Sleep, &[0.001])?;
            if self.flag(Op::WorldHasEnd, &[])? && self.flag(Op::WorldEnd, &[])? {
                ended = true;
                break;
            }
            self.action(Op::WorldReadFind, &[])?;
            if self.flag(Op::WorldHasCandidate, &[])? {
                self.action(Op::WorldStoreWalkTarget, &[])?;
            }
            let next = if !self.flag(Op::WorldHasWalkTarget, &[])? {
                match last {
                    0 => 2,
                    2 => 0,
                    1 => 3,
                    3 => 1,
                    _ => 0,
                }
            } else {
                let [x, y, height] = self.call(Op::WorldWalkTargetPoint, &[])?;
                let width = self.scalar(Op::WorldFrameWidth, &[])?;
                let y = (y - height * a[1]).max(0.);
                let dx = x - width * 0.5;
                centered = centered || dx.abs() <= width * if last < 0 { 0.04 } else { a[2] };
                if !centered {
                    if dx > 0. {
                        3
                    } else {
                        1
                    }
                } else {
                    let center = match last {
                        2 => 0.45,
                        0 => 0.6,
                        _ => 0.5,
                    };
                    if y > height * center {
                        2
                    } else {
                        0
                    }
                }
            };
            self.action(Op::WorldMoveDirectionId, &[next as f64])?;
            last = next;
            if running {
                if !self.flag(Op::WorldOnWall, &[])? {
                    self.action(Op::WorldMovementStopRunning, &[])?;
                    running = false;
                }
            } else if next == 0 && self.flag(Op::WorldOnWall, &[])? {
                self.action(Op::WorldMovementRun, &[])?;
                running = true;
            }
            if a[3] != 0. && next == 0 && self.flag(Op::WorldHookFound, &[])? {
                self.action(Op::WorldUseHook, &[])?;
            }
        }
        self.action(Op::WorldMovementStop, &[])?;
        Ok(if self.flag(Op::WorldHasEnd, &[])? {
            ended
        } else {
            last >= 0
        })
    }
    fn world_follow_boss(&self, timeout: f64, threshold: f64) -> Result<bool> {
        if !self.flag(Op::WorldHasCombat, &[])? {
            self.action(Op::WorldMovementFailure, &[2.])?;
        }
        self.action(Op::WorldCenterCamera, &[])?;
        self.action(Op::CreateWorldMovement, &[])?;
        let mut forward = false;
        let end = self.scalar(Op::Now, &[])? + timeout;
        while self.scalar(Op::Now, &[])? < end {
            self.action(Op::Sleep, &[0.01])?;
            if self.flag(Op::WorldCombat, &[])? {
                self.action(Op::WorldMovementStop, &[])?;
                return Ok(true);
            }
            let angle = self.scalar(Op::WorldBossMinimapTurn, &[threshold])?;
            if forward && ((10. ..=80.).contains(&angle) || (-80. ..=-10.).contains(&angle)) {
                let key = if angle > 0. { 3. } else { 1. };
                self.action(Op::WorldDirectionDown, &[key])?;
                let tapping = (|| {
                    self.action(Op::Sleep, &[0.1])?;
                    self.action(Op::WorldMiddleClick, &[0.1])?;
                    self.action(Op::WorldDirectionUp, &[key])?;
                    self.action(Op::Sleep, &[0.01])
                })();
                if tapping.is_err() {
                    self.action(Op::ClearException, &[])?;
                    self.action(Op::WorldReleaseInput, &[])?;
                    self.action(Op::RethrowHandled, &[])?;
                    return Err(HOST_ERROR);
                }
                continue;
            }
            let direction = if (-45. ..=45.).contains(&angle) {
                0
            } else if angle > 45. && angle <= 135. {
                3
            } else if angle > -135. && angle <= -45. {
                1
            } else {
                2
            };
            if !forward || direction != 0 {
                if forward {
                    self.action(Op::WorldMovementStop, &[])?;
                    self.wait_for(|| self.flag(Op::WorldCombat, &[]), 0.2)?;
                }
                if direction != 0 {
                    self.action(Op::WorldTapDirection, &[direction as f64, 0.5, 0.05])?;
                }
                self.action(Op::WorldCenterCamera, &[])?;
                self.action(Op::WorldMoveDirectionId, &[0.])?;
                self.wait_for(|| self.flag(Op::WorldCombat, &[]), 0.2)?;
                self.action(Op::WorldMovementRun, &[])?;
                forward = true;
                self.wait_for(|| self.flag(Op::WorldCombat, &[]), 1.)?;
            }
        }
        self.action(Op::WorldMovementStop, &[])?;
        self.flag(Op::WorldCombat, &[])
    }
    pub(super) fn run_world_movement(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let result = match method {
            Method::WorldWalkInteract => self.world_walk_interact(a)? as u8 as f64,
            Method::WorldInteractCondition => {
                (self.flag(Op::WorldFindRequestedInteract, &[])?
                    || (a[0] != 0.
                        && self.flag(Op::WorldHasCombat, &[])?
                        && self.flag(Op::WorldCombat, &[])?)) as u8 as f64
            }
            Method::WorldWalkEcho => self.world_walk_echo(a[0], a[1])? as u8 as f64,
            Method::WorldFindEcho => return self.world_find_echo(a),
            Method::WorldWalkBox => self.world_walk_box(a)? as u8 as f64,
            Method::WorldWalkTreasure => {
                if !self.flag(Op::WorldWalkClaimTreasure, &[])?
                    && !self.flag(Op::WorldWalkAnyTreasure, &[])?
                {
                    self.action(Op::WorldMovementFailure, &[1.])?;
                }
                if a[0] != 0. {
                    self.action(Op::WorldRequireTreasureInteract, &[])?;
                }
                self.action(Op::Sleep, &[1.])?;
                0.
            }
            Method::WorldFollowBoss => self.world_follow_boss(a[0], a[1])? as u8 as f64,
            Method::WorldEchoDirection => {
                if self.scalar(Op::WorldEchoCount, &[])? == 0. {
                    return Ok([0.; 3]);
                }
                let [x, bottom, height] = self.call(Op::WorldFirstEchoGeometry, &[])?;
                let width = self.scalar(Op::WorldFrameWidth, &[])?;
                let dx = x - width * 0.5;
                if dx.abs() < height * if a[0] != 0. { 0.15 } else { 0.05 } {
                    if bottom > height * 0.65 {
                        2.
                    } else {
                        0.
                    }
                } else if dx > 0. {
                    3.
                } else {
                    1.
                }
            }
            _ => return Err(INVALID_ACTION),
        };
        Ok([result, 0., 0.])
    }
}
