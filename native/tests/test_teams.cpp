#include "runtime/team_store.hpp"
#include "team_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
class TeamTests final : public QObject {
    Q_OBJECT
    const QString root = OKWW_SOURCE_ROOT;
    QJsonObject manifest(const QString &id) const {
        return readJson(root + "/native/personal/manifests/" + id + ".json");
    }
    void migrated(const QString &directory, const QJsonObject &m, TeamStore &store) {
        const auto team = m["team"].toArray();
        store.createTeam(team);
        QStringList names;
        for (const auto &name : team)
            names.append(name.toString());
        names.sort();
        writeJson(directory + "/configs-dualpc/custom_teams/" + names.join("__") + "/native-source.json",
                  {{"id", m["id"]}, {"original_sources", m["original_sources"]}});
    }
  private slots:
    void builtinEditorsCoverAllCharactersAndRejectOtherTeamMembers() {
        QTemporaryDir data;
        TeamStore store(root, data.path());
        const auto characters = readJson(root + "/native/resources/characters.json")["characters"].toArray();
        for (auto value : characters) {
            const auto member = value.toObject()["class"].toString();
            QJsonArray team{member};
            for (const auto &support : {QString("Verina"), QString("Baizhi"), QString("Mortefi")})
                if (!team.contains(support) && team.size() < 3)
                    team.append(support);
            store.createTeam(team);
            const auto result = store.editor(team, member);
            QCOMPARE(result["character"].toString(), member);
            QCOMPARE(result["code_kind"].toString(), QString("builtin"));
            QVERIFY(result["read_only"].toBool());
            if (member == "Chixia" || member == "Youhu")
                QVERIFY(result["code"].toString().contains("void Character::doPerform("));
            else
                QVERIFY2(result["code"].toString().contains("class " + member + " final"),
                         qPrintable(member));
            QVERIFY(!result["file_choices"].toArray().isEmpty());
        }
        const QJsonArray team{"Cartethyia", "Chisa", "Ciaccona"};
        store.createTeam(team);
        auto result = store.editor(team, "Chisa");
        QVERIFY(result["code"].toString().contains("class Chisa final"));
        QVERIFY(!result["code"].toString().contains("class Denia final"));
        QVERIFY_EXCEPTION_THROWN(store.editor(team, "Lucy"), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(store.editor(team, "Chisa", "../character.cpp"), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(store.editor(team, "Chisa", {}, "import"), std::runtime_error);
    }
    void memberEditorsFollowActiveModeAndKeepSourceFilesSeparate() {
        for (const auto &id : QStringList{"lucy", "cart", "denia", "aemeath", "hsin"}) {
            QTemporaryDir data;
            TeamStore store(root, data.path());
            const auto m = manifest(id);
            const auto team = m["team"].toArray();
            migrated(data.path(), m, store);
            store.setMode(team, "import");
            for (auto value : team) {
                const auto member = value.toString();
                const auto imported = store.editor(team, member);
                QCOMPARE(imported["character"].toString(), member);
                QCOMPARE(imported["code_kind"].toString(), QString("import"));
                const auto source = imported["source_file"].toString();
                QCOMPARE(imported["code"], m["sources"].toObject()[source]);
                QVERIFY(imported["shared_team_code"].toBool());
                store.setMode(team, "builtin");
                const auto builtin = store.editor(team, member);
                QCOMPARE(builtin["code_kind"].toString(), QString("builtin"));
                QVERIFY(builtin["read_only"].toBool());
                const auto retained = store.editor(team, member, {}, "import");
                QCOMPARE(retained["code"], imported["code"]);
                QVERIFY(retained["view_only"].toBool());
                QVERIFY(retained["read_only"].toBool());
                QCOMPARE(store.catalog()[0].toObject()["mode"].toString(), QString("builtin"));
                store.setMode(team, "import");
            }
        }
    }
    void restoredTeamActionsPreserveDraftsAndSkipNeverImportedTeams() {
        QTemporaryDir data;
        TeamStore store(root, data.path());
        const auto m = manifest("lucy");
        const auto team = m["team"].toArray();
        migrated(data.path(), m, store);
        const QJsonArray neverImported{"Cartethyia", "Chisa", "Ciaccona"};
        store.createTeam(neverImported);
        store.setAllModes("import");
        int imported = 0, builtin = 0;
        for (auto value : store.catalog()) {
            auto t = value.toObject();
            if (t["mode"] == "import")
                ++imported;
            else
                ++builtin;
        }
        QCOMPARE(imported, 1);
        QCOMPARE(builtin, 1);
        const auto source = m["sources"].toObject().keys().first();
        store.saveDraft(team, source, "// recoverable draft");
        const auto destination = store.removeTeam(team);
        QVERIFY(destination.startsWith(data.path() + "/deleted-teams/"));
        QVERIFY(QFile::exists(destination + "/team.json"));
        QVERIFY(QFile::exists(destination + "/native-source.json"));
        QFile draft(destination + "/drafts/" + source);
        QVERIFY(draft.open(QIODevice::ReadOnly));
        QCOMPARE(draft.readAll(), QByteArray("// recoverable draft"));
        QCOMPARE(store.catalog().size(), 1);
        QCOMPARE(store.catalog()[0].toObject()["mode"].toString(), QString("builtin"));
    }
    void allModulesExecuteViaStoreAndPreserveChoices() {
        for (const auto &id : QStringList{"lucy", "cart", "denia", "aemeath", "hsin"}) {
            QTemporaryDir temp;
            const auto m = manifest(id);
            const auto team = m["team"].toArray();
            {
                TeamStore store(root, temp.path());
                migrated(temp.path(), m, store);
                test::TeamReplay host({{"team", id},
                                       {"entry", id == "aemeath" ? 2
                                                 : id == "denia" ? 1
                                                                 : 0},
                                       {"cancel_at", 103.0}});
                QVERIFY(store.selection(host).isEmpty());
                store.setMode(team, "import");
                store.validateEnabled();
                QVERIFY(!store.selection(host).isEmpty());
                auto macro = store.create(store.selection(host), host);
                QVERIFY(macro);
                QVERIFY_EXCEPTION_THROWN(macro->perform(host.entry()), Cancelled);
                macro.reset();
                QVERIFY(!host.macroBusy);
                QVERIFY(!host.skipCombatCheck);
                QVERIFY(host.held.isEmpty());
                QVERIFY(!host.events.isEmpty());
                QCOMPARE(store.catalog().size(), 1);
                QVERIFY(store.catalog()[0].toObject()["available"].toBool());
                const auto files = m["sources"].toObject().keys();
                QVERIFY(!files.isEmpty());
                QCOMPARE(store.code(team, files[0])["code"], m["sources"].toObject()[files[0]]);
                store.saveDraft(team, files[0], "// local draft");
                QCOMPARE(store.code(team, files[0])["code"].toString(), QString("// local draft"));
                QVERIFY(store.code(team, files[0])["draft"].toBool());
                QVERIFY_EXCEPTION_THROWN(store.saveDraft(team, "../outside.cpp", "x"), std::runtime_error);
            }
            {
                TeamStore store(root, temp.path());
                test::TeamReplay host({{"team", id}});
                QVERIFY(!store.selection(host).isEmpty());
                store.setMode(team, "builtin");
                QVERIFY(store.selection(host).isEmpty());
            }
            TeamStore store(root, temp.path());
            QCOMPARE(store.catalog()[0].toObject()["mode"].toString(), QString("builtin"));
        }
    }
    void exportImportAndRegistryDoNotActivateOrphans() {
        QTemporaryDir source, destination, emptyRoot;
        const auto m = manifest("lucy");
        const auto team = m["team"].toArray();
        const auto package = source.path() + "/team.okww-team";
        TeamStore original(root, source.path());
        migrated(source.path(), m, original);
        original.setMode(team, "import");
        original.exportTeam(team, package);
        {
            TeamStore store(emptyRoot.path(), destination.path());
            store.importTeam(package);
            store.importTeam(package);
            store.validateEnabled();
            test::TeamReplay host({});
            QVERIFY(!store.selection(host).isEmpty());
            const auto copied = destination.path() + "/again.okww-team";
            store.exportTeam(team, copied);
            QCOMPARE(readJson(package), readJson(copied));
            auto damaged = readJson(package);
            damaged["sha256"] = "invalid";
            writeJson(copied, damaged);
            QVERIFY_EXCEPTION_THROWN(store.importTeam(copied), std::runtime_error);
            QVERIFY(!store.selection(host).isEmpty());
            QFile orphan(destination.path() + "/native-teams/okww_team_orphan.dll");
            QVERIFY(orphan.open(QIODevice::WriteOnly));
            orphan.write("not a DLL");
        }
        TeamStore reopened(emptyRoot.path(), destination.path());
        reopened.validateEnabled();
        test::TeamReplay host({});
        QVERIFY(!reopened.selection(host).isEmpty());
        auto macro = reopened.create(reopened.selection(host), host);
        QVERIFY(macro->perform(host.entry()));
        QVERIFY(!host.events.isEmpty());
    }
    void changedOriginalFailsClosedAndBuiltinRemainsAvailable() {
        QTemporaryDir temp;
        const auto m = manifest("lucy");
        const auto team = m["team"].toArray();
        TeamStore store(root, temp.path());
        migrated(temp.path(), m, store);
        store.setMode(team, "import");
        QFile changed(temp.path() + "/configs-dualpc/custom_teams/Lucy__Rebecca__ShoreKeeper/Lucy.py");
        QVERIFY(changed.open(QIODevice::WriteOnly));
        changed.write("# modified");
        changed.close();
        QVERIFY_EXCEPTION_THROWN(store.validateEnabled(), std::runtime_error);
        test::TeamReplay host({});
        QVERIFY_EXCEPTION_THROWN(store.selection(host), std::runtime_error);
        store.setMode(team, "builtin");
        store.validateEnabled();
        QVERIFY(store.selection(host).isEmpty());
        QVERIFY_EXCEPTION_THROWN(store.setMode(team, "import"), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(store.createTeam({"Lucy", "Lucy", "Rebecca"}), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(store.createTeam({"../escape", "Lucy", "Rebecca"}), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(TeamTests)
#include "test_teams.moc"
