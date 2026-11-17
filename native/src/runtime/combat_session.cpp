#include "combat_session.hpp"
#include "diagnostics.hpp"
#include "interaction.hpp"
#include "workshop.hpp"
#include <opencv2/imgcodecs.hpp>
namespace okww::runtime {
namespace {

QString skillName(Skill skill) {
    return QStringList{"resonance", "echo", "liberation", "extra_action"}[int(skill)];
}
} // namespace
CombatSession::CombatSession(TaskContext &context, const QString &resources, const QString &directory,
                             std::shared_ptr<TeamStore> teams)
    : context_(context), vision_(context, resources + "/native/resources/characters.json"),
      teams_(std::move(teams)), calibrationPath_(directory + "/_con_full_size.json") {
    std::array<double, 6> areas{};
    QJsonObject saved;
    rustRun(rust::SessionMethod::Initialize, {},
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::CalibrationExists:
                    out[0] = QFile::exists(calibrationPath_);
                    break;
                case rust::SessionOp::ReadCalibration:
                    saved = readJson(calibrationPath_);
                    break;
                case rust::SessionOp::CalibrationValue:
                    areas[size_t(a[0])] = saved[QString::number(int(a[0]))].toDouble();
                    break;
                case rust::SessionOp::CreateConcerto:
                    concerto_ = Concerto(areas);
                    break;
                case rust::SessionOp::InitialRevision:
                    enabledRevision_ = context_.state->combatEnableRevision;
                    break;
                default:
                    fail("Rust 战斗初始化宿主操作无效");
                }
            });
}
QJsonObject CombatSession::workshopTaskConfig() const {
    QJsonObject result;
    rust::sessionRun(rust::SessionMethod::WorkshopTaskConfig, {},
                     [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                         switch (op) {
                         case rust::SessionOp::ReadTaskConfig: {
                             result = context_.profile.group(context_.task);
                             break;
                         }
                         case rust::SessionOp::OverrideCount: {
                             out[0] = double(context_.taskOverrides.size());
                             break;
                         }
                         case rust::SessionOp::MergeOverride: {
                             const auto it = std::next(context_.taskOverrides.constBegin(), int(a[0]));
                             result[it.key()] = it.value();
                             break;
                         }
                         default:
                             fail("Rust WorkshopTaskConfig 宿主操作无效");
                         }
                     });
    return result;
}
QString CombatSession::targetName() const {
    const std::array args{double(context_.frame.cols)};
    const auto slot = rust::sessionRun(rust::SessionMethod::TargetName, args,
                                       [](rust::SessionOp, const double *, double *,
                                          const std::exception_ptr &) { fail("Rust 目标模板操作无效"); });
    const QString names[]{"has_target", "has_target_169"};
    return names[int(slot)];
}
void CombatSession::ensureElement(Character &character) {
    rustRun(rust::SessionMethod::EnsureElement, {},
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::CharacterRing: {
                    out[0] = character.state.ringIndex;
                    break;
                }
                case rust::SessionOp::CallCharacterConcerto: {
                    out[0] = concerto(character);
                    break;
                }
                default:
                    fail("Rust EnsureElement 宿主操作无效");
                }
            });
}
void CombatSession::log(const QString &message) {
    rustRun(rust::SessionMethod::LogIfInfo, {},
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::EmitCombatLog: {
                    context_.info("战斗", message);
                    break;
                }
                default:
                    fail("Rust LogIfInfo 宿主操作无效");
                }
            });
}
void CombatSession::pollControls() { rustRun(rust::SessionMethod::PollControls); }
Character *CombatSession::current() {
    const auto index = rustRun(rust::SessionMethod::Current);
    return index < 0 ? nullptr : party_.at(size_t(index)).get();
}
cv::Mat CombatSession::namedImage(const QString &name) {
    const auto box = vision_.box(name).intersected({0, 0, context_.frame.cols, context_.frame.rows});
    cv::Mat result;
    rustRun(rust::SessionMethod::NamedImage, {},
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::NamedRegionEmpty: {
                    out[0] = box.isEmpty();
                    break;
                }
                case rust::SessionOp::CloneNamedImage: {
                    result = context_.frame(cv::Rect(box.x(), box.y(), box.width(), box.height())).clone();
                    break;
                }
                default:
                    fail("Rust NamedImage 宿主操作无效");
                }
            });
    return result;
}
void CombatSession::snapshot(const QString &name) {
    QString path, safe;
    std::vector<uchar> bytes;
    std::optional<QSaveFile> file;
    rustRun(rust::SessionMethod::Snapshot, {},
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::SnapshotPath: {
                    path = QFileInfo(calibrationPath_).absolutePath() + "/../logs";
                    break;
                }
                case rust::SessionOp::MakeSnapshotDirectory: {
                    out[0] = QDir().mkpath(path);
                    break;
                }
                case rust::SessionOp::SanitizeSnapshotName: {
                    safe = name;
                    safe.remove(QRegularExpression("[<>:\"/\\\\|?*]"));
                    break;
                }
                case rust::SessionOp::EncodeSnapshot: {
                    out[0] = cv::imencode(".png", context_.frame, bytes);
                    break;
                }
                case rust::SessionOp::CreateSnapshotFile: {
                    file.emplace(path + "/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz_") +
                                 safe.left(80) + ".png");
                    break;
                }
                case rust::SessionOp::OpenSnapshot: {
                    out[0] = file->open(QIODevice::WriteOnly);
                    break;
                }
                case rust::SessionOp::WriteSnapshot: {
                    out[0] = file->write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) ==
                             static_cast<qint64>(bytes.size());
                    break;
                }
                case rust::SessionOp::CommitSnapshot: {
                    out[0] = file->commit();
                    break;
                }
                default:
                    fail("Rust Snapshot 宿主操作无效");
                }
            });
}
QVector<Character *> CombatSession::characters() {
    QVector<Character *> result;
    rustRun(rust::SessionMethod::Characters, {},
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::AppendMemberPointer: {
                    result.append(party_.at(size_t(a[0])).get());
                    break;
                }
                default:
                    fail("Rust Characters 宿主操作无效");
                }
            });
    return result;
}
void CombatSession::publishParty(const QString &codeState) {
    QJsonArray members;
    QJsonObject identity, report;
    QByteArray key;
    rustRun(rust::SessionMethod::PublishParty, {},
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::HasCombatInfo: {
                    out[0] = bool(context_.combatInfo);
                    break;
                }
                case rust::SessionOp::AppendPartyMember: {
                    const auto &member = *party_.at(size_t(a[0]));
                    members.append(QJsonObject{{"class", member.state.className},
                                               {"name", member.state.name},
                                               {"index", member.state.index},
                                               {"current", a[1] != 0}});
                    break;
                }
                case rust::SessionOp::SelectedTeamEmpty: {
                    out[0] = selectedTeam_.isEmpty();
                    break;
                }
                case rust::SessionOp::BuildPartyIdentity: {
                    const char *modes[]{"builtin", "python", "native-team"};
                    identity = {{"members", members},
                                {"mode", QLatin1String(modes[int(a[0])])},
                                {"code_state", codeState},
                                {"verified", a[1] != 0}};
                    break;
                }
                case rust::SessionOp::BuildPartyKey: {
                    key = QJsonDocument(identity).toJson(QJsonDocument::Compact);
                    break;
                }
                case rust::SessionOp::InitializePartyReport: {
                    report = identity;
                    break;
                }
                case rust::SessionOp::AppendPartyBridge: {
                    report["bridge"] = bridgeInfo_;
                    break;
                }
                case rust::SessionOp::PartyReportMatches: {
                    out[0] = report == lastPartyReport_;
                    break;
                }
                case rust::SessionOp::PartyKeyMatches: {
                    out[0] = key == lastPartyKey_;
                    break;
                }
                case rust::SessionOp::StorePartyKey: {
                    lastPartyKey_ = key;
                    break;
                }
                case rust::SessionOp::StorePartyReport: {
                    lastPartyReport_ = report;
                    break;
                }
                case rust::SessionOp::EmitPartyInfo: {
                    context_.combatInfo(std::move(report));
                    break;
                }
                default:
                    fail("Rust PublishParty 宿主操作无效");
                }
            });
}
bool CombatSession::loadParty() {
    QVector<std::optional<CharacterIdentity>> identities;
    std::optional<CharacterIdentity> observed;
    CharacterDefinition definition;
    QString selected;
    return rustRun(rust::SessionMethod::LoadParty, {},
                   [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                       switch (op) {
                       case rust::SessionOp::PartyScanMatches: {
                           out[0] = partyScanVersion_ == context_.frameVersion;
                           break;
                       }
                       case rust::SessionOp::StorePartyScan: {
                           partyScanVersion_ = context_.frameVersion;
                           break;
                       }
                       case rust::SessionOp::ReadIdentity: {
                           observed = vision_.character(int(a[0]), a[1] != 0 ? &party_.at(size_t(a[0]))->state
                                                                             : nullptr);
                           break;
                       }
                       case rust::SessionOp::ObservedIdentityExists: {
                           out[0] = observed.has_value();
                           break;
                       }
                       case rust::SessionOp::ObservedIdentitySupported: {
                           out[0] = nativeCharacterClasses().contains(observed->definition.className);
                           break;
                       }
                       case rust::SessionOp::UnsupportedCharacter: {
                           fail("当前队伍的 " + observed->definition.className +
                                " 尚未迁移；未发送战斗输入。");
                           break;
                       }
                       case rust::SessionOp::AppendIdentity: {
                           identities.append(observed);
                           break;
                       }
                       case rust::SessionOp::IdentityExists: {
                           out[0] = identities.at(int(a[0])).has_value();
                           break;
                       }
                       case rust::SessionOp::IdentityCount: {
                           out[0] = double(identities.size());
                           break;
                       }
                       case rust::SessionOp::MemberExists: {
                           out[0] = bool(party_.at(size_t(a[0])));
                           break;
                       }
                       case rust::SessionOp::MemberMatchesIdentity: {
                           out[0] = party_.at(size_t(a[0]))->state.name ==
                                    identities.at(int(a[0]))->definition.canonical;
                           break;
                       }
                       case rust::SessionOp::DropMacro: {
                           macro_.reset();
                           break;
                       }
                       case rust::SessionOp::ClearSelectedTeam: {
                           selectedTeam_.clear();
                           break;
                       }
                       case rust::SessionOp::ResizeParty: {
                           party_.resize(size_t(a[0]));
                           break;
                       }
                       case rust::SessionOp::ResizeDefinitions: {
                           partyDefinitions_.resize(int(a[0]));
                           break;
                       }
                       case rust::SessionOp::CopyIdentityDefinition: {
                           definition = identities.at(int(a[0]))->definition;
                           break;
                       }
                       case rust::SessionOp::CreateIdentifiedMember: {
                           CharacterState state;
                           state.index = int(a[0]);
                           state.name = definition.canonical;
                           state.role = definition.role;
                           state.buffTime = definition.buffTime;
                           state.ringIndex = definition.element;
                           state.confidence = identities.at(int(a[0]))->confidence;
                           party_.at(size_t(a[0])) = makeCharacter(definition.className, *this, state);
                           break;
                       }
                       case rust::SessionOp::StoreIdentityDefinition: {
                           partyDefinitions_[int(a[0])] = definition;
                           break;
                       }
                       case rust::SessionOp::CreateUnknownMember: {
                           CharacterState state;
                           state.index = int(a[0]);
                           state.name = "unknown";
                           party_.at(size_t(a[0])) = makeCharacter("BaseChar", *this, state);
                           break;
                       }
                       case rust::SessionOp::TeamsExist: {
                           out[0] = bool(teams_);
                           break;
                       }
                       case rust::SessionOp::ReadTeamSelection: {
                           selected = teams_->selection(*this);
                           break;
                       }
                       case rust::SessionOp::EmptyTeamSelection: {
                           selected = {};
                           break;
                       }
                       case rust::SessionOp::TeamSelectionMatches: {
                           out[0] = selected == selectedTeam_;
                           break;
                       }
                       case rust::SessionOp::StoreTeamSelection: {
                           selectedTeam_ = selected;
                           break;
                       }
                       case rust::SessionOp::ClearBridgeInfo: {
                           bridgeInfo_ = {};
                           break;
                       }
                       case rust::SessionOp::SelectionPython: {
                           out[0] = selected.startsWith("python:");
                           break;
                       }
                       case rust::SessionOp::PublishPartyState: {
                           publishParty(a[0] != 0 ? "loading" : "ready");
                           break;
                       }
                       case rust::SessionOp::CreateSelectedMacro: {
                           macro_ = teams_->create(selected, *this);
                           break;
                       }
                       default:
                           fail("Rust LoadParty 宿主操作无效");
                       }
                   }) != 0;
}
bool CombatSession::interval(double seconds) {
    const std::array args{seconds};
    return rustRun(rust::SessionMethod::Interval, args) != 0;
}
void CombatSession::tap(QString key, double after, double between, double down) {
    const std::array args{after, between, down};
    rustRun(rust::SessionMethod::Tap, args,
            [&](rust::SessionOp op, const double *a, double *, const std::exception_ptr &) {
                if (op == rust::SessionOp::InputKey)
                    context_.input.key(key, a[0]);
                else
                    fail("Rust 战斗按键宿主操作无效");
            });
}
void CombatSession::click(QString button, double between) {
    const std::array args{between};
    rustRun(rust::SessionMethod::Click, args,
            [&](rust::SessionOp op, const double *a, double *, const std::exception_ptr &) {
                if (op == rust::SessionOp::InputClick)
                    context_.input.click({}, a[0], button);
                else
                    fail("Rust 战斗鼠标宿主操作无效");
            });
}
QString CombatSession::hotkey(Skill skill) const {
    QString result;
    const std::array args{double(int(skill))};
    rust::sessionRun(rust::SessionMethod::Hotkey, args,
                     [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                         switch (op) {
                         case rust::SessionOp::ReadHotkey: {
                             const char *names[]{"Resonance Key", "Echo Key", "Liberation Key"};
                             const char *fallbacks[]{"e", "r", "q"};
                             result = context_.hotkey(QString::fromUtf8(names[int(a[0])]),
                                                      QString::fromUtf8(fallbacks[int(a[0])]));
                             break;
                         }
                         case rust::SessionOp::ExtraHotkey: {
                             result = "4";
                             break;
                         }
                         default:
                             fail("Rust Hotkey 宿主操作无效");
                         }
                     });
    return result;
}
void CombatSession::watchdog() { rustRun(rust::SessionMethod::Watchdog); }
void CombatSession::nextFrame() { rustRun(rust::SessionMethod::NextFrame); }
void CombatSession::sleep(double seconds) {
    const std::array args{seconds};
    rustRun(rust::SessionMethod::Sleep, args);
}
void CombatSession::refreshObservation() { rustRun(rust::SessionMethod::RefreshObservation); }
void CombatSession::workshopCheckpoint() { rustRun(rust::SessionMethod::WorkshopCheckpoint); }
bool CombatSession::hasCd(Skill skill, int index) {
    const std::array args{double(int(skill)), double(index)};
    return rustRun(rust::SessionMethod::HasCd, args) != 0;
}
double CombatSession::cooldown(Skill skill, int index) {
    const std::array args{double(int(skill)), double(index)};
    return rustRun(rust::SessionMethod::Cooldown, args);
}
bool CombatSession::feature(const QString &name, double threshold, const QString &box) {
    MatchOptions options;
    options.threshold = threshold;
    return rustRun(rust::SessionMethod::Feature, {},
                   [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                       switch (op) {
                       case rust::SessionOp::FeatureBoxEmpty: {
                           out[0] = box.isEmpty();
                           break;
                       }
                       case rust::SessionOp::FeatureSearchBox: {
                           options.search = vision_.box(box);
                           break;
                       }
                       case rust::SessionOp::FindFeature: {
                           out[0] = bool(context_.find(name, options));
                           break;
                       }
                       default:
                           fail("Rust Feature 宿主操作无效");
                       }
                   }) != 0;
}
QString CombatSession::bestInBox(const QString &box, const QStringList &names, double threshold) {
    QString result;
    std::optional<Match> match;
    const std::array args{threshold};
    rustRun(rust::SessionMethod::BestInBox, args,
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::CandidateNameCount: {
                    out[0] = double(names.size());
                    break;
                }
                case rust::SessionOp::FindCandidate: {
                    match = context_.find(names.at(int(a[0])),
                                          {.threshold = threshold, .search = vision_.box(box)});
                    out[0] = match.has_value();
                    break;
                }
                case rust::SessionOp::CandidateConfidence: {
                    out[0] = match->confidence;
                    break;
                }
                case rust::SessionOp::StoreBestCandidate: {
                    result = names.at(int(a[0]));
                    break;
                }
                default:
                    fail("Rust BestInBox 宿主操作无效");
                }
            });
    return result;
}
double CombatSession::whiteRatio(QSize reference, QRect area, cv::Scalar lower, bool hc) {
    return colorPercentage(
        context_.frame, Templates::anchored(area, reference, {context_.frame.cols, context_.frame.rows}, hc),
        lower, {255, 255, 255});
}
bool CombatSession::available(Skill skill, int index, bool checkColor, bool checkCd) {
    const std::array args{double(int(skill)), double(index), double(checkColor), double(checkCd)};
    return rustRun(rust::SessionMethod::Available, args) != 0;
}
double CombatSession::highlighted(Skill skill) { return vision_.white("box_" + skillName(skill)); }
cv::Mat CombatSession::imageRegion(QSize reference, QRect area, bool hc) {
    const auto r = Templates::anchored(area, reference, {context_.frame.cols, context_.frame.rows}, hc);
    const std::array args{double(r.x()),
                          double(r.y()),
                          double(r.right()),
                          double(r.bottom()),
                          double(context_.frame.cols),
                          double(context_.frame.rows)};
    cv::Mat result;
    rustRun(rust::SessionMethod::ImageRegion, args,
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::ExtractRegionImage: {
                    result = context_.frame(cv::Rect(r.x(), r.y(), r.width(), r.height()));
                    break;
                }
                default:
                    fail("Rust ImageRegion 宿主操作无效");
                }
            });
    return result;
}
bool CombatSession::featureInRegion(const QString &name, QSize reference, QRect area, double threshold) {
    return bool(context_.find(
        name,
        {.threshold = threshold,
         .search = Templates::anchored(area, reference, {context_.frame.cols, context_.frame.rows}, true)}));
}
int CombatSession::segmentedGauge(QSize reference, QRect area, ColorRange color, int segments,
                                  int minFrequency, int maxFrequency, double minAmplitude) {
    const auto r = Templates::anchored(area, reference, {context_.frame.cols, context_.frame.rows}, true);
    const std::array args{double(r.x()),
                          double(r.y()),
                          double(r.right()),
                          double(r.bottom()),
                          double(context_.frame.cols),
                          double(context_.frame.rows)};
    return int(rustRun(rust::SessionMethod::SegmentedGauge, args,
                       [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                           switch (op) {
                           case rust::SessionOp::MeasureSegmentedGauge: {
                               out[0] = segmentedGaugeValue(
                                   context_.frame(cv::Rect(r.x(), r.y(), r.width(), r.height())), color,
                                   segments, minFrequency, maxFrequency, minAmplitude);
                               break;
                           }
                           default:
                               fail("Rust SegmentedGauge 宿主操作无效");
                           }
                       }));
}
double CombatSession::concerto(Character &character) {
    std::array<double, 6> before{};
    double cached = 0;
    QJsonObject saved;
    return rustRun(rust::SessionMethod::Concerto, {},
                   [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                       switch (op) {
                       case rust::SessionOp::CaptureConcertoState: {
                           before = concerto_.fullAreas();
                           cached = character.state.currentCon;
                           break;
                       }
                       case rust::SessionOp::DetectConcerto: {
                           out[0] = concerto_.detect(context_.frame, character.state);
                           break;
                       }
                       case rust::SessionOp::RestoreConcertoCache: {
                           character.state.currentCon = cached;
                           break;
                       }
                       case rust::SessionOp::BeforeCalibrationArea: {
                           out[0] = before.at(size_t(a[0]));
                           break;
                       }
                       case rust::SessionOp::CurrentCalibrationArea: {
                           out[0] = concerto_.fullAreas().at(size_t(a[0]));
                           break;
                       }
                       case rust::SessionOp::StoreCalibrationArea: {
                           saved[QString::number(int(a[0]))] = a[1];
                           break;
                       }
                       case rust::SessionOp::WriteCalibration: {
                           writeJson(calibrationPath_, saved);
                           break;
                       }
                       default:
                           fail("Rust Concerto 宿主操作无效");
                       }
                   });
}
bool CombatSession::healthBar() { return rustRun(rust::SessionMethod::HealthBar) != 0; }
double CombatSession::concertoAreaRatio(Character &character) {
    QRect box;
    return rustRun(rust::SessionMethod::ConcertoAreaRatio, {},
                   [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                       switch (op) {
                       case rust::SessionOp::CharacterRing: {
                           out[0] = character.state.ringIndex;
                           break;
                       }
                       case rust::SessionOp::CallCharacterConcerto: {
                           out[0] = concerto(character);
                           break;
                       }
                       case rust::SessionOp::CurrentCalibrationArea: {
                           out[0] = concerto_.fullAreas().at(size_t(a[0]));
                           break;
                       }
                       case rust::SessionOp::BuildConcertoRegion: {
                           box = Concerto::region({context_.frame.cols, context_.frame.rows});
                           break;
                       }
                       case rust::SessionOp::ConcertoRegionInside: {
                           out[0] = QRect(0, 0, context_.frame.cols, context_.frame.rows).contains(box);
                           break;
                       }
                       case rust::SessionOp::FrameSize: {
                           out[0] = context_.frame.cols;
                           out[1] = context_.frame.rows;
                           break;
                       }
                       case rust::SessionOp::CountConcertoRing: {
                           out[0] = countRing(
                                        context_.frame(cv::Rect(box.x(), box.y(), box.width(), box.height())),
                                        concertoColors().at(size_t(a[0])), a[1])
                                        .area;
                           break;
                       }
                       default:
                           fail("Rust ConcertoAreaRatio 宿主操作无效");
                       }
                   });
}
bool CombatSession::visibleTarget() { return vision_.hasTarget(false, false); }
bool CombatSession::targetEnemy() { return rustRun(rust::SessionMethod::TargetEnemy) != 0; }
bool CombatSession::targetLocked() { return rustRun(rust::SessionMethod::TargetLocked) != 0; }
bool CombatSession::checkBreak() { return rustRun(rust::SessionMethod::CheckBreak) != 0; }
bool CombatSession::macroBreakPrompt() { return rustRun(rust::SessionMethod::MacroBreakPrompt) != 0; }
bool CombatSession::visibleBreakPrompt(double threshold) {
    const std::array args{threshold};
    return rustRun(rust::SessionMethod::VisibleBreakPrompt, args) != 0;
}
bool CombatSession::breakF(bool force) {
    const std::array args{double(force)};
    return rustRun(rust::SessionMethod::BreakF, args) != 0;
}
bool CombatSession::combatActive() { return rustRun(rust::SessionMethod::CombatActive) != 0; }
void CombatSession::checkCombat() { rustRun(rust::SessionMethod::CheckCombat); }
void CombatSession::resetCombat() { rustRun(rust::SessionMethod::ResetCombat); }
void CombatSession::updatePortraits() { rustRun(rust::SessionMethod::UpdatePortraits); }
void CombatSession::switchNext(Character &previous, bool freeIntro, bool targetLowCon,
                               const SwitchAction &post) {
    Character *target = nullptr;
    std::vector<SwitchCandidate> candidates;
    SwitchDecision decision{};
    const std::array args{double(freeIntro), double(targetLowCon)};
    rustRun(rust::SessionMethod::SwitchNext, args,
            [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
                switch (op) {
                case rust::SessionOp::PreviousCon: {
                    out[0] = previous.currentCon();
                    break;
                }
                case rust::SessionOp::ClearCandidates: {
                    std::vector<SwitchCandidate>{}.swap(candidates);
                    break;
                }
                case rust::SessionOp::PythonPriority: {
                    const auto value = dynamic_cast<PythonTeamMacro *>(macro_.get())
                                           ->priority(*party_.at(size_t(a[0])), a[1] != 0, a[2] != 0);
                    out[0] = value.has_value();
                    out[1] = value.value_or(0);
                    break;
                }
                case rust::SessionOp::NativeMacroPriority: {
                    const auto value = macro_->priority(*party_.at(size_t(a[0])), a[1] != 0);
                    out[0] = value.has_value();
                    out[1] = value.value_or(0);
                    break;
                }
                case rust::SessionOp::BuiltinPriority: {
                    out[0] = party_.at(size_t(a[0]))->switchPriority(&previous, a[1] != 0, a[2] != 0);
                    break;
                }
                case rust::SessionOp::AppendCandidate: {
                    candidates.push_back({&party_.at(size_t(a[0]))->state, int(a[1])});
                    break;
                }
                case rust::SessionOp::ChooseDecision: {
                    decision = chooseSwitch(previous.state, candidates, a[0] != 0, freezes, now());
                    out[0] = decision.index;
                    break;
                }
                case rust::SessionOp::LogSwitch: {
                    log(QString("%1 → %2：%3")
                            .arg(previous.state.index + 1)
                            .arg(decision.index + 1)
                            .arg(decision.reason));
                    break;
                }
                case rust::SessionOp::DecisionMatchesPrevious: {
                    out[0] = decision.index == previous.state.index;
                    break;
                }
                case rust::SessionOp::DisposeCandidates: {
                    std::vector<SwitchCandidate>{}.swap(candidates);
                    break;
                }
                case rust::SessionOp::ClearTarget: {
                    target = nullptr;
                    break;
                }
                case rust::SessionOp::SetTarget: {
                    target = party_.at(size_t(a[0])).get();
                    break;
                }
                case rust::SessionOp::PreviousNormalAttack: {
                    previous.normalAttacks(a[0]);
                    break;
                }
                case rust::SessionOp::ApplyIntro: {
                    applyIntro(previous.state, target->state, a[0] != 0);
                    break;
                }
                case rust::SessionOp::TargetSkipCheck: {
                    out[0] = target->skipSwitchCombatCheck(a[0] != 0);
                    break;
                }
                case rust::SessionOp::PreviousIndex: {
                    out[0] = previous.state.index;
                    break;
                }
                case rust::SessionOp::PreviousConFull: {
                    out[0] = previous.isConFull();
                    break;
                }
                case rust::SessionOp::PreviousCheckF: {
                    out[0] = previous.checkFOnSwitch;
                    break;
                }
                case rust::SessionOp::TargetWaitSwitch: {
                    out[0] = target->waitSwitch();
                    break;
                }
                case rust::SessionOp::TapTargetIndex: {
                    tap(QString::number(target->state.index + 1));
                    break;
                }
                case rust::SessionOp::TargetIndex: {
                    out[0] = target->state.index;
                    break;
                }
                case rust::SessionOp::PreviousSwitchOut: {
                    previous.switchOut(a[0] != 0);
                    break;
                }
                case rust::SessionOp::SetTargetCurrent: {
                    target->state.current = true;
                    break;
                }
                case rust::SessionOp::SetTargetSwitchIn: {
                    target->state.lastSwitchIn = a[0];
                    break;
                }
                case rust::SessionOp::FreezeSwitchIntro: {
                    freezes.add(now(), now(), target->introDuration(), -100);
                    break;
                }
                case rust::SessionOp::SetPreviousOutro: {
                    previous.state.lastOutro = a[0];
                    break;
                }
                case rust::SessionOp::HasSwitchPost: {
                    out[0] = bool(post);
                    break;
                }
                case rust::SessionOp::RunSwitchPost: {
                    post(*target, a[0] != 0);
                    break;
                }
                default:
                    fail("Rust 角色切换宿主操作无效");
                }
            });
}
void CombatSession::switchHealer() { rustRun(rust::SessionMethod::SwitchHealer); }
void CombatSession::performCurrent() { rustRun(rust::SessionMethod::PerformCurrent); }
bool CombatSession::runAutoCombat() { return rustRun(rust::SessionMethod::RunAutoCombat) != 0; }
bool CombatSession::active() { return rustRun(rust::SessionMethod::Active) != 0; }
void CombatSession::rustRecover(rust::SessionMethod method, bool realmEntryAtHealPoint,
                                const std::function<void()> &custom) {
    std::optional<Navigation> navigation;
    const std::array args{double(realmEntryAtHealPoint)};
    rustRun(method, args, [&](rust::SessionOp op, const double *a, double *out, const std::exception_ptr &) {
        switch (op) {
        case rust::SessionOp::CreateNavigation:
            navigation.emplace(context_);
            break;
        case rust::SessionOp::InRealm:
            out[0] = navigation->inRealm();
            break;
        case rust::SessionOp::ExitRealm:
            navigation->exitRealm(a[0]);
            break;
        case rust::SessionOp::HealAtBeacon:
            navigation->healAtBeacon();
            break;
        case rust::SessionOp::HasRecovery:
            out[0] = bool(custom);
            break;
        case rust::SessionOp::CustomRecovery:
            custom();
            break;
        default:
            fail("Rust 战斗恢复宿主操作无效");
        }
    });
}
void CombatSession::recoverStartingPopup(bool realmEntryAtHealPoint) {
    rustRecover(rust::SessionMethod::RecoverStartingPopup, realmEntryAtHealPoint);
}
bool CombatSession::combatOnce(const CombatOptions &options) {
    const std::array args{options.waitSeconds,           double(options.required),
                          double(options.target),        double(options.realmEntryAtHealPoint),
                          double(options.useLiberation), options.maximumSeconds};
    return rustRun(rust::SessionMethod::CombatOnce, args,
                   [&](rust::SessionOp op, const double *, double *, const std::exception_ptr &) {
                       if (op == rust::SessionOp::RecoverDead)
                           rustRecover(rust::SessionMethod::RecoverDead, options.realmEntryAtHealPoint,
                                       options.recover);
                       else
                           fail("Rust 单次战斗宿主操作无效");
                   }) != 0;
}
bool CombatSession::closeRevivalDialog() { return rustRun(rust::SessionMethod::CloseRevivalDialog) != 0; }
} // namespace okww::runtime
