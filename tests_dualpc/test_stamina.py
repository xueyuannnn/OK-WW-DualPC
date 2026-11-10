from ok import Box
from dualpc.stamina import stamina_values


def test_reserve_counter_ignores_performance_overlay_numbers_to_the_right():
    boxes = [Box(1190, 50, 65, 35, name="480"), Box(1410, 50, 140, 35, name="240/240"),
             Box(1650, 35, 75, 20, name="FPS 1%"), Box(1700, 52, 15, 20, name="1")]
    assert stamina_values(boxes) == (240, 480, 720)
    assert stamina_values(list(reversed(boxes))) == (240, 480, 720)


def test_counters_support_zero_and_missing_reserve_without_guessing_from_other_text():
    assert stamina_values([Box(1400, 50, 140, 35, name="0/240"),
                           Box(1650, 52, 20, 20, name="60")]) == (0, 0, 0)
    assert stamina_values([Box(1150, 50, 90, 35, name="MAX480"),
                           Box(1400, 50, 140, 35, name="180／240")]) == (180, 480, 660)


def test_no_stamina_fraction_does_not_report_zero_or_accept_rank_experience():
    assert stamina_values([]) == (-1, -1, -1)
    assert stamina_values([Box(1100, 50, 100, 30, name="1335900/9999999")]) == (-1, -1, -1)
