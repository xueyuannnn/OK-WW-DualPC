#include "team_store.hpp"
#include "workshop.hpp"
#include <QCryptographicHash>
namespace okww::runtime {
namespace {
QByteArray bytes(const QString &path, qint64 limit = 64 * 1024 * 1024) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit)
        fail("无法读取队伍文件：" + QFileInfo(path).fileName());
    return file.readAll();
}
void saveBytes(const QString &path, const QByteArray &value) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        fail("无法创建队伍目录");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(value) != value.size() || !file.commit())
        fail("队伍文件保存失败");
}
QString digest(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
QString canonical(QString name) {
    name = name.trimmed();
    if (name.compare("Shorekeeper", Qt::CaseInsensitive) == 0)
        name = "ShoreKeeper";
    if (name == "Lynae")
        name = "Linnai";
    return name;
}
bool safeSource(const QString &name) {
    const auto parts = name.split('/');
    return !name.isEmpty() && !QDir::isAbsolutePath(name) && !name.contains('\\') && !name.contains(':') &&
           !parts.contains("..") && !parts.contains(".") && !parts.contains("") &&
           QStringList{"cpp", "hpp", "h"}.contains(QFileInfo(name).suffix());
}
} // namespace
QStringList TeamStore::members(const QJsonValue &value) {
    QStringList result;
    if (value.isArray())
        for (const auto &v : value.toArray())
            result.append(canonical(v.toString()));
    else if (value.isString())
        for (const auto &v : value.toString().split(','))
            result.append(canonical(v));
    const auto classes = nativeCharacterClasses();
    QSet<QString> unique;
    for (const auto &name : result) {
        if (!classes.contains(name) || name == "BaseChar" || unique.contains(name))
            fail("队伍需要三名不同的已支持角色");
        unique.insert(name);
    }
    if (result.size() != 3)
        fail("队伍需要三名不同角色");
    result.sort();
    return result;
}
QString TeamStore::key(const QStringList &team) {
    auto sorted = team;
    sorted.sort();
    return sorted.join("__");
}
QString TeamStore::folder(const QStringList &team) const { return directory_ + "/" + key(team); }
QJsonObject TeamStore::teamConfig(const QStringList &team) const {
    const auto path = folder(team) + "/team.json";
    return QFile::exists(path) ? readJson(path) : QJsonObject{};
}
void TeamStore::saveConfig(const QStringList &team, QJsonObject config) {
    config["team"] = team.join(", ");
    if (!QDir().mkpath(folder(team)))
        fail("无法创建队伍配置目录");
    writeJson(folder(team) + "/team.json", config);
}
TeamStore::Module TeamStore::loadModule(const QString &path) const {
    auto library =
        std::shared_ptr<QLibrary>(new QLibrary(QFileInfo(path).absoluteFilePath()), [](QLibrary *value) {
            value->unload();
            delete value;
        });
    library->setLoadHints(QLibrary::ResolveAllSymbolsHint);
    if (!library->load())
        fail("原生队伍模块加载失败：" + library->errorString());
    using Read = const char *(*)();
    const auto read = reinterpret_cast<Read>(library->resolve("okww_team_manifest_v1"));
    const auto create = reinterpret_cast<Module::Create>(library->resolve("okww_team_create_v1"));
    if (!read || !create)
        fail("队伍模块与当前 C++ 版本不兼容");
    const char *text = read();
    if (!text)
        fail("队伍模块缺少清单");
    const auto doc = QJsonDocument::fromJson(QByteArray(text));
    if (!doc.isObject())
        fail("队伍模块清单损坏");
    auto manifest = doc.object();
    if (manifest["format"] != "okww-native-team" || manifest["abi"].toInt() != 1 ||
        manifest["original_sources"].toObject().size() != 3)
        fail("队伍模块格式不受支持");
    members(manifest["team"]);
    if (manifest["id"].toString().isEmpty())
        fail("队伍模块缺少标识");
    return {library, manifest, QFileInfo(path).absoluteFilePath(), create};
}
TeamStore::TeamStore(QString root, QString data)
    : root_(std::move(root)), data_(data), directory_(data + "/configs-dualpc/custom_teams"),
      moduleDirectory_(data + "/native-teams"), deletedDirectory_(data + "/deleted-teams") {
    QStringList directories{root_ + "/native-teams"};
    if (QFile::exists(root_ + "/native/CMakeLists.txt"))
        directories.append(root_ + "/build-native/Release");
    directories.removeDuplicates();
    for (const auto &directory : directories)
        for (const auto &name : QStringList{"lucy", "cart", "denia", "aemeath", "hsin"}) {
            const auto path = directory + "/okww_team_" + name + ".dll";
            if (!QFile::exists(path))
                continue;
            auto module = loadModule(path);
            modules_[key(members(module.manifest["team"]))] = std::move(module);
        }
    // Only explicitly activated imports may override bundled modules. Failed
    // imports or older DLLs left in this directory are never auto-activated.
    const auto registry = moduleDirectory_ + "/active.json";
    if (QFile::exists(registry)) {
        const auto active = readJson(registry);
        for (auto it = active.begin(); it != active.end(); ++it) {
            const auto file = it.value().toString();
            if (!QRegularExpression("^okww_team_[0-9a-f]{64}\\.dll$").match(file).hasMatch())
                fail("原生队伍注册文件损坏");
            const auto path = moduleDirectory_ + "/" + file;
            if (file != "okww_team_" + digest(bytes(path)) + ".dll")
                fail("原生队伍模块校验失败");
            auto module = loadModule(path);
            if (key(members(module.manifest["team"])) != it.key())
                fail("队伍注册记录与模块不一致");
            modules_[it.key()] = std::move(module);
        }
    }
}
bool TeamStore::pythonTeam(const QStringList &team) const {
    return std::all_of(team.begin(), team.end(), [&](const auto &member) {
        return QFile::exists(folder(team) + '/' + member + ".import.py");
    });
}
QJsonObject TeamStore::pythonCommand(QJsonObject request) const {
    std::lock_guard lock(mutex_);
    if (!fileSession_) fileSession_ = std::make_shared<WorkshopFileSession>(root_, data_);
    return fileSession_->command(std::move(request));
}
void TeamStore::verifySources(const QStringList &team, const Module &module) const {
    const auto originals = module.manifest["original_sources"].toObject();
    int present = 0;
    for (auto it = originals.begin(); it != originals.end(); ++it) {
        if (QFileInfo(it.key()).fileName() != it.key())
            fail("队伍源码清单路径无效");
        const auto path = folder(team) + "/" + it.key();
        if (QFile::exists(path)) {
            ++present;
            if (digest(bytes(path, 8 * 1024 * 1024)) != it.value().toString())
                fail("个人队伍源文件已修改，需要重新迁移后使用：" + key(team) + "/" + it.key());
        }
    }
    if (present == 3)
        return;
    if (present != 0)
        fail("队伍原始源码不完整，不能静默使用旧连招");
    const auto receipt = folder(team) + "/native-source.json";
    if (!QFile::exists(receipt))
        fail("此队伍还没有匹配的原生迁移记录，请导入原生队伍包");
    const auto saved = readJson(receipt);
    if (saved["id"] != module.manifest["id"] ||
        saved["original_sources"] != module.manifest["original_sources"])
        fail("原生队伍与迁移记录不匹配");
}
QJsonArray TeamStore::catalog() const {
    std::lock_guard lock(mutex_);
    QJsonArray result;
    for (const auto &entry : QDir(directory_).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = entry.absoluteFilePath() + "/team.json";
        if (!QFile::exists(path))
            continue;
        auto config = readJson(path);
        const auto team = members(config["team"]);
        const auto id = key(team);
        QJsonArray labels;
        for (const auto &name : team)
            labels.append(name);
        auto item = QJsonObject{{"team", QJsonArray::fromStringList(team)},
                                {"name", config["name"].toString(team.join(" / "))},
                                {"labels", labels},
                                {"mode", config["code_mode"].toString("builtin")},
                                {"native", true}};
        if (pythonTeam(team)) {
            item["python"] = true;
            QJsonArray sources;
            for (const auto &member : team)
                sources.append(member + ".py");
            item["source_files"] = sources;
            item["available"] = !workshopPython(root_).isEmpty();
            if (!item["available"].toBool())
                item["error"] = "缺少工坊 Python 兼容引擎，请使用完整包";
        } else if (modules_.contains(id)) {
            item["source_files"] =
                QJsonArray::fromStringList(modules_[id].manifest["sources"].toObject().keys());
            try {
                verifySources(team, modules_[id]);
                item["available"] = true;
            } catch (const std::exception &e) {
                item["available"] = false;
                item["error"] = errorText(e);
            }
        } else {
            item["available"] = false;
            item["error"] = "尚未导入此队伍的原生模块";
        }
        result.append(item);
    }
    return result;
}
void TeamStore::validateEnabled() const {
    const auto items = catalog();
    QJsonArray pythonTeams;
    QCryptographicHash sources(QCryptographicHash::Sha256);
    for (const auto &value : items) {
        const auto item = value.toObject();
        if (item["mode"] == "import" && !item["available"].toBool())
            fail(item["name"].toString() + "：" + item["error"].toString());
        if (!item["python"].toBool() || item["mode"] != "import")
            continue;
        pythonTeams.append(item["team"]);
        const auto directory = folder(members(item["team"]));
        sources.addData(key(members(item["team"])).toUtf8());
        sources.addData(bytes(directory + "/team.json"));
        QDirIterator files(directory, {"*.py"}, QDir::Files, QDirIterator::Subdirectories);
        QStringList paths;
        while (files.hasNext())
            paths.append(files.next());
        paths.sort();
        for (const auto &path : paths) {
            sources.addData(QDir(directory).relativeFilePath(path).toUtf8());
            sources.addData(bytes(path, 8 * 1024 * 1024));
        }
    }
    const auto fingerprint = sources.result();
    {
        std::lock_guard lock(mutex_);
        if (validatedPythonSources_ == fingerprint)
            return;
    }
    // Keep Python startup and imports off the GUI thread. All selected teams
    // share one checker; unchanged source contents reuse the successful check.
    if (!pythonTeams.isEmpty())
        pythonCommand({{"op", "validate"}, {"teams", pythonTeams}});
    std::lock_guard lock(mutex_);
    validatedPythonSources_ = fingerprint;
}
QString TeamStore::selection(CombatHost &host) const {
    QStringList team;
    for (auto *c : host.characters())
        if (c)
            team.append(canonical(c->state.className));
    if (team.size() != 3 || team.contains("BaseChar"))
        return {};
    std::lock_guard lock(mutex_);
    const auto config = teamConfig(team);
    if (config["code_mode"] != "import")
        return {};
    const auto id = key(team);
    if (pythonTeam(team))
        return "python:" + id;
    if (!modules_.contains(id))
        fail("当前队伍缺少原生模块：" + id);
    verifySources(team, modules_[id]);
    return id;
}
std::unique_ptr<TeamMacro> TeamStore::create(const QString &id, CombatHost &host) const {
    if (id.isEmpty())
        return {};
    if (id.startsWith("python:")) {
        auto result = std::make_unique<PythonTeamMacro>(host, root_, data_);
        result->configure();
        return result;
    }
    Module module;
    {
        std::lock_guard lock(mutex_);
        if (!modules_.contains(id)) fail("原生队伍模块不存在");
        module = modules_[id];
    }
    std::unique_ptr<TeamMacro> result(module.create(&host));
    if (!result)
        fail("原生队伍创建失败");
    result->configure();
    return result;
}
void TeamStore::setMode(const QJsonArray &value, const QString &mode) {
    std::lock_guard lock(mutex_);
    if (mode != "import" && mode != "builtin")
        fail("无效队伍模式");
    const auto team = members(value);
    auto config = teamConfig(team);
    if (config.isEmpty())
        fail("队伍不存在");
    if (pythonTeam(team)) {
        pythonCommand({{"op", "mode"}, {"team", QJsonArray::fromStringList(team)}, {"mode", mode}});
        return;
    }
    if (mode == "import") {
        if (!modules_.contains(key(team)))
            fail("请先导入此队伍的原生模块");
        verifySources(team, modules_[key(team)]);
    }
    config["code_mode"] = mode;
    saveConfig(team, config);
}
void TeamStore::createTeam(const QJsonArray &value) {
    std::lock_guard lock(mutex_);
    const auto team = members(value);
    if (!teamConfig(team).isEmpty())
        return;
    saveConfig(team, {{"name", team.join(" / ")}, {"code_mode", "builtin"}});
}
void TeamStore::setAllModes(const QString &mode) {
    std::lock_guard lock(mutex_);
    if (mode != "import" && mode != "builtin")
        fail("无效队伍模式");
    bool hasPython = false;
    for (auto value : catalog())
        hasPython |= value.toObject()["python"].toBool();
    if (hasPython)
        pythonCommand({{"op", "all_mode"}, {"mode", mode}});
    QVector<QPair<QStringList, QJsonObject>> before;
    for (auto value : catalog()) {
        const auto item = value.toObject();
        const auto team = members(item["team"]);
        if (pythonTeam(team) || !modules_.contains(key(team)))
            continue;
        if (mode == "import")
            verifySources(team, modules_[key(team)]);
        before.append({team, teamConfig(team)});
    }
    int changed = 0;
    try {
        for (const auto &entry : before) {
            auto config = entry.second;
            config["code_mode"] = mode;
            saveConfig(entry.first, config);
            ++changed;
        }
    } catch (...) {
        for (int i = 0; i < changed; ++i)
            saveConfig(before[i].first, before[i].second);
        throw;
    }
}
QString TeamStore::removeTeam(const QJsonArray &value) {
    std::lock_guard lock(mutex_);
    const auto team = members(value);
    if (teamConfig(team).isEmpty())
        fail("队伍不存在");
    const auto source = QDir::cleanPath(folder(team));
    const auto destination = QDir::cleanPath(deletedDirectory_ + '/' + key(team) + '-' +
                                             QUuid::createUuid().toString(QUuid::Id128));
    if (!source.startsWith(QDir::cleanPath(directory_) + '/', Qt::CaseInsensitive) ||
        !destination.startsWith(QDir::cleanPath(deletedDirectory_) + '/', Qt::CaseInsensitive))
        fail("队伍路径越界");
    if (!QDir().mkpath(deletedDirectory_) || !QDir().rename(source, destination))
        fail("无法将队伍移入恢复目录，原配置保留");
    return destination;
}
QJsonObject TeamStore::code(const QJsonArray &value, const QString &source) const {
    std::lock_guard lock(mutex_);
    const auto team = members(value);
    const auto id = key(team);
    if (!modules_.contains(id))
        return {{"code", "此队伍使用内置 C++ 连招，尚无导入的原生源码。"}, {"read_only", true}};
    const auto sources = modules_[id].manifest["sources"].toObject();
    if (!sources.contains(source) || !safeSource(source))
        fail("队伍源文件不存在");
    const auto draft = folder(team) + "/drafts/" + source;
    return {{"code", QFile::exists(draft) ? QString::fromUtf8(bytes(draft, 8 * 1024 * 1024))
                                          : sources[source].toString()},
            {"read_only", false},
            {"draft", QFile::exists(draft)},
            {"language", "cpp"}};
}
void TeamStore::saveDraft(const QJsonArray &value, const QString &source, const QString &text) {
    std::lock_guard lock(mutex_);
    const auto team = members(value);
    const auto id = key(team);
    if (pythonTeam(team) && source.endsWith(".py")) {
        const auto member = source.left(source.size() - 3);
        if (!team.contains(member))
            fail("角色不属于当前队伍");
        pythonCommand({{"op", "save"}, {"team", value}, {"member", member}, {"code", text}});
        return;
    }
    if (!modules_.contains(id) || !modules_[id].manifest["sources"].toObject().contains(source) ||
        !safeSource(source))
        fail("队伍源文件不存在");
    if (text.toUtf8().size() > 8 * 1024 * 1024)
        fail("队伍源码过大");
    saveBytes(folder(team) + "/drafts/" + source, text.toUtf8());
}
void TeamStore::exportTeam(const QJsonArray &value, const QString &destination) const {
    std::lock_guard lock(mutex_);
    const auto team = members(value);
    const auto id = key(team);
    if (pythonTeam(team)) {
        pythonCommand({{"op", "export"}, {"team", value}, {"path", destination}});
        return;
    }
    if (!modules_.contains(id))
        fail("内置队伍没有可导出的原生模块");
    const auto &module = modules_[id];
    verifySources(team, module);
    const auto binary = bytes(module.path);
    writeJson(destination, {{"format", "okww-native-team-package"},
                            {"abi", 1},
                            {"sha256", digest(binary)},
                            {"module", QString::fromLatin1(binary.toBase64())},
                            {"manifest", module.manifest}});
}
void TeamStore::importTeam(const QString &path) {
    std::lock_guard lock(mutex_);
    if (path.endsWith(".zip", Qt::CaseInsensitive)) {
        pythonCommand({{"op", "import"}, {"path", path}});
        return;
    }
    const auto raw = bytes(path, 96 * 1024 * 1024);
    const auto package = QJsonDocument::fromJson(raw).object();
    if (package["format"] != "okww-native-team-package" || package["abi"].toInt() != 1)
        fail("请选择工坊队伍 ZIP（.zip）或原生队伍包（.okww-team）。Python 工坊代码可直接导入，无需转为 C++。");
    const auto manifest = package["manifest"].toObject();
    const auto team = members(manifest["team"]);
    const auto binary = QByteArray::fromBase64(package["module"].toString().toLatin1(),
                                               QByteArray::AbortOnBase64DecodingErrors);
    if (binary.isEmpty() || binary.size() > 64 * 1024 * 1024 ||
        digest(binary) != package["sha256"].toString())
        fail("原生队伍包校验失败");
    const auto target = moduleDirectory_ + "/okww_team_" + digest(binary) + ".dll";
    if (!QFile::exists(target))
        saveBytes(target, binary);
    else if (bytes(target) != binary)
        fail("原生队伍模块文件冲突");
    auto module = loadModule(target);
    if (module.manifest != manifest)
        fail("原生队伍包清单与模块不一致");
    // Imported source already on disk must agree; importing a compiled module
    // never silently discards edited Python or C++ drafts.
    const auto receipt = folder(team) + "/native-source.json";
    bool anyOriginal = false;
    for (const auto &file : module.manifest["original_sources"].toObject().keys())
        anyOriginal |= QFile::exists(folder(team) + "/" + file);
    if (anyOriginal)
        verifySources(team, module);
    QJsonObject config = teamConfig(team);
    if (config.isEmpty())
        config = {{"name", manifest["name"]},
                  {"author", manifest["author"]},
                  {"description", manifest["description"]}};
    writeJson(receipt, {{"id", manifest["id"]}, {"original_sources", manifest["original_sources"]}});
    const auto registry = moduleDirectory_ + "/active.json";
    auto active = QFile::exists(registry) ? readJson(registry) : QJsonObject{};
    active[key(team)] = QFileInfo(target).fileName();
    writeJson(registry, active);
    modules_[key(team)] = std::move(module);
    config["code_mode"] = "import";
    saveConfig(team, config);
}
} // namespace okww::runtime
