#include "reference_team_store.hpp"
#include "team_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace okww::runtime {
struct TeamStoreContractAccess {
    template <class T> static QStringList members(QJsonValue value) { return T::members(value); }
    template <class T> static QString key(QStringList value) { return T::key(value); }
    template <class T> static QJsonObject config(T &store, QStringList team) {
        return store.teamConfig(team);
    }
    template <class T> static void save(T &store, QStringList team, QJsonObject config) {
        store.saveConfig(team, config);
    }
    template <class T> static void verify(T &store, QStringList team, QJsonObject manifest) {
        typename T::Module module;
        module.manifest = manifest;
        store.verifySources(team, module);
    }
    template <class T> static QJsonObject module(T &store, const QString &file) {
        const auto module = store.loadModule(file);
        return {{"manifest", module.manifest}, {"path", module.path}, {"factory", module.create != nullptr}};
    }
    template <class T> static QString fingerprint(T &store) {
        return QString::fromLatin1(store.validatedPythonSources_.toHex());
    }
    template <class T>
    static void inject(T &store, const QString &id, QJsonObject manifest, const QString &path) {
        typename T::Module module;
        module.manifest = manifest;
        module.path = path;
        store.modules_[id] = module;
    }
};
} // namespace okww::runtime
namespace {
template <class T> QJsonObject identity(QJsonValue value) {
    try {
        return {{"members", QJsonArray::fromStringList(TeamStoreContractAccess::members<T>(value))}};
    } catch (const std::exception &e) {
        return {{"error", errorText(e)}};
    }
}
template <class T> QJsonObject configurations(int scene) {
    QTemporaryDir fixture, project;
    QString root = fixture.path();
    if (scene % 13 == 0) {
        root += "/blocked";
        QFile blocked(root);
        if (!blocked.open(QIODevice::WriteOnly))
            fail("Cannot create blocker");
    }
    T store(project.path(), root);
    QJsonArray trace;
    const QJsonArray team{"Baizhi", "Mortefi", "Verina"};
    const QStringList names{"Baizhi", "Mortefi", "Verina"};
    auto run = [&](QString op, auto fn) {
        QJsonObject event{{"op", op}};
        try {
            event["result"] = fn();
        } catch (const std::exception &e) {
            event["error"] = errorText(e);
        }
        trace.append(event);
    };
    run("before", [&] { return QJsonValue(TeamStoreContractAccess::config(store, names)); });
    run("create", [&] {
        store.createTeam(team);
        return QJsonValue(true);
    });
    run("read", [&] { return QJsonValue(TeamStoreContractAccess::config(store, names)); });
    QJsonObject values{{"team", "incorrect"},
                       {"name", QString("队伍 %1").arg(scene)},
                       {"code_mode", scene % 2 ? "import" : "builtin"},
                       {"extra", QJsonArray{true, 2, "x"}}};
    run("save", [&] {
        TeamStoreContractAccess::save(store, names, values);
        return QJsonValue(true);
    });
    run("again", [&] { return QJsonValue(TeamStoreContractAccess::config(store, names)); });
    run("idempotent", [&] {
        store.createTeam(team);
        return QJsonValue(true);
    });
    run("final", [&] { return QJsonValue(TeamStoreContractAccess::config(store, names)); });
    run("catalog", [&] { return QJsonValue(store.catalog()); });
    return {{"trace", trace}};
}
template <class T> QJsonObject verification(int scene) {
    QTemporaryDir fixture, project;
    T store(project.path(), fixture.path());
    const QStringList team{"Baizhi", "Mortefi", "Verina"};
    const auto folder = fixture.path() + "/configs-dualpc/custom_teams/Baizhi__Mortefi__Verina";
    QDir().mkpath(folder);
    QJsonObject sources;
    for (int i = 0; i < 3; ++i) {
        const QString name = scene % 19 == 0 && i == 1 ? "../invalid.py" : QString("file%1.py").arg(i);
        const QByteArray bytes = QByteArray::number(scene) + QByteArray::number(i);
        sources[name] =
            QString::fromLatin1(QCryptographicHash::hash(scene % 13 == 0 ? QByteArray("mismatch") : bytes,
                                                         QCryptographicHash::Sha256)
                                    .toHex());
        if (scene & (1 << i)) {
            QFile file(folder + '/' + name);
            if (file.open(QIODevice::WriteOnly))
                file.write(bytes);
        }
    }
    QJsonObject manifest{{"id", "fixture"}, {"original_sources", sources}};
    if (scene % 11)
        writeJson(folder + "/native-source.json",
                  {{"id", scene % 17 ? "fixture" : "wrong"},
                   {"original_sources", scene % 23 ? sources : QJsonObject{}}});
    try {
        TeamStoreContractAccess::verify(store, team, manifest);
        return {{"ok", true}};
    } catch (const std::exception &e) {
        return {{"error", errorText(e)}};
    }
}
template <class T> QJsonObject moduleOutcome(const QString &root, const QString &data, const QString &file) {
    try {
        T store(root, data);
        return TeamStoreContractAccess::module(store, file);
    } catch (const std::exception &e) {
        return {{"error", errorText(e)}};
    }
}
QString projectRoot() { return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../.."); }
void writeFixtureText(const QString &path, const QByteArray &text);
QJsonValue normalizeFixture(QJsonValue value, const QString &root) {
    if (value.isString()) {
        auto text = value.toString();
        text.replace(root, "<fixture>");
        text.replace(QDir::toNativeSeparators(root), "<fixture>");
        text.replace(QRegularExpression("(?<=-)[0-9a-f]{32}(?=/|$)"), "<uuid>");
        return text;
    }
    if (value.isArray()) {
        QJsonArray out;
        for (auto v : value.toArray())
            out.append(normalizeFixture(v, root));
        return out;
    }
    if (value.isObject()) {
        auto out = value.toObject();
        for (auto it = out.begin(); it != out.end(); ++it)
            it.value() = normalizeFixture(it.value(), root);
        return out;
    }
    return value;
}
QJsonArray fixtureFiles(const QString &root) {
    QStringList out;
    QDirIterator walk(root, QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const auto path = walk.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            fail("Fixture read failed");
        auto bytes = file.readAll();
        if (path.endsWith(".json"))
            bytes = QJsonDocument(normalizeFixture(QJsonDocument::fromJson(bytes).object(), root).toObject())
                        .toJson(QJsonDocument::Compact);
        out << normalizeFixture(QDir(root).relativeFilePath(path), root).toString() + ":" +
                   QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    }
    out.sort();
    return QJsonArray::fromStringList(out);
}
template <class T> QJsonObject mutationReplay(int scene) {
    QTemporaryDir fixture, project;
    T store(project.path(), fixture.path());
    const QJsonArray team{"Baizhi", "Mortefi", "Verina"}, other{"Cartethyia", "Ciaccona", "Rover"};
    const auto base = fixture.path() + "/configs-dualpc/custom_teams/";
    const auto first = base + "Baizhi__Mortefi__Verina", second = base + "Cartethyia__Ciaccona__Rover";
    store.createTeam(team);
    store.createTeam(other);
    const QStringList names{"a.cpp",       "nested/b.hpp", "x.h",        "UPPER.CPP", "../escape.cpp",
                            "./local.cpp", "a//b.cpp",     "C:evil.cpp", "",          "a\\b.cpp"};
    QJsonObject sources, originals;
    for (const auto &name : names)
        sources[name] = "// original source\n";
    for (const auto &name : {"a.py", "b.py", "c.py"})
        originals[name] = "hash";
    QJsonObject manifest{
        {"id", "fixture"}, {"original_sources", originals}, {"sources", sources}, {"team", team}};
    const auto binary = fixture.path() + "/binary.dat";
    writeFixtureText(binary, "native fixture bytes");
    TeamStoreContractAccess::inject(store, "Baizhi__Mortefi__Verina", manifest, binary);
    TeamStoreContractAccess::inject(store, "Cartethyia__Ciaccona__Rover", manifest, binary);
    for (const auto &folder : {first, second})
        if (scene % 7)
            writeJson(folder + "/native-source.json", {{"id", "fixture"}, {"original_sources", originals}});
    QJsonArray trace;
    auto step = [&](QString op, auto fn) {
        QJsonObject event{{"op", op}};
        try {
            event["result"] = fn();
        } catch (const std::exception &e) {
            event["error"] = errorText(e);
        }
        event["files"] = fixtureFiles(fixture.path());
        trace.append(normalizeFixture(event, fixture.path()));
    };
    const auto source = names[scene % names.size()];
    step("code", [&] { return QJsonValue(store.code(team, source)); });
    step("draft", [&] {
        store.saveDraft(team, source, QString("// draft %1\n中文\n").arg(scene));
        return QJsonValue(true);
    });
    step("draft-code", [&] { return QJsonValue(store.code(team, source)); });
    step("import-mode", [&] {
        store.setMode(team, scene % 17 ? "import" : "invalid");
        return QJsonValue(true);
    });
    step("builtin-mode", [&] {
        store.setMode(team, "builtin");
        return QJsonValue(true);
    });
    HANDLE held = INVALID_HANDLE_VALUE;
    if (scene % 5 == 0) {
        const auto path = second + "/team.json";
        held = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE)
            fail("Fixture lock failed");
    }
    step("all-import", [&] {
        store.setAllModes(scene % 19 ? "import" : "invalid");
        return QJsonValue(true);
    });
    if (held != INVALID_HANDLE_VALUE)
        CloseHandle(held);
    step("all-builtin", [&] {
        store.setAllModes("builtin");
        return QJsonValue(true);
    });
    step("export", [&] {
        store.exportTeam(team, fixture.path() + "/package.okww-team");
        return QJsonValue(true);
    });
    step("remove", [&] { return QJsonValue(store.removeTeam(team)); });
    step("again", [&] { return QJsonValue(store.removeTeam(team)); });
    return {{"trace", trace}};
}
template <class T> QJsonObject importReplay(int scene) {
    QTemporaryDir fixture, project;
    T store(project.path(), fixture.path());
    QByteArray binary = "not a dll";
    QJsonObject package{
        {"format", "okww-native-team-package"},
        {"abi", 1},
        {"sha256", QString::fromLatin1(QCryptographicHash::hash(binary, QCryptographicHash::Sha256).toHex())},
        {"module", QString::fromLatin1(binary.toBase64())},
        {"manifest", QJsonObject{{"team", QJsonArray{"Baizhi", "Mortefi", "Verina"}}}}};
    switch (scene % 9) {
    case 0:
        package["format"] = "invalid";
        break;
    case 1:
        package["abi"] = 2;
        break;
    case 2:
        package["module"] = "???";
        break;
    case 3:
        package["sha256"] = "wrong";
        break;
    case 4:
        package["manifest"] = QJsonObject{{"team", QJsonArray{"Unknown", "Mortefi", "Verina"}}};
        break;
    case 5:
        package["module"] = "";
        break;
    default:
        break;
    }
    const auto path = fixture.path() + "/input.okww-team";
    writeJson(path, package);
    QJsonArray trace;
    for (int i = 0; i < 2; ++i) {
        QJsonObject item;
        try {
            store.importTeam(path);
            item["ok"] = true;
        } catch (const std::exception &e) {
            item["error"] = errorText(e);
        }
        item["files"] = fixtureFiles(fixture.path());
        trace.append(normalizeFixture(item, fixture.path()));
    }
    return {{"trace", trace}};
}
struct CatalogParty final : test::TeamReplay {
    int scene;
    explicit CatalogParty(int s)
        : TeamReplay({{"names", QJsonArray{s % 17 ? "Baizhi" : "BaseChar", "Mortefi", "Verina"}}}), scene(s) {
    }
    QVector<Character *> characters() override {
        auto out = TeamReplay::characters();
        if (scene % 11 == 0)
            out[0] = nullptr;
        if (scene % 13 == 0)
            out.removeLast();
        return out;
    }
};
void writeFixtureText(const QString &path, const QByteArray &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size())
        fail("Fixture write failed");
}
template <class T> QJsonObject catalogueReplay(int scene) {
    QTemporaryDir fixture;
    const auto custom = fixture.path() + "/configs-dualpc/custom_teams";
    const auto folder = custom + "/Baizhi__Mortefi__Verina";
    const QJsonArray team{"Baizhi", "Mortefi", "Verina"};
    writeJson(folder + "/team.json",
              {{"team", scene % 31 ? QJsonValue(team) : QJsonValue("Unknown,Mortefi,Verina")},
               {"name", QString("队伍%1").arg(scene)},
               {"code_mode", scene % 2 ? "import" : "builtin"}});
    for (int i = 0; i < 3; ++i)
        if (scene & (1 << i))
            writeFixtureText(folder + '/' + team[i].toString() + ".import.py", "# saved\n");
    QDir().mkpath(custom + "/empty");
    const auto nativeFolder = custom + "/Cartethyia__Ciaccona__Rover";
    writeJson(nativeFolder + "/team.json", {{"team", QJsonArray{"Cartethyia", "Ciaccona", "Rover"}},
                                            {"code_mode", scene % 3 ? "builtin" : "import"}});
    const auto manifestPath = projectRoot() + "/native/personal/manifests/cart.json";
    if (scene % 5 && QFile::exists(manifestPath)) {
        auto manifest = readJson(manifestPath);
        writeJson(nativeFolder + "/native-source.json",
                  {{"id", manifest["id"]}, {"original_sources", manifest["original_sources"]}});
    }
    T store(projectRoot(), fixture.path());
    CatalogParty host(scene);
    QJsonObject result;
    try {
        result["catalog"] = store.catalog();
    } catch (const std::exception &e) {
        result["catalog_error"] = errorText(e);
    }
    try {
        result["selection"] = store.selection(host);
    } catch (const std::exception &e) {
        result["selection_error"] = errorText(e);
    }
    return result;
}
template <class T> QJsonObject fingerprintReplay(int scene) {
    QTemporaryDir fixture;
    const auto folder = fixture.path() + "/configs-dualpc/custom_teams/Baizhi__Mortefi__Verina";
    const QJsonArray team{"Baizhi", "Mortefi", "Verina"};
    writeJson(folder + "/team.json", {{"team", team}, {"code_mode", "import"}});
    for (auto value : team) {
        const auto name = value.toString();
        const auto code =
            ("from src.char." + name + " import " + name + " as Base\nclass " + name + "(Base):\n    pass\n")
                .toUtf8();
        writeFixtureText(folder + '/' + name + ".import.py", code);
        writeFixtureText(folder + '/' + name + ".py", code);
    }
    T store(projectRoot(), fixture.path());
    QJsonArray trace;
    auto check = [&] {
        QJsonObject item;
        try {
            store.validateEnabled();
            item["ok"] = true;
        } catch (const std::exception &e) {
            auto error = errorText(e);
            error.replace(fixture.path(), "<fixture>");
            error.replace(QDir::toNativeSeparators(fixture.path()), "<fixture>");
            item["error"] = error;
        }
        item["fingerprint"] = TeamStoreContractAccess::fingerprint(store);
        trace.append(item);
    };
    check();
    check();
    const auto changed = QString("# scene %1\n").arg(scene).toUtf8();
    for (const auto &suffix : {".py", ".import.py"}) {
        QFile file(folder + "/Baizhi" + suffix);
        if (!file.open(QIODevice::Append))
            fail("Fixture append failed");
        file.write(changed);
    }
    check();
    check();
    if (scene % 2)
        writeFixtureText(folder + "/Baizhi.import.py", "syntax broken here !!!");
    else
        writeFixtureText(folder + "/nested/extra.py", "# affects source fingerprint\n");
    check();
    check();
    return {{"trace", trace}};
}
} // namespace
class TeamStoreContracts final : public QObject {
    Q_OBJECT
  private slots:
    void identity_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void identity() {
        QFETCH(int, scene);
        const QStringList names{"Baizhi",   "Mortefi", "Verina", " shoreKEEPER ", "Lynae",      "Linnai",
                                "BaseChar", "Unknown", "",       "Chisa",         "Cartethyia", "Ciaccona"};
        QJsonArray values;
        for (int i = 0; i < scene % 6; ++i)
            values.append(names[(scene / 6 + i * 5) % names.size()]);
        if (scene % 23 == 0 && !values.empty())
            values[0] = QJsonObject{{"class", "Baizhi"}};
        QJsonValue input = values;
        if (scene % 3 == 1) {
            QStringList words;
            for (auto v : values)
                words << v.toString();
            input = words.join(',');
        }
        if (scene % 31 == 0)
            input = true;
        if (scene % 37 == 0)
            input = QJsonValue{};
        QCOMPARE(::identity<TeamStore>(input), ::identity<team_store_reference::OriginalTeamStore>(input));
    }
    void keys_data() { identity_data(); }
    void keys() {
        QFETCH(int, scene);
        const QStringList words{"a",
                                "A",
                                "_",
                                "中文",
                                "",
                                QString(QChar(char16_t(0xd800))),
                                "Baizhi",
                                "__",
                                "Lynae",
                                " Shorekeeper ",
                                QString(QChar(char16_t(0)))};
        QStringList value;
        for (int i = 0; i < scene % 17; ++i)
            value << words[(scene + i * 7) % words.size()];
        QCOMPARE(TeamStoreContractAccess::key<TeamStore>(value),
                 TeamStoreContractAccess::key<team_store_reference::OriginalTeamStore>(value));
    }
    void config_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void config() {
        QFETCH(int, scene);
        QCOMPARE(configurations<TeamStore>(scene),
                 configurations<team_store_reference::OriginalTeamStore>(scene));
    }
    void verify_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void verify() {
        QFETCH(int, scene);
        QCOMPARE(verification<TeamStore>(scene),
                 verification<team_store_reference::OriginalTeamStore>(scene));
    }
    void modules_data() {
        QTest::addColumn<QString>("file");
        for (const auto &name : {"missing.dll", "okww_team_lucy.dll", "okww_team_cart.dll",
                                 "okww_team_denia.dll", "okww_team_aemeath.dll", "okww_team_hsin.dll"})
            QTest::newRow(name) << QCoreApplication::applicationDirPath() + '/' + name;
    }
    void modules() {
        QFETCH(QString, file);
        QTemporaryDir root, data;
        QCOMPARE(moduleOutcome<TeamStore>(root.path(), data.path(), file),
                 moduleOutcome<team_store_reference::OriginalTeamStore>(root.path(), data.path(), file));
    }
    void rejectsOldRuntimeBeforeItsEntryPoints() {
        // The frozen oracle predates runtime ABI 2. Only the guarded loader
        // may see this fixture: its manifest/factory deliberately abort.
        if (!QFile::exists(QCoreApplication::applicationDirPath()+"/okww_team_legacy_fixture.dll"))
            QSKIP("Legacy runtime guard fixture is built with the optional personal module tests");
        QTemporaryDir root, data;
        const auto result = moduleOutcome<TeamStore>(root.path(), data.path(),
                                                     QCoreApplication::applicationDirPath() +
                                                         "/okww_team_legacy_fixture.dll");
        QVERIFY(result["error"].toString().contains("重新编译"));
    }
    void catalogue_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 256; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void catalogue() {
        QFETCH(int, scene);
        QCOMPARE(catalogueReplay<TeamStore>(scene),
                 catalogueReplay<team_store_reference::OriginalTeamStore>(scene));
    }
    void fingerprint_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 8; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void fingerprint() {
        QFETCH(int, scene);
        QCOMPARE(fingerprintReplay<TeamStore>(scene),
                 fingerprintReplay<team_store_reference::OriginalTeamStore>(scene));
    }
    void mutations_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void mutations() {
        QFETCH(int, scene);
        QCOMPARE(mutationReplay<TeamStore>(scene),
                 mutationReplay<team_store_reference::OriginalTeamStore>(scene));
    }
    void imports_data() { mutations_data(); }
    void imports() {
        QFETCH(int, scene);
        QCOMPARE(importReplay<TeamStore>(scene),
                 importReplay<team_store_reference::OriginalTeamStore>(scene));
    }
};
QTEST_GUILESS_MAIN(TeamStoreContracts)
#include "test_team_store_contract.moc"
