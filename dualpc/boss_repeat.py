"""Observe and verify domain restarts without using offsets from claim icons."""
import re

from ok.task.exceptions import CannotFindException


RESTART = re.compile(r'^(?:重新挑战|再次挑战|重新挑戰|再次挑戰|challengeagain|retrychallenge|restartchallenge)$', re.I)
RESTART_CONTEXT = re.compile(r'重新挑战|再次挑战|重新挑戰|再次挑戰|challengeagain|restartchallenge', re.I)
CONFIRM = re.compile(r'^(?:确认|确定|確認|確定|confirm)$', re.I)
CANCEL = re.compile(r'^(?:取消|cancel)$', re.I)
RESOURCE = re.compile(r'结晶波片|結晶波片|体力|體力|领取奖励|領取獎勵|领取次数|領取次數|waveplate|claimreward|claimlimit', re.I)
QUESTION = re.compile(r'是否|确定|確定|确认|確認|sure|confirm', re.I)


def dialog(task):
    boxes = task.ocr(.16, .2, .86, .82, threshold=.7)
    text = ''.join(re.sub(r'\s+', '', box.name) for box in boxes)
    # Resource notices take precedence even if their explanation mentions retry.
    if RESOURCE.search(text) and (QUESTION.search(text) or any(CANCEL.fullmatch(re.sub(r'\s+', '', b.name)) for b in boxes)):
        cancel = next((b for b in boxes if CANCEL.fullmatch(re.sub(r'\s+', '', b.name))), None)
        return 'resource', cancel
    button = next((b for b in boxes if RESTART.fullmatch(re.sub(r'\s+', '', b.name))), None)
    if button:
        return 'restart', button
    if RESTART_CONTEXT.search(text) and QUESTION.search(text):
        button = next((b for b in boxes if CONFIRM.fullmatch(re.sub(r'\s+', '', b.name))), None)
        if button:
            return 'confirm', button
    return None, None


def restart_boss_domain(task):
    """Only called by remote Echo farming after a domain fight and pickup."""
    task.info_set('Domain Restart', '正在确认再次挑战')
    actions = {'resource': 0, 'restart': 0, 'confirm': 0, 'menu': 0}
    requested = False
    loading_seen = False
    pending_ticks = 0
    for _ in range(120):
        task.next_frame()
        kind, button = dialog(task)
        hud = bool(task.in_team()[0])
        if kind is not None:
            # A dialog hiding the HUD is not a loading screen.
            if requested and kind == 'restart' and pending_ticks < 4:
                pending_ticks += 1
                task.sleep(.5)
                continue
            actions[kind] += 1
            if actions[kind] > 3:
                break
            if kind == 'resource':
                task.log_info('双机重复挑战：关闭领奖提示，继续刷声骸')
                if button:
                    task.click(button, after_sleep=.3)
                else:
                    task.send_key('esc', after_sleep=.3)
                requested = False
                loading_seen = False
            else:
                task.log_info('双机重复挑战：点击' + ('再次挑战确认' if kind == 'confirm' else '重新挑战'))
                task.click(button, after_sleep=0)
                requested = True
                pending_ticks = 0
                # Observe immediately: a fast reload can otherwise finish
                # during the fixed sleep and leave only the new HUD visible.
                continue
        elif requested:
            if not hud:
                loading_seen = True
            elif loading_seen and task.in_realm():
                task.info_set('Domain Restart', '下一轮副本已就绪')
                task.log_info('双机重复挑战：已确认下一轮副本加载完成')
                return True
        elif hud:
            actions['menu'] += 1
            if actions['menu'] > 3:
                break
            task.send_key('esc', after_sleep=.5)
        task.sleep(.5)
    raise CannotFindException('再次挑战未完成：未确认下一轮副本加载，不继续空等或确认领奖')
