#include "echo_operation_context_replay.hpp"
#include "echo_operation_policy_replay.hpp"
#include "reference_echo_operations.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
const QStringList properties{"攻擊",
                             "生命",
                             "防禦",
                             "暴擊",
                             "暴擊傷害",
                             "共鳴效率",
                             "普攻傷害加成",
                             "重擊傷害加成",
                             "共鳴解放傷害加成",
                             "共鳴技能傷害加成",
                             "未知",
                             "攻擊效率",
                             "辅音",
                             "獲得聲骸"};
const QStringList values{"0",     "1",      "8.1％",  "16.2%",  "-1",    "nan", "inf",
                         "1e300", "９.５%", " 8.1 %", "4%junk", "+5.0%", "-0"};
} // namespace
class EchoOpsContracts : public QObject {
    Q_OBJECT
  private slots:
    void pipelines_data() {
        QTest::addColumn<int>("operation");
        QTest::addColumn<int>("scene");
        for (int operation = 0; operation < 3; ++operation)
            for (int scene = 0; scene < 2048; ++scene)
                QTest::newRow(qPrintable(QString("%1-%2").arg(operation).arg(scene))) << operation << scene;
    }
    void pipelines() {
        QFETCH(int, operation);
        QFETCH(int, scene);
        const auto expected = echo_operation_policy_test::trace(operation, scene, false),
                   actual = echo_operation_policy_test::trace(operation, scene, true);
        QVERIFY2(actual == expected,
                 qPrintable(
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                                           .toJson(QJsonDocument::Compact))));
    }
    void helpers_data() {
        QTest::addColumn<int>("operation");
        QTest::addColumn<int>("scene");
        for (int operation = 0; operation < 8; ++operation)
            for (int scene = 0; scene < 16; ++scene)
                QTest::newRow(qPrintable(QString("%1-%2").arg(operation).arg(scene))) << operation << scene;
    }
    void helpers() {
        QFETCH(int, operation);
        QFETCH(int, scene);
        try {
            const auto expected = echo_operation_context_test::trace<reference_echo_ops::EchoOperations>(
                           operation, scene),
                       actual = echo_operation_context_test::trace<EchoOperations>(operation, scene);
            QVERIFY2(actual == expected,
                     qPrintable(QString::fromUtf8(
                         QJsonDocument(QJsonObject{{"expected", expected}, {"actual", actual}})
                             .toJson(QJsonDocument::Compact))));
            QVERIFY(actual["commits"].toInt() <= 1);
        } catch (const std::exception &e) {
            QFAIL(qPrintable(errorText(e)));
        }
    }
    void decision_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 8192; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void decision() {
        QFETCH(int, scene);
        cv::RNG random(78453 + scene);
        QVector<EchoStat> stats;
        QVector<reference_echo_ops::EchoStat> oldStats;
        for (int i = 0; i < scene % 9; ++i) {
            const auto p = properties[random.uniform(0, int(properties.size()))],
                       v = values[random.uniform(0, int(values.size()))];
            stats.append({p, v});
            oldStats.append({p, v});
        }
        const QStringList canonical{"攻击",
                                    "攻击百分比",
                                    "生命",
                                    "生命百分比",
                                    "防御",
                                    "防御百分比",
                                    "暴击",
                                    "暴击伤害",
                                    "共鸣效率",
                                    "普攻伤害加成",
                                    "重击伤害加成",
                                    "共鸣解放伤害加成",
                                    "共鸣技能伤害加成"};
        QJsonArray valid;
        for (int i = 0; i < canonical.size(); ++i)
            if ((scene >> i) & 1)
                valid.append(canonical[i]);
        QJsonObject config{{"有效词条", valid},
                           {"双爆出现之前必须全有效词条", bool(scene & 1)},
                           {"首条双爆>=", scene % 7 * .8},
                           {"必须有双爆", bool(scene & 2)},
                           {"双爆总计>=", scene % 17 * .9},
                           {"第一条必须为有效词条", bool(scene & 4)},
                           {"有效词条>=", scene % 7}};
        const auto expected = reference_echo_ops::reference_evaluateEcho(oldStats, config);
        const auto actual = evaluateEcho(stats, config);
        QCOMPARE(actual.keep, expected.keep);
        QCOMPARE(actual.count, expected.count);
        QCOMPARE(actual.invalid, expected.invalid);
        QCOMPARE(actual.reason, expected.reason);
    }
    void pairing_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void pairing() {
        QFETCH(int, scene);
        cv::RNG random(6375 + scene);
        QVector<TextLine> lines;
        for (int i = 0; i < scene % 13; ++i) {
            auto word = i % 2 ? properties[random.uniform(0, int(properties.size()))]
                              : values[random.uniform(0, int(values.size()))];
            if (scene % 7 == 0)
                word += "8.1%";
            lines.append({word, {10, random.uniform(0, 6) * 20, 80, random.uniform(5, 30)}, .9});
        }
        const auto before = lines;
        const auto expected = reference_echo_ops::reference_pairEchoStats(lines);
        const auto actual = pairEchoStats(lines);
        QCOMPARE(actual.size(), expected.size());
        for (int i = 0; i < actual.size(); ++i) {
            QCOMPARE(actual[i].property, expected[i].property);
            QCOMPARE(actual[i].value, expected[i].value);
        }
        QCOMPARE(lines.size(), before.size());
        for (int i = 0; i < lines.size(); ++i) {
            QCOMPARE(lines[i].text, before[i].text);
            QCOMPARE(lines[i].box, before[i].box);
        }
    }
    void normalize_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 2048; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void normalize() {
        QFETCH(int, scene);
        auto property = properties[scene % properties.size()];
        const auto value = values[(scene / 7) % values.size()];
        if (scene % 31 == 0)
            property = QString(QChar(char16_t(0xd800))) + QStringLiteral("未知") + QChar(char16_t(0xdc00));
        if (scene % 37 == 0)
            property.prepend(QChar(char16_t(0)));
        if (scene % 43 == 0)
            property = QStringLiteral("");
        if (scene % 41 == 0)
            property = QString();
        const auto before = property;
        const auto actual = normalizeEchoStat(property, value);
        const auto expected = reference_echo_ops::reference_normalizeEchoStat(property, value);
        QCOMPARE(actual, expected);
        QCOMPARE(actual.isNull(), expected.isNull());
        QCOMPARE(property, before);
    }
    void merge_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 1024; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void merge() {
        QFETCH(int, scene);
        const QStringList words{"获得声骸",
                                "獲得聲骸！",
                                "数据融合成功。",
                                "數據融合成功",
                                "Echoes Obtained",
                                "Echo Obtained!",
                                "Data Merge Complete",
                                "Data Fusion Complete:",
                                "确认",
                                "Other"};
        QVector<TextLine> lines;
        for (int i = 0; i < scene % 4; ++i)
            lines.append({words[(scene + i * 3) % words.size()], {10, 20 * i, 100, 10}, .9});
        QCOMPARE(echoMergeResultConfirmed(lines),
                 reference_echo_ops::reference_echoMergeResultConfirmed(lines));
    }
};
QTEST_GUILESS_MAIN(EchoOpsContracts)
#include "test_echo_ops_contract.moc"
