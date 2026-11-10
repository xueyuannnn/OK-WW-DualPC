"""Complete the daily stamina objective with a persistent four-cube ceiling."""
from datetime import datetime, timedelta
import json
from pathlib import Path
import re

from ok.task.exceptions import CannotFindException

ENABLE = 'Use Stamina Cubes for Daily'
LIMIT = 'Daily Stamina Cube Limit'
HELP = 'When daily activity is incomplete, finish its stamina objective. Use one owned stamina cube only when needed, up to 4 per game day across retries. Never buy stamina.'
PROGRESS = re.compile(r'^(\d+)\s*[/／]\s*180$')
REFILL = re.compile(r'补充结晶波片|補充結晶波片|replenish waveplates', re.I)
ONE_SOLVENT = re.compile(r'消耗1[结結]晶[溶熔][剂劑][补補]充60[结結]晶波片|consume1crystalsolvent.*60waveplates', re.I)


def configure_daily(task):
    task.default_config.update({ENABLE: False, LIMIT: 4})
    task.config_description.update({ENABLE: HELP, LIMIT: 'Shared daily limit (0-4). Retries and app restarts do not reset usage. Resets at 04:00 local time.'})
    task.config_type[LIMIT] = {'min': 0, 'max': 4}


class CubeBudget:
    def __init__(self, path, limit=4, now=None):
        self.path = Path(path)
        self.limit = max(0, min(4, int(limit)))
        self.day = ((now or datetime.now()) - timedelta(hours=4)).date().isoformat()
        if self.path.exists():
            self.data = json.loads(self.path.read_text(encoding='utf-8'))
            if (not isinstance(self.data, dict) or not isinstance(self.data.get('reserved'), int)
                    or not 0 <= self.data['reserved'] <= 4):
                raise ValueError('Invalid daily stamina usage record; no cube will be used')
            if self.data.get('pending'):
                raise RuntimeError('Previous cube use is unverified; automatic use is stopped')
            if self.data.get('day') != self.day:
                self.data = {'day': self.day, 'reserved': 0, 'verified': 0, 'pending': False}
        else:
            self.data = {'day': self.day, 'reserved': 0, 'verified': 0, 'pending': False}

    @property
    def remaining(self):
        return max(0, self.limit - self.data['reserved'])

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix('.tmp')
        temporary.write_text(json.dumps(self.data, ensure_ascii=False, indent=2), encoding='utf-8')
        temporary.replace(self.path)

    def reserve(self):
        if self.data.get('pending') or not self.remaining:
            raise RuntimeError('Daily stamina cube budget exhausted or unverified')
        self.data['reserved'] += 1
        self.data['pending'] = True
        self.save()  # Persist before the material action, including crashes.

    def verified(self):
        self.data['pending'] = False
        self.data['verified'] += 1
        self.save()


def budget_for(task):
    path = getattr(task, '_daily_cube_budget_path', None)
    if path is None:
        path = Path(task.config.config_file).parent / 'DailyStaminaUsage.json'
    return CubeBudget(path, task.config.get(LIMIT, 4))


def complete_daily_stamina(task):
    if not task.config.get(ENABLE, False) or getattr(task, '_daily_stamina_no_progress', False):
        return
    try:
        budget = budget_for(task)
    except (OSError, ValueError, RuntimeError) as error:
        task.log_error(f'日常魔方记录需要核对：{error}', notify=True)
        return
    task.info_set('Daily Stamina Cubes Used', f"{budget.data['reserved']}/{budget.limit}")
    for _ in range(6):
        _, ready = task.open_daily()
        if ready:
            task.ensure_main()
            return
        boxes = task.ocr(.1, .1, .5, .75, match=PROGRESS, threshold=.75)
        progress = next((int(m[1]) for b in boxes if (m := PROGRESS.fullmatch(b.name.strip()))), None)
        if progress is None or not 0 <= progress < 180:
            task.log_info('日常补体力：未识别到尚未完成的体力目标，不使用魔方')
            task.ensure_main()
            return
        from src.task.TacetTask import TacetTask
        from src.task.ForgeryTask import ForgeryTask
        from src.task.SimulationTask import SimulationTask
        preferred = task.config.get('Which to Farm', 'Tacet Suppression')
        if task.config.get('Run Tacet or Forgery First') == 'Forgery First':
            preferred = 'Forgery Challenge'
        cls, method, cost = {
            'Tacet Suppression': (TacetTask, 'farm_tacet', 60),
            'Forgery Challenge': (ForgeryTask, 'farm_forgery', 40),
            'Simulation Challenge': (SimulationTask, 'farm_simulation', 40),
        }.get(preferred, (TacetTask, 'farm_tacet', 60))
        task.ensure_main()
        task.openF2Book('gray_book_boss')
        current, backup, total = task.get_stamina()
        if current < 0 or backup < 0 or total < 0:
            raise CannotFindException('无法确认体力，不使用魔方')
        while total < cost:
            if not budget.remaining:
                task.log_info('日常补体力：已达到魔方上限，保留未完成状态', notify=True)
                task.ensure_main()
                return
            if not use_one_cube(task, budget):
                task.ensure_main()
                return
            current, backup, total = task.get_stamina()
        task.ensure_main()
        domain = task.get_task_by_class(cls)
        from src.task.EnhancedUtils import hijack_use_stamina
        previous = domain.__dict__.get('use_stamina')
        hijack_use_stamina(domain)
        try:
            getattr(domain, method)(daily=True, used_stamina=180-cost, config=task.config)
        finally:
            if previous is None:
                domain.__dict__.pop('use_stamina', None)
            else:
                domain.use_stamina = previous
        _, ready = task.open_daily()
        if ready:
            task.ensure_main()
            return
        after = task.ocr(.1, .1, .5, .75, match=PROGRESS, threshold=.75)
        observed = next((int(m[1]) for b in after if (m := PROGRESS.fullmatch(b.name.strip()))), None)
        if observed is None or observed <= progress:
            task._daily_stamina_no_progress = True
            task.log_info('补做后体力进度待领取步骤复查，停止进一步消耗', notify=True)
            task.ensure_main()
            return
    task.ensure_main()


def use_one_cube(task, budget, consume=True):
    from dualpc.stamina import COUNTER
    from dualpc.garden_shop import enlarge_digits
    from dualpc.garden import CONFIRM

    def dialog_open():
        return any(REFILL.search(b.name) for b in task.ocr(.18, .17, .40, .23, threshold=.8))

    def open_refill():
        if dialog_open():
            return True
        counters = [(b, m) for b in task.ocr(.49, 0, .92, .10, threshold=.8)
                    if (m := COUNTER.fullmatch(b.name.strip())) and 20 <= int(m[2]) <= 9999]
        if not counters:
            return False
        counter, _ = max(counters, key=lambda item: int(item[1][2]))
        h, w = task.frame.shape[:2]
        task.click(round(counter.x+counter.width+.027*w), round(.06*h),
                   name='daily_stamina_refill_open', after_sleep=.4)
        task.next_frame()
        return bool(dialog_open())

    def select_and_read():
        task.click(.497, .444, name='daily_solvent_select', after_sleep=.7)
        observed = [None]
        def ready():
            task.next_frame()
            text = ''.join(b.name.replace(' ', '') for b in task.ocr(.37, .53, .63, .585, threshold=.8))
            if not ONE_SOLVENT.search(text):
                return False
            stock = task.ocr(.50, .49, .535, .53, threshold=.8, frame_processor=enlarge_digits)
            values = [int(b.name.strip()) for b in stock if b.name.strip().isdigit()]
            if len(values) != 1:
                return False
            observed[0] = values[0]
            return True
        task.wait_until(ready, time_out=3, raise_if_not_found=False)
        return observed[0]

    def cancel():
        buttons = task.ocr(.22, .68, .40, .76, threshold=.8)
        button = next((b for b in buttons if b.name.strip().lower() in ('取消', 'cancel')), None)
        if button:
            task.click(button, after_sleep=.2)

    if not budget.remaining or budget.data.get('pending'):
        return False
    current, backup, _ = task.get_stamina()
    if current < 0 or not open_refill():
        return False
    owned = select_and_read()
    if owned is None or owned <= 0:
        task.log_info('日常补体力：未确认可用结晶溶剂，不执行材料操作', notify=True)
        cancel()
        return False
    buttons = task.ocr(.60, .68, .79, .76, threshold=.8)
    confirm = next((b for b in buttons if CONFIRM.fullmatch(b.name.strip())), None)
    if confirm is None:
        cancel()
        return False
    if not consume:
        task.info_set('Daily Cube Inspection', f'结晶溶剂库存 {owned}；单次 1 个补充 60 体力；未消耗')
        task.sleep(.8)
        cancel()
        return True
    budget.reserve()
    task.click(confirm, after_sleep=.5)
    observed = [-1]
    def increased():
        task.next_frame()
        observed[0], reserve, _ = task.get_stamina()
        return observed[0] >= current+60 and reserve == backup
    if not task.wait_until(increased, time_out=5, raise_if_not_found=False):
        raise CannotFindException('魔方操作后体力变化未确认，停止继续消耗并保留次数记录')
    if not open_refill():
        raise CannotFindException('魔方操作后无法核对库存，停止继续消耗并保留次数记录')
    remaining = select_and_read()
    cancel()
    if remaining != owned-1:
        raise CannotFindException('魔方库存变化未确认，停止继续消耗并保留次数记录')
    budget.verified()
    task.info_set('Daily Stamina Cubes Used', f"{budget.data['reserved']}/{budget.limit}")
    task.log_info(f'日常补体力：已使用 1 个结晶溶剂，库存 {owned}→{remaining}，体力 {current}→{observed[0]}')
    return True
