"""Activity OCR reuses the host frame and model; standalone tasks keep SDK OCR."""
import re


class NativeActivity:
    def ocr(self, x=0, y=0, to_x=1, to_y=1, *, match=None, threshold=.2, **kwargs):
        interaction = self.executor.interaction
        channel = getattr(interaction, 'native_channel', None)
        if channel is None:
            return super().ocr(x, y, to_x, to_y, match=match, threshold=threshold, **kwargs)
        if kwargs:
            raise ValueError(f'原生活动 OCR 不支持参数：{", ".join(kwargs)}')
        from ok import Box
        response = channel.request('activity_ocr', x=x, y=y, to_x=to_x, to_y=to_y,
                                   threshold=threshold)
        boxes = [Box(*line['box'], name=line['text'], confidence=line['confidence'])
                 for line in response['lines']]
        if match is None:
            return boxes
        patterns = match if isinstance(match, (list, tuple)) else [match]
        return [box for box in boxes if any(
            pattern.search(box.name) if hasattr(pattern, 'search') else str(pattern) in box.name
            for pattern in patterns)]
