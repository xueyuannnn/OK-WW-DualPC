use super::*;
impl Host {
    pub(super) fn find(&self) -> Result {
        self.action(Op::Checkpoint, &[])?;
        if self.flag(Op::FrameEmpty, &[])? {
            self.action(Op::Refresh, &[])?;
        }
        self.action(Op::FindOne, &[])
    }
    pub(super) fn find_any(&self) -> Result {
        let count = self.scalar(Op::FindNameCount, &[])? as usize;
        for index in 0..count {
            if self.flag(Op::FindByIndex, &[index as f64])? {
                break;
            }
        }
        Ok(())
    }
    pub(super) fn read_text(&self) -> Result {
        self.action(Op::Checkpoint, &[])?;
        self.action(Op::ReadTextSdk, &[])?;
        self.action(Op::Checkpoint, &[])
    }
    pub(super) fn text_scaled(&self, scale: f64) -> Result {
        self.action(Op::Checkpoint, &[])?;
        if !scale.is_finite() || scale <= 0. || scale > 8. {
            self.action(Op::ScaleError, &[])?;
            return Err(HOST_ERROR);
        }
        self.action(Op::TextAreaIntersect, &[])?;
        if self.flag(Op::TextAreaEmpty, &[])? {
            return Ok(());
        }
        self.action(Op::ResizeText, &[scale])?;
        self.action(Op::ReadScaledText, &[])?;
        let count = self.scalar(Op::TextCount, &[])? as usize;
        for index in 0..count {
            self.action(Op::SelectTextLine, &[index as f64])?;
            let position = self.call(Op::TextBoxPosition, &[])?;
            let size = self.call(Op::TextBoxSize, &[])?;
            let origin = self.call(Op::TextAreaPosition, &[])?;
            let x = origin[0] + self.scalar(Op::PixelRound, &[position[0] / scale])?;
            let y = origin[1] + self.scalar(Op::PixelRound, &[position[1] / scale])?;
            let width = self.scalar(Op::PixelRound, &[size[0] / scale])?.max(1.);
            let height = self.scalar(Op::PixelRound, &[size[1] / scale])?.max(1.);
            self.action(Op::StoreMappedTextBox, &[x, y, width, height])?;
        }
        self.action(Op::Checkpoint, &[])
    }
}
