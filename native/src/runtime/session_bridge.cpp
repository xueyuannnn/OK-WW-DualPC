#include "combat_session.hpp"
#include "diagnostics.hpp"
#include "interaction.hpp"
#include "workshop.hpp"

namespace okww::rust {
int32_t sessionException(const std::exception_ptr &error) noexcept {
    if (!error)
        return 0;
    try {
        std::rethrow_exception(error);
    } catch (const runtime::Cancelled &) {
        return 1;
    } catch (const runtime::CharacterDead &) {
        return 2;
    } catch (const runtime::CombatInterrupted &) {
        return 3;
    } catch (const runtime::CharacterRevived &) {
        return 7;
    } catch (const runtime::CombatEnded &) {
        return 4;
    } catch (const std::exception &) {
        return 5;
    } catch (...) {
        return 6;
    }
}
} // namespace okww::rust
namespace okww::runtime {
bool CombatSession::rustAccess(rust::SessionOp op, const double *a, double *out, uint64_t &revision,
                               Character *&actor, const std::exception_ptr &error) {
    using O = rust::SessionOp;
    switch (op) {
    case O::GetBool:
        switch (int(a[0])) {
#define OKWW_SESSION_BOOL(name, id, field)                                                                   \
    case id:                                                                                                 \
        out[0] = field;                                                                                      \
        return true;
#define OKWW_SESSION_TIME(name, id, field)
#define OKWW_SESSION_COUNT(name, id, field)
#include "../../rust/abi/session_state.def"
#undef OKWW_SESSION_COUNT
#undef OKWW_SESSION_TIME
#undef OKWW_SESSION_BOOL
        }
        break;
    case O::SetBool:
        switch (int(a[0])) {
#define OKWW_SESSION_BOOL(name, id, field)                                                                   \
    case id:                                                                                                 \
        field = a[1] != 0;                                                                                   \
        return true;
#define OKWW_SESSION_TIME(name, id, field)
#define OKWW_SESSION_COUNT(name, id, field)
#include "../../rust/abi/session_state.def"
#undef OKWW_SESSION_COUNT
#undef OKWW_SESSION_TIME
#undef OKWW_SESSION_BOOL
        }
        break;
    case O::GetTime:
        switch (int(a[0])) {
#define OKWW_SESSION_BOOL(name, id, field)
#define OKWW_SESSION_TIME(name, id, field)                                                                   \
    case id:                                                                                                 \
        out[0] = field;                                                                                      \
        return true;
#define OKWW_SESSION_COUNT(name, id, field)
#include "../../rust/abi/session_state.def"
#undef OKWW_SESSION_COUNT
#undef OKWW_SESSION_TIME
#undef OKWW_SESSION_BOOL
        }
        break;
    case O::SetTime:
        switch (int(a[0])) {
#define OKWW_SESSION_BOOL(name, id, field)
#define OKWW_SESSION_TIME(name, id, field)                                                                   \
    case id:                                                                                                 \
        field = a[1];                                                                                        \
        return true;
#define OKWW_SESSION_COUNT(name, id, field)
#include "../../rust/abi/session_state.def"
#undef OKWW_SESSION_COUNT
#undef OKWW_SESSION_TIME
#undef OKWW_SESSION_BOOL
        }
        break;
    case O::GetCount:
        switch (int(a[0])) {
#define OKWW_SESSION_BOOL(name, id, field)
#define OKWW_SESSION_TIME(name, id, field)
#define OKWW_SESSION_COUNT(name, id, field)                                                                  \
    case id:                                                                                                 \
        out[0] = field;                                                                                      \
        return true;
#include "../../rust/abi/session_state.def"
#undef OKWW_SESSION_COUNT
#undef OKWW_SESSION_TIME
#undef OKWW_SESSION_BOOL
        }
        break;
    case O::SetCount:
        switch (int(a[0])) {
#define OKWW_SESSION_BOOL(name, id, field)
#define OKWW_SESSION_TIME(name, id, field)
#define OKWW_SESSION_COUNT(name, id, field)                                                                  \
    case id:                                                                                                 \
        field = int(a[1]);                                                                                   \
        return true;
#include "../../rust/abi/session_state.def"
#undef OKWW_SESSION_COUNT
#undef OKWW_SESSION_TIME
#undef OKWW_SESSION_BOOL
        }
        break;
    case O::Now:
        out[0] = now();
        return true;
    case O::Checkpoint:
        context_.execution.checkpoint();
        return true;
    case O::Wait:
        context_.execution.wait(a[0]);
        return true;
    case O::Refresh:
        context_.refresh();
        return true;
    case O::Watchdog:
        watchdog();
        return true;
    case O::PollControls:
        pollControls();
        return true;
    case O::ReleaseInput:
        context_.input.release();
        return true;
    case O::MacroExists:
        out[0] = bool(macro_);
        return true;
    case O::MacroReset:
        macro_->reset();
        return true;
    case O::AutoTask:
        out[0] = context_.task == "AutoCombatTask";
        return true;
    case O::AutoEnabled:
        out[0] = context_.profile.value("AutoCombatTask", "_enabled", true).toBool();
        return true;
    case O::RevisionChanged:
        revision = context_.state->combatEnableRevision.load();
        out[0] = revision != enabledRevision_;
        return true;
    case O::StoreRevision:
        enabledRevision_ = revision;
        return true;
    case O::PhysicalFExists:
        out[0] = bool(context_.physicalFDown);
        return true;
    case O::PhysicalFRead:
        out[0] = context_.physicalFDown();
        return true;
    case O::PartySize:
        out[0] = double(party_.size());
        return true;
    case O::MemberCurrent:
        out[0] = party_.at(size_t(a[0]))->state.current;
        return true;
    case O::Interval:
        out[0] = interval(a[0]);
        return true;
    case O::Sleep:
        sleep(a[0]);
        return true;
    case O::CheckCombat:
        checkCombat();
        return true;
    case O::InTeam:
        out[0] = inTeam();
        return true;
    case O::CloseRevivalDialog:
        out[0] = closeRevivalDialog();
        return true;
    case O::ContextEscape:
        context_.key("esc", a[0]);
        return true;
    case O::WaitDeathClosed:
        out[0] = context_.wait([&] { return rustRun(rust::SessionMethod::DeathClosed) != 0; }, a[0]);
        return true;
    case O::ClosePopupClick:
        context_.clickRelative(a[0], a[1], a[2]);
        return true;
    case O::ContextSleep:
        context_.sleep(a[0]);
        return true;
    case O::HasInfo:
        out[0] = bool(context_.info);
        return true;
    case O::CombatCountInfo:
        context_.info("战斗次数", QString::number(int(a[0])));
        return true;
    case O::ResumeInterrupted:
        out[0] = context_.profile.value("Task Recovery", "Resume Interrupted Tasks", true).toBool();
        return true;
    case O::RecoveryRetryLimit:
        out[0] = context_.profile.value("Task Recovery", "Recovery Retry Limit", 3).toInt();
        return true;
    case O::WaitTeamOrDeath:
        out[0] = context_.wait([&] { return rustRun(rust::SessionMethod::TeamOrDeath) != 0; }, a[0]);
        return true;
    case O::LogRetry:
        log(QString("继续当前战斗 %1/%2").arg(int(a[0])).arg(int(a[1])));
        return true;
    case O::WaitTeamAfterCombat:
        Navigation(context_).waitTeam(a[0], false);
        return true;
    case O::CheckedVersionMatches:
        out[0] = checkedVersion_ == context_.frameVersion;
        return true;
    case O::SetCheckedVersionFrame:
        checkedVersion_ = context_.frameVersion;
        return true;
    case O::WaitHudRestore: {
        bool checked = false;
        double checkedAt = -1;
        out[0] = context_.wait(
            [&] {
                return rustRun(rust::SessionMethod::RestoreCondition, {},
                               [&](rust::SessionOp action, const double *args, double *value,
                                   const std::exception_ptr &) {
                                   switch (action) {
                                   case O::ResultChecked:
                                       value[0] = checked;
                                       break;
                                   case O::SetResultChecked:
                                       checked = args[0] != 0;
                                       break;
                                   case O::ResultCheckedAt:
                                       value[0] = checkedAt;
                                       break;
                                   case O::SetResultCheckedAt:
                                       checkedAt = args[0];
                                       break;
                                   default:
                                       fail("Rust 队伍恢复条件宿主操作无效");
                                   }
                               }) != 0;
            },
            a[0], a[1]);
        return true;
    }
    case O::PythonSkipCombatCheck:
        out[0] = dynamic_cast<PythonTeamMacro *>(macro_.get())->skipsCombatCheck(*actor);
        return true;
    case O::BuiltinSkipCombatCheck:
        out[0] = actor->skipCombatCheck();
        return true;
    case O::HasOnCombatCheck:
        out[0] = bool(onCombatCheck);
        return true;
    case O::OnCombatCheck:
        out[0] = onCombatCheck();
        return true;
    case O::HasEndCondition:
        out[0] = bool(combatEndCondition);
        return true;
    case O::EndCondition:
        out[0] = combatEndCondition();
        return true;
    case O::MonthlyCard:
        out[0] = context_.monthlyCard();
        return true;
    case O::ResetCombat:
        resetCombat();
        return true;
    case O::LoadParty:
        out[0] = loadParty();
        return true;
    case O::AutoTargetSetting:
        out[0] = context_.profile.value("AutoCombatTask", "Auto Target", true).toBool();
        return true;
    case O::BossFeatures:
        out[0] = bool(context_.findAny({"boss_break_shield", "boss_break_lock"}));
        return true;
    case O::LevitatorFeature:
        out[0] = bool(context_.find("edge_levitator", {.threshold = .65}));
        return true;
    case O::EarthFeature:
        out[0] = bool(context_.find("world_earth_icon", {.threshold = .55, .filter = Filter::White}));
        return true;
    case O::RealmExitFeature:
        out[0] = bool(context_.find("illusive_realm_exit", {.threshold = .7, .filter = Filter::White}));
        return true;
    case O::UseLiberationSetting:
        out[0] = context_.profile.value("AutoCombatTask", "Use Liberation", true).toBool();
        return true;
    case O::SwitchHealer:
        switchHealer();
        return true;
    case O::PerformCurrent:
        performCurrent();
        return true;
    case O::MacroCombatEnd:
        macro_->combatEnd();
        return true;
    case O::ActorCombatEnd:
        actor->combatEnd();
        return true;
    case O::ResetMember:
        party_.at(size_t(a[0]))->resetState();
        return true;
    case O::LogException:
        try {
            std::rethrow_exception(error);
        } catch (const std::exception &e) {
            log(errorText(e));
        }
        return true;
    case O::TargetEnemy:
        out[0] = targetEnemy();
        return true;
    case O::SelectedPython:
        out[0] = selectedTeam_.startsWith("python:");
        return true;
    case O::PythonMacro:
        out[0] = dynamic_cast<PythonTeamMacro *>(macro_.get()) != nullptr;
        return true;
    case O::PythonShortTarget:
        out[0] = dynamic_cast<PythonTeamMacro *>(macro_.get())->usesShortTarget(*actor);
        return true;
    case O::BuiltinShortTarget:
        out[0] = partyDefinitions_[actor->state.index].shortTarget;
        return true;
    case O::ObserveTarget: {
        const auto target = vision_.targetObservation(a[0] != 0, true);
        out[0] = target.locked;
        out[1] = target.extended;
        out[2] = target.matched;
        return true;
    }
    case O::TapEscape:
        tap("esc", a[0]);
        return true;
    case O::FullBreakFeature:
        out[0] = bool(context_.find("f_break_full", {.threshold = .92}));
        return true;
    case O::BreakFeature:
        out[0] =
            bool(context_.find("f_break", {.search = gameBox(context_.frame, .2, .2, .75, .8, true, true),
                                           .targetHeight = 720}));
        return true;
    case O::DialogInteract:
        out[0] = isDialogInteract(context_);
        return true;
    case O::VisibleBreakPrompt:
        out[0] = visibleBreakPrompt(a[0]);
        return true;
    case O::FindBreakPrompt: {
        const auto area = Templates::anchored(QRect(384, 216, 1056, 648), {1920, 1080},
                                              {context_.frame.cols, context_.frame.rows}, true, true);
        out[0] = bool(context_.find("f_break", {.threshold = a[0], .search = area, .targetHeight = 720}));
        return true;
    }
    case O::TapF:
        tap("f", a[0]);
        return true;
    case O::ClickDefault:
        click();
        return true;
    case O::CheckBreak:
        out[0] = checkBreak();
        return true;
    case O::BreakF:
        out[0] = breakF();
        return true;
    case O::UpdatePortraits:
        updatePortraits();
        return true;
    case O::CombatActive:
        out[0] = combatActive();
        return true;
    case O::ClearCheckedVersion:
        checkedVersion_ = UINT64_MAX;
        return true;
    case O::VisionReset:
        vision_.reset();
        return true;
    case O::ReadHud: {
        const auto hud = vision_.party();
        out[0] = hud.visible;
        out[1] = hud.size;
        out[2] = hud.current;
        return true;
    }
    case O::MemberIndex:
        out[0] = party_.at(size_t(a[0]))->state.index;
        return true;
    case O::SetMemberCurrent:
        party_.at(size_t(a[0]))->state.current = a[1] != 0;
        return true;
    case O::SetActorPerform:
        actor->state.lastPerform = a[0];
        return true;
    case O::MacroPerform:
        out[0] = macro_->perform(*actor);
        return true;
    case O::ActorPerform:
        actor->perform();
        return true;
    case O::PublishParty:
        publishParty();
        return true;
    case O::MemberRing:
        out[0] = party_.at(size_t(a[0]))->state.ringIndex;
        return true;
    case O::MemberLiberationReady:
        out[0] = party_.at(size_t(a[0]))->state.liberationReady;
        return true;
    case O::FindLiberationPortrait: {
        const auto &member = *party_.at(size_t(a[0]));
        const QStringList names{"lib_ready_spectro", "lib_ready_electric", "lib_ready_fire",
                                "lib_ready_ice",     "lib_ready_wind",     "lib_ready_havoc"};
        const auto area = vision_.box(QString("lib_mark_char_%1").arg(member.state.index + 1));
        out[0] = bool(context_.find(names[member.state.ringIndex], {.search = area}));
        return true;
    }
    case O::SetMemberLiberationReady:
        party_.at(size_t(a[0]))->state.liberationReady = true;
        return true;
    case O::TaskIdMatch: {
        const char *ids[]{"FarmEchoTask", "FarmEchoTask2"};
        out[0] = context_.task == QLatin1String(ids[int(a[0])]);
        return true;
    }
    case O::HealerTaskSetting:
        out[0] = context_.setting("Switch to Healer before and after Combat", true).toBool();
        return true;
    case O::HealerGlobalSetting:
        out[0] = context_.profile.value("AutoCombatTask", "Switch to Healer before and after Combat", true)
                     .toBool();
        return true;
    case O::PythonHealer: {
        const auto *member = a[0] < 0 ? actor : party_.at(size_t(a[0])).get();
        out[0] = dynamic_cast<PythonTeamMacro *>(macro_.get())->isHealer(*member);
        return true;
    }
    case O::MemberRole: {
        const auto *member = a[0] < 0 ? actor : party_.at(size_t(a[0])).get();
        out[0] = int(member->state.role);
        return true;
    }
    case O::TapMember:
        tap(QString::number(party_.at(size_t(a[0]))->state.index + 1));
        return true;
    case O::CurrentExists:
        actor = current();
        out[0] = actor != nullptr;
        return true;
    case O::CurrentIndex:
        out[0] = actor->state.index;
        return true;
    case O::RefreshCooldowns:
        vision_.refreshCooldowns(int(a[0]));
        return true;
    case O::Cooldown:
        out[0] = vision_.cooldown(Skill(int(a[0])), int(a[1]), freezes, now());
        return true;
    case O::Highlighted:
        out[0] = highlighted(Skill(int(a[0])));
        return true;
    case O::HasCd:
        out[0] = hasCd(Skill(int(a[0])), int(a[1]));
        return true;
    case O::HealthBarNative:
        out[0] = vision_.healthBar(a[0] != 0);
        return true;
    case O::ShiftedHealthBar:
        out[0] = vision_.shiftedHealthBar();
        return true;
    case O::SessionHealthBar:
        out[0] = healthBar();
        return true;
    case O::TargetLocked:
        out[0] = targetLocked();
        return true;
    case O::ClickMiddle:
        click("middle", a[0]);
        return true;
    case O::NextFrame:
        nextFrame();
        return true;
    case O::RefreshObservation:
        refreshObservation();
        return true;
    case O::ReadRunDialog:
        out[0] = systemRunDialogConfirmed(context_.text(0, .7, .35, 1, .65));
        return true;
    case O::DeathDialog:
        out[0] = vision_.deathDialog();
        return true;
    case O::HudVisible:
        out[0] = vision_.party().visible;
        return true;
    case O::ChallengeResult: {
        const auto result = vision_.challengeResult();
        out[0] = result ? (*result ? 2 : 1) : 0;
        return true;
    }
    case O::InputEscape:
        context_.input.key("esc", a[0]);
        return true;
    case O::Log: {
        const char *messages[]{"检测到外部运行弹窗，正在自动关闭并恢复当前战斗",
                               "外部弹窗已关闭，继续当前战斗",
                               "队伍界面暂时消失，等待恢复，不执行战斗收尾",
                               "队伍界面已恢复，继续当前战斗",
                               "角色已恢复，本轮重新进入后再执行",
                               "遗留复苏弹窗已关闭并免费回血",
                               "未能创建诊断截图目录",
                               "诊断截图保存失败"};
        log(QString::fromUtf8(messages[int(a[0])]));
        return true;
    }
    case O::Throw:
        switch (int(a[0])) {
        case 0:
            throw CombatEnded("自动战斗已关闭");
        case 1:
            throw CombatEnded("自动战斗已重新启用，重新准备启动连招");
        case 2:
            fail("战斗等待时间无效");
        case 3:
            throw NavigationFailure("本轮战斗超过等待上限，未记录为完成");
        case 4:
            throw ExecutionFailure("已自动关闭外部弹窗 3 次，但画面仍被遮挡，已保留任务现场",
                                   "system_run_dialog_recovery_exhausted");
        case 5:
            throw CharacterDead();
        case 6:
            throw CombatEnded("已确认挑战成功结算，停止本轮工坊连招");
        case 7:
            throw CombatInterrupted("已确认挑战失败结算，停止本轮工坊连招，等待重新挑战");
        case 8:
            throw CombatEnded("目标消失，战斗结束");
        case 9:
            throw CombatInterrupted("当前角色无法确认");
        case 10:
            throw CombatEnded("已确认挑战成功结算");
        case 11:
            throw CombatInterrupted("挑战失败，等待任务重新挑战");
        case 12:
            throw CombatInterrupted("队伍界面尚未恢复，保留任务并尝试重新识别");
        case 13:
            throw CombatInterrupted("角色切换超时");
        case 14:
            throw CombatInterrupted("切换角色时队伍界面消失");
        case 15:
            throw NavigationFailure("等待进入战斗超时，未记录为完成");
        case 16:
            try {
                std::rethrow_exception(error);
            } catch (const std::exception &e) {
                throw NavigationFailure("本轮连招中断次数达到上限，未记录为完成：" + errorText(e));
            }
        case 17:
            throw NavigationFailure("连招中断后未恢复队伍界面，未记录为完成");
        case 18:
            throw CharacterRevived();
        case 19:
            fail("复苏弹窗未关闭，避免误用道具，停止本次恢复");
        case 20:
            fail("战斗等待参数无效");
        }
        break;
    default:
        return false;
    }
    fail("Rust 战斗状态标识无效");
}
} // namespace okww::runtime
