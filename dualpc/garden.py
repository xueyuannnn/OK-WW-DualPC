"""Resume an already open Garden run without resetting its current progress."""
from functools import wraps
from functools import lru_cache
from pathlib import Path
import re
import time
import cv2
import numpy as np
from ok import Box

TITLE = re.compile(r"本[周週][游遊][园園]")
STAGE = re.compile(r"[阶階]段\s*[:：]?\s*\d\s*/\s*7")
POINTS = re.compile(r'(\d[\d,，]*)\s*[/／]\s*(\d[\d,，]*)')
MAXED = re.compile(r'本[周週][游遊][历歷]值.{0,16}(?:已[满滿]|已[达達](?:到)?上限|[达達]到上限)|weekly.{0,24}(?:limit reached|maxed)', re.I)
CONFIRM = re.compile(r'^(?:确认|確認|confirm)$', re.I)
SKIP = re.compile(r'^(?:跳过|跳過|skip)$', re.I)
ECHO = re.compile(r'^(?:声骸|聲骸|echo)$', re.I)
SPEED = re.compile(r'^(?:[x×*]\s*\d{1,2}(?:\.\d{1,2})?|\d{1,2}\.\d{1,2})$', re.I)
USE_BLESSING = re.compile(r'^(?:使用祝福|use blessing)$', re.I)
USES = re.compile(r'(?:剩[余餘]次[数數]|remaining\s+uses?)\s*[:：]?\s*(\d+)', re.I)
STYLES = {'Devour Dream': '吞噬之梦', 'Exchange Dream': '换取之梦', 'Common Dream': '共通之梦'}


def normalize_style(text):
    return re.sub(r'\s+', '', text).translate(str.maketrans('換夢', '换梦')).casefold()


def preferred_style(task):
    config = getattr(task, 'config', {})
    value = config.get('Preferred Garden Style', 'Any Style')
    if value == 'Custom Style':
        return normalize_style(config.get('Garden Custom Style', '')) or None
    return normalize_style(STYLES[value]) if value in STYLES else None


def blessing_controls(task):
    boxes = task.ocr(.035, .70, .23, .815, threshold=.75)
    button = next((b for b in boxes if USE_BLESSING.fullmatch(b.name.strip())), None)
    match = USES.search(' '.join(b.name for b in boxes))
    uses = int(match[1]) if match else None
    return button, uses


def garden_turn(task):
    texts = ' '.join(b.name for b in task.ocr(.025, .085, .23, .185, threshold=.75))
    stage = re.search(r'(?:[阶階]段|stage)\s*[:：]?\s*(\d)\s*[/／]\s*7', texts, re.I)
    day = re.search(r'第\s*(\d+)\s*天|day\s*(\d+)', texts, re.I)
    return (int(stage[1]), int(day[1] or day[2])) if stage and day else None


def use_garden_blessing(task, already_open=False):
    turn = garden_turn(task)
    if not already_open:
        if turn is None or getattr(task, '_garden_skill_checked_turn', None) == turn:
            return False
        attempts = getattr(task, '_garden_skill_attempts', {})
        if attempts.get(turn, 0) >= 3:
            return False
        attempts[turn] = attempts.get(turn, 0) + 1
        task._garden_skill_attempts = attempts
        task.click(.18, .785, name='garden_blessing_open', after_sleep=.3)
        task.next_frame()
    button, uses = blessing_controls(task)
    if button and uses is not None and uses > 0:
        task.click(button, after_sleep=.3)
        task.next_frame()
        after_button, after_uses = blessing_controls(task)
        if after_button is None or (after_uses is not None and after_uses < uses):
            task._garden_skill_checked_turn = turn
            task.info_set('Garden Blessing', f'已使用：第 {turn[0] if turn else "?"} 阶段，第 {turn[1] if turn else "?"} 天')
    else:
        if uses == 0:
            task._garden_skill_checked_turn = turn
            task.info_set('Garden Blessing', '本回合暂无可用次数，下一回合重新检查')
        # Dismiss the observed left-hand tooltip instead of leaving it blocking.
        task.click(.5, .8, name='garden_blessing_dismiss', after_sleep=.2)
    task._garden_blessing_open = False
    return True


@lru_cache(maxsize=1)
def recommendation_template():
    path = Path(__file__).resolve().parents[1] / 'assets/dualpc/garden_recommended.png'
    template = cv2.imread(str(path), cv2.IMREAD_GRAYSCALE)
    # Exclude the card/background and selected outline surrounding the badge.
    return template[6:42, 6:41] if template is not None else None


def recommendation_score(frame, bounds, scale=1):
    template = recommendation_template()
    if template is None:
        return 0
    x1, y1, x2, y2 = bounds
    crop = frame[max(0, y1):min(frame.shape[0], y2), max(0, x1):min(frame.shape[1], x2)]
    if not crop.size:
        return 0
    gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
    score = 0
    for adjustment in (.85, 1, 1.1):
        resized = cv2.resize(template, (max(1, round(template.shape[1]*scale*adjustment)),
                                       max(1, round(template.shape[0]*scale*adjustment))))
        if gray.shape[0] >= resized.shape[0] and gray.shape[1] >= resized.shape[1]:
            score = max(score, cv2.minMaxLoc(cv2.matchTemplate(gray, resized, cv2.TM_CCOEFF_NORMED))[1])
    return score


def card_rarity(frame, label):
    """Read broad header hues, rather than exact captured RGB values."""
    h, w = frame.shape[:2]
    crop = frame[max(0, int(label.y-.015*h)):min(h, int(label.y+.04*h)),
                 max(0, int(label.x-.07*w)):max(0, int(label.x-.015*w))]
    if not crop.size:
        return 0
    hue, saturation, _ = np.median(cv2.cvtColor(crop, cv2.COLOR_BGR2HSV).reshape(-1, 3), axis=0)
    if 12 <= hue <= 45 and saturation >= 100:
        return 3  # Gold.
    if 118 <= hue <= 165 and saturation >= 45:
        return 2  # Purple.
    if 75 <= hue < 118 and saturation >= 85:
        return 1  # Blue. Low-saturation gray/white stays last.
    return 0


def recommended_cards(frame, labels):
    h, w = frame.shape[:2]
    selected = []
    for label in labels:
        if recommendation_score(frame, (int(label.x-.06*w), int(label.y-.13*h),
                                        int(label.x+.06*w), int(label.y+.01*h)), w/1920) >= .88:
            selected.append(label)
    return selected


def maximize_garden_speed(task):
    """Change only the observed speed control on the active next-day page."""
    now = time.monotonic()
    if now - getattr(task, '_garden_speed_checked_at', float('-inf')) < 30:
        return False
    boxes = task.ocr(.625, .025, .715, .09, threshold=.75)
    for box in boxes:
        text = box.name.strip().replace(' ', '')
        if 'MAX' in text.upper() or text in ('最大', '最快'):
            task._garden_speed_checked_at = now
            task._garden_speed_attempts = 0
            task.info_set('Garden Speed', 'MAX')
            return False
        if SPEED.fullmatch(text):
            attempts = getattr(task, '_garden_speed_attempts', 0)
            if attempts >= 6:
                task._garden_speed_checked_at = now
                task._garden_speed_attempts = 0
                task.log_info('周常：尚未确认 MAX，保留当前速度继续，稍后复查')
                return False
            task.click(box, after_sleep=.3)
            task._garden_speed_attempts = attempts + 1
            return True
    return False


def garden_points_complete(texts):
    text = ' '.join(str(getattr(box, 'name', box)) for box in texts)
    return bool(MAXED.search(text)) or any(int(m[1].replace(',', '').replace('，', '')) >= 6000
               and int(m[2].replace(',', '').replace('，', '')) == 6000
               for m in POINTS.finditer(text))


def modern_reward_choice(task):
    buttons = task.ocr(.50, .73, .91, .86, threshold=.75)
    confirm = next((b for b in buttons if CONFIRM.fullmatch(b.name.strip())), None)
    if not confirm or not any(SKIP.fullmatch(b.name.strip()) for b in buttons):
        return None
    labels = task.ocr(.15, .15, .86, .38, threshold=.75)
    echoes = [b for b in labels if ECHO.fullmatch(b.name.strip())]
    if len(echoes) < 2:
        return None
    frame = task.frame
    h, w = frame.shape[:2]
    # Disabled buttons also have a green background. The selected card's lime
    # border/checkmark is the evidence that a free choice has actually been made.
    crop = frame[int(h*.62):int(h*.72), int(w*.15):int(w*.88)]
    if crop.size:
        b, g, r = [crop[:, :, i].astype(np.float32) for i in range(3)]
        enabled = np.mean((g > 150) & (g > r * 1.15) & (g > b * 1.25)) > .003
        if enabled:
            confirm.name = 'garden_reward_confirm_ocr'
            return confirm
    # OCR can omit the pale text on a gray card. After the three-card modal is
    # confirmed, evaluate all three fixed slots, including a missing OCR label.
    y = int(np.median([b.y for b in echoes]))
    echoes = [Box(round(w*(.335+.235*i)), y, round(w*.035), round(h*.028), name=f'card_{i+1}')
              for i in range(3)]
    # Recommendation first, then gold/purple/blue/white. Grayscale is appropriate
    # for the thumb shape, but would erase the color distinction between cards.
    recommended = recommended_cards(frame, echoes)
    style = preferred_style(task)
    styles = [' '.join(b.name for b in labels
                       if b.width <= .23*w and (.15+.235*i)*w <= b.x+b.width/2 <= (.39+.235*i)*w)
              for i in range(3)]
    preferred = [card for i, card in enumerate(echoes) if style and style in normalize_style(styles[i])]
    if preferred:
        selected = max(preferred, key=lambda b: (card_rarity(frame, b), b in recommended, -b.x))
    else:
        selected = max(recommended or echoes, key=lambda b: (card_rarity(frame, b), -b.x))
    rarity = ('white', 'blue', 'purple', 'gold')[card_rarity(frame, selected)]
    if hasattr(task, 'info_set'):
        names = ('灰白', '蓝', '紫', '金')
        ranked = [f"{i+1}:{'流派·' if b in preferred else ''}{'推荐·' if b in recommended else ''}{names[card_rarity(frame, b)]}"
                  for i, b in enumerate(echoes)]
        task.info_set('Garden Choice', ' | '.join(ranked) + f' -> {echoes.index(selected)+1}')
    selected.name = f"garden_reward_choice_{'style_' if preferred else ''}{'recommended_' if selected in recommended else ''}{rarity}"
    return selected


def in_active_garden(task):
    task.next_frame()
    if task.find_one(["garden_next_day", "garden_new_stage", "garden_get_skip", "garden_shop_close"], threshold=0.85):
        return True
    if modern_reward_choice(task):
        return True
    if blessing_controls(task)[0]:
        task._garden_blessing_open = True
        return True
    texts = task.ocr(0, 0, 0.3, 0.2, threshold=0.8)
    names = " ".join(box.name for box in texts)
    return bool(TITLE.search(names) and STAGE.search(names))


def install_garden_hook():
    from src.task.GardenTask import GardenTask
    original = GardenTask.run
    if getattr(original, "_dualpc_resume_garden", False):
        return

    @wraps(original)
    def run(task):
        if (getattr(task.executor.interaction, "is_remote", False)
                and in_active_garden(task)):
            task.log_info("双机乐园：从当前阶段继续")
            task.run_garden_loop()
            # A resumed round follows the same overview check as a fresh run.
        return original(task)

    run._dualpc_resume_garden = True
    GardenTask.run = run
    original_find = GardenTask.find_best_garden_feature
    original_complete = GardenTask.is_weekly_garden_completed
    original_done = GardenTask.is_garden_done

    @wraps(original_find)
    def find(task):
        if getattr(task.executor.interaction, 'is_remote', False):
            if not getattr(task, '_garden_initial_blessing_checked', False):
                task._garden_initial_blessing_checked = True
                if blessing_controls(task)[0]:
                    task._garden_blessing_open = True
            if getattr(task, '_garden_blessing_open', False):
                use_garden_blessing(task, already_open=True)
                return None
            if modern := modern_reward_choice(task):
                if modern.name.startswith('garden_reward_choice_'):
                    task.sleep(.3)
                    task.next_frame()
                    # Do not use an old fallback if the popup changed meanwhile.
                    return modern_reward_choice(task)
                return modern
        target = original_find(task)
        if (getattr(task.executor.interaction, 'is_remote', False)
                and target and target.name in ('garden_shop_close', 'garden_shop_buy')):
            from dualpc.garden_shop import run_shop
            run_shop(task)
            return None
        if (getattr(task.executor.interaction, 'is_remote', False)
                and target and target.name == 'garden_next_day' and maximize_garden_speed(task)):
            # Re-read the page after changing the control before clicking next.
            return None
        if (getattr(task.executor.interaction, 'is_remote', False)
                and target and target.name == 'garden_next_day' and use_garden_blessing(task)):
            return None
        return target

    def completed(task):
        if not getattr(task.executor.interaction, 'is_remote', False):
            return original_complete(task)
        progress = task.read_weekly_garden_progress()
        return progress is not None and progress >= 6000

    def done(task, texts):
        if not getattr(task.executor.interaction, 'is_remote', False):
            return original_done(task, texts)
        return garden_points_complete(texts)

    GardenTask.find_best_garden_feature = find
    GardenTask.is_weekly_garden_completed = completed
    GardenTask.is_garden_done = done
    from dualpc.garden_shop import run_shop
    GardenTask.run_garden_shop = run_shop
