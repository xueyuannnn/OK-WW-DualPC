#include "multi_account.hpp"
namespace okww::runtime {
QString normalizeAccount(QString value) {
    value = value.toLower().trimmed().remove(QRegularExpression("\\s+"));
    return value.replace('0', 'o').replace(".con", ".com");
}
void AccountCycle::complete(const QString &account) {
    const auto name = normalizeAccount(account);
    if (name.isEmpty())
        fail("未识别到账号，停止切换以免重复执行日常");
    completed_.insert(name);
}
bool AccountCycle::completed(const QString &account) const {
    return completed_.contains(normalizeAccount(account));
}
std::optional<TextLine> AccountCycle::next(const QVector<TextLine> &accounts) const {
    for (const auto &account : accounts)
        if (!normalizeAccount(account.text).isEmpty() && !completed(account.text))
            return account;
    return {};
}
QVector<TextLine> MultiAccountDaily::accounts() {
    // The login dialog belongs to the game PC. Never inspect local Windows controls.
    QVector<TextLine> result;
    const QRegularExpression pattern("(?:\\*{3,}|[•·]{3,}|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,})");
    for (const auto &line : c_.text(.15, .12, .85, .85, .65))
        if (pattern.match(line.text).hasMatch())
            result.append(line);
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.box.y() < b.box.y(); });
    return result;
}
TextLine MultiAccountDaily::currentAccount() {
    QVector<TextLine> lines;
    if (!c_.wait(
            [&] {
                lines = accounts();
                return lines.size() == 1;
            },
            60, .4))
        fail("无法唯一识别登录框当前账号，请收起账号列表并确认已保存账号；不会尝试输入密码");
    return lines.first();
}
void MultiAccountDaily::logout() {
    navigation_.ensureMain(100);
    c_.key("esc", 1.5);
    if (!c_.wait([&] { return bool(c_.find("esc_setting")); }, 10))
        fail("未确认进入设置菜单，停止切换账号");
    c_.clickRelative(.04, .96, 1);
    navigation_.confirmDialog();
    c_.state->loggedIn = false;
    currentAccount();
}
std::optional<TextLine> MultiAccountDaily::choose(AccountCycle &cycle) {
    auto displayed = currentAccount();
    c_.click(displayed, .8);
    QString previous;
    std::optional<TextLine> selected;
    const int pages = std::clamp(c_.setting("Account List Pages", 12).toInt(), 1, 40);
    for (int page = 0; page < pages; ++page) {
        const auto lines = accounts();
        if (lines.empty())
            fail("账号列表不可读，停止切换账号");
        selected = cycle.next(lines);
        if (selected)
            break;
        QStringList names;
        for (const auto &line : lines)
            names << normalizeAccount(line.text);
        const auto fingerprint = names.join('|');
        if (fingerprint == previous) {
            c_.key("esc", .3);
            return {};
        }
        previous = fingerprint;
        c_.input.scroll(-3, lines.last().center());
        c_.sleep(.5);
    }
    if (!selected)
        fail("账号列表达到翻页上限，未确认全部完成，已停止");
    c_.click(*selected, .8);
    const auto expected = normalizeAccount(selected->text);
    if (!c_.wait(
            [&] {
                const auto a = accounts();
                return a.size() == 1 && normalizeAccount(a[0].text) == expected;
            },
            8, .3))
        fail("选择后显示的账号不一致，已停止以免重复执行");
    return selected;
}
void MultiAccountDaily::run(const std::function<void()> &daily) {
    AccountCycle cycle;
    const int maximum = std::clamp(c_.setting("Maximum Accounts", 20).toInt(), 1, 100);
    daily();
    logout();
    cycle.complete(currentAccount().text);
    for (;;) {
        if (c_.info)
            c_.info("多账号日常", QString("已完成 %1 个账号").arg(cycle.count()));
        const auto selected = choose(cycle);
        if (!selected)
            return;
        if (cycle.count() >= maximum)
            fail("达到账号数量上限，剩余账号未运行");
        const auto login =
            matchingText(c_.text(.25, .25, .75, .85, .65), "登录|登入|登錄|Log\\s*[Ii]n|Sign\\s*[Ii]n");
        if (!login)
            fail("未识别到登录按钮，停止切换账号");
        c_.click(*login, 2);
        c_.state->loggedIn = false;
        navigation_.ensureMain(180);
        daily();
        cycle.complete(selected->text);
        logout();
        if (normalizeAccount(currentAccount().text) != normalizeAccount(selected->text))
            fail("日常完成后登录框显示的账号发生变化，已停止");
    }
}
} // namespace okww::runtime
