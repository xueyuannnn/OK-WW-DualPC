import unittest
from workshop.bridge.script_worker import progress_payload


class ProgressTests(unittest.TestCase):
    def test_legacy_info_values_remain_legacy(self):
        self.assertIsNone(progress_payload('checked', 'yes'))
        self.assertIsNone(progress_payload('角色状态', {'current': 1, 'total': 3}))

    def test_typed_progress_and_percentage(self):
        value = progress_payload('运行进度', {'stage': '选择目标', 'current': 2, 'total': 7, 'unit': '步'})
        self.assertEqual((value['stage'], value['current'], value['total']), ('选择目标', 2, 7))
        self.assertEqual(progress_payload('progress', 25)['total'], 100)
        self.assertEqual(progress_payload('progress', 25)['unit'], '%')

    def test_invalid_progress_does_not_break_existing_scripts(self):
        for value in (True, float('nan'), float('inf'), 10**1000, -1, '3/8',
                      {'current': 2, 'total': 0}, {'current': '2', 'total': 3},
                      {'current': 2, 'total': False}):
            with self.subTest(value=value):
                self.assertIsNone(progress_payload('进度', value))


if __name__ == '__main__':
    unittest.main()
