#include "team_macro.hpp"
#include "workflow_abi.hpp"
namespace okww::runtime {
TeamMacro::~TeamMacro() {
    try {
        release();
    } catch (...) {
    }
}
void TeamMacro::click() { h_.rawClick("left", .01); }
void TeamMacro::key(QString key, double hold) { h_.rawTap(key, hold); }
void TeamMacro::send(Character &, Skill skill, double hold) { key(h_.hotkey(skill), hold); }
TeamMacro::RustResult TeamMacro::rustMacro(int method, std::initializer_list<double> args,
                                           RustRequest request) {
    RustResult result;
    QVector<Character *> characters;
    Character *selected = nullptr, *source = request.previous;
    const auto out = rust::workflowRun(
        rust::WorkflowMethod(method), {args.begin(), args.size()},
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
            case rust::WorkflowOp::Now:
                out[0] = h_.now();
                break;
            case rust::WorkflowOp::MacroNumber: {
                const std::array<double, 14> values{
                    attackInterval_,    switchKeyInterval_, switchKeyDown_, double(untrustedConfirm_),
                    animationTimeout_,  noAnimationGrace_,  conTimeout_,    conInterval_,
                    resendStep_,        skillSettle_,       lastOpener_,    h_.macroRearmAt,
                    h_.macroStartDelay, macroStart_};
                out[0] = values.at(size_t(a[0]));
                break;
            }
            case rust::WorkflowOp::MacroFlag: {
                const std::array flags{holding_,           poking_,      armed_,         h_.useLiberation,
                                       h_.skipCombatCheck, h_.macroBusy, h_.inLiberation};
                out[0] = flags.at(size_t(a[0]));
                break;
            }
            case rust::WorkflowOp::SetMacroFlag:
                switch (int(a[0])) {
                case 0:
                    holding_ = bool(a[1]);
                    break;
                case 1:
                    poking_ = bool(a[1]);
                    break;
                case 2:
                    armed_ = bool(a[1]);
                    break;
                case 3:
                    h_.useLiberation = bool(a[1]);
                    break;
                case 4:
                    h_.skipCombatCheck = bool(a[1]);
                    break;
                case 5:
                    h_.macroBusy = bool(a[1]);
                    break;
                case 6:
                    h_.inLiberation = bool(a[1]);
                    break;
                }
                break;
            case rust::WorkflowOp::MacroPreciseWait:
                h_.preciseWait(a[0]);
                break;
            case rust::WorkflowOp::MacroFailure:
                switch (int(a[0])) {
                case 0:
                    throw MacroBroken(request.text);
                case 1:
                    fail("宏普攻时间参数无效");
                case 2:
                    fail("宏技能时间参数无效");
                case 3:
                    throw MacroBroken("宏技能重复次数异常");
                }
                break;
            case rust::WorkflowOp::SetMacroSkillTime:
                switch (int(a[0])) {
                case 0:
                    request.actor->state.lastResonance = a[1];
                    break;
                case 1:
                    request.actor->state.lastEcho = a[1];
                    break;
                case 2:
                    request.actor->state.lastLiberation = a[1];
                    break;
                }
                break;
            case rust::WorkflowOp::MacroMouseDown:
                h_.mouseDown();
                break;
            case rust::WorkflowOp::MacroMouseUp:
                h_.mouseUp();
                break;
            case rust::WorkflowOp::SetMacroStart:
                macroStart_ = a[0];
                break;
            case rust::WorkflowOp::ClearMacroMarks:
                marks_.clear();
                break;
            case rust::WorkflowOp::AppendMacroMark:
                marks_.append(QString("%1@%2").arg(request.text).arg(a[0], 0, 'f', 2));
                break;
            case rust::WorkflowOp::MacroClick:
                click();
                break;
            case rust::WorkflowOp::MacroHasSample:
                out[0] = request.sample && bool(*request.sample);
                break;
            case rust::WorkflowOp::MacroSample:
                (*request.sample)();
                break;
            case rust::WorkflowOp::MacroSpamOption: {
                const std::array values{request.spam->interval, request.spam->clickInterval,
                                        double(request.spam->presses)};
                out[0] = values.at(size_t(a[0]));
                break;
            }
            case rust::WorkflowOp::MacroSender:
                (*request.sender)();
                break;
            case rust::WorkflowOp::MacroHasStop:
                out[0] = bool(request.spam->stop);
                break;
            case rust::WorkflowOp::MacroStop:
                out[0] = request.spam->stop();
                break;
            case rust::WorkflowOp::MacroSpamPoke:
                out[0] = request.spam->poke.value_or(poking_);
                break;
            case rust::WorkflowOp::MacroSpamNested:
                out[0] = spam(a[0]);
                break;
            case rust::WorkflowOp::LoadMacroCharacters:
                characters = h_.characters();
                break;
            case rust::WorkflowOp::MacroCharacterCount:
                out[0] = double(characters.size());
                break;
            case rust::WorkflowOp::LoadMacroCharacter:
                selected = characters.at(qsizetype(a[0]));
                break;
            case rust::WorkflowOp::MacroCharacterPresent:
                out[0] = selected != nullptr;
                break;
            case rust::WorkflowOp::MacroCharacterClassMatches:
                out[0] = selected->state.className.compare(request.text, Qt::CaseInsensitive) == 0;
                break;
            case rust::WorkflowOp::MacroCharacterNameMatches:
                out[0] = selected->state.name.compare(request.text, Qt::CaseInsensitive) == 0;
                break;
            case rust::WorkflowOp::ReturnMacroCharacter:
                result.actor = selected;
                break;
            case rust::WorkflowOp::MacroSwitchSourceOut:
                source->switchOut(bool(a[0]));
                break;
            case rust::WorkflowOp::SetMacroTargetFlag:
                switch (int(a[0])) {
                case 0:
                    request.actor->state.current = bool(a[1]);
                    break;
                case 1:
                    request.actor->state.hasIntro = bool(a[1]);
                    break;
                case 2:
                    request.actor->state.hasSubDpsIntro = bool(a[1]);
                    break;
                }
                break;
            case rust::WorkflowOp::MacroSourceRole:
                out[0] = int(source->state.role);
                break;
            case rust::WorkflowOp::SetMacroTargetSwitchIn:
                request.actor->state.lastSwitchIn = a[0];
                break;
            case rust::WorkflowOp::MacroIntroFreeze:
                h_.freezes.add(h_.now(), h_.now(), request.actor->introDuration(), -100);
                break;
            case rust::WorkflowOp::SetMacroSourceOutro:
                source->state.lastOutro = a[0];
                break;
            case rust::WorkflowOp::SetCurrentMacroTarget:
                cur_ = request.actor;
                break;
            case rust::WorkflowOp::MacroTargetPresent:
                out[0] = request.actor != nullptr;
                break;
            case rust::WorkflowOp::MacroTargetCurrent:
                out[0] = request.actor == cur_;
                break;
            case rust::WorkflowOp::MacroCurrentPresent:
                out[0] = cur_ != nullptr;
                break;
            case rust::WorkflowOp::FindMacroCurrent:
                cur_ = onField();
                break;
            case rust::WorkflowOp::SaveMacroSwitchSource:
                source = cur_;
                break;
            case rust::WorkflowOp::SetMacroSwitchPress:
                switchPress_ = a[0];
                break;
            case rust::WorkflowOp::MacroSwitchOption: {
                const std::array values{double(request.switching->clickFirst),
                                        request.switching->clickInterval, request.switching->timeout,
                                        request.switching->settle};
                out[0] = values.at(size_t(a[0]));
                break;
            }
            case rust::WorkflowOp::MacroSwitchKey:
                key(QString::number(request.actor->state.index + 1), switchKeyDown_);
                break;
            case rust::WorkflowOp::MacroAssumeConcerto:
                out[0] = request.switching->assumeCon;
                break;
            case rust::WorkflowOp::MacroReadConcerto:
                out[0] = request.switching->readCon;
                break;
            case rust::WorkflowOp::MacroSourceConcerto:
                out[0] = source->isConFull();
                break;
            case rust::WorkflowOp::MacroLivePartySize:
                out[0] = int(h_.characters().size());
                break;
            case rust::WorkflowOp::MacroSwitchPoke:
                out[0] = request.switching->clickDuring.value_or(poking_);
                break;
            case rust::WorkflowOp::MacroNextFrame:
                h_.nextFrame();
                break;
            case rust::WorkflowOp::MacroInTeam:
                out[0] = h_.inTeam();
                break;
            case rust::WorkflowOp::MacroPartyTarget:
                out[0] = h_.partyIndex() == request.actor->state.index;
                break;
            case rust::WorkflowOp::MacroPartyCount:
                out[0] = h_.partyCount();
                break;
            case rust::WorkflowOp::MacroHandoffNested:
                handoff(*source, *request.actor, bool(a[0]));
                break;
            case rust::WorkflowOp::MacroSwitchClick:
                out[0] = request.switching->click;
                break;
            case rust::WorkflowOp::MacroGapNested:
                out[0] = gap(a[0]);
                break;
            case rust::WorkflowOp::MacroCharacterOnField:
                out[0] = selected->state.index == h_.partyIndex();
                break;
            case rust::WorkflowOp::MacroHudFreeze:
                h_.freezes.add(h_.now(), a[0], h_.now() - a[0]);
                break;
            case rust::WorkflowOp::MacroActorElapsed:
                out[0] = request.actor->elapsed(a[0]);
                break;
            case rust::WorkflowOp::MacroRefreshObservation:
                h_.refreshObservation();
                break;
            case rust::WorkflowOp::MacroActorConcerto:
                out[0] = request.actor->currentCon();
                break;
            case rust::WorkflowOp::MacroAvailable:
                out[0] = (*request.available)();
                break;
            case rust::WorkflowOp::MacroPressSkillNested: {
                const auto skill = Skill(int(a[0]));
                out[0] = pressUntilCd([&] { send(*request.actor, skill); },
                                      [&] {
                                          return skill == Skill::Resonance
                                                     ? request.actor->resonanceAvailable()
                                                     : request.actor->echoAvailable();
                                      },
                                      a[1], a[2], bool(a[3]));
                break;
            }
            case rust::WorkflowOp::MacroEchoSpamNested:
                spamKey([&] { send(*request.actor, Skill::Echo); }, a[1],
                        {.interval = 0, .poke = false, .presses = int(a[0])});
                break;
            case rust::WorkflowOp::MacroRecordEcho:
                record(*request.actor, Skill::Echo);
                break;
            case rust::WorkflowOp::SetMacroEntryCurrent:
                cur_ = request.actor;
                break;
            case rust::WorkflowOp::DisableMacroSwitchF:
                selected->checkFOnSwitch = false;
                break;
            case rust::WorkflowOp::MacroStartNested:
                start();
                break;
            case rust::WorkflowOp::MacroBody:
                (*request.body)();
                break;
            case rust::WorkflowOp::MacroErrorBroken:
                try {
                    if (error)
                        std::rethrow_exception(error);
                    out[0] = false;
                } catch (const MacroBroken &) {
                    out[0] = true;
                } catch (...) {
                    out[0] = false;
                }
                break;
            case rust::WorkflowOp::ReportMacroBroken:
                try {
                    std::rethrow_exception(error);
                } catch (const MacroBroken &e) {
                    h_.log("队伍连招中断，重新确认场上角色后继续：" + errorText(e));
                }
                break;
            case rust::WorkflowOp::MacroReleaseNested:
                release();
                break;
            case rust::WorkflowOp::ReportMacroMarks:
                h_.log(marks_.join(' '));
                break;
            default:
                fail("Rust 队伍宏宿主无效");
            }
        });
    result.number = out[0];
    return result;
}
void TeamMacro::wait(double seconds) { rustMacro(int(rust::WorkflowMethod::MacroWait), {seconds}, {}); }
void TeamMacro::record(Character &actor, Skill skill) {
    RustRequest r;
    r.actor = &actor;
    rustMacro(int(rust::WorkflowMethod::MacroRecord), {double(int(skill))}, r);
}
void TeamMacro::hold() { rustMacro(int(rust::WorkflowMethod::MacroHold), {}, {}); }
void TeamMacro::release() { rustMacro(int(rust::WorkflowMethod::MacroRelease), {}, {}); }
void TeamMacro::start() { rustMacro(int(rust::WorkflowMethod::MacroStart), {}, {}); }
void TeamMacro::mark(const QString &text) {
    RustRequest r;
    r.text = text;
    rustMacro(int(rust::WorkflowMethod::MacroMark), {}, r);
}
void TeamMacro::need(bool ok, const QString &reason) {
    RustRequest r;
    r.text = reason;
    rustMacro(int(rust::WorkflowMethod::MacroNeed), {double(ok)}, r);
}
int TeamMacro::spam(double window, double interval, int maxClicks, const std::function<bool()> &sample,
                    int sampleEvery) {
    RustRequest r;
    r.sample = &sample;
    return int(rustMacro(int(rust::WorkflowMethod::MacroSpam),
                         {window, interval, double(maxClicks), double(sampleEvery)}, r)
                   .number);
}
int TeamMacro::spamKey(const std::function<void()> &sender, double window, const MacroKeySpam &options) {
    RustRequest r;
    r.sender = &sender;
    r.spam = &options;
    return int(rustMacro(int(rust::WorkflowMethod::MacroSpamKey), {window}, r).number);
}
int TeamMacro::gap(double seconds) {
    return int(rustMacro(int(rust::WorkflowMethod::MacroGap), {seconds}, {}).number);
}
Character *TeamMacro::teammate(const QString &name) {
    RustRequest r;
    r.text = name;
    return rustMacro(int(rust::WorkflowMethod::MacroTeammate), {}, r).actor;
}
void TeamMacro::handoff(Character &previous, Character &next, bool intro) {
    RustRequest r;
    r.previous = &previous;
    r.actor = &next;
    rustMacro(int(rust::WorkflowMethod::MacroHandoff), {double(intro)}, r);
}
bool TeamMacro::quickSwitch(Character *target, const MacroSwitch &options) {
    RustRequest r;
    r.actor = target;
    r.switching = &options;
    return rustMacro(int(rust::WorkflowMethod::MacroQuickSwitch), {}, r).number != 0;
}
Character *TeamMacro::onField(double timeout) {
    return rustMacro(int(rust::WorkflowMethod::MacroOnField), {timeout}, {}).actor;
}
bool TeamMacro::waitHud(bool poke, double timeout, double tail) {
    return rustMacro(int(rust::WorkflowMethod::MacroWaitHud), {double(poke), timeout, tail}, {}).number != 0;
}
bool TeamMacro::waitHudUp(double cap) {
    return rustMacro(int(rust::WorkflowMethod::MacroWaitHudUp), {cap}, {}).number != 0;
}
bool TeamMacro::conAtLeast(Character &actor, double level, double cap, double interval, bool poke) {
    RustRequest r;
    r.actor = &actor;
    return rustMacro(int(rust::WorkflowMethod::MacroConcertoAtLeast), {level, cap, interval, double(poke)}, r)
               .number != 0;
}
bool TeamMacro::pressUntilCd(const std::function<void()> &sender, const std::function<bool()> &available,
                             double cap, double grace, bool poke) {
    RustRequest r;
    r.sender = &sender;
    r.available = &available;
    return rustMacro(int(rust::WorkflowMethod::MacroPressUntilCd), {cap, grace, double(poke)}, r).number != 0;
}
bool TeamMacro::pressRes(Character &actor, double cap, double grace, bool poke) {
    RustRequest r;
    r.actor = &actor;
    return rustMacro(int(rust::WorkflowMethod::MacroPressResonance), {cap, grace, double(poke)}, r).number !=
           0;
}
bool TeamMacro::pressEcho(Character &actor, double cap, double grace) {
    RustRequest r;
    r.actor = &actor;
    return rustMacro(int(rust::WorkflowMethod::MacroPressEcho), {cap, grace}, r).number != 0;
}
void TeamMacro::echoNow(Character &actor, int presses, double window) {
    RustRequest r;
    r.actor = &actor;
    rustMacro(int(rust::WorkflowMethod::MacroEchoNow), {double(presses), window}, r);
}
bool TeamMacro::openerPending(double gap) const {
    return const_cast<TeamMacro *>(this)
               ->rustMacro(int(rust::WorkflowMethod::MacroOpenerPending), {gap}, {})
               .number != 0;
}
bool TeamMacro::execute(Character &entry, const std::function<void()> &body) {
    RustRequest r;
    r.actor = &entry;
    r.body = &body;
    return rustMacro(int(rust::WorkflowMethod::MacroExecute), {}, r).number != 0;
}
void TeamMacro::reset() { rustMacro(int(rust::WorkflowMethod::MacroReset), {}, {}); }
void TeamMacro::combatEnd() { rustMacro(int(rust::WorkflowMethod::MacroCombatEnd), {}, {}); }
} // namespace okww::runtime
