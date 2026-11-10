"""Buy observed Garden goods with Garden currency, never the refresh button."""
from dataclasses import dataclass
import re
import cv2

from ok import Box

NUMBER = re.compile(r'^[-−]?\s*(\d+)\s*$')
BUY = re.compile(r'^(?:购买|購買|buy|purchase)$', re.I)
BUY_WITH_PRICE = re.compile(r'^(?:[-−]?\s*(\d+)\s*(?:购买|購買|buy|purchase)|(?:购买|購買|buy|purchase)\s*[-−]?\s*(\d+))$', re.I)
SOLD_OUT = re.compile(r'^(?:已售罄|售罄|已购买|已購買|sold\s*out|purchased)$', re.I)


def enlarge_digits(image):
    return cv2.resize(image, None, fx=3, fy=3, interpolation=cv2.INTER_CUBIC)


@dataclass
class Item:
    index: int
    price: int | None
    recommended: bool
    box: Box


def read_money(task):
    boxes = task.ocr(.68, .035, .79, .09, threshold=.8)
    values = [int(b.name) for b in boxes if b.name.strip().isdigit()]
    return values[0] if len(values) == 1 else None


def inventory(task):
    from dualpc.garden import recommendation_score
    frame = task.frame
    h, w = frame.shape[:2]
    items = []
    for row in range(2):
        for column in range(6 if row == 0 else 2):
            x, y = .198+.080*column, .302+.191*row
            # A whole row is often merged into one OCR line ("2 2 4 8 ...").
            # Read each observed cell's price separately, excluding its icon.
            labels = task.ocr(x+.006, .372+.191*row, x+.032, .400+.191*row,
                              threshold=.75, frame_processor=enlarge_digits)
            prices = [int(m[1]) for b in labels if (m := NUMBER.fullmatch(b.name.strip()))
                      and int(m[1]) > 0]
            recommended = recommendation_score(frame, (round((x+.012)*w), round((y-.077)*h),
                                                       round((x+.045)*w), round((y-.005)*h)), w/1920*.72) >= .88
            price = prices[0] if len(prices) == 1 else None
            # A faint/merged price must not erase a visible recommendation.
            # Open that slot to read its detail price before ordinary goods.
            if price is None and not recommended:
                continue
            box = Box(round((x-.018)*w), round((y-.03)*h), round(.036*w), round(.06*h),
                      name=f'garden_shop_item_{row*6+column+1}')
            items.append(Item(row*6+column, price, recommended, box))
    return items


def affordable_order(items, money):
    return sorted((item for item in items if (item.price is not None and item.price <= money)
                   or (item.price is None and item.recommended)),
                  key=lambda item: (not item.recommended, item.index))


def purchase_controls(boxes):
    """Accept both separate cost/button OCR and one merged '-4购买' label."""
    buy = None
    prices = []
    for box in boxes:
        text = box.name.strip()
        if match := BUY_WITH_PRICE.fullmatch(text):
            buy = box
            prices.append(int(match[1] or match[2]))
        elif BUY.fullmatch(text):
            buy = box
        elif match := NUMBER.fullmatch(text):
            prices.append(int(match[1]))
    return buy, prices[0] if len(prices) == 1 and prices[0] > 0 else None


def read_purchase_detail(task, item):
    for _ in range(3):
        task.next_frame()
        boxes = task.ocr(.66, .66, .86, .73, threshold=.8)
        if any(SOLD_OUT.fullmatch(b.name.strip()) for b in boxes):
            return None, None, True
        buy, price = purchase_controls(boxes)
        if buy is not None and price is not None and (item.price is None or price == item.price):
            return buy, price, False
        task.sleep(.2)
    return None, None, False


def wait_for_purchase(task, before, price):
    from dualpc.garden import modern_reward_choice
    for _ in range(12):
        task.next_frame()
        current = read_money(task)
        if current is not None and current < before:
            return current == before - price
        # Random-box purchases open a selected reward receipt before the shop
        # returns. Confirm that receipt, without choosing or refreshing goods.
        receipt = modern_reward_choice(task)
        if receipt and receipt.name == 'garden_reward_confirm_ocr':
            task.click(receipt, after_sleep=.3)
        else:
            task.sleep(.2)
    return False


def run_shop(task):
    skipped = set()
    purchased = 0
    reserve = max(0, int(getattr(task, 'config', {}).get('Garden Shop Reserve', 10)))
    task.sleep(.4)  # Let the shop entrance animation and badges finish loading.
    for _ in range(30):
        task.next_frame()
        money = read_money(task)
        if money is None:
            task.log_info('乐园商店：余额未识别，停止购买')
            break
        available = money - reserve
        if available <= 0:
            break
        items = affordable_order([item for item in inventory(task) if item.index not in skipped], available)
        if not items:
            break
        item = items[0]
        task.info_set('Garden Shop', f"{'推荐商品' if item.recommended else '顺序购买'} {item.index+1}，价格 {item.price if item.price is not None else '详情复查'}，余额 {money}，保留 {reserve}")
        task.click(item.box, after_sleep=.5)
        buy, price, sold = read_purchase_detail(task, item)
        if sold or (price is not None and price > available):
            skipped.add(item.index)
            continue
        if buy is None or price is None:
            if item.recommended:
                task.log_info('乐园商店：推荐商品详情未确认，保留余额，不跳过购买普通商品')
                break
            skipped.add(item.index)
            continue
        task.click(buy, after_sleep=.4)
        if not wait_for_purchase(task, money, price):
            # An unconfirmed click may still complete later. Do not spend the
            # same balance on another item, or retry a possibly successful buy.
            task.log_info('乐园商店：未确认本次扣款，停止继续购买，避免跳过推荐或重复点击')
            break
        purchased += 1
        skipped.add(item.index)
    balance = read_money(task)
    close = task.find_one('garden_shop_close', threshold=.82)
    if close:
        task.click(close, after_sleep=.3)
    task.info_set('Garden Shop', f'已购买 {purchased} 件，剩余乐园货币 {balance}，保留 {reserve}')
