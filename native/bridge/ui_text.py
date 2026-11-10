"""Chinese presentation metadata; script identifiers and downloaded bytes stay original."""
from __future__ import annotations
import re


ACTION_NAMES = {
    'get_task_by_class': '按类型获取任务', 'get_tasks': '获取任务列表',
    'box_in_horizontal_center': '判断区域是否水平居中', 'get_overlay_view': '获取画面叠加层',
    'clipboard': '读取剪贴板', 'click': '点击', 'back': '返回', 'middle_click': '鼠标中键点击',
    'right_click': '鼠标右键点击', 'is_adb': '判断是否为安卓调试连接', 'is_browser': '判断是否为浏览器',
    'mouse_down': '按住鼠标', 'mouse_up': '松开鼠标', 'swipe_relative': '按画面比例拖动',
    'input_text': '输入文本', 'scroll_relative': '在相对位置滚动', 'scroll': '鼠标滚轮滚动',
    'swipe': '拖动鼠标', 'screenshot': '保存截图', 'click_box_if_name_match': '按名称点击匹配区域',
    'box_of_screen': '按画面比例创建区域', 'out_of_ratio': '检查画面比例',
    'ensure_in_front': '置前目标窗口', 'box_of_screen_scaled': '按参考分辨率创建区域',
    'height_of_screen': '计算画面高度比例', 'width_of_screen': '计算画面宽度比例',
    'click_relative': '按画面比例点击', 'middle_click_relative': '按画面比例点击中键',
    'move_relative': '按画面比例移动鼠标', 'move': '移动鼠标', 'click_box': '点击区域',
    'sleep': '等待一段时间', 'send_key': '按下并松开按键', 'send_key_down': '按住按键',
    'send_key_up': '松开按键', 'wait_until': '等待条件成立', 'wait_click_box': '等待并点击区域',
    'next_frame': '等待下一帧', 'adb_ui_dump': '读取安卓界面结构', 'draw_boxes': '显示区域标记',
    'clear_box': '清除区域标记', 'calculate_color_percentage': '计算颜色占比',
    'adb_shell': '发送安卓调试命令', 'find_feature': '查找图像模板',
    'get_feature_by_name': '按名称获取模板', 'find_feature_and_set': '查找并保存匹配模板',
    'wait_feature': '等待图像模板出现', 'wait_click_feature': '等待并点击图像模板',
    'find_one': '查找单个模板', 'feature_exists': '判断模板是否存在',
    'find_best_match_in_box': '在区域中查找最佳匹配', 'find_first_match_in_box': '在区域中查找首个匹配',
    'ocr': '识别画面文字', 'add_text_fix': '添加文字识别纠错规则',
    'wait_click_ocr': '等待并点击识别文字', 'wait_ocr': '等待指定文字出现',
    'create_shortcut': '创建快捷方式', 'sleep_check': '检查等待状态', 'tr': '翻译文本',
    'get_status': '获取任务状态', 'ensure_capture': '确保采集可用', 'update_capture': '更新采集配置',
    'enable': '启用任务', 'pause': '暂停任务', 'unpause': '继续任务',
    'log_info': '记录普通日志', 'log_debug': '记录调试日志', 'log_warning': '记录警告日志',
    'log_error': '记录错误日志', 'go_to_tab': '切换页面', 'notification': '发送本地通知',
    'emit_web_event': '发送网页界面事件', 'info_clear': '清空任务统计', 'info_incr': '增加统计数值',
    'info_add_to_list': '追加统计列表', 'info_set': '设置统计值', 'info_get': '读取任务统计',
    'info_add': '累计统计次数', 'disable': '停用任务', 'trigger': '触发任务',
    'find_boxes': '筛选匹配区域',
}

CATEGORIES = {'Mouse': '鼠标操作', 'Key': '键盘操作', 'Control': '流程控制',
              'OCR': '文字识别', 'Template Matching': '图像识别', 'Logging': '日志与统计',
              'Box': '画面区域', 'Window': '窗口操作', 'ADB': '安卓调试', 'Other': '其他操作'}

PARAM_NAMES = {
    'box': '目标区域', 'boxes': '区域列表', 'off_percent': '居中容差比例',
    'x': '横向位置', 'y': '纵向位置', 'move_back': '操作后移回鼠标', 'name': '名称',
    'interval': '最短执行间隔（秒）', 'move': '操作前移动鼠标', 'down_time': '按住时长（秒）',
    'after_sleep': '操作后等待（秒）', 'key': '按键', 'hcenter': '以水平中心为基准',
    'vcenter': '以垂直中心为基准', 'from_x': '起点横向位置', 'from_y': '起点纵向位置',
    'to_x': '终点横向位置', 'to_y': '终点纵向位置', 'duration': '持续时长（秒）',
    'settle_time': '稳定等待（秒）', 'text': '输入内容', 'count': '次数',
    'frame': '指定图像帧', 'show_box': '显示区域边框', 'frame_box': '截图区域',
    'names': '匹配名称列表', 'relative_x': '区域内横向比例', 'relative_y': '区域内纵向比例',
    'width': '区域宽度', 'height': '区域高度', 'confidence': '置信度',
    'original_screen_width': '参考画面宽度（像素）', 'original_screen_height': '参考画面高度（像素）',
    'x_original': '参考横向位置', 'y_original': '参考纵向位置',
    'width_original': '参考区域宽度', 'height_original': '参考区域高度', 'percent': '画面比例',
    'raise_if_not_found': '未找到时报告错误', 'timeout': '等待时长（秒）',
    'condition': '判断条件', 'time_out': '最长等待（秒）', 'pre_action': '检测前操作',
    'post_action': '检测后操作', 'feature_name': '模板名称', 'color': '颜色', 'debug': '调试显示',
    'horizontal_variance': '横向搜索扩展比例', 'vertical_variance': '纵向搜索扩展比例',
    'threshold': '匹配阈值', 'use_gray_scale': '使用灰度图像', 'use_grayscale': '使用灰度图像',
    'canny_lower': '边缘检测下限', 'canny_higher': '边缘检测上限', 'frame_processor': '图像处理函数',
    'template': '指定模板图像', 'match_method': '匹配算法', 'screenshot': '保存调试截图',
    'mask_function': '遮罩处理函数', 'limit': '最多匹配数量', 'target_height': '识别目标高度（像素）',
    'features': '模板列表', 'feature': '目标模板', 'click_after_delay': '点击前等待（秒）',
    'to_find': '待查找模板', 'match': '匹配文本或规则', 'log': '记录识别日志',
    'lib': '识别引擎', 'fix': '文字纠错规则', 'recheck_time': '再次检测间隔（秒）',
    'message': '消息内容', 'config': '配置内容', 'notify': '同时显示通知',
    'images': '附加图片', 'exception': '异常对象', 'tab': '目标页面', 'title': '通知标题',
    'error': '错误通知', 'tray': '显示托盘通知', 'show_tab': '打开指定页面',
    'params': '附加参数', 'event': '事件名称', 'payload': '事件数据', 'inc': '增加量',
    'item': '追加内容', 'value': '数值或内容', 'boundary': '筛选边界',
}

ACTION_DOCS = {
    'click': '点击指定位置或识别区域。坐标在 0 到 1 之间时按画面比例计算；其他数值按像素计算。',
    'click_relative': '使用 0 到 1 的画面比例定位并点击，例如横向 0.5、纵向 0.5 表示画面中心。',
    'move_relative': '使用 0 到 1 的画面比例移动鼠标。',
    'move': '将鼠标移动到指定像素坐标。双机模式通过当前选择的盒子输出。',
    'send_key': '按下指定按键，保持设定时长后松开。字母按键可直接输入，例如 f。',
    'send_key_down': '保持按键按下，之后应使用“松开按键”结束长按。',
    'send_key_up': '松开指定按键，适合与“按住按键”配合使用。',
    'mouse_down': '按住指定鼠标键，之后使用“松开鼠标”释放。',
    'mouse_up': '松开指定鼠标键。', 'sleep': '等待指定秒数后继续执行下一步。',
    'next_frame': '等待新的采集帧，适合连续识别画面。',
    'wait_until': '反复检测条件，成立后继续执行；可设置最长等待和检测前后操作。',
    'wait_click_feature': '等待指定图像模板出现，再点击匹配区域。可调整阈值、等待时间和区域内点击位置。',
    'wait_click_ocr': '等待画面出现匹配文字，再点击识别结果。',
    'ocr': '在指定画面区域识别文字，可按文本或正则规则筛选结果。',
    'wait_ocr': '持续识别指定区域，直到出现匹配文字或等待超时。',
    'find_feature': '按模板名称搜索当前画面，返回匹配区域；高级选项可控制灰度、边缘和搜索范围。',
    'find_one': '返回第一个匹配的图像模板区域。',
    'feature_exists': '检查当前画面是否包含指定模板。',
    'wait_feature': '等待模板出现在画面中，找到后返回匹配区域。',
    'screenshot': '将当前采集画面保存为截图，可指定名称及区域。',
    'input_text': '输入文本；实际可输入字符取决于当前键鼠设备和输入接口。',
    'adb_shell': '使用安卓调试接口发送命令。该动作保留原版兼容性，双机盒子不提供安卓调试连接。',
    'adb_ui_dump': '读取安卓设备的界面节点。需要安卓调试连接。',
    'notification': '在软件或托盘显示本地通知。',
}

PARAM_DOCS = {
    'x': '横向坐标；相对坐标使用 0 到 1，像素坐标使用实际像素值。',
    'y': '纵向坐标；相对坐标使用 0 到 1，像素坐标使用实际像素值。',
    'relative_x': '0 表示区域左侧，0.5 表示中心，1 表示右侧。',
    'relative_y': '0 表示区域顶部，0.5 表示中心，1 表示底部。',
    'threshold': '匹配可信度，通常使用 0 到 1；留空沿用原动作默认值。',
    'key': '键盘动作填写按键名；鼠标动作选择左键、中键或右键。',
    'time_out': '最多等待的秒数；0 的具体含义沿用原动作定义。',
    'condition': '填写返回真或假的判断表达式，或已有判断函数。',
    'feature': '填写已保存的模板名称或模板对象。', 'feature_name': '填写模板页面中保存的模板名称。',
    'match': '输入要识别的文字，也可填写正则表达式。',
    'move_back': '启用后，在操作结束时把鼠标移回原位置。',
}


def localize_templates(templates):
    """Add display-only labels without changing the callable or parameter keys."""
    result = []
    for original in templates:
        info = dict(original)
        name = info['name']
        label = ACTION_NAMES.get(name, '扩展操作')
        info['display_name'] = label
        info['category_label'] = CATEGORIES.get(info['category'], '其他操作')
        info['doc_zh'] = ACTION_DOCS.get(name, '执行“' + label + '”。留空的选项沿用原动作默认设置。')
        info['param_labels'] = {p[0]: PARAM_NAMES.get(p[0], '扩展参数') for p in info['params']}
        if info['category'] == 'Logging' and 'key' in info['param_labels']:
            info['param_labels']['key'] = '统计项名称'
        info['param_docs_zh'] = {p[0]: PARAM_DOCS.get(p[0], '设置' + PARAM_NAMES.get(p[0], '此参数') + '；可保留默认值。')
                                 for p in info['params']}
        info['full_doc_zh'] = info['doc_zh'] + '\n\n' + '\n'.join(
            info['param_labels'][p[0]] + '：' + info['param_docs_zh'][p[0]] for p in info['params'])
        result.append(info)
    return result


ALIASES = {'lynae': 'linnai', 'buling': 'douling', 'sigrika': 'xigelika',
           'yangyang: xuanling': 'yangyangsp', 'yangyang:_xuanling': 'yangyangsp',
           'luuk herssen': 'luhesi', 'xiangli yao': 'xiangliyao'}


def workshop_text(entry, characters):
    names = {item['id'].casefold(): item['name'] for item in characters}
    # Keep legacy cached descriptions readable even with a partial character list.
    names.update(qingxiao='清霄', suoming='锁瞑')
    for alias, ident in ALIASES.items():
        if ident in names:
            names[alias] = names[ident]

    def translate(text):
        text = str(text or '')
        # URLs, paths, key names and author handles are not translated.
        pieces = re.split(r'(https?://\S+|`[^`]*`)', text)
        for i in range(0, len(pieces), 2):
            value = pieces[i]
            for source in sorted(names, key=len, reverse=True):
                value = re.sub(r'(?<![A-Za-z])' + re.escape(source) + r'(?![A-Za-z])', names[source], value, flags=re.I)
            for source, label in {'boss': '首领', 'tips': '提示', 'ping': '网络延迟',
                                  'buff': '增益', 'False': '否', 'True': '是',
                                  'Switch to Healer before and after Combat': '战斗前后切换治疗角色',
                                  'R fix': '共鸣解放修复', 'Issue': '问题反馈'}.items():
                value = re.sub(r'(?<![A-Za-z])' + re.escape(source) + r'(?![A-Za-z])', label, value, flags=re.I)
            pieces[i] = value
        return ''.join(pieces)

    team = ' / '.join(translate(n.strip()) for n in entry.get('team', '').split(','))
    name = translate(entry.get('name', '')).replace('_', ' · ')
    description = translate(entry.get('description', ''))
    if not description.strip() or description.strip().isdigit():
        description = '作者暂未提供详细使用说明。' + ('原说明：' + description if description.strip() else '')
    return dict(team_display=team, name_display=name, description_display=description)
