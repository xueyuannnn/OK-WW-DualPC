from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from dualpc.garden import in_active_garden, install_garden_hook, modern_reward_choice, garden_points_complete, maximize_garden_speed, recommendation_template
from src.task.GardenTask import GardenTask


@pytest.mark.parametrize("names,active", [(["本周游园", "阶段：4/7"], True),
                                         (["本週遊園", "階段: 2/7"], True),
                                         (["本周游园", "周游值 0/6000"], False),
                                         (["幻梦游园·狂想", "4/7"], False)])
def test_garden_resume_requires_both_active_run_labels(names, active):
    task = SimpleNamespace(ocr=Mock(return_value=[SimpleNamespace(name=name) for name in names]),
                           next_frame=Mock(), find_one=Mock(return_value=None))
    assert in_active_garden(task) is active


def test_active_garden_popup_is_detected_even_when_background_labels_are_blurred():
    task = SimpleNamespace(next_frame=Mock(), find_one=Mock(return_value=object()),
                           ocr=Mock(side_effect=AssertionError("Background OCR is unnecessary")))
    assert in_active_garden(task)
    task.next_frame.assert_called_once_with()


def test_resume_finishes_current_round_then_checks_weekly_overview(monkeypatch):
    from dualpc import garden
    calls = []
    monkeypatch.setattr(GardenTask, "run", lambda task: calls.append("new_run"))
    monkeypatch.setattr(garden, "in_active_garden", lambda task: True)
    install_garden_hook()
    task = SimpleNamespace(executor=SimpleNamespace(interaction=SimpleNamespace(is_remote=True)),
                           run_garden_loop=lambda: calls.append("resume"), log_info=Mock())
    GardenTask.run(task)
    assert calls == ["resume", "new_run"]
    task.executor.interaction.is_remote = False
    GardenTask.run(task)
    assert calls == ["resume", "new_run", "new_run"]


@pytest.mark.parametrize('text,complete', [('0/6000', False), ('5900/6000', False),
                                         ('6000/6000', True), ('6,000 / 6,000', True), ('6000', False), ('', False),
                                         ('本周游历值 已达到上限', True), ('本周游历值已满', True),
                                         ('本週遊歷值 已達上限', True), ('最大日收益 已达到上限', False)])
def test_weekly_cap_requires_earned_points_not_just_the_denominator(text, complete):
    assert garden_points_complete([SimpleNamespace(name=text)]) is complete


@pytest.mark.parametrize('enabled', [True, False])
def test_new_reward_modal_confirms_selected_card_or_selects_a_free_card(enabled):
    import numpy as np
    from ok import Box
    frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
    if enabled:
        frame[700:745, 1580:1630] = [15, 240, 110]
    frame[820:910, 1280:1680] = [15, 240, 110]  # Green even when disabled.
    task = SimpleNamespace(frame=frame, ocr=Mock(side_effect=[
        [Box(1080, 849, 70, 32, name='跳过'), Box(1440, 849, 75, 32, name='确认')],
        [Box(620, 250, 60, 30, name='声骸'), Box(1080, 250, 60, 30, name='声骸')]]))
    box = modern_reward_choice(task)
    assert box.name == ('garden_reward_confirm_ocr' if enabled else 'garden_reward_choice_white')
    assert box.x == (1440 if enabled else 643)


def test_unrelated_confirm_and_skip_without_echo_cards_are_not_clicked():
    task = SimpleNamespace(ocr=Mock(side_effect=[
        [SimpleNamespace(name='确认'), SimpleNamespace(name='跳过')], []]))
    assert modern_reward_choice(task) is None


@pytest.mark.parametrize('left,right,expected', [('white', 'blue', 'blue'), ('blue', 'purple', 'purple'),
                                               ('purple', 'gold', 'gold'), ('gold', 'blue', 'gold')])
def test_card_colors_keep_gold_purple_blue_white_order(left, right, expected):
    import numpy as np
    import cv2
    from ok import Box
    hues = {'white': [110, 35, 190], 'blue': [95, 180, 220],
            'purple': [135, 100, 200], 'gold': [28, 230, 240]}
    frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
    for x, rarity in ((620, left), (1080, right)):
        frame[230:300, x-140:x-25] = cv2.cvtColor(np.uint8([[hues[rarity]]]), cv2.COLOR_HSV2BGR)[0, 0]
    task = SimpleNamespace(frame=frame,
        ocr=Mock(side_effect=[
            [Box(1080, 849, 70, 32, name='跳过'), Box(1440, 849, 75, 32, name='确认')],
            [Box(620, 250, 60, 30, name='声骸'), Box(1080, 250, 60, 30, name='声骸')]]))
    target = modern_reward_choice(task)
    assert target.name == f'garden_reward_choice_{expected}'
    assert target.x == (643 if left == expected else 1094)


@pytest.mark.parametrize('width,height', [(1280, 720), (1920, 1080), (2560, 1440)])
def test_recommended_white_card_beats_unrecommended_gold_at_each_resolution(width, height):
    import cv2
    import numpy as np
    from ok import Box
    frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
    frame[230:300, 480:595] = [10, 220, 245]  # Left gold.
    template = recommendation_template()
    frame[163:163+template.shape[0], 1134:1134+template.shape[1]] = cv2.cvtColor(template, cv2.COLOR_GRAY2BGR)
    frame = cv2.resize(frame, (width, height))
    scale = width/1920
    task = SimpleNamespace(frame=frame, ocr=Mock(side_effect=[
        [Box(1080*scale, 849*scale, 70*scale, 32*scale, name='跳过'),
         Box(1440*scale, 849*scale, 75*scale, 32*scale, name='确认')],
        [Box(620*scale, 250*scale, 60*scale, 30*scale, name='声骸'),
         Box(1080*scale, 250*scale, 60*scale, 30*scale, name='声骸')]]))
    selected = modern_reward_choice(task)
    assert selected.name == 'garden_reward_choice_recommended_white'
    assert selected.x == round(width*.57)


def test_gray_recommended_card_is_compared_even_if_its_echo_label_is_missing():
    import cv2
    import numpy as np
    from ok import Box
    frame = np.zeros((1080, 1920, 3), dtype=np.uint8)
    frame[230:300, 510:620] = [10, 220, 245]
    template = recommendation_template()
    frame[163:163+template.shape[0], 1587:1587+template.shape[1]] = cv2.cvtColor(template, cv2.COLOR_GRAY2BGR)
    task = SimpleNamespace(frame=frame, ocr=Mock(side_effect=[
        [Box(1080, 849, 70, 32, name='跳过'), Box(1440, 849, 75, 32, name='确认')],
        [Box(620, 250, 60, 30, name='声骸'), Box(1080, 250, 60, 30, name='声骸')]]))
    selected = modern_reward_choice(task)
    assert selected.name == 'garden_reward_choice_recommended_white'
    assert selected.x == 1546


def test_speed_cycles_only_observed_control_and_verifies_max():
    from ok import Box
    task = SimpleNamespace(ocr=Mock(side_effect=[[Box(1250, 50, 60, 30, name=name)]
                                               for name in ('×1.0', '×2.0', 'MAX')]),
                           click=Mock(), info_set=Mock(), log_info=Mock())
    assert maximize_garden_speed(task)
    assert maximize_garden_speed(task)
    assert not maximize_garden_speed(task)
    assert not maximize_garden_speed(task)  # A verified MAX is cached.
    assert task.click.call_count == 2
    task.info_set.assert_called_once_with('Garden Speed', 'MAX')


def test_unknown_speed_text_does_not_trigger_coordinate_clicks():
    task = SimpleNamespace(ocr=Mock(return_value=[SimpleNamespace(name='9')]),
                           click=Mock(), info_set=Mock(), log_info=Mock())
    assert not maximize_garden_speed(task)
    task.click.assert_not_called()


@pytest.mark.parametrize('config,styles,colors,recommended,slot', [
    ({'Preferred Garden Style': 'Devour Dream'}, ['换取之梦', '吞噬之梦', '共通之梦'], [3, 0, 2], [0], 1),
    ({'Preferred Garden Style': 'Devour Dream'}, ['吞噬之梦', '吞噬之梦', '共通之梦'], [0, 2, 3], [0, 2], 1),
    ({'Preferred Garden Style': 'Devour Dream'}, ['共通之梦'] * 3, [3, 0, 2], [1], 1),
    ({'Preferred Garden Style': 'Custom Style', 'Garden Custom Style': '自定义流派'},
     ['换取之梦', '共通之梦', '自定义流派'], [3, 2, 0], [0], 2),
])
def test_style_priority_then_color_and_recommended_fallback(monkeypatch, config, styles, colors, recommended, slot):
    import numpy as np
    from ok import Box
    from dualpc import garden
    labels = [Box(620+i*450, 250, 60, 30, name='声骸') for i in range(3)]
    labels += [Box(500+i*451, 195, 160, 30, name=style) for i, style in enumerate(styles)]
    monkeypatch.setattr(garden, 'card_rarity', lambda frame, card: colors[int(card.name[-1])-1])
    monkeypatch.setattr(garden, 'recommended_cards', lambda frame, cards: [cards[i] for i in recommended])
    task = SimpleNamespace(frame=np.zeros((1080, 1920, 3), dtype=np.uint8), config=config,
        ocr=Mock(side_effect=[[Box(1000, 849, 60, 30, name='跳过'), Box(1440, 849, 75, 32, name='确认')], labels]))
    assert garden.modern_reward_choice(task).x == round(1920*(.335+.235*slot))
