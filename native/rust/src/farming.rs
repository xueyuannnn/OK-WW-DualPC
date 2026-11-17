#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Spend {
    pub used: i32,
    pub repeat: i32,
}
pub fn stamina_spend(
    current: i32,
    reserve: i32,
    total: i32,
    once: i32,
    remaining: i32,
    exact: bool,
    prefer_double: bool,
) -> Option<Spend> {
    if once <= 0
        || once > 240
        || current < 0
        || reserve < 0
        || current.checked_add(reserve) != Some(total)
    {
        return None;
    }
    if total < once {
        return Some(Spend::default());
    }
    let twice = prefer_double
        && if exact {
            remaining >= once * 2 && total >= once * 2
        } else {
            current >= once * 2 || (remaining > once && total >= once * 2)
        };
    let used = once * if twice { 2 } else { 1 };
    let enough = total - used >= once;
    let repeat = enough
        && if exact {
            remaining as i64 - used as i64 > 0
        } else {
            remaining as i64 - used as i64 > 0 || current - used >= once
        };
    Some(Spend {
        used,
        repeat: repeat as i32,
    })
}
pub fn daily_batch(progress: i32, cost: i32, available: i32, prefer_double: bool) -> Option<i32> {
    if !(0..=180).contains(&progress) || cost <= 0 || available < 0 {
        return None;
    }
    let cost = cost as i64;
    Some(
        (cost
            * (if prefer_double { 2 } else { 1 })
                .min((180 - progress as i64 + cost - 1) / cost)
                .min(available as i64 / cost)) as i32,
    )
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Card {
    pub rarity: i32,
    pub recommended: i32,
    pub preferred: i32,
    pub x: i32,
}
pub fn select_card(cards: &[Card]) -> i32 {
    if cards.is_empty() {
        return -1;
    }
    let style = cards.iter().any(|c| c.preferred != 0);
    let rank = |c: &Card| {
        if style {
            (
                c.preferred != 0,
                c.rarity,
                c.recommended != 0,
                -i64::from(c.x),
            )
        } else {
            (c.recommended != 0, c.rarity, false, -i64::from(c.x))
        }
    };
    let mut selected = 0;
    for i in 1..cards.len() {
        if rank(&cards[i]) > rank(&cards[selected]) {
            selected = i;
        }
    }
    selected as i32
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Item {
    pub index: i32,
    pub price: i32,
    pub has_price: i32,
    pub recommended: i32,
}
pub fn order_items(items: &[Item], money: i32, output: &mut [usize]) -> usize {
    let mut count = 0;
    for (i, item) in items.iter().enumerate() {
        if if item.has_price != 0 {
            item.price > money
        } else {
            item.recommended == 0
        } {
            continue;
        }
        let mut pos = count;
        while pos > 0 {
            let previous = &items[output[pos - 1]];
            let before = if (item.recommended != 0) != (previous.recommended != 0) {
                item.recommended != 0
            } else {
                item.index < previous.index
            };
            if !before {
                break;
            }
            output[pos] = output[pos - 1];
            pos -= 1;
        }
        output[pos] = i;
        count += 1;
    }
    count
}

#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct RestartState {
    pub actions: [i32; 4],
    pub requested: i32,
    pub loading: i32,
    pub pending: i32,
}
impl RestartState {
    pub fn step(&mut self, kind: i32, hud: bool, realm: bool) -> i32 {
        if !(0..=3).contains(&kind) {
            return 5;
        }
        if kind != 0 {
            if self.requested != 0 && kind == 2 && self.pending < 4 {
                self.pending += 1;
                return 1;
            }
            self.actions[(kind - 1) as usize] += 1;
            if self.actions[(kind - 1) as usize] > 3 {
                return 5;
            }
            if kind == 1 {
                self.requested = 0;
                self.loading = 0;
            } else {
                self.requested = 1;
                self.pending = 0;
            }
            return 3;
        }
        if self.requested != 0 {
            if !hud {
                self.loading = 1;
            } else if self.loading != 0 && realm {
                return 4;
            }
        } else if hud {
            self.actions[3] += 1;
            if self.actions[3] > 3 {
                return 5;
            }
            return 2;
        }
        0
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn reward_boundaries() {
        assert_eq!(
            stamina_spend(240, 0, 240, 60, 60, true, true).unwrap().used,
            60
        );
        assert_eq!(
            stamina_spend(240, 0, 240, 60, 180, true, true)
                .unwrap()
                .used,
            120
        );
        assert_eq!(
            stamina_spend(240, 0, 240, 60, 180, true, false)
                .unwrap()
                .used,
            60
        );
        assert!(stamina_spend(-1, 0, -1, 60, 60, true, true).is_none());
        assert_eq!(daily_batch(180, 60, 240, true), Some(0));
        assert_eq!(daily_batch(120, 60, 240, true), Some(60));
    }
    #[test]
    fn style_then_color_recommendation_then_color() {
        let mut cards = [
            Card {
                rarity: 0,
                recommended: 1,
                preferred: 0,
                x: 0,
            },
            Card {
                rarity: 3,
                recommended: 0,
                preferred: 0,
                x: 100,
            },
        ];
        assert_eq!(select_card(&cards), 0);
        cards[1].preferred = 1;
        assert_eq!(select_card(&cards), 1);
    }
    #[test]
    fn unknown_recommended_prices_remain_first() {
        let items = [
            Item {
                index: 0,
                price: 3,
                has_price: 1,
                recommended: 0,
            },
            Item {
                index: 1,
                price: 0,
                has_price: 0,
                recommended: 1,
            },
        ];
        let mut out = [0; 2];
        assert_eq!(order_items(&items, 5, &mut out), 2);
        assert_eq!(out, [1, 0]);
    }
    #[test]
    fn restart_requires_new_loading_cycle() {
        let mut s = RestartState::default();
        assert_eq!(s.step(0, true, false), 2);
        assert_eq!(s.step(2, true, true), 3);
        assert_eq!(s.step(0, true, true), 0);
        assert_eq!(s.step(0, false, false), 0);
        assert_eq!(s.step(0, true, true), 4);
    }
}
