#include "ocr.hpp"
#include "backend.hpp"
#include "combat_vision.hpp"
#include "echo_detector.hpp"
#include "capture.hpp"
#include "desktop.hpp"
#include "garden.hpp"
#include <iostream>
using namespace okww;
using namespace okww::runtime;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions(
        {{"root", "Resource root", "path"},
         {"models", "PP-OCR model directory", "path"},
         {"image", "Saved image to recognize (no capture/input)", "path"},
         {"report", "JSON report", "path"},
         {"capture-config", "Read-only capture using this dualpc.json; never connect a box", "path"},
         {"capture-seconds", "Capture diagnostic duration, 1 to 30 seconds", "seconds", "5"},
         {"desktop-targets", "List local monitors and windows without capturing or sending input"},
         {"feature", "Template to match", "name"},
         {"garden-scan", "Profile saved Garden template scan without issuing input"},
         {"echo", "Run original echo model on saved image"},
         {"combat", "Analyze saved combat HUD without issuing input"},
         {"garden", "Analyze saved weekly overview without issuing input"}});
    parser.process(app);
    QJsonObject report{{"runtime", "C++"}, {"hardware_input_sent", false}};
    try {
        const QString root = parser.value("root");
        if (parser.isSet("desktop-targets")) {
            report["targets"] = desktopTargets();
        } else if (parser.isSet("capture-config")) {
            if (parser.isSet("image") || !parser.isSet("report"))
                fail("采集诊断需要 --report，且不能同时指定 --image");
            const auto path = parser.value("capture-config");
            if (!QFile::exists(path))
                fail("采集诊断配置文件不存在");
            bool valid = false;
            const auto seconds = parser.value("capture-seconds").toDouble(&valid);
            if (!valid || !std::isfinite(seconds) || seconds < 1 || seconds > 30)
                fail("采集诊断时间须为 1 至 30 秒");
            const auto config = Config::load(path);
            config.validateCapture();
            QJsonArray available;
            if (config.s("capture.source") != "desktop") for (const auto &mode : modes(config))
                if (mode.width == config.n("capture.width") && mode.height == config.n("capture.height") &&
                    mode.fourcc == config.s("capture.fourcc"))
                    available.append(mode.json());
            report["advertised_modes"] = available;
            // Capture owns only the video device. No Backend/Controller is
            // created on this diagnostic path, even with a live box profile.
            Capture capture(config);
            capture.start();
            QElapsedTimer timer;
            timer.start();
            uint64_t last = 0, first = 0;
            int observed = 0;
            while (timer.elapsed() < seconds * 1000)
                if (auto frame = capture.next(last)) {
                    if (!observed)
                        first = frame->sequence;
                    last = frame->sequence;
                    ++observed;
                }
            report["actual"] = capture.actual();
            report["measured_fps"] = capture.fps();
            report["frames_observed"] = observed;
            report["elapsed_ms"] = timer.elapsed();
            report["frames_skipped_by_reader"] = double(observed ? last - first + 1 - observed : 0);
            const auto sample = capture.latest();
            const auto error = capture.error();
            capture.stop();
            if (!error.isEmpty() || !sample || !observed)
                fail(error.isEmpty() ? "未采集到有效画面" : error);
            const auto folder = QFileInfo(parser.value("report")).absolutePath();
            if (!QDir().mkpath(folder))
                fail("无法创建采集诊断目录");
            const auto samplePath = folder + "/capture-frame.png";
            report["sample_saved"] = frameImage(*sample).save(samplePath);
            report["sample_path"] = samplePath;
            report["preview_valid"] = !previewImage(*sample).isNull();
            if (!report["sample_saved"].toBool() || !report["preview_valid"].toBool())
                fail("采集诊断画面转换或保存失败");
        } else {
            auto image = readImage(parser.value("image"));
            report["image_size"] = QJsonArray{image.cols, image.rows};
            if (parser.isSet("echo")) {
                EchoDetector detector(root + "/assets/echo_model/echo.onnx");
                QJsonArray cases;
                for (double threshold : {.3, .5, .8}) {
                    QJsonArray found;
                    for (const auto &item : detector.detect(image, threshold))
                        found.append(QJsonObject{{"name", item.name},
                                                 {"confidence", item.confidence},
                                                 {"box", QJsonArray{item.box.x(), item.box.y(),
                                                                    item.box.width(), item.box.height()}}});
                    cases.append(QJsonObject{{"threshold", threshold}, {"detections", found}});
                }
                report["echo"] = cases;
            }
            if (parser.isSet("feature")) {
                Templates templates(root + "/assets/coco_annotations.json");
                QJsonArray matches;
                for (const auto &name : parser.values("feature")) {
                    auto match = templates.one(image, name);
                    QJsonObject value{{"name", name}, {"found", bool(match)}};
                    if (match)
                        value["confidence"] = match->confidence;
                    matches.append(value);
                }
                report["features"] = matches;
            }
            if (parser.isSet("garden-scan")) {
                Templates templates(root + "/assets/coco_annotations.json");
                QElapsedTimer scan;
                scan.start();
                QJsonArray costs;
                for (const auto &name : templates.names()) {
                    if (!name.startsWith("garden_") || name == "garden_get_confirm_gray" ||
                        name == "garden_not_interested_confirm") continue;
                    MatchOptions options;
                    options.limit = 0;
                    if (name == "garden_not_interested") options.vertical = .4;
                    const auto began = scan.nsecsElapsed();
                    const auto matches = templates.find(image, name, options);
                    costs.append(QJsonObject{{"name", name}, {"ms", (scan.nsecsElapsed()-began)/1e6},
                                             {"matches", matches.size()}});
                }
                report["garden_scan"] = QJsonObject{{"elapsed_ms", scan.nsecsElapsed()/1e6}, {"templates", costs}};
            }
            std::unique_ptr<Ocr> ocr;
            if (parser.isSet("models")) {
                QElapsedTimer timer;
                timer.start();
                ocr = std::make_unique<Ocr>(parser.value("models"));
                report["ocr_load_ms"] = timer.elapsed();
                timer.restart();
                QJsonArray lines;
                for (const auto &line : ocr->read(image))
                    lines.append(line.json());
                report["ocr_ms"] = timer.elapsed();
                report["lines"] = lines;
            }
            if (parser.isSet("garden")) {
                if (!ocr) fail("周常图像检查需要 --models");
                Templates templates(root + "/assets/coco_annotations.json");
                QTemporaryDir directory;
                Profile profile(directory.path(), root + "/native/resources/catalog.json");
                SteadyClock clock;
                ExecutionGate gate;
                Execution execution(gate, gate.start(), clock);
                Input input(Config::defaults(), {image.cols, image.rows}, false,
                            [](auto command) -> QJsonObject {
                                if (command["op"] != "release") fail("离线图像检查禁止输入");
                                return {};
                            }, execution);
                TaskContext context(execution, input, templates, profile, [&] { return image; },
                    [&](const auto &frame, auto area, double threshold) { return ocr->read(frame, area, threshold); });
                context.refresh();
                QJsonArray probes;
                for (const auto area : {gameBox(image, .19, .83, .275, .90),
                                        gameBox(image, .16, .81, .30, .95),
                                        gameBox(image, .05, .78, .98, .97),
                                        gameBox(image, 0, 0, 1, 1)}) {
                    QJsonArray lines;
                    for (const auto &line : context.text(area)) lines.append(line.json());
                    probes.append(QJsonObject{{"area", QJsonArray{area.x(), area.y(), area.width(), area.height()}},
                                               {"lines", lines}});
                }
                Garden garden(context, root);
                const auto points = garden.weeklyProgress();
                report["garden"] = QJsonObject{{"progress", points ? QJsonValue(*points) : QJsonValue()},
                                                {"probes", probes}, {"observation", garden.weeklyObservation()}};
            }
            if (parser.isSet("combat")) {
                if (!ocr)
                    fail("战斗图像检查需要 --models");
                Templates templates(root + "/assets/coco_annotations.json");
                QTemporaryDir directory;
                Profile profile(directory.path(), root + "/native/resources/catalog.json");
                SteadyClock clock;
                ExecutionGate gate;
                Execution execution(gate, gate.start(), clock);
                Input input(
                    Config::defaults(), {image.cols, image.rows}, false,
                    [](auto command) -> QJsonObject {
                        if (command["op"] != "release")
                            fail("离线图像检查禁止输入");
                        return {};
                    },
                    execution);
                TaskContext context(
                    execution, input, templates, profile, [&] { return image; },
                    [&](const auto &frame, auto area, double threshold) {
                        return ocr->read(frame, area, threshold);
                    });
                context.refresh();
                CombatVision vision(context, root + "/native/resources/characters.json");
                auto hud = vision.party();
                QJsonArray chars;
                Concerto concerto;
                QJsonObject combat{{"party", hud.visible}, {"current", hud.current}, {"size", hud.size}};
                if (hud.visible) {
                    for (int i = 0; i < hud.size; ++i) {
                        auto identity = vision.character(i);
                        QJsonObject c{{"index", i}, {"recognized", bool(identity)}};
                        if (identity) {
                            c["class"] = identity->definition.className;
                            c["confidence"] = identity->confidence;
                        }
                        if (i == hud.current) {
                            CharacterState state;
                            state.ringIndex = identity ? identity->definition.element : -1;
                            c["concerto"] = concerto.detect(image, state);
                            c["element"] = state.ringIndex;
                        }
                        chars.append(c);
                    }
                    vision.refreshCooldowns(hud.current);
                    auto values = vision.cooldowns().value(hud.current).seconds;
                    combat["cooldowns"] =
                        QJsonObject{{"resonance", values[0]}, {"echo", values[1]}, {"liberation", values[2]}};
                }
                combat["characters"] = chars;
                combat["target"] = vision.hasTarget();
                combat["health"] = vision.healthBar(false);
                combat["shifted_health"] = vision.shiftedHealthBar();
                combat["death_dialog"] = vision.deathDialog();
                const auto result = vision.challengeResult();
                combat["challenge_result"] = result ? (*result ? "success" : "failure") : "none";
                combat["mouse_forte"] = vision.forte(Forte::Mouse);
                combat["bar_forte"] = vision.forte(Forte::Bar);
                report["combat"] = combat;
            }
        }
        bool python = false;
        for (int minor = 8; minor <= 15; ++minor)
            python |= GetModuleHandleW(
                          reinterpret_cast<LPCWSTR>(QString("python3%1.dll").arg(minor).utf16())) != nullptr;
        python |= GetModuleHandleW(L"python3.dll") != nullptr;
        report["python_loaded"] = python;
        report["ok"] = !python;
    } catch (const std::exception &error) {
        report["ok"] = false;
        report["error"] = errorText(error);
    }
    if (parser.isSet("report"))
        writeJson(parser.value("report"), report);
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
    return report["ok"].toBool() ? 0 : 1;
}
