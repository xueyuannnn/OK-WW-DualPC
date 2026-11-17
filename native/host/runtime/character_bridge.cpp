#include "character.hpp"
#include "character_registry.hpp"
#include "gauge_analysis.hpp"
#include <opencv2/imgproc.hpp>

namespace okww::runtime {
std::array<double, 3> Character::rustRun(rust::RotationMethod method, std::initializer_list<double> args,
                                         const QString &key, const std::function<bool()> &condition,
                                         const SwitchAction &post, Character **selected,
                                         std::span<double> local, Character **related) {
    struct Context {
        Character *actor;
        const QString &key;
        const std::function<bool()> &condition;
        const SwitchAction &post;
        Character **result;
        std::span<double> local;
        Character **related;
        QVector<Character *> party;
        Character *selected = nullptr;
        std::optional<QString> capturedKey;
        std::optional<QString> capturedTarget;
        std::exception_ptr error;
    } context{this, key, condition, post, selected, local, related, {}, nullptr, {}, {}, {}};
    const rust::RotationHost bridge{
        &context,
        [](void *opaque, int32_t operation, const double *a, size_t, double *out) -> int32_t {
            auto &context = *static_cast<Context *>(opaque);
            auto &c = *context.actor;
            auto &h = c.host_;
            try {
                switch (static_cast<rust::RotationOp>(operation)) {
                case rust::RotationOp::Now:
                    out[0] = h.now();
                    break;
                case rust::RotationOp::NextFrame:
                    h.nextFrame();
                    break;
                case rust::RotationOp::CheckCombat:
                    h.checkCombat();
                    break;
                case rust::RotationOp::InTeam:
                    out[0] = h.inTeam();
                    break;
                case rust::RotationOp::HostFlag:
                    switch (int(a[0])) {
                    case 0:
                        out[0] = h.inLiberation;
                        break;
                    case 1:
                        out[0] = h.skipCombatCheck;
                        break;
                    case 2:
                        out[0] = h.useLiberation;
                        break;
                    case 3:
                        out[0] = h.hasLevitator;
                        break;
                    case 4:
                        out[0] = h.openWorldAutoCombat();
                        break;
                    case 5:
                        out[0] = h.isAutoCombatTask();
                        break;
                    default:
                        fail("Rust 战斗宿主字段无效");
                    }
                    break;
                case rust::RotationOp::SetHostFlag:
                    switch (int(a[0])) {
                    case 0:
                        h.inLiberation = a[1] != 0;
                        break;
                    case 1:
                        h.skipCombatCheck = a[1] != 0;
                        break;
                    default:
                        fail("Rust 战斗宿主字段不可修改");
                    }
                    break;
                case rust::RotationOp::State:
                    switch (int(a[0])) {
                    case 0:
                        out[0] = c.state.hasIntro;
                        break;
                    case 1:
                        out[0] = c.state.hasSubDpsIntro;
                        break;
                    case 2:
                        out[0] = c.state.current;
                        break;
                    case 3:
                        out[0] = c.state.ringIndex;
                        break;
                    case 4:
                        out[0] = c.state.currentCon;
                        break;
                    case 5:
                        out[0] = c.state.lastPerform;
                        break;
                    case 6:
                        out[0] = c.introMotion;
                        break;
                    case 7:
                        out[0] = c.cycleStartTime;
                        break;
                    case 8:
                        out[0] = c.state.liberationReady;
                        break;
                    case 9:
                        out[0] = c.state.echoReady;
                        break;
                    case 10:
                        out[0] = c.state.resonanceReady;
                        break;
                    case 11:
                        out[0] = c.state.hasToolBox;
                        break;
                    case 12:
                        out[0] = c.state.lastFullConSwitch;
                        break;
                    case 13:
                        out[0] = c.state.lastResonance;
                        break;
                    case 14:
                        out[0] = c.state.lastEcho;
                        break;
                    case 15:
                        out[0] = c.state.lastLiberation;
                        break;
                    case 16:
                        out[0] = c.state.index;
                        break;
                    case 17:
                        out[0] = int(c.state.role);
                        break;
                    case 18:
                        out[0] = c.state.buffTime;
                        break;
                    case 19:
                        out[0] = c.checkFOnSwitch;
                        break;
                    case 20:
                        out[0] = c.state.lastSwitch;
                        break;
                    case 21:
                        out[0] = c.state.lastOutro;
                        break;
                    default:
                        fail("Rust 角色字段无效");
                    }
                    break;
                case rust::RotationOp::SetState:
                    switch (int(a[0])) {
                    case 0:
                        c.state.hasIntro = a[1] != 0;
                        break;
                    case 1:
                        c.state.hasSubDpsIntro = a[1] != 0;
                        break;
                    case 2:
                        c.state.current = a[1] != 0;
                        break;
                    case 3:
                        c.state.ringIndex = int(a[1]);
                        break;
                    case 4:
                        c.state.currentCon = a[1];
                        break;
                    case 5:
                        c.state.lastPerform = a[1];
                        break;
                    case 6:
                        c.introMotion = a[1];
                        break;
                    case 7:
                        c.cycleStartTime = a[1];
                        break;
                    case 8:
                        c.state.liberationReady = a[1] != 0;
                        break;
                    case 9:
                        c.state.echoReady = a[1] != 0;
                        break;
                    case 10:
                        c.state.resonanceReady = a[1] != 0;
                        break;
                    case 11:
                        c.state.hasToolBox = a[1] != 0;
                        break;
                    case 12:
                        c.state.lastFullConSwitch = a[1];
                        break;
                    case 13:
                        c.state.lastResonance = a[1];
                        break;
                    case 14:
                        c.state.lastEcho = a[1];
                        break;
                    case 15:
                        c.state.lastLiberation = a[1];
                        break;
                    case 17:
                        c.state.role = Role(int(a[1]));
                        break;
                    case 18:
                        c.state.buffTime = a[1];
                        break;
                    case 19:
                        c.checkFOnSwitch = a[1] != 0;
                        break;
                    case 21:
                        c.state.lastOutro = a[1];
                        break;
                    default:
                        fail("Rust 角色字段不可修改");
                    }
                    break;
                case rust::RotationOp::Sleep:
                    h.sleep(a[0]);
                    break;
                case rust::RotationOp::SkillAvailable:
                    switch (int(a[0])) {
                    case 0:
                        out[0] = c.resonanceAvailable();
                        break;
                    case 1:
                        out[0] = c.echoAvailable();
                        break;
                    case 2:
                        out[0] = c.liberationAvailable();
                        break;
                    default:
                        fail("Rust 技能类型无效");
                    }
                    break;
                case rust::RotationOp::HasCd:
                    out[0] = h.hasCd(Skill(int(a[0])), a[1] == -2 ? c.state.index : int(a[1]));
                    break;
                case rust::RotationOp::Available:
                    out[0] = h.available(Skill(int(a[0])), int(a[1]), a[2] != 0, a[3] != 0);
                    break;
                case rust::RotationOp::Highlighted:
                    out[0] = h.highlighted(Skill(int(a[0])));
                    break;
                case rust::RotationOp::LevitatorWhite:
                    out[0] = h.levitatorWhite();
                    break;
                case rust::RotationOp::Forte:
                    out[0] = h.forte(Forte(int(a[0])));
                    break;
                case rust::RotationOp::Concerto:
                    out[0] = h.concerto(c);
                    break;
                case rust::RotationOp::Click: {
                    static const QStringList buttons{"left", "right", "middle"};
                    h.click(buttons.at(int(a[0])), a[1]);
                    break;
                }
                case rust::RotationOp::Mouse:
                    if (a[0] != 0)
                        h.mouseDown();
                    else
                        h.mouseUp();
                    break;
                case rust::RotationOp::Key:
                    if (a[0] != 0)
                        h.keyDown(context.capturedKey.value_or(context.key));
                    else
                        h.keyUp(context.capturedKey.value_or(context.key));
                    break;
                case rust::RotationOp::Tap:
                    h.tap(int(a[0]) == 1   ? QString("f")
                          : int(a[0]) == 2 ? h.toolKey()
                          : int(a[0]) == 3 ? h.jumpKey()
                                           : context.key,
                          a[1], a[2], a[3]);
                    break;
                case rust::RotationOp::Send:
                    c.send(Skill(int(a[0])), a[1], a[2], a[3]);
                    break;
                case rust::RotationOp::FreezeAdd:
                    h.freezes.add(a[0], a[1], a[2]);
                    break;
                case rust::RotationOp::Elapsed:
                    out[0] = c.elapsed(a[0], a[1] != 0);
                    break;
                case rust::RotationOp::SkillFailed:
                    c.alertSkillFailed();
                    break;
                case rust::RotationOp::Interrupted: {
                    static const QStringList reasons{"解放技能持续点击超时",
                                                     "解放动画超时或战斗已经结束",
                                                     "洛可可下落攻击等待超时",
                                                     "忌炎等待回路或协奏超时",
                                                     "相里要解放期间等待共鸣技能超时",
                                                     "秋远连招等待超时",
                                                     "玄翎秧秧长按连招超时",
                                                     "千咲循环等待超时",
                                                     "尤诺循环等待超时",
                                                     "尤诺重击图标未消失",
                                                     "尤诺跳跃图标未消失",
                                                     "爱弥斯长动作条未结束",
                                                     "爱弥斯连招等待超时",
                                                     "西格莉卡循环等待超时",
                                                     "今汐共鸣阶段等待超时",
                                                     "布兰特延奏阶段超时",
                                                     "Changli liberation input timed out",
                                                     "Changli liberation animation timed out",
                                                     "Cartethyia resonance observation timed out",
                                                     "Carlotta liberation chain timed out",
                                                     "Carlotta outro forte timed out",
                                                     "Carlotta outro timed out",
                                                     "Carlotta liberation animation timed out",
                                                     "Camellya ephemeral did not consume its indicator",
                                                     "Camellya budding indicator did not end",
                                                     "Zani liberation exit indicator timed out"};
                    throw CombatInterrupted(reasons.at(int(a[0])));
                }
                case rust::RotationOp::Condition:
                    out[0] = context.condition();
                    break;
                case rust::RotationOp::Virtual:
                    switch (int(a[0])) {
                    case 0: {
                        const auto r = c.resonance({a[1], a[2] != 0, a[3] != 0, a[4], a[5], a[6] != 0});
                        out[0] = r.clicked;
                        out[1] = r.duration;
                        out[2] = r.animated;
                        break;
                    }
                    case 1:
                        out[0] = c.echo({a[1], a[2], a[3]});
                        break;
                    case 2:
                        out[0] = c.liberation({a[1], a[2] != 0, a[3], a[4], a[5] != 0});
                        break;
                    case 3:
                        c.switchNext(a[1] != 0, a[2] != 0);
                        break;
                    case 4:
                        out[0] = c.forteFull();
                        break;
                    case 5:
                        out[0] = c.down();
                        break;
                    case 6:
                        out[0] = c.flying();
                        break;
                    case 7:
                        c.normalAttacks(a[1], a[2], a[3], a[4] != 0, a[5] != 0);
                        break;
                    case 8:
                        c.waitIntro(a[1], a[2] != 0);
                        break;
                    case 9:
                        out[0] = c.heavyForte(Forte(int(a[1])));
                        break;
                    case 10:
                        c.doPerform();
                        break;
                    case 11:
                        c.repeatedKey(h.hotkey(Skill(int(a[1]))), a[2], a[3]);
                        break;
                    default:
                        fail("Rust 角色虚调用无效");
                    }
                    break;
                case rust::RotationOp::Switch:
                    h.switchNext(c, a[0] != 0, a[1] != 0, context.post);
                    break;
                case rust::RotationOp::CheckBreak:
                    out[0] = h.checkBreak();
                    break;
                case rust::RotationOp::TapSkill:
                    h.tap(h.hotkey(Skill(int(a[0]))), a[1], a[2], a[3]);
                    break;
                case rust::RotationOp::HealerLocked:
                    out[0] = c.state.healerLocked(h.freezes, h.now());
                    break;
                case rust::RotationOp::LoadParty:
                    context.party = h.characters();
                    out[0] = double(context.party.size());
                    break;
                case rust::RotationOp::PartyField: {
                    auto *member = context.party.value(int(a[0]));
                    if (int(a[1]) == 0)
                        out[0] = member != nullptr;
                    else if (member)
                        switch (int(a[1])) {
                        case 1:
                            out[0] = member == &c;
                            break;
                        case 2:
                            out[0] = int(member->state.role);
                            break;
                        case 3:
                            out[0] = member->state.index;
                            break;
                        case 4:
                            out[0] = member->state.lastSwitch;
                            break;
                        case 5:
                            out[0] = member->state.buffTime;
                            break;
                        case 6:
                            out[0] = member->hasBuff();
                            break;
                        case 7:
                            out[0] = member->state.current;
                            break;
                        case 8:
                            out[0] = member->state.className == context.key;
                            break;
                        case 9:
                            out[0] = member == context.selected;
                            break;
                        case 10:
                            out[0] = member->state.name == "Augusta" || member->state.name == "char_augusta";
                            break;
                        case 11:
                            out[0] = isShoreKeeper(member);
                            break;
                        default:
                            fail("Rust 队伍字段无效");
                        }
                    break;
                }
                case rust::RotationOp::SelectParty:
                    context.selected = context.party.value(int(a[0]));
                    if (context.result)
                        *context.result = context.selected;
                    break;
                case rust::RotationOp::PartyPriority:
                    out[0] = context.party.at(int(a[0]))->switchPriority(context.selected, false, false);
                    break;
                case rust::RotationOp::PartySetCurrent:
                    context.party.at(int(a[0]))->state.current = a[1] != 0;
                    break;
                case rust::RotationOp::PartyIndex:
                    out[0] = h.partyIndex();
                    break;
                case rust::RotationOp::TapSlot:
                    h.tap(QString::number(int(a[0]) + 1));
                    break;
                case rust::RotationOp::LongFeature: {
                    static const QStringList boxes{"box_target_enemy_long", "target_box_long2",
                                                   "target_box_short"};
                    out[0] = h.feature(h.targetName(), .6, boxes.at(int(a[0])));
                    break;
                }
                case rust::RotationOp::DodgeSelected:
                    out[0] = context.selected->autoDodge([&] { return c.flying(); });
                    break;
                case rust::RotationOp::Local:
                    if (context.local.empty())
                        fail("Rust 角色私有状态缺失");
                    out[0] = context.local[0];
                    break;
                case rust::RotationOp::SetLocal:
                    if (context.local.empty())
                        fail("Rust 角色私有状态缺失");
                    context.local[0] = a[0];
                    break;
                case rust::RotationOp::SuomingDps:
                    out[0] = h.characterSetting("Suoming DPS", false).toBool();
                    break;
                case rust::RotationOp::SuomingForte:
                    out[0] = h.feature("suoming_forte_e");
                    break;
                case rust::RotationOp::CaptureKey:
                    context.capturedKey = h.hotkey(Skill(int(a[0])));
                    break;
                case rust::RotationOp::LocalAt:
                    if (a[0] < 0 || size_t(a[0]) >= context.local.size())
                        fail("Rust 角色私有状态下标无效");
                    out[0] = context.local[size_t(a[0])];
                    break;
                case rust::RotationOp::SetLocalAt:
                    if (a[0] < 0 || size_t(a[0]) >= context.local.size())
                        fail("Rust 角色私有状态下标无效");
                    context.local[size_t(a[0])] = a[1];
                    break;
                case rust::RotationOp::BaseSwitch:
                    c.Character::switchNext(a[0] != 0, a[1] != 0, context.post);
                    break;
                case rust::RotationOp::RocciaSwitch:
                    c.Character::switchNext(a[0] != 0, a[1] != 0, [](Character &next, bool intro) {
                        next.state.hasToolBox =
                            rust::okww_rs_toolbox_intro(intro, next.state.hasToolBox) != 0;
                    });
                    break;
                case rust::RotationOp::BaseSwitchOut:
                    c.Character::switchOut(a[0] != 0);
                    break;
                case rust::RotationOp::PartyDodge:
                    context.party.at(int(a[0]))->autoDodge([&] { return c.flying(); });
                    break;
                case rust::RotationOp::HasBuff:
                    out[0] = c.hasBuff();
                    break;
                case rust::RotationOp::BreakF:
                    out[0] = h.breakF(a[0] != 0);
                    break;
                case rust::RotationOp::KeeperDodgeQuery:
                    out[0] = c.shoreKeeperDodge();
                    break;
                case rust::RotationOp::CaptureTarget:
                    context.capturedTarget = h.targetName();
                    break;
                case rust::RotationOp::Cooldown:
                    out[0] = h.cooldown(Skill(int(a[0])), int(a[1]));
                    break;
                case rust::RotationOp::Related: {
                    if (!context.related)
                        fail("Rust 角色关联状态缺失");
                    auto *member = *context.related;
                    if (int(a[0]) == 0) {
                        out[0] = member != nullptr;
                        break;
                    }
                    if (!member)
                        fail("Rust 关联角色未设置");
                    switch (int(a[0])) {
                    case 1:
                        out[0] = member->partnerReady();
                        break;
                    case 2:
                        out[0] = member->smallForm();
                        break;
                    case 3:
                        member->clearPartnerGauge();
                        break;
                    case 4:
                        member->setPartner(&c);
                        break;
                    case 5:
                        out[0] = member->partnerPhase();
                        break;
                    case 6:
                        out[0] = member->partnerGauge();
                        break;
                    case 7:
                        out[0] = member->partnerTimeLeft();
                        break;
                    case 8:
                        out[0] = member->outroCount();
                        break;
                    case 9:
                        out[0] = member->inOutro();
                        break;
                    default:
                        fail("Rust 关联角色字段无效");
                    }
                    break;
                }
                case rust::RotationOp::SetRelated:
                    if (!context.related)
                        fail("Rust 角色关联状态缺失");
                    *context.related = a[0] == 0 ? nullptr : context.selected;
                    break;
                case rust::RotationOp::EnsureElement:
                    h.ensureElement(c);
                    break;
                case rust::RotationOp::CombatStart:
                    out[0] = h.combatStart;
                    break;
                case rust::RotationOp::TargetLocked:
                    out[0] = h.targetLocked();
                    break;
                case rust::RotationOp::FreezeThreshold:
                    h.freezes.add(a[0], a[1], a[2], a[3]);
                    break;
                default:
                    fail("Rust 角色操作无效");
                }
                return 1;
            } catch (...) {
                // Preserve the initial failure while allowing Rust to run exactly
                // the same flag/key cleanup that C++ stack unwinding used to run.
                if (!context.error)
                    context.error = std::current_exception();
                return 0;
            }
        },
        [](void *opaque, int32_t kind, const uint8_t *name, size_t nameLength, const uint8_t *area,
           size_t areaLength, const double *args, size_t count, double *out) -> int32_t {
            auto &context = *static_cast<Context *>(opaque);
            try {
                const auto key =
                    QString::fromUtf8(reinterpret_cast<const char *>(name), qsizetype(nameLength));
                switch (kind) {
                case 0:
                    if (count != 1)
                        fail("Rust 角色特征查询参数无效");
                    out[0] = context.actor->host_.feature(
                        key, args[0],
                        QString::fromUtf8(reinterpret_cast<const char *>(area), qsizetype(areaLength)));
                    break;
                case 1:
                    if (count != 0)
                        fail("Rust 角色设置查询参数无效");
                    out[0] = context.actor->host_.characterSetting(key).toBool();
                    break;
                case 2:
                    if (count != 1)
                        fail("Rust 角色模板查询参数无效");
                    out[0] = !context.actor->host_
                                  .bestInBox(QString::fromUtf8(reinterpret_cast<const char *>(area),
                                                               qsizetype(areaLength)),
                                             key.split(QChar('\0')), args[0])
                                  .isEmpty();
                    break;
                case 3:
                    if (count != 10)
                        fail("Rust 角色像素查询参数无效");
                    out[0] = context.actor->host_.whiteRatio(
                        {int(args[0]), int(args[1])},
                        {int(args[2]), int(args[3]), int(args[4]), int(args[5])}, {args[6], args[7], args[8]},
                        args[9] != 0);
                    break;
                case 4:
                    if (count != 0)
                        fail("Rust 角色亮度查询参数无效");
                    out[0] = context.actor->host_.white(key);
                    break;
                case 5:
                    if (count != 0)
                        fail("Rust 角色延奏查询参数无效");
                    out[0] = key.split(QChar('\0')).contains(context.actor->outroSource());
                    break;
                case 6:
                    if (count != 0)
                        fail("Rust 角色队伍查询参数无效");
                    out[0] = context.actor->hasCharacter(key) != nullptr;
                    break;
                case 7:
                    if (count != 0)
                        fail("Rust 目标特征名称查询参数无效");
                    out[0] = context.actor->host_.targetName().startsWith(key);
                    break;
                case 8:
                    if (count != 1 || !context.capturedTarget)
                        fail("Rust 目标模板查询参数无效");
                    out[0] = !context.actor->host_
                                  .bestInBox(QString::fromUtf8(reinterpret_cast<const char *>(area),
                                                               qsizetype(areaLength)),
                                             {*context.capturedTarget, key}, args[0])
                                  .isEmpty();
                    break;
                case 9: {
                    if (count != 1)
                        fail("Rust 队伍成员名称查询参数无效");
                    const auto *member = context.party.value(int(args[0]));
                    out[0] = member && member->state.className == key;
                    break;
                }
                case 10:
                    if (count != 16)
                        fail("Rust 分段量表查询参数无效");
                    out[0] = context.actor->host_.segmentedGauge(
                        {int(args[0]), int(args[1])},
                        {int(args[2]), int(args[3]), int(args[4]), int(args[5])},
                        {{args[6], args[7], args[8]}, {args[9], args[10], args[11]}}, int(args[12]),
                        int(args[13]), int(args[14]), args[15]);
                    break;
                case 11:
                    if (count != 0)
                        fail("Rust 任务名称查询参数无效");
                    out[0] = context.actor->host_.taskId() == key;
                    break;
                case 12:
                    if (count != 0)
                        fail("Rust 关联角色查询参数无效");
                    context.selected = context.actor->hasCharacter(key);
                    out[0] = context.selected != nullptr;
                    break;
                case 13:
                    if (count != 3)
                        fail("Rust 局部模板查询参数无效");
                    out[0] = context.actor->host_.partialFeature(key, args[0], args[1], args[2]);
                    break;
                case 14: {
                    if (count != 7)
                        fail("Rust 灰度观测参数无效");
                    const auto image = context.actor->host_.imageRegion(
                        {int(args[0]), int(args[1])},
                        {int(args[2]), int(args[3]), int(args[4]), int(args[5])}, args[6] != 0);
                    if (image.empty())
                        break;
                    cv::Mat gray;
                    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
                    cv::Scalar mean, deviation;
                    cv::meanStdDev(gray, mean, deviation);
                    out[0] = mean[0];
                    out[1] = deviation[0];
                    out[2] = 1;
                    break;
                }
                case 15: {
                    if (count < 14)
                        fail("Rust 图像量表观测参数无效");
                    const auto image = context.actor->host_.imageRegion(
                        {int(args[1]), int(args[2])},
                        {int(args[3]), int(args[4]), int(args[5]), int(args[6])}, args[7] != 0);
                    const ColorRange color{{args[8], args[9], args[10]}, {args[11], args[12], args[13]}};
                    switch (int(args[0])) {
                    case 0:
                        if (count != 14)
                            fail("Rust 颜色量表参数无效");
                        out[0] = gaugeColorRatio(image, color);
                        break;
                    case 1:
                        if (count != 17)
                            fail("Rust 环形量表参数无效");
                        out[0] = gaugeRingRatio(image, color, args[14], args[15], args[16] != 0);
                        break;
                    case 2:
                        if (count != 14)
                            fail("Rust 条纹量表参数无效");
                        out[0] = stripedGaugeRatio(image, color);
                        break;
                    case 3:
                        if (count != 18)
                            fail("Rust 菲比量表参数无效");
                        out[0] = phoebeGaugeValue(image, color, int(args[14]), int(args[15]), int(args[16]),
                                                  args[17]);
                        break;
                    default:
                        fail("Rust 图像量表类型无效");
                    }
                    break;
                }
                case 16:
                    if (count != 7)
                        fail("Rust 区域特征参数无效");
                    out[0] = context.actor->host_.featureInRegion(
                        key, {int(args[0]), int(args[1])},
                        {int(args[2]), int(args[3]), int(args[4]), int(args[5])}, args[6]);
                    break;
                default:
                    fail("Rust 角色查询无效");
                }
                return 1;
            } catch (...) {
                if (!context.error)
                    context.error = std::current_exception();
                return 0;
            }
        }};
    std::array<double, 3> out{};
    const auto status =
        rust::okww_rs_rotation_run(&bridge, int32_t(method), args.begin(), args.size(), out.data());
    if (context.error)
        std::rethrow_exception(context.error);
    if (status == -2)
        fail("战斗等待时间无效");
    if (status != 1)
        fail("Rust 角色执行接口异常");
    return out;
}

bool CombatHost::wait(const std::function<bool()> &condition, double timeout,
                      const std::function<void()> &post, double settle, const std::function<void()> &pre) {
    struct Context {
        CombatHost *host;
        const std::function<bool()> &condition;
        const std::function<void()> &pre, &post;
        std::exception_ptr error;
    } context{this, condition, pre, post, {}};
    const rust::RotationHost bridge{
        &context, [](void *opaque, int32_t operation, const double *, size_t, double *out) -> int32_t {
            auto &c = *static_cast<Context *>(opaque);
            try {
                switch (static_cast<rust::RotationOp>(operation)) {
                case rust::RotationOp::Now:
                    out[0] = c.host->now();
                    break;
                case rust::RotationOp::NextFrame:
                    c.host->nextFrame();
                    break;
                case rust::RotationOp::Condition:
                    out[0] = c.condition();
                    break;
                case rust::RotationOp::Pre:
                    if (c.pre)
                        c.pre();
                    break;
                case rust::RotationOp::Post:
                    if (c.post)
                        c.post();
                    break;
                default:
                    fail("Rust 战斗等待操作无效");
                }
                return 1;
            } catch (...) {
                c.error = std::current_exception();
                return 0;
            }
        }};
    const double args[]{timeout, settle};
    double out[3]{};
    const auto status =
        rust::okww_rs_rotation_run(&bridge, int32_t(rust::RotationMethod::Wait), args, 2, out);
    if (context.error)
        std::rethrow_exception(context.error);
    if (status == -2)
        fail("战斗等待时间无效");
    if (status != 1)
        fail("Rust 战斗等待接口异常");
    return out[0] != 0;
}
} // namespace okww::runtime
