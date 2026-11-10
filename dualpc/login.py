"""Handle a delayed monthly reward overlay after login has completed."""
from functools import wraps
import re


def normalized(text):
    return re.sub(r'\s+', '', text).lower()


def monthly_text_confirmed(texts):
    lines = [normalized(text) for text in texts]
    claim = any((('点击领取' in line or '點擊領取' in line)
                 and ('今日' in line or '本日' in line)
                 and ('月相' in line or '月卡' in line))
                or ('claim' in line and ('today' in line or 'daily' in line)
                    and ('lunite' in line or 'subscription' in line)) for line in lines)
    days = any(re.search(r'(剩余|剩餘)\d+天|remaining\d+days?|\d+days?remaining', line) for line in lines)
    return claim and days


def monthly_visible(task):
    if task.find_monthly_card():
        return True
    texts = task.ocr(box=task.box_of_screen(.28, .70, .72, .96, hcenter=True), threshold=.7)
    return monthly_text_confirmed([text.name for text in texts])


def reward_visible(task):
    texts = task.ocr(box=task.box_of_screen(.2, .12, .8, .96, hcenter=True), threshold=.7)
    text = normalized(' '.join(box.name for box in texts))
    heading = any(re.fullmatch(
        r'(获得(?:物品|奖励)?|獲得(?:物品|獎勵)?|rewardsobtained|itemsobtained|obtained)[.:：·]*',
        normalized(box.name)) for box in texts)
    close = any(word in text for word in ('点击空白', '點擊空白', 'clickanywhere', 'clickthedark', 'tapanywhere'))
    return heading and close


def claim_monthly(task):
    clicked = False
    # Recheck each frame, rather than sending the old unconditional follow-up
    # clicks after the overlay has already gone. Device/stop errors propagate.
    for _ in range(3):
        if monthly_visible(task):
            task.log_info('双机月卡：已确认领取界面，点击领取')
        elif reward_visible(task):
            task.log_info('双机月卡：关闭领取结果')
        else:
            if clicked:
                task.set_check_monthly_card(next_day=True)
                task.log_info('双机月卡：领取界面已关闭，恢复原自动任务')
            return clicked
        task.click_relative(.5, .89, after_sleep=.8)
        clicked = True
        task.next_frame()
    return clicked


def install_login_hook():
    from src.task.AutoLoginTask import AutoLoginTask
    original = AutoLoginTask.run
    if getattr(original, '_dualpc_monthly', False):
        return

    @wraps(original)
    def run(task):
        if (getattr(task.executor.interaction, 'is_remote', False)
                and task.monthly_card_config.get('Check Monthly Card', True)):
            # AutoLoginTask.logged_in can become true before the delayed
            # monthly overlay appears. This check must precede that gate.
            if monthly_visible(task) or reward_visible(task):
                return claim_monthly(task)
        return original(task)

    run._dualpc_monthly = True
    AutoLoginTask.run = run
