use crate::services::*;
impl Host {
    fn world_find_interact(&self) -> Result<bool> {
        self.action(Op::WorldFindInteract, &[])?;
        if !self.flag(Op::WorldHasMatch, &[])? || self.flag(Op::WorldPatternEmpty, &[])? {
            return self.flag(Op::WorldHasMatch, &[]);
        }
        self.wait_for_interval(
            || {
                self.action(Op::WorldReadInteractWhite, &[])?;
                Ok(self.scalar(Op::WorldInteractWhite, &[])? > 0.5)
            },
            1.,
            0.01,
        )?;
        if self.scalar(Op::WorldInteractWhite, &[])? < 0.5 {
            self.action(Op::WorldClearMatch, &[])?;
            return Ok(false);
        }
        self.action(Op::WorldReadInteractText, &[])?;
        if self.flag(Op::WorldHasInteractText, &[])? {
            if self.flag(Op::WorldInteractTextBelow, &[])? {
                self.action(Op::WorldScrollInteract, &[])?;
            }
            return Ok(true);
        }
        self.action(Op::WorldClearMatch, &[])?;
        Ok(false)
    }
    fn world_move_direction(&self) -> Result {
        if self.flag(Op::WorldSameDirection, &[])? {
            return Ok(());
        }
        if !self.flag(Op::WorldOldDirectionEmpty, &[])? {
            self.action(Op::WorldOldDirectionUp, &[])?;
            self.action(Op::WorldClearOldDirection, &[])?;
            self.action(Op::Sleep, &[0.01])?;
        }
        if !self.flag(Op::WorldNewDirectionEmpty, &[])? {
            self.action(Op::WorldNewDirectionDown, &[])?;
            self.action(Op::WorldStoreDirection, &[])?;
        }
        Ok(())
    }
    fn world_walk_until(&self, a: &[f64]) -> Result<bool> {
        let (timeout, running, required, post) = (a[0], a[1] != 0., a[2] != 0., a[3]);
        if !timeout.is_finite() || timeout < 0. || post < 0. {
            self.action(Op::WorldFailure, &[0.])?;
        }
        if timeout == 0. {
            return Ok(false);
        }
        self.action(Op::CreateWorldMovement, &[])?;
        self.action(Op::WorldMovementRequestedDirection, &[])?;
        if running {
            self.action(Op::WorldMovementRun, &[])?;
        }
        let found = self.wait_for_interval(|| self.flag(Op::WorldCondition, &[]), timeout, 0.02)?;
        if found && post > 0. {
            self.action(Op::Sleep, &[post])?;
        }
        self.action(Op::WorldMovementStop, &[])?;
        if !found && required {
            self.action(Op::WorldFailure, &[1.])?;
        }
        Ok(found)
    }
    pub(in crate::services) fn run_world(&self, method: Method, a: &[f64]) -> Result<[f64; 3]> {
        let mut result = [0.; 3];
        match method {
            Method::WorldFSearchBox => {
                let [x, y, w] = self.call(Op::WorldInteractBox, &[])?;
                let h = self.scalar(Op::WorldInteractBoxHeight, &[])?;
                self.action(
                    Op::WorldStoreSearchBox,
                    &[
                        (x - 0.3 * w).round_ties_even(),
                        y - 5. * h,
                        (w * 1.65).round_ties_even(),
                        (h * 7.5).round_ties_even(),
                    ],
                )?;
            }
            Method::WorldFindInteract => {
                result[0] = self.world_find_interact()? as u8 as f64;
            }
            Method::WorldDialogInteract => {
                self.action(Op::WorldReadUnfilteredInteract, &[])?;
                if self.flag(Op::WorldHasMatch, &[])? {
                    self.action(Op::WorldFindDialogDots, &[])?;
                    result[0] = self.flag(Op::WorldHasDialogDots, &[])? as u8 as f64;
                }
            }
            Method::WorldClaimDialog => {
                result[0] = (!self.flag(Op::InTeam, &[])?
                    && self.flag(Op::WorldClaimCancelFound, &[])?) as u8
                    as f64;
            }
            Method::WorldDismissClaim => {
                if self.wait_for(|| self.flag(Op::WorldClaimDialog, &[]), 1.5)? {
                    self.action(Op::Sleep, &[0.5])?;
                    self.action(Op::WorldEscape, &[0.5])?;
                    result[0] = 1.;
                }
            }
            Method::WorldPick => {
                if self.flag(Op::WorldFindPick, &[a[1]])? {
                    self.action(Op::WorldKeyF, &[if a[1] != 0. { 0. } else { 1. }])?;
                    result[0] =
                        (a[0] == 0. || !self.flag(Op::WorldDismissClaim, &[])?) as u8 as f64;
                }
            }
            Method::WorldMoveDirection => self.world_move_direction()?,
            Method::WorldMoveRun => {
                self.action(Op::Sleep, &[0.1])?;
                self.action(Op::WorldRightDown, &[])?;
                self.action(Op::WorldStoreRunning, &[1.])?;
            }
            Method::WorldMoveStopRunning => {
                if self.flag(Op::WorldMovementRunning, &[])? {
                    self.action(Op::WorldRightUp, &[])?;
                    self.action(Op::WorldStoreRunning, &[0.])?;
                }
            }
            Method::WorldMoveStop => {
                self.action(Op::WorldMovementClearDirection, &[])?;
                if self.flag(Op::WorldMovementRunning, &[])? {
                    self.action(Op::Sleep, &[0.1])?;
                    self.action(Op::WorldRightUp, &[])?;
                    self.action(Op::WorldStoreRunning, &[0.])?;
                }
            }
            Method::WorldMoveDrop => {
                if !self.flag(Op::WorldOldDirectionEmpty, &[])?
                    || self.flag(Op::WorldMovementRunning, &[])?
                {
                    self.action(Op::WorldReleaseInput, &[])?;
                }
            }
            Method::WorldEchoes => {
                if !self.flag(Op::WorldHasDetector, &[])? {
                    self.action(Op::WorldFailure, &[2.])?;
                }
                self.action(Op::Checkpoint, &[])?;
                self.action(Op::WorldDetectEchoes, &[a[0]])?;
                self.action(Op::Checkpoint, &[])?;
                let count = self.scalar(Op::WorldEchoCount, &[])? as usize;
                for index in 0..count {
                    self.action(Op::WorldFlattenEcho, &[index as f64])?;
                }
            }
            Method::WorldCenterCamera => {
                self.action(Op::WorldCenterMiddleClick, &[])?;
                self.action(Op::Sleep, &[1.])?;
            }
            Method::WorldCircleEcho => {
                let circles = a[0] as i32;
                if !(0..=20).contains(&circles) {
                    self.action(Op::WorldFailure, &[3.])?;
                }
                let mut index = 0;
                let mut duration = 0.8;
                for count in 0..circles {
                    for key in 0..4 {
                        if index > 2 && (index + 1) % 2 == 0 && !(count == circles - 1 && key == 3)
                        {
                            duration += 0.8;
                        }
                        if self.flag(Op::WorldCircleWalk, &[key as f64, duration])?
                            && self.flag(Op::WorldPickDefault, &[])?
                        {
                            return Ok([1., 0., 0.]);
                        }
                        index += 1;
                    }
                }
            }
            Method::WorldTreasure => self.action(Op::WorldFindTreasure, &[])?,
            Method::WorldArrowAngle => {
                self.action(Op::WorldReadArrowBox, &[])?;
                let mut best = 0.;
                let mut angle = 0.;
                for index in 0..360 {
                    self.action(Op::Checkpoint, &[])?;
                    self.action(Op::WorldFindRotatedArrow, &[index as f64])?;
                    if self.flag(Op::WorldHasMatch, &[])? {
                        let confidence = self.scalar(Op::WorldMatchConfidence, &[])?;
                        if confidence > best {
                            best = confidence;
                            angle = index as f64;
                        }
                    }
                }
                result[0] = angle;
            }
            Method::WorldMiniMapTurn => {
                self.action(Op::WorldReadMinimapBox, &[])?;
                self.action(Op::WorldFindMinimapTarget, &[a[0]])?;
                if !self.flag(Op::WorldHasMatch, &[])? {
                    return Ok([0., 0., 0.]);
                }
                let [dx, dy, _] = self.call(Op::WorldMinimapDelta, &[a[1], a[2]])?;
                let mut angle = dy.atan2(dx) * 180. / std::f64::consts::PI;
                if angle < 0. {
                    angle += 360.;
                }
                let mut turn = angle - self.scalar(Op::WorldReadArrowAngle, &[])?;
                if turn > 180. {
                    turn -= 360.;
                } else if turn < -180. {
                    turn += 360.;
                }
                result = [turn, 1., 0.];
            }
            Method::WorldWalkUntil => result[0] = self.world_walk_until(a)? as u8 as f64,
            _ => return Err(INVALID_ACTION),
        }
        Ok(result)
    }
}
