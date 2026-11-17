// Match the original C++ comparisons, including unordered floating-point values.
#![allow(clippy::neg_cmp_op_on_partial_ord)]

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Freeze {
    pub start: f64,
    pub duration: f64,
    pub threshold: f64,
}

#[derive(Default)]
pub struct FreezeTimeline {
    pub entries: Vec<Freeze>,
}

impl FreezeTimeline {
    pub fn add(
        &mut self,
        now: f64,
        start: f64,
        mut duration: f64,
        threshold: f64,
    ) -> Result<(), std::collections::TryReserveError> {
        if duration < 0.0 {
            duration = now - start;
        }
        if start > 0.0 && duration > threshold {
            self.entries.retain(|f| !(f.start <= now - 60.0));
            self.entries.try_reserve(1)?;
            self.entries.push(Freeze {
                start,
                duration,
                threshold,
            });
        }
        Ok(())
    }
    pub fn elapsed(&self, now: f64, start: f64, intro_motion: bool) -> f64 {
        if start < 0.0 {
            return 10000.0;
        }
        let mut frozen = 0.0;
        for f in &self.entries {
            if start >= f.start || (f.threshold == -100.0 && !intro_motion) {
                continue;
            }
            frozen += f.duration
                - if f.threshold == -100.0 {
                    0.0
                } else {
                    f.threshold
                };
        }
        now - start - frozen
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct CharacterState {
    pub index: i32,
    pub role: i32,
    pub buff_time: f64,
    pub last_buff: f64,
    pub last_switch: f64,
    pub last_switch_in: f64,
    pub last_full_con_switch: f64,
    pub current_con: f64,
    pub flags: u32,
    pub reserved: u32,
}

impl CharacterState {
    pub fn has_buff(&self, time: &FreezeTimeline, now: f64) -> bool {
        self.buff_time > 0.0
            && self.last_buff > 0.0
            && time.elapsed(now, self.last_buff, false) < self.buff_time
    }
    pub fn buff_remaining(&self, time: &FreezeTimeline, now: f64) -> f64 {
        if self.has_buff(time, now) {
            (self.buff_time - time.elapsed(now, self.last_buff, false)).max(0.0)
        } else {
            0.0
        }
    }
    pub fn healer_locked(&self, time: &FreezeTimeline, now: f64) -> bool {
        self.role == 2
            && self.last_full_con_switch >= 0.0
            && time.elapsed(now, self.last_full_con_switch, false) < 16.0
    }
    pub fn switch_cooling_down(&self, time: &FreezeTimeline, now: f64) -> bool {
        time.elapsed(now, self.last_switch, false) <= 1.0
    }
    pub fn switch_out(&mut self, full_con: bool, now: f64) {
        self.last_switch = now;
        self.flags &= !7;
        if full_con || self.current_con == 1.0 {
            if self.buff_time > 0.0 {
                self.last_buff = now;
            }
            if self.role == 2 {
                self.last_full_con_switch = now;
            }
            self.current_con = 0.0;
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Candidate {
    pub character: CharacterState,
    pub priority: i32,
    pub valid: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Decision {
    pub index: i32,
    pub reason: i32,
    pub priority: i32,
    pub reserved: i32,
}

fn older(a: &CharacterState, b: &CharacterState) -> bool {
    a.last_switch_in < b.last_switch_in
        || (!(b.last_switch_in < a.last_switch_in) && a.index < b.index)
}

pub fn choose_switch(
    current: &CharacterState,
    input: &[Candidate],
    intro: bool,
    time: &FreezeTimeline,
    now: f64,
) -> Decision {
    let valid = |c: &&Candidate| {
        c.valid != 0
            && c.character.index != current.index
            && !c.character.healer_locked(time, now)
            && c.priority > 0
    };
    let highest = input
        .iter()
        .filter(valid)
        .map(|c| c.priority)
        .max()
        .unwrap_or(0);
    if highest == 0 {
        return Decision {
            index: current.index,
            reason: 0,
            ..Decision::default()
        };
    }
    let available = !intro
        && input
            .iter()
            .filter(valid)
            .any(|c| c.priority == highest && !c.character.switch_cooling_down(time, now));
    let eligible = |c: &&Candidate| {
        valid(c)
            && c.priority == highest
            && (!available || !c.character.switch_cooling_down(time, now))
    };
    let pick = |predicate: &dyn Fn(&CharacterState) -> bool| -> Option<&CharacterState> {
        input
            .iter()
            .filter(eligible)
            .map(|c| &c.character)
            .filter(|c| predicate(c))
            .fold(None, |best, c| match best {
                Some(b) if !older(c, b) => Some(b),
                _ => Some(c),
            })
    };
    let support = || {
        pick(&|c| c.role == 2 && c.buff_time > 0.0 && !c.has_buff(time, now))
            .or_else(|| pick(&|c| c.role == 1 && c.buff_time > 0.0 && !c.has_buff(time, now)))
    };
    if intro && highest >= 400 {
        return Decision {
            index: pick(&|_| true).unwrap().index,
            reason: 1,
            priority: highest,
            reserved: 0,
        };
    }
    let mut target = None;
    if intro {
        target = support();
        for role in [0, 1, 2] {
            if target.is_none() {
                target = pick(&|c| c.role == role);
            }
        }
    } else {
        if current.role == 0 {
            for candidate in input.iter().filter(eligible) {
                let c = &candidate.character;
                if c.role == 0 || !(c.buff_time > 0.0) {
                    continue;
                }
                let better = target
                    .map(|b: &CharacterState| {
                        let a_remaining = c.buff_remaining(time, now);
                        let b_remaining = b.buff_remaining(time, now);
                        a_remaining < b_remaining || (!(b_remaining < a_remaining) && older(c, b))
                    })
                    .unwrap_or(true);
                if better {
                    target = Some(c);
                }
            }
        }
        if target.is_none() && current.role != 0 && current.buff_time > 0.0 {
            target = pick(&|c| c.role != 0 && c.buff_time > 0.0 && !c.has_buff(time, now));
        }
        if target.is_none() && current.role != 0 {
            target = pick(&|c| c.role == 0);
        }
        if target.is_none() {
            target = support();
        }
        if target.is_none() {
            target = pick(&|c| c.role == 0);
        }
    }
    let target = target.or_else(|| pick(&|_| true)).unwrap();
    let reason = if target.index != current.index && target.role != 0 && !target.has_buff(time, now)
    {
        match (intro, target.role == 2) {
            (true, true) => 2,
            (true, false) => 3,
            (false, true) => 4,
            (false, false) => 5,
        }
    } else if intro {
        6
    } else if current.role == 0 && target.role != 0 {
        7
    } else if current.role != 0 && target.role == 0 {
        8
    } else {
        9
    };
    Decision {
        index: target.index,
        reason,
        priority: highest,
        reserved: 0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn intro_and_plain_switch_keep_different_support_order() {
        let time = FreezeTimeline::default();
        let current = CharacterState {
            index: 0,
            role: 0,
            buff_time: 14.0,
            last_buff: -1.0,
            last_switch: -1.0,
            ..CharacterState::default()
        };
        let mut candidates = [
            Candidate {
                character: CharacterState {
                    index: 1,
                    role: 2,
                    buff_time: 14.0,
                    last_buff: -1.0,
                    last_switch: -1.0,
                    last_full_con_switch: -1.0,
                    last_switch_in: 5.0,
                    ..CharacterState::default()
                },
                priority: 200,
                valid: 1,
            },
            Candidate {
                character: CharacterState {
                    index: 2,
                    role: 1,
                    buff_time: 14.0,
                    last_buff: -1.0,
                    last_switch: -1.0,
                    last_full_con_switch: -1.0,
                    last_switch_in: 1.0,
                    ..CharacterState::default()
                },
                priority: 200,
                valid: 1,
            },
        ];
        assert_eq!(
            choose_switch(&current, &candidates, false, &time, 100.0).index,
            2
        );
        assert_eq!(
            choose_switch(&current, &candidates, true, &time, 100.0).index,
            1
        );
        candidates[1].priority = 400;
        assert_eq!(
            choose_switch(&current, &candidates, true, &time, 100.0).index,
            2
        );
        candidates[0].character.last_full_con_switch = 95.0;
        candidates[1].priority = 0;
        assert_eq!(
            choose_switch(&current, &candidates, true, &time, 100.0).index,
            0
        );
    }
    #[test]
    fn freeze_thresholds_and_intro_motion() {
        let mut time = FreezeTimeline::default();
        time.add(10.0, 2.0, 2.0, 0.1).unwrap();
        time.add(10.0, 6.0, 1.0, -100.0).unwrap();
        assert!((time.elapsed(10.0, 1.0, false) - 7.1).abs() < 1e-12);
        assert!((time.elapsed(10.0, 1.0, true) - 6.1).abs() < 1e-12);
        assert_eq!(time.elapsed(10.0, -1.0, true), 10000.0);
        time.add(100.0, 99.0, 0.1, 0.1).unwrap();
        assert_eq!(time.entries.len(), 2); // Prune only on an accepted freeze.
        time.add(100.0, 99.0, 0.2, 0.1).unwrap();
        assert_eq!(time.entries.len(), 1);
    }
    #[test]
    fn switch_out_and_healer_lock() {
        let time = FreezeTimeline::default();
        let mut state = CharacterState {
            role: 2,
            buff_time: 14.0,
            current_con: 1.0,
            flags: 7,
            ..CharacterState::default()
        };
        state.switch_out(false, 100.0);
        assert_eq!(state.flags, 0);
        assert_eq!(state.current_con, 0.0);
        assert!(state.healer_locked(&time, 115.99));
        assert!(!state.healer_locked(&time, 116.0));
    }
}
