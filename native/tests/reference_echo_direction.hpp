#pragma once
#include "runtime/echo_detector.hpp"
namespace okww::runtime::reference_world {
QString reference_echoWalkDirection(const QVector<Match> &echoes, QSize screen, bool moving) {
    if (echoes.empty())
        return "w";
    const auto &echo = echoes.front();
    const double dx = echo.center().x() - screen.width() * .5;
    if (std::abs(dx) < screen.height() * (moving ? .15 : .05))
        return echo.box.y() + echo.box.height() > screen.height() * .65 ? "s" : "w";
    return dx > 0 ? "d" : "a";
}
}
