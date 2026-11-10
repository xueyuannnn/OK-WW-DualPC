"""Verified exit paths for completed Nightmare Nest instances."""
from functools import wraps
import re

from ok.task.exceptions import CannotFindException

EXIT = re.compile(r'^(?:F[:：]?)?(?:(?:离开|離開|退出)(?:当前|當前)?(?:副本|秘境|挑战|挑戰)|'
                  r'(?:leave|exit)\s*(?:domain|instance|challenge))$', re.I)
COMPLETE = re.compile(r'(?:梦魔|夢魔|梦魇|夢魘).*(?:已被除|已破除|已净化|已淨化)|'
                      r'(?:nightmare|nest).*(?:purified|cleared|defeated)', re.I)
EXIT_CONTEXT = re.compile(r'(?:离开|離開|退出).{0,20}(?:副本|秘境|挑战|挑戰)|'
                          r'(?:leave|exit).{0,20}(?:domain|instance|challenge)', re.I)
QUESTION = re.compile(r'是否|确定|確定|确认|確認|进度|進度|sure|progress', re.I)
RESOURCE = re.compile(r'消耗|结晶波片|結晶波片|贝币|貝幣|星声|星聲|重构|重構|复苏|復甦|购买|購買|purchase', re.I)
CONFIRM = re.compile(r'^(?:确认|確認|确定|確定|离开|離開|退出|confirm|leave|exit)$', re.I)


def clean(text):
    return re.sub(r'\s+', '', text)


def completed_nest(task):
    return any(COMPLETE.search(clean(box.name)) for box in task.ocr(.015, .15, .38, .38, threshold=.7))


def exit_prompt(task):
    # Retain the F icon as well as exact text: other F interactions, such as
    # Echo absorption and reward claims, must not be used as exits.
    icon = task.find_one('pick_up_f_hcenter_vcenter', box=task.f_search_box, threshold=.7)
    if not icon:
        return False
    return any(EXIT.fullmatch(clean(box.name)) for box in task.ocr(.59, .34, .94, .67, threshold=.7))


def exit_button(task):
    boxes = task.ocr(.04, .18, .96, .92, threshold=.7)
    return next((box for box in boxes if EXIT.fullmatch(clean(box.name))), None)


def exit_confirmation(task):
    boxes = task.ocr(.2, .25, .82, .78, threshold=.7)
    text = ''.join(clean(box.name) for box in boxes)
    if not EXIT_CONTEXT.search(text) or not QUESTION.search(text) or RESOURCE.search(text):
        return None
    buttons = [box for box in boxes if CONFIRM.fullmatch(clean(box.name))
               and box.x + box.width / 2 > task.width_of_screen(.5)]
    if buttons:
        return max(buttons, key=lambda box: box.x)
    return task.find_one(['confirm_btn_hcenter_vcenter', 'confirm_btn_highlight_hcenter_vcenter'], threshold=.6)


def outside_nest(task):
    # in_team_and_world only checks the team HUD; that HUD is also present
    # inside an instance. Require the actual open-world marker instead.
    return bool(task.in_world()) and not completed_nest(task)


def leave_nest(task, send_esc=True):
    if outside_nest(task):
        return True
    esc_sent = not send_esc
    for _ in range(4):
        task.next_frame()
        if outside_nest(task):
            task.log_info('双机巢穴：已确认回到大世界，继续下一目标')
            return True
        if button := exit_confirmation(task):
            task.log_info('双机巢穴：确认离开当前副本')
            task.click(button, after_sleep=1)
        elif exit_prompt(task):
            task.log_info('双机巢穴：识别到 F 离开副本')
            task.send_key('f', after_sleep=1)
        elif button := exit_button(task):
            task.log_info('双机巢穴：点击离开副本入口')
            task.click(button, after_sleep=1)
        elif not task.in_team()[0]:
            task.sleep(1)
            continue
        elif not esc_sent:
            task.send_key('esc', after_sleep=1)
            esc_sent = True
            continue
        else:
            task.sleep(1)
            continue
        if task.wait_until(lambda: outside_nest(task), time_out=4, raise_if_not_found=False):
            task.log_info('双机巢穴：已确认回到大世界，继续下一目标')
            return True
    if task.wait_until(lambda: outside_nest(task), time_out=45, raise_if_not_found=False):
        return True
    raise CannotFindException('巢穴退出未完成：尚未确认回到大世界；未自动确认未知弹窗')


def install_nightmare_hook():
    from src.task.NightmareNestTask import NightmareNestTask
    if getattr(NightmareNestTask, '_dualpc_nest_exit', False):
        return
    original_exit = NightmareNestTask.esc_world_confirm
    original_run = NightmareNestTask.run

    def exit_to_world(task, send_esc=True):
        if not getattr(task.executor.interaction, 'is_remote', False):
            return original_exit(task, send_esc=send_esc)
        return leave_nest(task, send_esc=send_esc)

    @wraps(original_run)
    def run(task):
        if (getattr(task.executor.interaction, 'is_remote', False)
                and not outside_nest(task) and completed_nest(task)):
            task.log_info('双机巢穴：先退出上次已完成的副本，再继续刷取')
            leave_nest(task)
        return original_run(task)

    NightmareNestTask.esc_world_confirm = exit_to_world
    NightmareNestTask.run = run
    NightmareNestTask._dualpc_nest_exit = True
