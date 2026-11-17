#pragma once
#include "update.hpp"
#include <algorithm>
namespace okww::update::version_reference {
struct OriginalVersion {
    quint64 major = 0, minor = 0, patch = 0;
    QStringList pre;
    static std::optional<OriginalVersion> parse(QString text);
    int compare(const OriginalVersion &other) const;
};
std::optional<OriginalVersion> OriginalVersion::parse(QString text) {
    static const QRegularExpression pattern(
        R"(^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$)");
    const auto match = pattern.match(text);
    if (!match.hasMatch())
        return {};
    OriginalVersion value;
    quint64 *numbers[]{&value.major, &value.minor, &value.patch};
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        *numbers[i] = match.captured(i + 1).toULongLong(&ok);
        if (!ok)
            return {};
    }
    if (!match.captured(4).isEmpty()) {
        value.pre = match.captured(4).split('.');
        for (const auto &part : value.pre)
            if (part.size() > 1 && part.front() == '0' &&
                std::all_of(part.begin(), part.end(), [](QChar c) { return c.isDigit(); }))
                return {};
    }
    return value;
}
int OriginalVersion::compare(const OriginalVersion &other) const {
    for (const auto [a, b] : {std::pair{major, other.major}, {minor, other.minor}, {patch, other.patch}})
        if (a != b)
            return a > b ? 1 : -1;
    if (pre.isEmpty() != other.pre.isEmpty())
        return pre.isEmpty() ? 1 : -1;
    for (int i = 0; i < std::min(pre.size(), other.pre.size()); ++i) {
        const auto &a = pre[i], &b = other.pre[i];
        if (a == b)
            continue;
        const auto numeric = [](const QString &s) {
            return std::all_of(s.begin(), s.end(), [](QChar c) { return c >= '0' && c <= '9'; });
        };
        const bool an = numeric(a), bn = numeric(b);
        if (an != bn)
            return an ? -1 : 1;
        if (an && a.size() != b.size())
            return a.size() > b.size() ? 1 : -1;
        return a > b ? 1 : -1;
    }
    return pre.size() == other.pre.size() ? 0 : pre.size() > other.pre.size() ? 1 : -1;
}

} // namespace okww::update::version_reference
