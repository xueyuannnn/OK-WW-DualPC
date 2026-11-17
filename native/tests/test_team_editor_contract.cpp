#ifndef Q_MOC_RUN
#include "reference_team_editor.hpp"
#include "team_editor_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace okww::runtime {
struct TeamStoreContractAccess {
    template <class T> static void inject(T &store, const QString &id, const QJsonObject &sources) {
        typename T::Module module;
        module.manifest = {{"sources", sources}};
        store.modules_[id] = module;
    }
};
} // namespace okww::runtime
namespace {
QString excerpt(const QString &text, int start, bool rust) {
    try {
        return rust ? editor_test::definitionExcerpt(text, start)
                    : team_store_reference::definitionExcerpt(text, start);
    } catch (const std::exception &e) {
        return "ERROR:" + errorText(e);
    }
}
template <class T> QJsonObject editorReplay(int scene) {
    QTemporaryDir root, data;
    const auto classes = nativeCharacterClasses();
    const auto member = classes[scene % classes.size()];
    QJsonArray team{member};
    for (const auto &support : {"Verina", "Baizhi", "Mortefi"})
        if (!team.contains(support) && team.size() < 3)
            team.append(support);
    QStringList sorted;
    for (auto value : team)
        sorted << value.toString();
    sorted.sort();
    const auto id = sorted.join("__"), folder = data.path() + "/configs-dualpc/custom_teams/" + id;
    QDir().mkpath(folder);
    T store(root.path(), data.path());
    QJsonObject result;
    try {
        store.createTeam(team);
        const int kind = scene % 5;
        if (kind == 1 || kind == 2) {
            for (auto value : team) {
                QFile file(folder + '/' + value.toString() + ".import.py");
                if (!file.open(QIODevice::WriteOnly))
                    fail("Fixture write failed");
                file.write(scene == 1 ? QByteArray(8 * 1024 * 1024 + 1, 'x')
                                      : QByteArray("# workshop source\n"));
            }
        }
        if (kind >= 3) {
            QJsonObject sources{{"a.cpp", "// imported shared source"}, {"b.hpp", "// declarations"}};
            if (scene % 2)
                sources["hsin_core.cpp"] = "// hsin shared source";
            if (scene % 29 == 0)
                sources = {{"only.hpp", "// no entry"}};
            TeamStoreContractAccess::inject(store, id, sources);
        }
        auto config = readJson(folder + "/team.json");
        config["code_mode"] = kind == 1 || kind == 3 ? "import" : "builtin";
        writeJson(folder + "/team.json", config);
        QString view = kind == 2 || kind == 4 ? "import" : "active", requested;
        if (scene % 31 == 0)
            view = "invalid";
        if (scene % 7 == 0)
            requested = "not-owned.cpp";
        else if (scene % 11 == 0)
            requested = "character.hpp";
        else if (scene % 13 == 0)
            requested = member + ".py";
        if (scene % 17 == 0)
            requested = "b.hpp";
        const auto current = scene % 19 == 0 ? QString("Unknown") : member;
        result = store.editor(team, current, requested, view);
        if (kind == 0 && requested.isEmpty() && scene % 3 == 0)
            result = store.editor(team, current, "full:" + result["source_file"].toString(), view);
    } catch (const std::exception &e) {
        result = {{"error", errorText(e)}};
    }
    // The old choices/default excerpt remain identical; Rust sources are additive.
    if (result.contains("file_choices")) {
        QJsonArray legacy;
        for (auto choice : result["file_choices"].toArray())
            if (!choice.toObject()["id"].toString().startsWith("rust/"))
                legacy.append(choice);
        result["file_choices"] = legacy;
    }
    return result;
}
} // namespace
class TeamEditorContracts final : public QObject {
    Q_OBJECT
  private slots:
    void rustSourcesUseExistingSelector() {
        QTemporaryDir root, data;
        TeamStore store(root.path(), data.path());
        auto classes = nativeCharacterClasses();
        // BaseChar is the internal base implementation, not a selectable game character.
        classes.removeAll("BaseChar");
        QString activeMember, activeFile;
        try {
            for (const auto &member : classes) {
                activeMember = member;
                activeFile.clear();
                QJsonArray team{member};
                for (const auto &support : {"Verina", "Baizhi", "Mortefi"})
                    if (!team.contains(support) && team.size() < 3)
                        team.append(support);
                store.createTeam(team);
                const auto baseline = store.editor(team, member);
                int count = 0;
                for (auto choice : baseline["file_choices"].toArray()) {
                    const auto id = choice.toObject()["id"].toString();
                    if (!id.startsWith("rust/"))
                        continue;
                    activeFile = id;
                    const auto source = store.editor(team, member, id);
                    QVERIFY(source["read_only"].toBool());
                    QCOMPARE(source["source_file"].toString(), id);
                    QVERIFY(source["code"].toString().size() > 100);
                    ++count;
                }
                QCOMPARE(count, 2);
                QVERIFY_EXCEPTION_THROWN(store.editor(team, member, "rust/../private.rs"),
                                         std::runtime_error);
            }
        } catch (const std::exception &error) {
            QFAIL(qPrintable(activeMember + " / " + activeFile + " : " + errorText(error)));
        }
    }
    void excerpts_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void excerpts() {
        QFETCH(int, scene);
        const QStringList tokens{"{",
                                 "}",
                                 ";",
                                 "// ignored {}\n",
                                 "/* ignored { } */",
                                 "\"literal { \\\" }\"",
                                 "'{'",
                                 "'\\\''",
                                 "\n",
                                 "text",
                                 "\\",
                                 ""};
        QString text = "class Fixture ";
        for (int i = 0; i < scene % 35; ++i)
            text += tokens[(scene + i * 5) % tokens.size()];
        if (scene % 3)
            text += "{}; trailing";
        const int start = scene % 17 == 0 ? -1 : scene % 13 == 0 ? int(text.size()) : scene % 5;
        QCOMPARE(excerpt(text, start, true), excerpt(text, start, false));
    }
    void sources_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void sources() {
        QFETCH(int, scene);
        QCOMPARE(editorReplay<TeamStore>(scene),
                 editorReplay<team_store_reference::OriginalTeamStore>(scene));
    }
};
QTEST_GUILESS_MAIN(TeamEditorContracts)
#include "test_team_editor_contract.moc"
