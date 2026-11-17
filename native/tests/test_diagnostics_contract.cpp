#ifndef Q_MOC_RUN
#include "diagnostics_policy_replay.hpp"
#endif
#include <QCryptographicHash>
#include <QDirIterator>
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
void put(QString path, QByteArray bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        throw std::runtime_error("fixture write failed");
}
QJsonValue normalize(QJsonValue value, const QString &root) {
    if (value.isString())
        return value.toString().replace(root, "<root>");
    if (value.isObject()) {
        auto object = value.toObject();
        for (auto i = object.begin(); i != object.end(); ++i)
            i.value() = normalize(i.value(), root);
        return object;
    }
    if (value.isArray()) {
        auto array = value.toArray();
        for (int i = 0; i < array.size(); ++i)
            array[i] = normalize(array[i], root);
        return array;
    }
    return value;
}
QJsonObject outcome(int scene, bool rust) {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        throw std::runtime_error("fixture directory failed");
    const auto root = temporary.path();
    const auto folder = root + "/logs/diagnostics/" +
                        diagnostic_test::baseDate(scene).toString("yyyyMMdd-HHmmss-zzz") + "-00000000";
    const int mode = scene % 8;
    if (mode != 0) {
        put(root + "/logs/native.log", QByteArray(scene % 3 == 0 ? 17 : 80000, char(scene)) + "native-end");
        put(root + "/logs/workshop.log", QString("中文日志").repeated(10000).toUtf8() + "workshop-end");
    }
    HANDLE locked = INVALID_HANDLE_VALUE;
    auto close = qScopeGuard([&] {
        if (locked != INVALID_HANDLE_VALUE)
            CloseHandle(locked);
    });
    if (mode == 3)
        put(root + "/logs/diagnostics", "not-a-directory");
    if (mode == 4 || mode == 5 || mode == 6) {
        const auto path =
            mode == 6 ? root + "/logs/native.log" : folder + (mode == 4 ? "/native.log" : "/report.json");
        if (mode != 6)
            put(path, "locked-existing-file");
        locked = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE, 0,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (locked == INVALID_HANDLE_VALUE)
            throw std::runtime_error("fixture lock failed");
    }
    diagnostic_test::State state(scene);
    diagnostic_test::active = &state;
    const QJsonObject event{{"task", "fixture"},
                            {"error", "fixture error"},
                            {"diagnostic_id", "old"},
                            {"memory", QJsonObject{{"previous", true}}}};
    auto record = rust ? diagnostic_test::current::saveDiagnostic(root, event)
                       : diagnostic_test::original::saveDiagnostic(root, event);
    if (locked != INVALID_HANDLE_VALUE) {
        CloseHandle(locked);
        locked = INVALID_HANDLE_VALUE;
    }
    QJsonObject files;
    QDirIterator iterator(folder, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error("fixture snapshot failed");
        const auto bytes = f.readAll();
        if (QFileInfo(path).fileName() == "report.json") {
            auto document = QJsonDocument::fromJson(bytes);
            files[QFileInfo(path).fileName()] = document.isObject() ? normalize(document.object(), root)
                                                                    : QJsonValue(QString::fromUtf8(bytes));
        } else
            files[QFileInfo(path).fileName()] = QJsonObject{
                {"bytes", double(bytes.size())},
                {"sha256",
                 QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
    }
    return {{"record", normalize(record, root)}, {"files", files}, {"trace", state.trace}};
}
} // namespace
class DiagnosticsContracts final : public QObject {
    Q_OBJECT
  private slots:
    void persistence_data() {
        QTest::addColumn<int>("scene");
        for (int scene = 0; scene < 256; ++scene)
            QTest::newRow(qPrintable(QString::number(scene))) << scene;
    }
    void persistence() {
        QFETCH(int, scene);
        QCOMPARE(outcome(scene, true), outcome(scene, false));
    }
};
QTEST_GUILESS_MAIN(DiagnosticsContracts)
#include "test_diagnostics_contract.moc"
