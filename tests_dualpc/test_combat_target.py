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
        get_box_by_name=lambda name: Box(),
        allow_target_box_short_combat_check=lambda: True,
        find_best_match_in_box=Mock(side_effect=[None, None, None, SimpleNamespace(name=marker)]),
        esc_count=0,
    )
    assert bool(CombatCheck.has_target(task)) is expected
    assert task.find_best_match_in_box.call_count == 4
