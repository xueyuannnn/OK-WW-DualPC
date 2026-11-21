"""Workshop combat checks must distinguish an empty lock marker from an enemy."""
from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from src.combat.CombatCheck import CombatCheck


@pytest.mark.parametrize('marker, expected', [('has_target', True), ('no_target', False)])
def test_short_target_checks_marker_identity(marker, expected):
    class Box:
        def scale(self, *args):
            return self

    task = SimpleNamespace(
        get_target_names=lambda: ('has_target', 'no_target'),
        is_browser=lambda: False,
        is_cloud_game=lambda: False,
        get_box_by_name=lambda name: Box(),
        allow_target_box_short_combat_check=lambda: True,
        find_best_match_in_box=Mock(side_effect=[None, None, None, SimpleNamespace(name=marker)]),
        esc_count=0,
    )
    assert bool(CombatCheck.has_target(task)) is expected
    assert task.find_best_match_in_box.call_count == 4


@pytest.mark.parametrize('name, expected', [('has_target_cloud', True), ('no_target_cloud', False)])
def test_cloud_lock_uses_gold_pixels_instead_of_the_template_name(name, expected):
    from pathlib import Path
    import cv2
    from ok import Box
    path = next((Path(__file__).parents[1] / 'assets/upstream-20261121/templates').glob(name + '_*.png'))
    image = cv2.imread(str(path))
    task = SimpleNamespace(is_cloud_game=lambda: True,
        box_of_screen=lambda *args: None, get_target_names=lambda: ('has_target_cloud','no_target_cloud'),
        frame=image, find_best_match_in_box=Mock(return_value=Box(0,0,image.shape[1],image.shape[0],name='no_target_cloud')))
    assert bool(CombatCheck.has_target(task)) is expected
