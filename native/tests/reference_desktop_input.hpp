#pragma once
#include "desktop.hpp"
#include <map>
namespace okww::desktop_input_reference {
QPoint desktopAbsolute(QPoint point, QRect desktop) {
    if (desktop.width() < 2 || desktop.height() < 2 || !desktop.contains(point))
        fail("鼠标坐标超出本机桌面");
    return {int(std::llround(double(point.x() - desktop.x()) * 65535 / (desktop.width() - 1))),
            int(std::llround(double(point.y() - desktop.y()) * 65535 / (desktop.height() - 1)))};
}
INPUT hidInput(int usage, bool down) {
    // USB HID usage -> set-1 scan code. Game keys follow physical positions,
    // independent of the active Windows text input layout.
    static const std::map<int, int> codes{
        {4, 0x1e},   {5, 0x30},   {6, 0x2e},    {7, 0x20},    {8, 0x12},   {9, 0x21},    {10, 0x22},
        {11, 0x23},  {12, 0x17},  {13, 0x24},   {14, 0x25},   {15, 0x26},  {16, 0x32},   {17, 0x31},
        {18, 0x18},  {19, 0x19},  {20, 0x10},   {21, 0x13},   {22, 0x1f},  {23, 0x14},   {24, 0x16},
        {25, 0x2f},  {26, 0x11},  {27, 0x2d},   {28, 0x15},   {29, 0x2c},  {30, 2},      {31, 3},
        {32, 4},     {33, 5},     {34, 6},      {35, 7},      {36, 8},     {37, 9},      {38, 10},
        {39, 11},    {40, 0x1c},  {41, 1},      {42, 0xe},    {43, 0xf},   {44, 0x39},   {45, 0xc},
        {46, 0xd},   {47, 0x1a},  {48, 0x1b},   {49, 0x2b},   {50, 0x2b},  {51, 0x27},   {52, 0x28},
        {53, 0x29},  {54, 0x33},  {55, 0x34},   {56, 0x35},   {57, 0x3a},  {58, 0x3b},   {59, 0x3c},
        {60, 0x3d},  {61, 0x3e},  {62, 0x3f},   {63, 0x40},   {64, 0x41},  {65, 0x42},   {66, 0x43},
        {67, 0x44},  {68, 0x57},  {69, 0x58},   {70, 0x137},  {71, 0x46},  {73, 0x152},  {74, 0x147},
        {75, 0x149}, {76, 0x153}, {77, 0x14f},  {78, 0x151},  {79, 0x14d}, {80, 0x14b},  {81, 0x150},
        {82, 0x148}, {83, 0x45},  {84, 0x135},  {85, 0x37},   {86, 0x4a},  {87, 0x4e},   {88, 0x11c},
        {89, 0x4f},  {90, 0x50},  {91, 0x51},   {92, 0x4b},   {93, 0x4c},  {94, 0x4d},   {95, 0x47},
        {96, 0x48},  {97, 0x49},  {98, 0x52},   {99, 0x53},   {100, 0x56}, {101, 0x15d}, {224, 0x1d},
        {225, 0x2a}, {226, 0x38}, {227, 0x15b}, {228, 0x11d}, {229, 0x36}, {230, 0x138}, {231, 0x15c}};
    const auto code = codes.find(usage);
    if (code == codes.end())
        fail("SendInput 不支持此 HID 键码：" + QString::number(usage));
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = WORD(code->second & 0xff);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (code->second & 0x100 ? KEYEVENTF_EXTENDEDKEY : 0) |
                       (down ? 0 : KEYEVENTF_KEYUP);
    input.ki.dwExtraInfo = localInputTag;
    return input;
}
} // namespace okww::desktop_input_reference
