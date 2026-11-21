"""Offline adapters: activity workflows must never create a local input backend."""
import re
from types import SimpleNamespace
from unittest.mock import Mock

import pytest
import numpy as np

from src.task.native_activity import NativeActivity
from src.task.RahaiBlocksTask import RahaiBlocksTask
from src.task.rahai_blocks import Board, Piece, choose_move


def test_activity_ocr_preserves_host_coordinates_and_matches_text():
    channel = SimpleNamespace(request=Mock(return_value={
        'sequence': 42,
        'lines': [dict(text='开始挑战', box=[100, 200, 80, 30], confidence=.99),
                  dict(text='进行中 1/2', box=[200, 400, 90, 20], confidence=.95)]}))
    task = NativeActivity()
    task.executor = SimpleNamespace(interaction=SimpleNamespace(native_channel=channel))
    boxes = task.ocr(.1, .2, .3, .4, match=re.compile('进行中'))
    assert len(boxes) == 1
    assert (boxes[0].x, boxes[0].y, boxes[0].name) == (200, 400, '进行中 1/2')
    channel.request.assert_called_once_with('activity_ocr', x=.1, y=.2, to_x=.3, to_y=.4, threshold=.2)


@pytest.mark.parametrize('focused', [True, False])
def test_rahai_cancel_releases_through_the_selected_native_backend(focused):
    class Task(RahaiBlocksTask):
        width = 1920
        height = 1080

        def __init__(self):
            self.waits = 0
            self._executor = SimpleNamespace(frame=np.zeros((1080,1920,3),np.uint8))

        def sleep(self, _):
            self.waits += 1
            if self.waits >= 2:
                raise RuntimeError('cancelled')

    task = Task()
    board = Board(0, (Piece(0, ((0, 0),), (1500, 700)),))
    move = choose_move(board)
    interaction = SimpleNamespace(move=Mock(), mouse_down=Mock(),
        clickable=Mock(return_value=focused), client=SimpleNamespace(release_all=Mock()))
    # The initial focus guard must pass; focus can be lost after pressing.
    interaction.clickable.side_effect = [True, True, focused]
    with pytest.raises(RuntimeError, match='cancelled'):
        task._place(interaction, board, move)
    interaction.mouse_down.assert_called_once()
    interaction.client.release_all.assert_called_once()


def test_activity_import_has_no_pynput_or_direct_input_escape_hatch():
    import inspect
    source = inspect.getsource(RahaiBlocksTask)
    assert 'PynputInteraction' not in source
    assert 'pynput' not in source
    assert 'self.executor.interaction' in source
