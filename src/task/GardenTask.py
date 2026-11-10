import re


from ok import Logger, run_task, CannotFindException
from config import config
from src.Labels import Labels
from src.task.BaseWWTask import BaseWWTask
from src.task.WWOneTimeTask import WWOneTimeTask

logger = Logger.get_logger(__name__)


class GardenTask(WWOneTimeTask, BaseWWTask):
    GARDEN_TARGET_POINTS = re.compile('6000')

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.name = "🎡 自动周常乐园"
        self.description = "Check skills each turn and return after every run to verify weekly completion and claim rewards."
        self.default_config.update({'Preferred Garden Style': 'Any Style', 'Garden Custom Style': '', 'Garden Shop Reserve': 10})
        self.config_description.update({
            'Preferred Garden Style': 'Prefer the selected style, then gold, purple, blue and gray-white. If unavailable, use recommended cards first.',
            'Garden Custom Style': 'Enter the style name exactly as displayed on the game card. No paid refresh is used.',
            'Garden Shop Reserve': 'Keep this many coins after every purchase. Spend the rest on recommended goods first, then in original order. No paid refresh.',
        })
        self.config_type.update({'Preferred Garden Style': {
            'type': 'drop_down',
            'options': ['Any Style', 'Devour Dream', 'Exchange Dream', 'Common Dream', 'Custom Style'],
            'sub_configs': {'Custom Style': ['Garden Custom Style']},
        }, 'Garden Shop Reserve': {'min': 0, 'max': 100}})
        self.garden_features = [
            label.value for label in Labels
            if label.value.startswith("garden_")
        ]
        self.garden_priority_features = [
            "garden_get_skip",
            "garden_not_interested_confirm",
        ]

    def run(self):
        WWOneTimeTask.run(self)
        self.ensure_main()
        while True:
            self.open_garden_weekly_page()
            observed = [None]
            def read_progress():
                observed[0] = self.read_weekly_garden_progress()
                return observed[0] is not None
            self.wait_until(read_progress, time_out=5, raise_if_not_found=False)
            progress = observed[0]
            if progress is None:
                self.screenshot('garden_weekly_progress_unknown')
                raise CannotFindException('无法确认周任务进度，保留当前界面，不自动重开乐园')
            self.info_set('Garden Weekly Progress', f'{progress}/6000')
            if progress >= 6000:
                self.log_info('乐园任务完成, 已达到上限', notify=True)
                return
            self.click(0.246, 0.486, after_sleep=1)
            self.run_garden_loop()

    def run_garden_loop(self):
        # dualpc: keep active Garden progress when resuming
        self._garden_skill_checked_turn = None
        self._garden_skill_attempts = {}
        self._garden_initial_blessing_checked = False
        self._garden_speed_checked_at = float('-inf')
        while True:
            self.sleep(0.1)
            target = self.find_best_garden_feature()
            self.sleep(0.2)
            if target:
                self.info_set("current task", target.name)
                if target.name == 'garden_get_skip':
                    self.sleep(1)
                    self.log_info(f"click garden_get_confirm")
                    if gold := self.find_one('garden_get_gold', horizontal_variance=0.9):
                        self.click(gold, after_sleep=1)
                    elif purple := self.find_one('garden_get_purple', horizontal_variance=0.9):
                        self.click(purple, after_sleep=1)
                    else:
                        self.click(0.5, 0.2, after_sleep=1)
                    self.click(self.get_box_by_name('garden_get_confirm_gray'), after_sleep=1)
                    continue
                elif target.name == 'garden_not_interested':
                    not_interested = self.find_feature('garden_not_interested', vertical_variance=0.4)
                    self.click(not_interested[-1], after_sleep=1)
                    self.click(self.get_box_by_name('garden_not_interested_confirm'), after_sleep=1)
                    continue
                elif target.name == 'garden_start_game':
                    # At Garden Entrance, choose blessing1
                    self._choose_first_blessing()
                self.log_info(f"click {target.name} {target.confidence:.3f}")
                self.click(target, after_sleep=1)
            else:
                garden_restart = self.find_one('a_garden_restart')
                garden_back = self.find_one('a_garden_back')
                if garden_restart and garden_back:
                    # 避免因点击太快，导致[挑战失败]页面中点击[返回主页]失败
                    self.sleep(2)
                    texts = self.ocr(0.373, 0.346, 0.859, 0.615)
                    self.log_info('garden end {}'.format(texts))
                    # Always leave the completed run and check the actual weekly
                    # overview. Result OCR never decides to click restart.
                    self.click(garden_back, after_sleep=1)
                    if self.wait_feature('garden_start_game', settle_time=1, time_out=5,
                                         raise_if_not_found=False):
                        self.back(after_sleep=1)
                    self.info_set('current task', 'check weekly garden after round')
                    return
                self.sleep(0.2)

    def open_garden_weekly_page(self):
        self.openF2Book('gray_book_quest')
        self.sleep(1)
        self.click(0.343, 0.129, after_sleep=1)
        self.click(0.927, 0.893, after_sleep=3)
        self.click(0.927, 0.893, after_sleep=2)

    def is_weekly_garden_completed(self):
        progress = self.read_weekly_garden_progress()
        return progress is not None and progress >= 6000

    def read_weekly_garden_progress(self):
        # This small overview counter contains earned points only. The reward
        # milestones (1000 ... 6000) sit to its right and must not be read here.
        counters = self.ocr(.19, .83, .275, .90, threshold=.8)
        for box in counters:
            text = box.name.replace(',', '').replace('，', '').strip()
            if text.isdigit() and 0 <= int(text) <= 6000:
                return int(text)
        from dualpc.garden import garden_points_complete, POINTS
        boxes = self.ocr(.08, .68, .36, .97, threshold=.75)
        if garden_points_complete(boxes):
            return 6000
        text = ' '.join(box.name for box in boxes)
        for match in POINTS.finditer(text):
            earned = int(match[1].replace(',', '').replace('，', ''))
            maximum = int(match[2].replace(',', '').replace('，', ''))
            if maximum == 6000 and 0 <= earned <= maximum:
                return earned
        return None

    def is_garden_done(self, texts):
        text = " ".join(str(getattr(box, "name", box)) for box in texts)
        return text.count(self.GARDEN_TARGET_POINTS.pattern) != 1

    def find_best_garden_feature(self):
        matches = []
        for feature_name in self.garden_features:
            if not self.feature_exists(feature_name):
                continue
            if feature_name == 'garden_get_confirm_gray' or feature_name == 'garden_not_interested_confirm':
                continue
            if feature_name == 'garden_not_interested':
                matches.extend(self.find_feature(feature_name, vertical_variance=0.4))
            else:
                matches.extend(self.find_feature(feature_name))
        for priority_feature in self.garden_priority_features:
            priority_matches = [
                match for match in matches
                if match.name == priority_feature
            ]
            if priority_matches:
                return max(priority_matches, key=lambda box: box.confidence)
        return max(matches, key=lambda box: box.confidence, default=None)

    def _choose_first_blessing(self):
        """At Garden Entrance, choose first blessing"""
        # click blessing botton
        self.click(965 / 1920, 860 / 1080, after_sleep=2)
        # choose blessing1(Add-on)
        self.click(700 / 1920, 666 / 1080, after_sleep=2)
        # confirm
        self.click(1600 / 1920, 900 / 1080, after_sleep=2)


if __name__ == "__main__":
    run_task(config, task=GardenTask, debug=True)
