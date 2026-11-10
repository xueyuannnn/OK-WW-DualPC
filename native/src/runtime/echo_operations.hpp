#pragma once
#include "navigation.hpp"
namespace okww::runtime {
struct EchoStat {
    QString property, value;
};
struct EchoDecision {
    bool keep = true;
    int count = 0, invalid = 0;
    QString reason;
};
QVector<EchoStat> pairEchoStats(const QVector<TextLine> &lines);
EchoDecision evaluateEcho(const QVector<EchoStat> &stats, const QJsonObject &config);
QString normalizeEchoStat(QString property, const QString &value);
bool echoMergeResultConfirmed(const QVector<TextLine> &lines);
void awaitEchoMergeResult(TaskContext &context);
class EchoOperations {
    TaskContext &c_;
    Navigation navigation_;
    QString screenshots_;
    std::optional<TextLine> cultivate();
    std::optional<TextLine> waitText(QRect area, const QString &pattern, double timeout = 5);
    void enterCultivate();
    void backToBag();
    void mark(bool lock);
    void screenshot(const QString &category, int count, QString reason = {});
    void upgrade();
    bool zeroLevel(bool change = false);
    void centered(double x, double y, double after);

  public:
    EchoOperations(TaskContext &context, QString screenshots)
        : c_(context), navigation_(context), screenshots_(std::move(screenshots)) {}
    int merge();
    int enhance();
    int change();
};
} // namespace okww::runtime
