use crate::services::*;
impl Host {
    fn garden_money(&self) -> Result<[f64; 3]> {
        let count = self.scalar(Op::ReadGardenMoneyText, &[])? as usize;
        let mut valid = 0;
        let mut value = 0.;
        for index in 0..count {
            if self.flag(Op::GardenMoneyMatches, &[index as f64])? {
                let [number, ok, _] = self.call(Op::GardenMoneyValue, &[index as f64])?;
                if ok != 0. {
                    valid += 1;
                    value = number;
                }
            }
        }
        Ok([if valid == 1 { 1. } else { 0. }, value, 0.])
    }
    fn garden_inventory(&self) -> Result {
        let [w, h, _] = self.call(Op::ImageSize, &[])?;
        for row in 0..2 {
            for column in 0..if row == 0 { 6 } else { 2 } {
                let x = 0.198 + 0.080 * column as f64;
                let y = 0.302 + 0.191 * row as f64;
                let count = self.scalar(
                    Op::ReadGardenItemPrices,
                    &[
                        x + 0.006,
                        0.372 + 0.191 * row as f64,
                        x + 0.032,
                        0.400 + 0.191 * row as f64,
                        0.75,
                    ],
                )? as usize;
                let mut prices = 0;
                let mut price = 0.;
                for index in 0..count {
                    if self.flag(Op::GardenItemPriceMatches, &[index as f64])? {
                        let n = self.scalar(Op::GardenItemPrice, &[])?;
                        if n > 0. {
                            prices += 1;
                            price = n;
                        }
                    }
                }
                let left = self.scalar(Op::PixelRound, &[(x + 0.012) * w])?;
                let top = self.scalar(Op::PixelRound, &[(y - 0.077) * h])?;
                let right = self.scalar(Op::PixelRound, &[(x + 0.045) * w])?;
                let bottom = self.scalar(Op::PixelRound, &[(y - 0.005) * h])?;
                let recommended = self.scalar(
                    Op::GardenItemRecommendation,
                    &[left, top, right - left, bottom - top, w / 1920. * 0.72],
                )? >= 0.88;
                if prices != 1 && !recommended {
                    continue;
                }
                let bx = self.scalar(Op::PixelRound, &[(x - 0.018) * w])?;
                let by = self.scalar(Op::PixelRound, &[(y - 0.03) * h])?;
                let bw = self.scalar(Op::PixelRound, &[0.036 * w])?;
                let bh = self.scalar(Op::PixelRound, &[0.06 * h])?;
                self.action(
                    Op::AppendGardenItem,
                    &[
                        (row * 6 + column) as f64,
                        (prices == 1) as u8 as f64,
                        price,
                        recommended as u8 as f64,
                        bx,
                        by,
                        bw,
                        bh,
                    ],
                )?;
            }
        }
        Ok(())
    }
    fn garden_wait_purchase(&self, before: f64, price: f64) -> Result<bool> {
        for _ in 0..12 {
            self.action(Op::Refresh, &[])?;
            let current = self.call(Op::ReadGardenBalance, &[])?;
            if current[0] != 0. && current[1] < before {
                return Ok(current[1] == before - price);
            }
            if self.flag(Op::ReadGardenReceipt, &[])? && self.flag(Op::GardenReceiptConfirm, &[])? {
                self.action(Op::ClickGardenReceipt, &[0.3])?;
            } else {
                self.action(Op::Sleep, &[0.2])?;
            }
        }
        Ok(false)
    }
    fn garden_shop(&self) -> Result<f64> {
        let mut purchased = 0;
        let reserve = self.scalar(Op::GardenShopReserve, &[])?.max(0.);
        self.action(Op::Sleep, &[0.4])?;
        for _attempt in 0..30 {
            self.action(Op::Refresh, &[])?;
            let balance = self.call(Op::ReadGardenBalance, &[])?;
            if balance[0] == 0. || balance[1] <= reserve {
                break;
            }
            let count = self.scalar(Op::ReadGardenInventory, &[])? as usize;
            for index in 0..count {
                if !self.flag(Op::GardenItemSkipped, &[index as f64])? {
                    self.action(Op::AppendAvailableGardenItem, &[index as f64])?;
                }
            }
            if self.scalar(Op::AffordableGardenInventory, &[balance[1] - reserve])? == 0. {
                break;
            }
            self.action(Op::SelectGardenShopItem, &[])?;
            if self.flag(Op::HasInfo, &[])? {
                self.action(Op::GardenShopItemInfo, &[balance[1], reserve])?;
            }
            self.action(Op::ClickGardenShopItem, &[0.5])?;
            for _read in 0..3 {
                self.action(Op::Refresh, &[])?;
                self.action(Op::ReadGardenPurchaseDetail, &[])?;
                if self.flag(Op::GardenDetailSold, &[])?
                    || (self.flag(Op::GardenDetailBuy, &[])?
                        && self.flag(Op::GardenDetailPrice, &[])?
                        && (!self.flag(Op::GardenSelectedItemPrice, &[])?
                            || self.flag(Op::GardenDetailPriceMatches, &[])?))
                {
                    break;
                }
                self.action(Op::ClearGardenDetailBuy, &[])?;
                self.action(Op::ClearGardenDetailPrice, &[])?;
                self.action(Op::Sleep, &[0.2])?;
            }
            if self.flag(Op::GardenDetailSold, &[])?
                || (self.flag(Op::GardenDetailPrice, &[])?
                    && self.scalar(Op::GardenDetailPriceValue, &[])? > balance[1] - reserve)
            {
                self.action(Op::SkipSelectedGardenItem, &[])?;
                continue;
            }
            if !self.flag(Op::GardenDetailBuy, &[])? || !self.flag(Op::GardenDetailPrice, &[])? {
                if self.flag(Op::GardenSelectedItemRecommended, &[])? {
                    break;
                }
                self.action(Op::SkipSelectedGardenItem, &[])?;
                continue;
            }
            self.action(Op::ClickGardenDetailBuy, &[0.4])?;
            let price = self.scalar(Op::GardenDetailPriceValue, &[])?;
            if !self.flag(Op::WaitGardenPurchase, &[balance[1], price])? {
                if self.flag(Op::HasInfo, &[])? {
                    self.action(Op::UnverifiedGardenPurchaseInfo, &[])?;
                }
                break;
            }
            purchased += 1;
            self.action(Op::SkipSelectedGardenItem, &[])?;
        }
        self.action(Op::FinalGardenBalance, &[])?;
        if self.flag(Op::FindGardenShopClose, &[0.82])? {
            self.action(Op::ClickGardenShopClose, &[0.3])?;
        }
        if self.flag(Op::HasInfo, &[])? {
            self.action(Op::GardenShopSummary, &[purchased as f64, reserve])?;
        }
        Ok(purchased as f64)
    }
    pub(in crate::services) fn run_garden_shop(
        &self,
        method: Method,
        a: &[f64],
    ) -> Result<[f64; 3]> {
        match method {
            Method::GardenMoney => return self.garden_money(),
            Method::GardenInventory => self.garden_inventory()?,
            Method::GardenWaitPurchase => {
                return Ok([self.garden_wait_purchase(a[0], a[1])? as u8 as f64, 0., 0.])
            }
            Method::GardenShop => return Ok([self.garden_shop()?, 0., 0.]),
            _ => return Err(INVALID_ACTION),
        }
        Ok([0.; 3])
    }
}
