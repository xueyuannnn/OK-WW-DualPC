#pragma once
#include "runtime/input.hpp"
namespace okww::reference {
inline int keyCode(QString key) {
    key = key.toLower();
    if (key.size() == 1) {
        auto c = key[0].unicode();
        if (c >= 'a' && c <= 'z')
            return 4 + c - 'a';
        if (c >= '1' && c <= '9')
            return 0x1d + c - '0';
        if (c == '0')
            return 0x27;
        static const QMap<QChar, int> map{{'-', 0x2d},  {'=', 0x2e}, {'[', 0x2f},  {']', 0x30},
                                          {'\\', 0x31}, {';', 0x33}, {'\'', 0x34}, {'`', 0x35},
                                          {',', 0x36},  {'.', 0x37}, {'/', 0x38}};
        if (map.contains(key[0]))
            return map[key[0]];
    }
    static const QMap<QString, int> map{
        {"enter", 0x28},     {"return", 0x28},   {"esc", 0x29},      {"escape", 0x29},   {"backspace", 0x2a},
        {"tab", 0x2b},       {"space", 0x2c},    {"capslock", 0x39}, {"insert", 0x49},   {"home", 0x4a},
        {"pageup", 0x4b},    {"page_up", 0x4b},  {"delete", 0x4c},   {"end", 0x4d},      {"pagedown", 0x4e},
        {"page_down", 0x4e}, {"right", 0x4f},    {"left", 0x50},     {"down", 0x51},     {"up", 0x52},
        {"ctrl", 0xe0},      {"lctrl", 0xe0},    {"control", 0xe0},  {"lcontrol", 0xe0}, {"ctrl_l", 0xe0},
        {"rctrl", 0xe4},     {"rcontrol", 0xe4}, {"ctrl_r", 0xe4},   {"shift", 0xe1},    {"lshift", 0xe1},
        {"shift_l", 0xe1},   {"rshift", 0xe5},   {"shift_r", 0xe5},  {"alt", 0xe2},      {"lalt", 0xe2},
        {"alt_l", 0xe2},     {"ralt", 0xe6},     {"alt_r", 0xe6},    {"win", 0xe3},      {"cmd", 0xe3}};
    if (map.contains(key))
        return map[key];
    bool ok = false;
    int n = key.mid(1).toInt(&ok);
    if (key.startsWith('f') && ok && n >= 1 && n <= 12)
        return 0x39 + n;
    fail("不支持的 USB HID 按键：" + key);
}
}
