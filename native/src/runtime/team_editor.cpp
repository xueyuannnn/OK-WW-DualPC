#include "team_store.hpp"

// Referencing the resource initializer also retains its object when linking
// okww_runtime as a static library. These are the actual compiled source units.
static void initBuiltinCodeResources() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(builtin_code);
        return true;
    }();
    Q_UNUSED(initialized);
}
namespace okww::runtime {
namespace {
QString builtinText(const QString &file) {
    initBuiltinCodeResources();
    QFile input(":/builtin-code/" + file);
    if (!input.open(QIODevice::ReadOnly))
        fail("内置角色源码资源缺失：" + file);
    return QString::fromUtf8(input.readAll());
}
QString definitionExcerpt(const QString &text, int start) {
    if (start < 0)
        fail("内置源码中缺少对应角色定义");
    const QRegularExpression tokens(
        R"code(//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}])code");
    auto matches = tokens.globalMatch(text, start);
    int depth = 0;
    bool opened = false;
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.captured() == "{") {
            opened = true;
            ++depth;
        } else if (match.captured() == "}" && opened && --depth == 0) {
            int end = match.capturedEnd();
            if (end < text.size() && text[end] == ';')
                ++end;
            return text.mid(start, end - start);
        }
    }
    fail("内置源码定义不完整");
}
QJsonObject builtinEditor(const QString &member, const QString &requested) {
    initBuiltinCodeResources();
    QString primary;
    const QRegularExpression declaration("\\bclass\\s+" + QRegularExpression::escape(member) + "\\b");
    for (const auto &file : QDir(":/builtin-code").entryList({"characters_*.cpp"}, QDir::Files))
        if (declaration.match(builtinText(file)).hasMatch()) {
            primary = file;
            break;
        }
    const bool common = member == "Chixia" || member == "Youhu";
    if (common)
        primary = "character.cpp";
    if (primary.isEmpty())
        fail("未找到当前角色对应的内置源码：" + member);
    QStringList files{primary, "full:" + primary};
    for (const auto &file : {QString("character.cpp"), QString("character.hpp")})
        if (!files.contains(file))
            files.append(file);
    const auto source = requested.isEmpty() ? primary : requested;
    if (!files.contains(source))
        fail("该文件不属于当前角色的内置代码");
    const auto file = source.startsWith("full:") ? source.mid(5) : source;
    auto text = builtinText(file);
    int offset = source == primary ? declaration.match(text).capturedStart() : -1;
    if (common && source == primary)
        offset = text.indexOf("void Character::doPerform(");
    if (source == primary) {
        text = "// " + primary + " · " + member + "\n" + definitionExcerpt(text, offset) + '\n';
        offset = 0;
    }
    QJsonArray choices;
    for (const auto &file : files)
        choices.append(QJsonObject{{"id", file},
                                   {"label", file.startsWith("full:") ? "完整文件 · " + file.mid(5)
                                             : file == primary        ? "当前角色实现 · " + file
                                                                      : file}});
    return {{"code", text},
            {"read_only", true},
            {"source_file", source},
            {"file_choices", choices},
            {"code_kind", "builtin"},
            {"focus_line", offset < 0 ? 1 : int(text.left(offset).count('\n')) + 1},
            {"message", common ? "该角色沿用通用内置连招，下面显示其实际 C++ 实现。"
                               : "下面显示所选角色的内置 C++ 定义；完整源码和通用逻辑可在代码文件中查看。"}};
}
QString primaryImportedSource(const QJsonObject &sources) {
    // An imported module can coordinate three characters across several files.
    // A filename is a source-file identity, never a character identity.
    if (sources.contains("hsin_core.cpp"))
        return "hsin_core.cpp";
    for (const auto &file : sources.keys())
        if (file.endsWith(".cpp"))
            return file;
    fail("导入模块缺少可查看的 C++ 实现文件");
}
} // namespace
QJsonObject TeamStore::editor(const QJsonArray &value, const QString &member, const QString &requested,
                              const QString &view) const {
    std::lock_guard lock(mutex_);
    const auto team = members(value);
    if (!team.contains(member))
        fail("所选角色不属于当前队伍");
    const auto config = teamConfig(team);
    if (config.isEmpty())
        fail("队伍不存在");
    if (view != "active" && view != "import")
        fail("代码查看模式无效");
    const auto mode = config["code_mode"].toString("builtin");
    QJsonObject result;
    if (pythonTeam(team) && (view == "import" || mode == "import")) {
        const auto source = member + ".py";
        if (!requested.isEmpty() && requested != source)
            fail("所选文件不属于当前角色");
        QFile input(folder(team) + '/' + member + ".import.py");
        if (!input.open(QIODevice::ReadOnly) || input.size() > 8 * 1024 * 1024)
            fail("无法读取工坊角色代码");
        result = {{"code", QString::fromUtf8(input.readAll())},
                  {"read_only", false},
                  {"source_file", source},
                  {"file_choices",
                   QJsonArray{QJsonObject{{"id", source}, {"label", source + " · Python 工坊代码"}}}},
                  {"code_kind", "python"},
                  {"language", "python"},
                  {"focus_line", 1},
                  {"message", "运行原始 Python 工坊代码；保存后通过语法与加载校验，下次战斗段生效。"}};
    } else if (view == "import" || mode == "import") {
        const auto id = key(team);
        if (!modules_.contains(id))
            fail("此队伍尚未导入原生模块");
        const auto sources = modules_[id].manifest["sources"].toObject();
        const auto source = requested.isEmpty() ? primaryImportedSource(sources) : requested;
        result = code(value, source);
        QJsonArray choices;
        const auto primary = primaryImportedSource(sources);
        choices.append(QJsonObject{{"id", primary}, {"label", primary + " · 队伍连招"}});
        for (const auto &file : sources.keys())
            if (file != primary)
                choices.append(QJsonObject{{"id", file}, {"label", file}});
        result["source_file"] = source;
        result["file_choices"] = choices;
        result["code_kind"] = "import";
        result["shared_team_code"] = true;
        result["focus_line"] = 1;
        result["message"] = "三名角色由同一队伍模块统一编排；代码文件单独选择，不代表角色。";
    } else {
        result = builtinEditor(member, requested);
    }
    result["character"] = member;
    result["active_mode"] = mode;
    result["view_only"] = view == "import" && mode != "import";
    if (result["view_only"].toBool())
        result["read_only"] = true;
    return result;
}
} // namespace okww::runtime
