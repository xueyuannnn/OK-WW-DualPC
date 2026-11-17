#include "combat_vision.hpp"
#include "gauge_analysis.hpp"
#include "vision_abi.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
bool systemRunDialogConfirmed(const QVector<TextLine> &lines) {
    return rust::visionRun(
               rust::VisionMethod::RunDialog, {}, [&](rust::VisionOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::VisionOp::RunAnchor: {
                       const QStringList patterns{"Windows.*(名称|名稱|name)|Type.*name.*program",
                                                  "^(打开|開啟|Open)\\s*[(（:]", "浏览|瀏覽|Browse"};
                       out[0] = bool(matchingText(lines, patterns.at(int(a[0]))));
                       break;
                   }
                   default:
                       fail("Rust RunDialog 识别宿主操作无效");
                   }
               })[0] != 0;
}
static bool enemyLevelConfirmed(const QVector<TextLine> &lines) {
    return rust::visionRun(
               rust::VisionMethod::LevelConfirmed, {}, [&](rust::VisionOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::VisionOp::FullLevel: {
                       out[0] = bool(matchingText(lines, "(?<![A-Za-z])Lv[\\s.·]*\\d{1,3}(?!\\d)"));
                       break;
                   }
                   case rust::VisionOp::TextCount: {
                       out[0] = double(lines.size());
                       break;
                   }
                   case rust::VisionOp::LevelPrefix: {
                       out[0] = QRegularExpression("^Lv[\\s.·]*$", QRegularExpression::CaseInsensitiveOption)
                                    .match(lines.at(int(a[0])).text.trimmed())
                                    .hasMatch();
                       break;
                   }
                   case rust::VisionOp::LevelNumber: {
                       out[0] = QRegularExpression("^\\d{1,3}$")
                                    .match(lines.at(int(a[0])).text.trimmed())
                                    .hasMatch();
                       break;
                   }
                   case rust::VisionOp::LevelGeometry: {
                       const auto &prefix = lines.at(int(a[0]));
                       const auto &number = lines.at(int(a[1]));
                       out[0] = std::max(prefix.box.height(), number.box.height());
                       out[1] = number.box.left() - prefix.box.right();
                       out[2] = number.center().y() - prefix.center().y();
                       break;
                   }
                   default:
                       fail("Rust LevelConfirmed 识别宿主操作无效");
                   }
               })[0] != 0;
}
int segmentedGaugeValue(const cv::Mat &image, ColorRange color, int segments, int minFrequency,
                        int maxFrequency, double minAmplitude) {
    cv::Mat mask;
    const std::array args{double(segments), double(minFrequency), double(maxFrequency), minAmplitude};
    return int(rust::visionRun(
        rust::VisionMethod::SegmentedGauge, args, [&](rust::VisionOp op, const double *a, double *out) {
            switch (op) {
            case rust::VisionOp::ImageEmpty: {
                out[0] = image.empty();
                break;
            }
            case rust::VisionOp::MakeGaugeMask: {
                cv::inRange(image, color.lower, color.upper, mask);
                break;
            }
            case rust::VisionOp::AnalyzeGauge: {
                out[0] = nativeSegmentedGaugeMask(mask, int(a[0]), int(a[1]), int(a[2]), a[3]);
                break;
            }
            default:
                fail("Rust SegmentedGauge 识别宿主操作无效");
            }
        })[0]);
}
QVector<Match> colorRectangles(const cv::Mat &frame, ColorRange color, double minWidth, double minHeight,
                               double maxHeight, std::optional<QRect> area, double minimumFill) {
    QRect r;
    cv::Mat mask;
    cv::Rect b;
    std::vector<std::vector<cv::Point>> contours;
    QVector<Match> results;
    const std::array args{minWidth, minHeight, maxHeight, minimumFill};
    rust::visionRun(rust::VisionMethod::Rectangles, args,
                    [&](rust::VisionOp op, const double *a, double *out) {
                        switch (op) {
                        case rust::VisionOp::IntersectColorArea: {
                            r = area.value_or(QRect(0, 0, frame.cols, frame.rows))
                                    .intersected({0, 0, frame.cols, frame.rows});
                            break;
                        }
                        case rust::VisionOp::ColorAreaEmpty: {
                            out[0] = r.isEmpty();
                            break;
                        }
                        case rust::VisionOp::MakeColorContours: {
                            cv::inRange(frame(cv::Rect(r.x(), r.y(), r.width(), r.height())), color.lower,
                                        color.upper, mask);
                            cv::findContours(mask, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
                            break;
                        }
                        case rust::VisionOp::ContourCount: {
                            out[0] = double(contours.size());
                            break;
                        }
                        case rust::VisionOp::SelectContour: {
                            b = cv::boundingRect(contours.at(size_t(a[0])));
                            break;
                        }
                        case rust::VisionOp::ContourBounds: {
                            out[0] = b.width;
                            out[1] = b.height;
                            out[2] = b.y;
                            break;
                        }
                        case rust::VisionOp::ContourFill: {
                            out[0] = double(cv::countNonZero(mask(b))) / b.area();
                            break;
                        }
                        case rust::VisionOp::AppendColorRect: {
                            results.append({"health", {r.x() + b.x, r.y() + b.y, b.width, b.height}, a[0]});
                            break;
                        }
                        default:
                            fail("Rust Rectangles 识别宿主操作无效");
                        }
                    });
    return results;
}
QVector<Match> shiftedBarCandidates(const cv::Mat &frame) {
    cv::Mat hsv, kernel, mask;
    cv::Rect b, core;
    std::vector<std::vector<cv::Point>> contours;
    QVector<Match> boxes, unique;
    rust::visionRun(
        rust::VisionMethod::ShiftedCandidates, {}, [&](rust::VisionOp op, const double *a, double *out) {
            switch (op) {
            case rust::VisionOp::ImageSize: {
                out[0] = frame.cols;
                out[1] = frame.rows;
                break;
            }
            case rust::VisionOp::PixelRound: {
                out[0] = pixelRound(a[0]);
                break;
            }
            case rust::VisionOp::PrepareShifted: {
                cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
                kernel = cv::Mat::ones(1, int(a[0]), CV_8U);
                break;
            }
            case rust::VisionOp::ShiftedContours: {
                cv::Mat upper, joined, horizontal;
                cv::inRange(hsv, cv::Scalar(0, 55, a[0]), cv::Scalar(26, 255, 255), mask);
                cv::inRange(hsv, cv::Scalar(170, 55, a[0]), cv::Scalar(179, 255, 255), upper);
                mask |= upper;
                cv::morphologyEx(mask, joined, cv::MORPH_CLOSE, kernel);
                cv::findContours(joined, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                cv::morphologyEx(mask, horizontal, cv::MORPH_OPEN, cv::Mat::ones(1, int(a[1]), CV_8U));
                std::vector<std::vector<cv::Point>> isolated;
                cv::findContours(horizontal, isolated, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                contours.insert(contours.end(), isolated.begin(), isolated.end());
                break;
            }
            case rust::VisionOp::ContourCount: {
                out[0] = double(contours.size());
                break;
            }
            case rust::VisionOp::SelectContour: {
                b = cv::boundingRect(contours.at(size_t(a[0])));
                break;
            }
            case rust::VisionOp::ContourBounds: {
                out[0] = b.width;
                out[1] = b.height;
                out[2] = b.y;
                break;
            }
            case rust::VisionOp::ContourFill: {
                out[0] = double(cv::countNonZero(mask(b))) / b.area();
                break;
            }
            case rust::VisionOp::SelectContourCore: {
                core = a[0] != 0 ? cv::Rect(b.x + 1, b.y + 1, b.width - 2, b.height - 2) : b;
                break;
            }
            case rust::VisionOp::CoreFill: {
                out[0] = double(cv::countNonZero(mask(core))) / core.area();
                break;
            }
            case rust::VisionOp::AppendShiftedRect: {
                boxes.append({"shifted_health", {b.x, b.y, b.width, b.height}, a[0]});
                break;
            }
            case rust::VisionOp::CandidateCount: {
                out[0] = double(boxes.size());
                break;
            }
            case rust::VisionOp::BarConfidence: {
                out[0] = boxes.at(int(a[0])).confidence;
                break;
            }
            case rust::VisionOp::AllocationFailure: {
                throw std::bad_alloc();
                break;
            }
            case rust::VisionOp::CandidateOverlap: {
                const auto &box = boxes.at(int(a[0]));
                const auto &other = boxes.at(int(a[1]));
                const auto intersection = box.box.intersected(other.box);
                out[0] = double(intersection.width()) * intersection.height() /
                         std::min(box.box.width() * box.box.height(), other.box.width() * other.box.height());
                break;
            }
            case rust::VisionOp::AppendUnique: {
                unique.append(boxes.at(int(a[0])));
                break;
            }
            default:
                fail("Rust ShiftedCandidates 识别宿主操作无效");
            }
        });
    return unique;
}
CombatVision::CombatVision(TaskContext &context, const QString &catalog) : context_(context) {
    QJsonArray entries, templates;
    QJsonObject entry;
    CharacterDefinition definition;
    rust::visionRun(rust::VisionMethod::Initialize, {}, [&](rust::VisionOp op, const double *a, double *out) {
        switch (op) {
        case rust::VisionOp::CharacterEntries: {
            entries = readJson(catalog)["characters"].toArray();
            out[0] = double(entries.size());
            break;
        }
        case rust::VisionOp::SelectEntry: {
            entry = entries.at(int(a[0])).toObject();
            definition = {};
            break;
        }
        case rust::VisionOp::InitializeDefinition: {
            definition.className = entry["class"].toString();
            definition.canonical = entry["canonical"].toString();
            definition.role = Role(entry["role"].toInt());
            definition.element = entry["element"].toInt(-1);
            definition.buffTime = entry["buff_time"].toDouble();
            definition.shortTarget = entry["target_box_short_combat_check"].toBool();
            break;
        }
        case rust::VisionOp::TemplateEntries: {
            templates = entry["templates"].toArray();
            out[0] = double(templates.size());
            break;
        }
        case rust::VisionOp::AppendTemplate: {
            definition.templates.append(templates.at(int(a[0])).toString());
            break;
        }
        case rust::VisionOp::TemplatesEmpty: {
            out[0] = definition.templates.isEmpty();
            break;
        }
        case rust::VisionOp::ClassNameEmpty: {
            out[0] = definition.className.isEmpty();
            break;
        }
        case rust::VisionOp::Throw: {
            fail("角色模板目录无效");
            break;
        }
        case rust::VisionOp::AppendDefinition: {
            definitions_.append(std::move(definition));
            break;
        }
        default:
            fail("Rust Initialize 识别宿主操作无效");
        }
    });
}
void CombatVision::reset() {
    rust::visionRun(rust::VisionMethod::Reset, {}, [&](rust::VisionOp op, const double *a, double *out) {
        switch (op) {
        case rust::VisionOp::ClearCooldowns: {
            cooldowns_.clear();
            break;
        }
        case rust::VisionOp::ResetPartyVersion: {
            partyVersion_ = UINT64_MAX;
            break;
        }
        case rust::VisionOp::ResetCooldownVersion: {
            cdVersion_ = UINT64_MAX;
            break;
        }
        default:
            fail("Rust Reset 识别宿主操作无效");
        }
    });
}
PartyHud CombatVision::party() {
    const auto result =
        rust::visionRun(rust::VisionMethod::Party, {}, [&](rust::VisionOp op, const double *a, double *out) {
            switch (op) {
            case rust::VisionOp::PartyVersionMatches: {
                out[0] = partyVersion_ == context_.frameVersion;
                break;
            }
            case rust::VisionOp::ReadParty: {
                party_ = context_.party();
                break;
            }
            case rust::VisionOp::StorePartyVersion: {
                partyVersion_ = context_.frameVersion;
                break;
            }
            case rust::VisionOp::PartyData: {
                out[0] = party_.visible;
                out[1] = party_.current;
                out[2] = party_.size;
                break;
            }
            default:
                fail("Rust Party 识别宿主操作无效");
            }
        });
    return {result[0] != 0, int(result[1]), int(result[2])};
}
QRect CombatVision::box(const QString &name) {
    return context_.templates.box(name, {context_.frame.cols, context_.frame.rows});
}
std::optional<Match> CombatVision::best(QRect area, QStringList names, double threshold) {
    std::optional<Match> result, match;
    const std::array args{threshold};
    rust::visionRun(rust::VisionMethod::Best, args, [&](rust::VisionOp op, const double *a, double *out) {
        switch (op) {
        case rust::VisionOp::MatchNamesCount: {
            out[0] = double(names.size());
            break;
        }
        case rust::VisionOp::FindMatch: {
            match = context_.find(names.at(int(a[0])), {.threshold = a[1], .search = area});
            out[0] = match.has_value();
            break;
        }
        case rust::VisionOp::ResultExists: {
            out[0] = result.has_value();
            break;
        }
        case rust::VisionOp::CandidateConfidence: {
            out[0] = match->confidence;
            break;
        }
        case rust::VisionOp::ResultConfidence: {
            out[0] = result->confidence;
            break;
        }
        case rust::VisionOp::StoreMatch: {
            result = match;
            break;
        }
        default:
            fail("Rust Best 识别宿主操作无效");
        }
    });
    return result;
}
std::optional<CharacterIdentity> CombatVision::character(int index, const CharacterState *previous) {
    QRect area;
    const CharacterDefinition *definition = nullptr;
    std::optional<Match> match;
    std::optional<CharacterIdentity> result, softened;
    const std::array args{double(index)};
    rust::visionRun(
        rust::VisionMethod::Character, args, [&](rust::VisionOp op, const double *a, double *out) {
            switch (op) {
            case rust::VisionOp::Throw: {
                fail("角色位置无效");
                break;
            }
            case rust::VisionOp::CharacterArea: {
                area = box(QString("box_char_%1").arg(int(a[0]) + 1));
                break;
            }
            case rust::VisionOp::PreviousExists: {
                out[0] = previous != nullptr;
                break;
            }
            case rust::VisionOp::PreviousConfidence: {
                out[0] = previous->confidence;
                break;
            }
            case rust::VisionOp::DefinitionsCount: {
                out[0] = double(definitions_.size());
                break;
            }
            case rust::VisionOp::SelectDefinition: {
                definition = &definitions_.at(int(a[0]));
                break;
            }
            case rust::VisionOp::DefinitionHasPrevious: {
                out[0] = definition->templates.contains(previous->name);
                break;
            }
            case rust::VisionOp::FindDefinitionBest: {
                match = best(area, definition->templates, a[0]);
                out[0] = match.has_value();
                break;
            }
            case rust::VisionOp::BuildResultIdentity: {
                result = CharacterIdentity{*definition, match->confidence};
                break;
            }
            case rust::VisionOp::ResultIdentityExists: {
                out[0] = result.has_value();
                break;
            }
            case rust::VisionOp::CandidateConfidence: {
                out[0] = match->confidence;
                break;
            }
            case rust::VisionOp::IdentityConfidence: {
                out[0] = result->confidence;
                break;
            }
            case rust::VisionOp::TemplateCount: {
                out[0] = double(definition->templates.size());
                break;
            }
            case rust::VisionOp::FindSoftTemplate: {
                match =
                    context_.find(definition->templates.at(int(a[0])),
                                  {.threshold = a[1], .search = area, .grayscale = true, .blurSigma = a[2]});
                out[0] = match.has_value();
                break;
            }
            case rust::VisionOp::SoftConfidence: {
                out[0] = match->confidence;
                break;
            }
            case rust::VisionOp::SoftResultExists: {
                out[0] = softened.has_value();
                break;
            }
            case rust::VisionOp::SoftResultConfidence: {
                out[0] = softened->confidence;
                break;
            }
            case rust::VisionOp::StoreSoftIdentity: {
                softened = CharacterIdentity{*definition, a[0]};
                break;
            }
            case rust::VisionOp::AdoptSoftIdentity: {
                result = softened;
                break;
            }
            default:
                fail("Rust Character 识别宿主操作无效");
            }
        });
    return result;
}
double CombatVision::cdX(int width, int height, double x) {
    const std::array args{double(width), double(height), x};
    return rust::visionRun(rust::VisionMethod::CdX, args,
                           [&](rust::VisionOp op, const double *a, double *out) {
                               switch (op) {
                               default:
                                   fail("Rust CdX 识别宿主操作无效");
                               }
                           })[0];
}
void CombatVision::refreshCooldowns(int index) {
    Cooldowns cds;
    QRect area;
    QVector<TextLine> lines;
    QRegularExpressionMatch match;
    const QRegularExpression regex("^\\s*(\\d{1,2})(?:[.,·]\\s*(\\d))?\\s*$");
    const std::array args{double(index)};
    rust::visionRun(rust::VisionMethod::RefreshCooldowns, args,
                    [&](rust::VisionOp op, const double *a, double *out) {
                        switch (op) {
                        case rust::VisionOp::CooldownVersionMatches: {
                            out[0] = cdVersion_ == context_.frameVersion;
                            break;
                        }
                        case rust::VisionOp::ReadHud: {
                            const auto hud = party();
                            out[0] = hud.visible;
                            out[1] = hud.current;
                            out[2] = hud.size;
                            break;
                        }
                        case rust::VisionOp::Now: {
                            out[0] = context_.execution.now();
                            break;
                        }
                        case rust::VisionOp::CooldownExists: {
                            out[0] = cooldowns_.contains(int(a[0]));
                            break;
                        }
                        case rust::VisionOp::CooldownTimestamp: {
                            out[0] = cooldowns_[int(a[0])].timestamp;
                            break;
                        }
                        case rust::VisionOp::CopyCooldowns: {
                            cds = cooldowns_.value(int(a[0]));
                            break;
                        }
                        case rust::VisionOp::SetCooldownTimestamp: {
                            cds.timestamp = a[0];
                            break;
                        }
                        case rust::VisionOp::ImageSize: {
                            out[0] = context_.frame.cols;
                            out[1] = context_.frame.rows;
                            break;
                        }
                        case rust::VisionOp::CooldownArea: {
                            const int w = context_.frame.cols, h = context_.frame.rows;
                            area = relativeBox(context_.frame, cdX(w, h, .82), .86, cdX(w, h, .97), .93);
                            break;
                        }
                        case rust::VisionOp::ReadCooldownText: {
                            if (a[0] != 0)
                                lines = context_.textScaled(area, a[1], a[2]);
                            else
                                lines = context_.text(area, a[1], Filter::InvertWhite);
                            out[0] = double(lines.size());
                            break;
                        }
                        case rust::VisionOp::CooldownTextCenter: {
                            const auto center = lines.at(int(a[0])).center();
                            out[0] = center.x();
                            out[1] = center.y();
                            break;
                        }
                        case rust::VisionOp::ParseCooldown: {
                            match = regex.match(lines.at(int(a[0])).text);
                            out[0] = match.hasMatch();
                            break;
                        }
                        case rust::VisionOp::ParsedCooldown: {
                            out[0] = match.captured(1).toInt() + match.captured(2).toInt() / 10.;
                            break;
                        }
                        case rust::VisionOp::WorkingCooldown: {
                            out[0] = cds.seconds[int(a[0])];
                            break;
                        }
                        case rust::VisionOp::SetWorkingCooldown: {
                            cds.seconds[int(a[0])] = a[1];
                            cds.observedAt[int(a[0])] = cds.timestamp;
                            break;
                        }
                        case rust::VisionOp::FallbackAt: {
                            out[0] = cds.fallbackAt;
                            break;
                        }
                        case rust::VisionOp::SetFallbackAt: {
                            cds.fallbackAt = a[0];
                            break;
                        }
                        case rust::VisionOp::CommitCooldowns: {
                            cooldowns_[int(a[0])] = cds;
                            break;
                        }
                        case rust::VisionOp::StoreCooldownVersion: {
                            cdVersion_ = context_.frameVersion;
                            break;
                        }
                        default:
                            fail("Rust RefreshCooldowns 识别宿主操作无效");
                        }
                    });
}
double CombatVision::cooldown(Skill skill, int index, const FreezeTimeline &time, double now) const {
    const std::array args{double(int(skill)), double(index)};
    return rust::visionRun(rust::VisionMethod::Cooldown, args,
                           [&](rust::VisionOp op, const double *a, double *out) {
                               switch (op) {
                               case rust::VisionOp::CooldownExists: {
                                   out[0] = cooldowns_.contains(int(a[0]));
                                   break;
                               }
                               case rust::VisionOp::CooldownSeconds: {
                                   out[0] = cooldowns_[int(a[1])].seconds[int(a[0])];
                                   break;
                               }
                               case rust::VisionOp::ObservedElapsed: {
                                   out[0] = time.elapsed(now, cooldowns_[int(a[1])].observedAt[int(a[0])]);
                                   break;
                               }
                               default:
                                   fail("Rust Cooldown 识别宿主操作无效");
                               }
                           })[0];
}
static QRect scaled(QRect r, double x, double y) {
    QRect result;
    const std::array args{double(r.x()), double(r.y()), double(r.width()), double(r.height()), x, y};
    rust::visionRun(rust::VisionMethod::Scaled, args, [&](rust::VisionOp op, const double *a, double *out) {
        switch (op) {
        case rust::VisionOp::PixelRound: {
            out[0] = pixelRound(a[0]);
            break;
        }
        case rust::VisionOp::StoreScaled: {
            result = QRect(int(a[0]), int(a[1]), int(a[2]), int(a[3]));
            break;
        }
        default:
            fail("Rust Scaled 识别宿主操作无效");
        }
    });
    return result;
}
bool CombatVision::hasTarget(bool allowShort, bool extended) {
    return targetObservation(allowShort, extended).locked;
}
TargetObservation CombatVision::targetObservation(bool allowShort, bool extended) {
    QString has, no;
    QStringList names;
    cv::Mat ring;
    QRect area;
    std::optional<Match> raw, yes, absent;
    const std::array args{double(allowShort), double(extended)};
    const auto result = rust::visionRun(
        rust::VisionMethod::Target, args, [&](rust::VisionOp op, const double *a, double *out) {
            switch (op) {
            case rust::VisionOp::ImageSize: {
                out[0] = context_.frame.cols;
                out[1] = context_.frame.rows;
                break;
            }
            case rust::VisionOp::TargetNames: {
                const QString suffix = a[0] != 0 ? "_169" : "";
                has = "has_target" + suffix;
                no = "no_target" + suffix;
                names = {has, no};
                break;
            }
            case rust::VisionOp::LoadTargetRing: {
                const auto reference =
                    context_.templates.pattern(has, {context_.frame.cols, context_.frame.rows});
                cv::Mat hsv;
                cv::cvtColor(reference, hsv, cv::COLOR_BGR2HSV);
                cv::inRange(hsv, cv::Scalar(12, 65, 55), cv::Scalar(45, 255, 255), ring);
                break;
            }
            case rust::VisionOp::TargetArea: {
                switch (int(a[0])) {
                case 0:
                    area = scaled(box(has), 1.1, 1.1);
                    break;
                case 1:
                    area = box("box_target_enemy_long");
                    break;
                case 2:
                    area = box("target_box_long2");
                    break;
                case 3:
                    area = box("target_box_short");
                    break;
                case 4:
                    area = scaled(box(has), 1.1, 2);
                    break;
                default:
                    fail("目标区域无效");
                }
                break;
            }
            case rust::VisionOp::FindTargetRaw: {
                raw = best(area, names, a[0]);
                break;
            }
            case rust::VisionOp::RawExists: {
                out[0] = raw.has_value();
                break;
            }
            case rust::VisionOp::RawConfidence: {
                out[0] = raw->confidence;
                break;
            }
            case rust::VisionOp::FindTargetYes: {
                yes = context_.find(
                    has, {.threshold = a[0], .search = area, .grayscale = true, .blurSigma = a[1]});
                break;
            }
            case rust::VisionOp::FindTargetNo: {
                absent = context_.find(
                    no, {.threshold = a[0], .search = area, .grayscale = true, .blurSigma = a[1]});
                break;
            }
            case rust::VisionOp::YesExists: {
                out[0] = yes.has_value();
                break;
            }
            case rust::VisionOp::NoExists: {
                out[0] = absent.has_value();
                break;
            }
            case rust::VisionOp::YesConfidence: {
                out[0] = yes->confidence;
                break;
            }
            case rust::VisionOp::NoConfidence: {
                out[0] = absent->confidence;
                break;
            }
            case rust::VisionOp::AdoptYes: {
                raw = yes;
                break;
            }
            case rust::VisionOp::AdoptNo: {
                raw = absent;
                break;
            }
            case rust::VisionOp::RingCount: {
                out[0] = cv::countNonZero(ring);
                break;
            }
            case rust::VisionOp::RawSizeMatches: {
                out[0] = raw->box.size() == box(has).size();
                break;
            }
            case rust::VisionOp::GoldRatio: {
                const auto b = raw->box;
                cv::Mat hsv, gold;
                cv::cvtColor(context_.frame(cv::Rect(b.x(), b.y(), b.width(), b.height())), hsv,
                             cv::COLOR_BGR2HSV);
                cv::inRange(hsv, cv::Scalar(10, 35, 55), cv::Scalar(48, 255, 255), gold);
                cv::dilate(gold, gold, cv::Mat::ones(3, 3, CV_8U));
                out[0] = double(cv::countNonZero(gold & ring)) / cv::countNonZero(ring);
                break;
            }
            case rust::VisionOp::NameRaw: {
                raw->name = a[0] != 0 ? has : no;
                break;
            }
            case rust::VisionOp::RawHasTarget: {
                out[0] = raw->name == has;
                break;
            }
            default:
                fail("Rust Target 识别宿主操作无效");
            }
        });
    return {result[0] != 0, result[1] != 0, result[2] != 0};
}
bool CombatVision::healthBar(bool inCombat) {
    const auto &image = context_.frame;
    const std::array args{double(inCombat)};
    return rust::visionRun(
               rust::VisionMethod::Health, args, [&](rust::VisionOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::VisionOp::ImageSize: {
                       out[0] = image.cols;
                       out[1] = image.rows;
                       break;
                   }
                   case rust::VisionOp::NormalHealth: {
                       out[0] = !colorRectangles(image, {{55, 55, 174}, {76, 85, 225}}, a[0], a[1], a[2])
                                     .isEmpty();
                       break;
                   }
                   case rust::VisionOp::BossHealth: {
                       const auto area = Templates::anchored({1269, 58, 1264, 142}, {3840, 2160},
                                                             {image.cols, image.rows}, true, true);
                       out[0] =
                           double(colorRectangles(image, {{4, 30, 245}, {75, 185, 255}}, a[0], a[1], -1, area)
                                      .size());
                       break;
                   }
                   default:
                       fail("Rust Health 识别宿主操作无效");
                   }
               })[0] != 0;
}
bool CombatVision::shiftedHealthBar() {
    const auto &image = context_.frame;
    cv::Mat mask;
    cv::Rect b;
    size_t selectedContour = 0;
    QRect levelArea;
    std::vector<std::vector<cv::Point>> contours;
    QVector<Match> bars;
    return rust::visionRun(
               rust::VisionMethod::ShiftedHealth, {}, [&](rust::VisionOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::VisionOp::ImageSize: {
                       out[0] = image.cols;
                       out[1] = image.rows;
                       break;
                   }
                   case rust::VisionOp::PixelRound: {
                       out[0] = pixelRound(a[0]);
                       break;
                   }
                   case rust::VisionOp::PrepareTopHealth: {
                       const auto top = relativeBox(image, .28, .025, .72, .09);
                       cv::Mat hsv, upper;
                       cv::cvtColor(image(cv::Rect(top.x(), top.y(), top.width(), top.height())), hsv,
                                    cv::COLOR_BGR2HSV);
                       cv::inRange(hsv, cv::Scalar(0, 90, 120), cv::Scalar(35, 255, 255), mask);
                       cv::inRange(hsv, cv::Scalar(170, 90, 120), cv::Scalar(179, 255, 255), upper);
                       mask |= upper;
                       cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                       break;
                   }
                   case rust::VisionOp::ContourCount: {
                       out[0] = double(contours.size());
                       break;
                   }
                   case rust::VisionOp::SelectContour: {
                       selectedContour = size_t(a[0]);
                       b = cv::boundingRect(contours.at(selectedContour));
                       break;
                   }
                   case rust::VisionOp::ContourBounds: {
                       out[0] = b.width;
                       out[1] = b.height;
                       out[2] = b.y;
                       break;
                   }
                   case rust::VisionOp::ContourAreaRatio: {
                       out[0] = cv::contourArea(contours.at(size_t(selectedContour))) / b.area();
                       break;
                   }
                   case rust::VisionOp::TopLevelArea: {
                       levelArea = relativeBox(image, .3, 0, .7, .052);
                       break;
                   }
                   case rust::VisionOp::ReadLevel: {
                       if (a[0] == 0)
                           out[0] = enemyLevelConfirmed(context_.text(levelArea, a[1]));
                       else
                           out[0] = enemyLevelConfirmed(context_.textScaled(levelArea, a[1], a[2]));
                       break;
                   }
                   case rust::VisionOp::ReadShiftedBars: {
                       bars = shiftedBarCandidates(image);
                       out[0] = double(bars.size());
                       break;
                   }
                   case rust::VisionOp::ShiftedBarPosition: {
                       const auto &r = bars.at(int(a[0])).box;
                       out[0] = r.x();
                       out[1] = r.y();
                       break;
                   }
                   case rust::VisionOp::ShiftedBarSize: {
                       const auto &r = bars.at(int(a[0])).box;
                       out[0] = r.width();
                       out[1] = r.height();
                       break;
                   }
                   case rust::VisionOp::SetLevelArea: {
                       levelArea = QRect(int(a[0]), int(a[1]), int(a[2]), int(a[3]));
                       break;
                   }
                   default:
                       fail("Rust ShiftedHealth 识别宿主操作无效");
                   }
               })[0] != 0;
}
bool CombatVision::deathDialog() {
    return rust::visionRun(
               rust::VisionMethod::Death, {}, [&](rust::VisionOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::VisionOp::DeathAnchor: {
                       if (a[0] == 0)
                           out[0] = bool(matchingText(context_.text(.15, .14, .85, .40),
                                                      "^(?:选择复苏物品|選擇復甦物品|失去意识|失去意識|"
                                                      "Select\\s+Revival\\s+Item|Unconscious)$"));
                       else
                           out[0] = bool(matchingText(context_.text(.35, .65, .82, .93),
                                                      "^(?:复苏|復甦|确认|確認|Revive|Confirm)$"));
                       break;
                   }
                   default:
                       fail("Rust Death 识别宿主操作无效");
                   }
               })[0] != 0;
}
std::optional<bool> CombatVision::challengeResult() {
    QVector<TextLine> titles;
    const auto result = rust::visionRun(
        rust::VisionMethod::Challenge, {}, [&](rust::VisionOp op, const double *a, double *out) {
            switch (op) {
            case rust::VisionOp::ReadChallengeTitles: {
                titles = context_.text(.2, .12, .8, .55);
                break;
            }
            case rust::VisionOp::ChallengeTitle: {
                const QString pattern =
                    a[0] != 0 ? "^(?:挑战成功|挑戰成功|Challenge\\s*(?:Success|Complete|Completed))$"
                              : "^(?:挑战失败|挑戰失敗|Challenge\\s*Failed)$";
                out[0] = bool(matchingText(titles, pattern));
                break;
            }
            case rust::VisionOp::ChallengeButton: {
                out[0] = bool(matchingText(
                    context_.text(.2, .6, .8, .95),
                    "^(?:确认成绩|確認成績|再次挑战|再次挑戰|重新挑战|重新挑戰|返回深塔|Confirm\\s*(?:Score|"
                    "Results?)|Challenge\\s*Again|Retry\\s*Challenge|Return\\s*to\\s*Tower)$"));
                break;
            }
            default:
                fail("Rust Challenge 识别宿主操作无效");
            }
        });
    if (result[0] == 0)
        return {};
    return result[1] != 0;
}
bool CombatVision::forte(Forte kind) {
    const std::array args{double(int(kind))};
    return rust::visionRun(
               rust::VisionMethod::Forte, args, [&](rust::VisionOp op, const double *a, double *out) {
                   switch (op) {
                   case rust::VisionOp::MouseForte: {
                       out[0] = bool(context_.find("mouse_forte", {.threshold = a[0],
                                                                   .horizontal = a[1],
                                                                   .vertical = a[2],
                                                                   .filter = Filter::Binary244}));
                       break;
                   }
                   case rust::VisionOp::ResonanceForte: {
                       out[0] = bool(context_.find(
                           "e_forte", {.threshold = a[0], .horizontal = a[1], .filter = Filter::Binary220}));
                       break;
                   }
                   case rust::VisionOp::BarForte: {
                       const auto r = Templates::anchored({2251, 1993, 60, 23}, {3840, 2160},
                                                          {context_.frame.cols, context_.frame.rows}, true);
                       out[0] = colorPercentage(context_.frame, r, {250, 246, 244}, {255, 255, 255});
                       break;
                   }
                   default:
                       fail("Rust Forte 识别宿主操作无效");
                   }
               })[0] != 0;
}
double CombatVision::white(const QString &name) {
    QRect area;
    return rust::visionRun(
        rust::VisionMethod::White, {}, [&](rust::VisionOp op, const double *a, double *out) {
            switch (op) {
            case rust::VisionOp::WhiteArea: {
                area = box(name);
                break;
            }
            case rust::VisionOp::WhiteRatio: {
                out[0] = colorPercentage(context_.frame, area, {a[0], a[0], a[0]}, {255, 255, 255});
                break;
            }
            case rust::VisionOp::WhiteSize: {
                out[0] = area.width();
                out[1] = area.height();
                break;
            }
            default:
                fail("Rust White 识别宿主操作无效");
            }
        })[0];
}
} // namespace okww::runtime
