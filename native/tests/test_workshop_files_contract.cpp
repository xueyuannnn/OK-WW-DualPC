#include "workshop_files_policy_replay.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
class WorkshopFileContracts final : public QObject {
    Q_OBJECT
  private slots:
    void exchange_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 512; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void exchange() {
        QFETCH(int, scene);
        workshop_file_test::Replay current(scene), original(scene);
        QCOMPARE(current.outcome(true, false), original.outcome(false, false));
    }
    void command_data() { exchange_data(); }
    void command() {
        QFETCH(int, scene);
        workshop_file_test::Replay current(scene), original(scene);
        QCOMPARE(current.outcome(true, true), original.outcome(false, true));
    }
    void paths_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 32; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void paths() {
        QFETCH(int, scene);
        QTemporaryDir root;
        auto touch = [&](const QString &p) {
            QDir().mkpath(QFileInfo(p).absolutePath());
            QFile file(p);
            QVERIFY(file.open(QIODevice::WriteOnly));
        };
        if (scene & 1)
            touch(root.path() + "/workshop-engine/native/bridge/workshop_worker.py");
        if (scene & 2)
            touch(root.path() + "/workshop-engine/runtime/python.exe");
        if (scene & 4)
            touch(root.path() + "/workshop-engine/.venv/Scripts/python.exe");
        if (scene & 8)
            touch(root.path() + "/runtime/python.exe");
        if (scene & 16)
            touch(root.path() + "/.venv/Scripts/python.exe");
        const auto engine = scene & 1 ? root.path() + "/workshop-engine" : root.path();
        QCOMPARE(workshopRoot(root.path()), engine);
        QString python;
        for (const auto &suffix : {"/runtime/python.exe", "/.venv/Scripts/python.exe"})
            if (QFile::exists(engine + suffix)) {
                python = engine + suffix;
                break;
            }
        QCOMPARE(workshopPython(root.path()), python);
    }
};
QTEST_GUILESS_MAIN(WorkshopFileContracts)
#include "test_workshop_files_contract.moc"
