#ifndef Q_MOC_RUN
#include "reference_notification_desktop.hpp"
#endif
#include <QtTest>
#include <random>
using namespace okww;
using namespace okww::runtime;
class NotificationDesktopContracts final : public QObject {
    Q_OBJECT
  private slots:
    void contacts_data() {
        QTest::addColumn<int>("seed");
        for (int i = 0; i < 4096; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void contacts() {
        QFETCH(int, seed);
        std::mt19937 rng(seed);
        const QStringList words{"联系人", "CONTACTS", " features ",   "功能",   "聊天记录",
                                "更多",   "群聊",     "网络搜索结果", "公眾號", "chat history",
                                "TARGET", " target ", "other",        "Straße", " STRASSE ",
                                "İ",      "😀"};
        QVector<TextLine> lines;
        const int n = int(rng() % 30);
        for (int i = 0; i < n; ++i) {
            TextLine line;
            line.text = words[int(rng() % words.size())];
            line.box = {int(rng() % 80) - 10, int(rng() % 200) - 20, int(rng() % 30) - 5,
                        int(rng() % 35) - 5};
            if (i > 0 && rng() % 7 == 0)
                line = lines[int(rng() % lines.size())];
            lines.append(line);
        }
        const QString nickname = seed % 19 == 0 ? QString(" ") : words[10 + seed % 7];
        for (bool popup : {false, true})
            QCOMPARE(messengerContact(lines, nickname, popup),
                     notice_desktop_reference::messengerContact(lines, nickname, popup));
    }
    void capture_data() {
        QTest::addColumn<int>("scene");
        for (int i = 0; i < 128; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void capture() {
        QFETCH(int, scene);
        auto outcome = [&](bool rust) {
            int checks = 0, calls = 0;
            NoticeCancelled cancel;
            if (scene % 5 == 0)
                cancel = [&] {
                    ++checks;
                    return true;
                };
            else if (scene % 7 == 0)
                cancel = [&] {
                    ++checks;
                    return false;
                };
            std::function<cv::Mat()> capture = [&] {
                ++calls;
                if (scene % 11 == 0)
                    throw std::runtime_error("capture fixture");
                if (scene % 13 == 0)
                    throw 27;
                return scene % 3 ? cv::Mat(2, 3, CV_8UC3, cv::Scalar(1, 2, scene)) : cv::Mat{};
            };
            if (scene % 17 == 0)
                capture = {};
            const int timeout = scene % 19 == 0 ? 0 : scene % 23 == 0 ? 30001 : 1000;
            QJsonObject result;
            try {
                auto image = rust ? boundedDesktopCapture(capture, cancel, timeout)
                                  : notice_desktop_reference::boundedDesktopCapture(capture, cancel, timeout);
                result["size"] = QJsonArray{image.cols, image.rows};
                if (!image.empty())
                    result["pixel"] = int(image.at<cv::Vec3b>(0, 0)[2]);
            } catch (const std::exception &e) {
                result["error"] = errorText(e);
            } catch (...) {
                result["error"] = "unknown";
            }
            result["calls"] = calls;
            // A ready future can require another poll under OS scheduling; cancellation
            // is checked before starting the worker and on every poll in both paths.
            result["checked"] = checks > 0;
            return result;
        };
        QCOMPARE(outcome(true), outcome(false));
    }
};
QTEST_GUILESS_MAIN(NotificationDesktopContracts)
#include "test_notification_desktop_contract.moc"
