//! Concerto components, calibration and gauge image pipelines.
use crate::services::{Host, Method, Op, Result, HOST_ERROR, INVALID_ACTION};
impl Host {
    fn count_concerto_ring(&self, min_area: f64) -> Result<[f64; 3]> {
        if self.flag(Op::RingImageEmpty, &[])?
            || !self.flag(Op::RingImageBgr, &[])?
            || self.scalar(Op::RingHeight, &[])? < 4.
            || self.scalar(Op::RingWidth, &[])? < 4.
        {
            self.action(Op::RingInvalidImage, &[])?;
            return Err(HOST_ERROR);
        }
        let width = self.scalar(Op::RingWidth, &[])? as i32;
        let height = self.scalar(Op::RingHeight, &[])? as i32;
        let cx = width / 2;
        let cy = height / 2;
        self.action(Op::RingCreateMask, &[])?;
        self.action(
            Op::RingDrawOuter,
            &[
                f64::from(cx),
                f64::from(cy),
                (f64::from(height) * 0.42261).ceil(),
            ],
        )?;
        self.action(
            Op::RingDrawInner,
            &[
                f64::from(cx),
                f64::from(cy),
                (f64::from(height) * 0.35119).floor(),
            ],
        )?;
        self.action(Op::RingMaskImage, &[])?;
        self.action(Op::RingThreshold, &[])?;
        self.action(Op::RingClose, &[])?;
        self.action(
            Op::RingRestoreStripe,
            &[
                f64::from(cx + 1),
                f64::from(cy - 1),
                f64::from(width - cx - 1),
                3.,
            ],
        )?;
        let count = self.scalar(Op::RingComponents, &[])? as i32;
        let (mut area, mut full, mut components) = (0., false, 0);
        for label in 1..count {
            let label = f64::from(label);
            let width = self.scalar(Op::RingStatistic, &[label, 0.])?;
            let height = self.scalar(Op::RingStatistic, &[label, 1.])?;
            if width * height < min_area {
                continue;
            }
            self.action(Op::RingSelectComponent, &[label])?;
            self.action(Op::RingFindContours, &[])?;
            if self.scalar(Op::RingContourCount, &[])? == 1. {
                let epsilon = 0.05 * self.scalar(Op::RingContourArcLength, &[])?;
                self.action(Op::RingApproxPolygon, &[epsilon])?;
                if self.scalar(Op::RingPolygonSize, &[])? >= 4.
                    && self.flag(Op::RingPolygonConvex, &[])?
                {
                    full = true;
                }
                self.action(Op::RingDropPolygon, &[])?;
            }
            area = self.scalar(Op::RingStatistic, &[label, 2.])?;
            components += 1;
            self.action(Op::RingDropComponent, &[])?;
        }
        if components > 1 {
            full = false;
            area = 0.;
        }
        Ok([area, f64::from(full), f64::from(components)])
    }
    fn detect_concerto(&self) -> Result<f64> {
        self.action(Op::ConcertoRegion, &[])?;
        if !self.flag(Op::ConcertoRegionContained, &[])? {
            self.action(Op::ConcertoInvalidRegion, &[])?;
            return Err(HOST_ERROR);
        }
        if self.scalar(Op::ConcertoRingIndex, &[])? < 0. {
            let mut best = 0.;
            self.action(Op::ConcertoSetRingIndex, &[0.])?;
            for index in 0..6 {
                let p = self.scalar(Op::ConcertoColorRatio, &[f64::from(index)])?;
                if p > best {
                    best = p;
                    self.action(Op::ConcertoSetRingIndex, &[f64::from(index)])?;
                }
            }
        }
        if self.scalar(Op::ConcertoRingIndex, &[])? >= 6. {
            self.action(Op::ConcertoInvalidIndex, &[])?;
            return Err(HOST_ERROR);
        }
        let width = self.scalar(Op::ConcertoFrameWidth, &[])?;
        let height = self.scalar(Op::ConcertoFrameHeight, &[])?;
        let ring = self.call(
            Op::ConcertoCountRing,
            &[1500. / 3840. / 2160. * width * height],
        )?;
        if ring[1] != 0. {
            self.action(Op::ConcertoSetFullArea, &[ring[0]])?;
        }
        let percent = if ring[1] != 0. {
            1.
        } else {
            let full = self.scalar(Op::ConcertoFullArea, &[])?;
            if full > 0. {
                let ratio = ring[0] / full;
                if ratio < 0.99 {
                    ratio
                } else {
                    0.99
                }
            } else {
                0.
            }
        };
        self.action(Op::ConcertoSetCurrent, &[percent])?;
        Ok(percent)
    }
    pub(in crate::services) fn run_combat_images(
        &self,
        method: Method,
        args: &[f64],
    ) -> Result<[f64; 3]> {
        let mut out = [0.; 3];
        match method {
            Method::CountConcertoRing => return self.count_concerto_ring(args[0]),
            Method::DetectConcerto => out[0] = self.detect_concerto()?,
            Method::GaugeColorPipeline
            | Method::GaugePhoebePipeline
            | Method::GaugeStripedPipeline => {
                if self.flag(Op::GaugeImageEmpty, &[])? {
                    return Ok(out);
                }
                self.action(Op::GaugeThreshold, &[])?;
                out[0] = match method {
                    Method::GaugeColorPipeline => self.scalar(Op::GaugeMaskRatio, &[])?,
                    Method::GaugePhoebePipeline => self.scalar(Op::GaugePhoebeAnalyze, args)?,
                    _ => {
                        let stripe = self.call(Op::GaugeFindStripe, &[])?;
                        self.scalar(Op::GaugeStripedRatio, &stripe[..2])?
                    }
                };
            }
            Method::GaugeRingPipeline => {
                if self.flag(Op::GaugeImageEmpty, &[])? {
                    return Ok(out);
                }
                let radii = self.call(Op::GaugeRingBounds, args)?;
                if radii[0] == 0. {
                    return Ok(out);
                }
                self.action(Op::GaugeCreateRing, &[])?;
                if radii[2] > 0. {
                    self.action(Op::GaugeOuterCircle, &[radii[2]])?;
                }
                if radii[1] > 0. {
                    self.action(Op::GaugeInnerCircle, &[radii[1]])?;
                }
                if args[2] != 0. {
                    self.action(Op::GaugeMaskRingImage, &[])?;
                    self.action(Op::GaugeNonzeroMask, &[])?;
                    self.action(Op::GaugeCountNonzeroDenominator, &[])?;
                    self.action(Op::GaugeColorMasked, &[])?;
                    self.action(Op::GaugeDropMasked, &[])?;
                } else {
                    self.action(Op::GaugeCountRingDenominator, &[])?;
                    self.action(Op::GaugeColorImage, &[])?;
                    self.action(Op::GaugeApplyRing, &[])?;
                }
                out[0] = self.scalar(Op::GaugeRingRatio, &[])?;
            }
            Method::GaugeStripePipeline => {
                if !self.flag(Op::GaugeGrayType, &[])? {
                    return Ok([-1., -1., 0.]);
                }
                out = self.call(Op::GaugeStripeAnalyze, &[])?;
            }
            _ => return Err(INVALID_ACTION),
        }
        Ok(out)
    }
}
