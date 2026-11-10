#include "runtime/echo_operations.hpp"
using namespace okww;
using namespace okww::runtime;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 3)
        return 2;
    try {
        const auto input = readJson(app.arguments()[1]);
        QJsonArray results;
        for (auto value : input["cases"].toArray()) {
            const auto item = value.toObject();
            QVector<EchoStat> stats;
            for (auto entry : item["stats"].toArray()) {
                const auto pair = entry.toArray();
                stats.append({pair[0].toString(), pair[1].toString()});
            }
            const auto result = evaluateEcho(stats, item["config"].toObject());
            results.append(QJsonObject{{"keep", result.keep}, {"reason", result.reason}});
        }
        writeJson(app.arguments()[2], {{"results", results}, {"hardware_input_sent", false}});
        return 0;
    } catch (const std::exception &) {
        return 1;
    }
}
