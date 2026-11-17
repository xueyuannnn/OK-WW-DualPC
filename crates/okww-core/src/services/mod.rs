//! Task services. The host owns SDK values; retries and sequencing stay here.
use std::{cell::Cell, ffi::c_void};
const HOST_ERROR: i32 = -1;
const INVALID_ACTION: i32 = -3;
type Result<T = ()> = std::result::Result<T, i32>;
include!(concat!(env!("OUT_DIR"), "/workflow_abi.rs"));
mod application;
mod capture;
mod input;
mod notifications;
mod runtime;
mod tasks;
mod teams;
mod updates;
mod vision;
mod workshop;

#[repr(C)]
pub struct Host {
    pub context: *mut c_void,
    pub invoke: unsafe extern "C" fn(*mut c_void, i32, *const f64, usize, *mut f64) -> i32,
}
impl Host {
    fn call(&self, op: Op, args: &[f64]) -> Result<[f64; 3]> {
        let mut out = [0.; 3];
        // SAFETY: argument/output slices and the host live for this synchronous
        // call; the adapter catches every foreign exception.
        if unsafe {
            (self.invoke)(
                self.context,
                op as i32,
                args.as_ptr(),
                args.len(),
                out.as_mut_ptr(),
            )
        } == 1
        {
            Ok(out)
        } else {
            Err(HOST_ERROR)
        }
    }
    fn scalar(&self, op: Op, args: &[f64]) -> Result<f64> {
        Ok(self.call(op, args)?[0])
    }
    fn flag(&self, op: Op, args: &[f64]) -> Result<bool> {
        Ok(self.scalar(op, args)? != 0.)
    }
    fn action(&self, op: Op, args: &[f64]) -> Result {
        self.call(op, args)?;
        Ok(())
    }
    fn fail(&self, reason: f64) -> Result {
        self.action(Op::Throw, &[reason])?;
        Err(HOST_ERROR)
    }
    fn wait_for(&self, condition: impl Fn() -> Result<bool>, timeout: f64) -> Result<bool> {
        self.wait_for_interval(condition, timeout, 0.05)
    }
    fn wait_for_interval(
        &self,
        condition: impl Fn() -> Result<bool>,
        timeout: f64,
        interval: f64,
    ) -> Result<bool> {
        let value = crate::execution::until_sequence(
            || self.scalar(Op::Now, &[]),
            || self.action(Op::Checkpoint, &[]),
            |seconds| self.action(Op::ExecutionWait, &[seconds]),
            || {
                self.action(Op::Refresh, &[])?;
                condition()
            },
            timeout,
            interval,
        );
        if value == Err(crate::execution::INVALID_CONDITION) {
            self.action(Op::InvalidCondition, &[])?;
        }
        value
    }
    fn observe(
        &self,
        condition: impl Fn() -> Result<bool>,
        timeout: f64,
        settle: f64,
    ) -> Result<bool> {
        let stable = Cell::new(-1.);
        self.wait_for(
            || {
                if !condition()? {
                    stable.set(-1.);
                    return Ok(false);
                }
                if settle <= 0. {
                    return Ok(true);
                }
                if stable.get() < 0. {
                    stable.set(self.scalar(Op::Now, &[])?);
                }
                Ok(self.scalar(Op::Now, &[])? - stable.get() >= settle)
            },
            timeout,
        )
    }
    pub fn run(&self, method: i32, args: &[f64]) -> Result<[f64; 3]> {
        let method = Method::try_from(method)?;
        if args.len() != method.arg_count() {
            return Err(INVALID_ACTION);
        }
        match method {
            Method::DomainCost
            | Method::DomainCountLimit
            | Method::DomainEnter
            | Method::DomainNext
            | Method::DomainLeave
            | Method::DomainRun
            | Method::DomainFarm => self.run_domain(method, args),
            Method::CombineDialog
            | Method::ExactDialog
            | Method::RestartDialog
            | Method::NestExitText
            | Method::NestCompleteText
            | Method::NestExitContext
            | Method::NestExitConfirmation
            | Method::RestartBoss
            | Method::CompletedNest
            | Method::LeaveNest => self.run_instance(method, args),
            Method::NextNestTarget
            | Method::NestTab
            | Method::NestTravel
            | Method::NestCapturedNotice
            | Method::NestSeekTick
            | Method::NestWaitCombat
            | Method::NestWalkEcho
            | Method::NestFight
            | Method::NestRun
            | Method::NestCombatCheck => self.run_nightmare(method, args),
            Method::StaminaCounters
            | Method::JournalInteger
            | Method::StaminaValues
            | Method::ReadStamina
            | Method::CubeLimit
            | Method::CubeRemaining
            | Method::CubeRead
            | Method::CubeReserve
            | Method::CubeVerify => self.run_stamina(method, args),
            Method::ClaimStamina | Method::UseDailyCube => self.run_stamina(method, args),
            Method::PreferredGardenStyle
            | Method::GardenPointsComplete
            | Method::GardenRarity
            | Method::RecommendationScore
            | Method::GardenPurchaseControls
            | Method::GardenWeeklyPoints
            | Method::GardenInitialize
            | Method::GardenWeeklyProgress => self.run_garden_vision(method, args),
            Method::GardenRewardChoice => self.run_garden_vision(method, args),
            Method::GardenMoney
            | Method::GardenInventory
            | Method::GardenWaitPurchase
            | Method::GardenShop => self.run_garden_shop(method, args),
            Method::GardenTurn
            | Method::GardenBlessingControls
            | Method::GardenUseBlessing
            | Method::GardenMaxSpeed
            | Method::GardenBestFeature
            | Method::GardenActive
            | Method::GardenLoop
            | Method::GardenOpenWeekly
            | Method::GardenWeeklyFailure
            | Method::GardenRunLoop
            | Method::GardenRun => self.run_garden(method, args),
            Method::DailyCountKey
            | Method::DailyReady
            | Method::DailyProgress
            | Method::ObserveDailyProgress
            | Method::DailyDomain
            | Method::DailyStatus
            | Method::DailyStage
            | Method::DailyObservationFailure => self.run_daily(method, args),
            Method::DailyOpen
            | Method::DailyClaim
            | Method::DailyCompleteStamina
            | Method::DailyFarmCount
            | Method::DailyMail
            | Method::DailyBattlePass
            | Method::DailyValidate
            | Method::DailyStaminaBatch
            | Method::DailyQuotaOperation
            | Method::DailyQuotaSpent => self.run_daily_actions(method, args),
            Method::DailyRun => self.run_daily_main(method, args),
            Method::NormalizeAccount
            | Method::CompleteAccount
            | Method::AccountCompleted
            | Method::NextAccount
            | Method::ReadAccounts
            | Method::CurrentAccount
            | Method::LogoutAccount
            | Method::ChooseAccount
            | Method::RunAccounts
            | Method::AccountCount => self.run_account_method(method, args),
            Method::WorldFSearchBox
            | Method::WorldFindInteract
            | Method::WorldDialogInteract
            | Method::WorldClaimDialog
            | Method::WorldDismissClaim
            | Method::WorldPick
            | Method::WorldMoveDirection
            | Method::WorldMoveRun
            | Method::WorldMoveStopRunning
            | Method::WorldMoveStop
            | Method::WorldMoveDrop
            | Method::WorldEchoes
            | Method::WorldCenterCamera
            | Method::WorldCircleEcho
            | Method::WorldTreasure
            | Method::WorldArrowAngle
            | Method::WorldMiniMapTurn
            | Method::WorldWalkUntil => self.run_world(method, args),
            Method::WorldWalkInteract
            | Method::WorldInteractCondition
            | Method::WorldWalkEcho
            | Method::WorldFindEcho
            | Method::WorldWalkBox
            | Method::WorldWalkTreasure
            | Method::WorldFollowBoss
            | Method::WorldEchoDirection => self.run_world_movement(method, args),
            Method::EchoRotationTargets
            | Method::WeeklyMaterialReward
            | Method::AwaitWeeklyMaterialReward
            | Method::EchoTeleportEnabled
            | Method::EchoParameters
            | Method::EchoCheckRealm
            | Method::EchoBossLevel
            | Method::EchoEnterF
            | Method::EchoSeekCondition
            | Method::EchoSeekCombat
            | Method::EchoScrollInteract
            | Method::EchoTreasureRestart
            | Method::EchoTreasureHunt
            | Method::EchoChangeNight => self.run_echo_farm(method, args),
            Method::EchoTeleport
            | Method::EchoTeleportFound
            | Method::EchoTeleportSeek
            | Method::EchoNearestBoss
            | Method::EchoRecognizeBoss
            | Method::EchoHeal
            | Method::EchoWeeklyCount => self.run_echo_travel(method, args),
            Method::EchoInteractions | Method::EchoSingle => self.run_echo_combat(method, args),
            Method::EchoRun | Method::EchoCombatCheck | Method::EchoCombatEnd => {
                self.run_echo_root(method, args)
            }
            Method::EchoStatNumber
            | Method::EchoFixedMain
            | Method::EchoNormalizeStat
            | Method::EchoPairStats
            | Method::EchoEvaluateStats
            | Method::EchoMergeResult
            | Method::EchoAwaitMerge => self.run_echo_stats(method, args),
            Method::EchoWaitText
            | Method::EchoCultivate
            | Method::EchoZeroLevel
            | Method::EchoEnterCultivate
            | Method::EchoBackToBag
            | Method::EchoMark
            | Method::EchoScreenshot
            | Method::EchoUpgrade
            | Method::EchoEnhance
            | Method::EchoChange
            | Method::EchoMerge => self.run_echo_operations(method, args),
            Method::InputWorkshopCommand
            | Method::InputSend
            | Method::InputTransmit
            | Method::InputScreenPoint
            | Method::InputKeyDown
            | Method::InputKeyUp
            | Method::InputKey
            | Method::InputButtonDown
            | Method::InputSettle
            | Method::InputButtonUp
            | Method::InputCounts
            | Method::InputMove
            | Method::InputRelative
            | Method::InputClick
            | Method::InputScroll
            | Method::InputSwipe
            | Method::InputText => self.run_input_control(method, args),
            Method::InferenceWait => self.run_inference(method, args),
            Method::EchoModelInitialize | Method::EchoModelDetect => {
                self.run_echo_detection(method, args)
            }
            Method::OcrInitialize
            | Method::OcrCompile
            | Method::OcrNormalize
            | Method::OcrDetect
            | Method::OcrOrient
            | Method::OcrRecognize
            | Method::OcrRead => self.run_ocr(method, args),
            Method::OcrMiniBox | Method::OcrBoxScore | Method::OcrUnclip | Method::OcrTextCrop => {
                self.run_ocr_geometry(method)
            }
            Method::ReadImage
            | Method::DecodeFrame
            | Method::InferenceImage
            | Method::CachedInferenceFrame
            | Method::FilterImage
            | Method::ColorPercentage => self.run_imaging(method, args),
            Method::TemplateAdd
            | Method::TemplateScaled
            | Method::TemplateFind
            | Method::TemplateOne => self.run_templates(method, args),
            Method::ProfileInitialize
            | Method::ProfileFilename
            | Method::ProfileDefaults
            | Method::ProfileGroup
            | Method::ProfileValue
            | Method::ProfileSet
            | Method::ProfileReset
            | Method::ProfileCatalog => self.run_profile(method),
            Method::BusinessTasks | Method::DispatchBusinessTask => {
                self.run_dispatcher(method, args)
            }
            Method::MacroWait
            | Method::MacroRecord
            | Method::MacroHold
            | Method::MacroRelease
            | Method::MacroStart
            | Method::MacroMark
            | Method::MacroNeed
            | Method::MacroSpam
            | Method::MacroSpamKey
            | Method::MacroGap
            | Method::MacroTeammate
            | Method::MacroHandoff
            | Method::MacroQuickSwitch
            | Method::MacroOnField
            | Method::MacroWaitHud
            | Method::MacroWaitHudUp
            | Method::MacroConcertoAtLeast
            | Method::MacroPressUntilCd
            | Method::MacroPressResonance
            | Method::MacroPressEcho
            | Method::MacroEchoNow
            | Method::MacroOpenerPending
            | Method::MacroExecute
            | Method::MacroReset
            | Method::MacroCombatEnd => self.run_team_macro(method, args),
            Method::ScheduleInitialize
            | Method::ScheduleCurrentUser
            | Method::ScheduleRunCommand
            | Method::ScheduleQuoteArgument
            | Method::ScheduleNumber
            | Method::ScheduleXml
            | Method::ScheduleRegistry
            | Method::ScheduleSave
            | Method::ScheduleValidateName
            | Method::ScheduleList
            | Method::ScheduleRegister
            | Method::ScheduleAdd
            | Method::ScheduleUpdate
            | Method::ScheduleRemove
            | Method::ScheduleSetEnabled => self.run_scheduler(method, args),
            Method::ScriptEnvironment
            | Method::ScriptManage
            | Method::ScriptCatalogue
            | Method::RecordedScript
            | Method::ScriptStop
            | Method::ScriptWrite
            | Method::ScriptRpc
            | Method::ScriptReceive
            | Method::ScriptStart
            | Method::ScriptRun => self.run_scripts(method),
            Method::StoreDocument
            | Method::StoreImagePath
            | Method::StoreImages
            | Method::StoreAnnotations
            | Method::StoreAdd
            | Method::StoreSaveAnnotations
            | Method::StoreRemove
            | Method::StorePublish
            | Method::StoreNextId
            | Method::StoreWithoutImage => self.run_template_store(method, args),
            Method::WorkshopCombatState
            | Method::WorkshopApplyState
            | Method::WorkshopValidateHandshake
            | Method::WorkshopFinite
            | Method::WorkshopBoolean
            | Method::WorkshopPercentile => self.run_workshop_state(method, args),
            Method::WorkshopRoot
            | Method::WorkshopPython
            | Method::WorkshopEnvironment
            | Method::WorkshopManage
            | Method::WorkshopFileStop
            | Method::WorkshopFileExchange
            | Method::WorkshopFileCommand => self.run_workshop_files(method),
            Method::WorkshopStop
            | Method::WorkshopWrite
            | Method::WorkshopRpc
            | Method::WorkshopReceive
            | Method::WorkshopStart
            | Method::WorkshopCall
            | Method::WorkshopSync
            | Method::WorkshopReport
            | Method::WorkshopReset
            | Method::WorkshopCombatEnd
            | Method::WorkshopPerform
            | Method::WorkshopPriority
            | Method::WorkshopSkipsCombatCheck => self.run_workshop_runtime(method, args),
            Method::TeamStoreBytes
            | Method::TeamStoreSaveBytes
            | Method::TeamStoreCanonical
            | Method::TeamStoreSafeSource
            | Method::TeamStoreMembers
            | Method::TeamStoreKey
            | Method::TeamStoreConfig
            | Method::TeamStoreSaveConfig => self.run_team_store(method, args),
            Method::TeamStoreLoadModule
            | Method::TeamStoreInitialize
            | Method::TeamStorePythonTeam
            | Method::TeamStorePythonCommand
            | Method::TeamStoreVerifySources => self.run_team_modules(method),
            Method::TeamStoreCatalog
            | Method::TeamStoreValidateEnabled
            | Method::TeamStoreSelection
            | Method::TeamStoreCreate => self.run_team_catalog(method),
            Method::TeamStoreSetMode
            | Method::TeamStoreCreateTeam
            | Method::TeamStoreSetAllModes
            | Method::TeamStoreRemoveTeam
            | Method::TeamStoreCode
            | Method::TeamStoreSaveDraft
            | Method::TeamStoreExportTeam
            | Method::TeamStoreImportTeam => self.run_team_mutations(method),
            Method::TeamEditorBuiltinText
            | Method::TeamEditorDefinition
            | Method::TeamEditorBuiltin
            | Method::TeamEditorPrimaryImported
            | Method::TeamStoreEditor => self.run_team_editor(method, args),
            Method::UpdateTrustedDownload
            | Method::UpdateNewerReleases
            | Method::UpdateSummary
            | Method::UpdatePackageChecksum
            | Method::UpdateInstallationBusy
            | Method::UpdateReleaseInstallable => self.run_update_policy(method, args),
            Method::UpdateClientInitialize
            | Method::UpdateClientFail
            | Method::UpdateClientCancel
            | Method::UpdateClientGet
            | Method::UpdateClientReadyRead
            | Method::UpdateClientFinished
            | Method::UpdateClientCheck
            | Method::UpdateClientGetPage
            | Method::UpdateClientPageReply
            | Method::UpdateClientDownload
            | Method::UpdateClientChecksumReply
            | Method::UpdateClientPackageReply
            | Method::UpdateClientTimeout => self.run_update_client(method, args),
            Method::NoticeRequired
            | Method::NoticeContent
            | Method::NoticeChunks
            | Method::NoticeWebhook
            | Method::NoticeMultipart
            | Method::NoticeWrappedBase64
            | Method::NoticeAddress
            | Method::NoticeCheckpoint
            | Method::NoticeRequests
            | Method::NoticeValidateResponse
            | Method::NoticeMime => self.run_notification(method, args),
            Method::NoticeHttp
            | Method::NoticeHttpPoll
            | Method::NoticeSmtp
            | Method::NoticeSmtpUntil
            | Method::NoticeSmtpResponse
            | Method::NoticeSmtpWrite
            | Method::NoticeSmtpCommand => self.run_notification_transport(method, args),
            Method::NoticeQueueSelectTransport
            | Method::NoticeQueueStop
            | Method::NoticeQueueIdle
            | Method::NoticeQueueSubmit
            | Method::NoticeQueueRun
            | Method::NoticeQueueReadyPredicate
            | Method::NoticeQueueDelayPredicate => self.run_notification_queue(method),
            Method::NoticeDesktopBoundedCapture
            | Method::NoticeDesktopCaptureWorker
            | Method::NoticeDesktopContact => self.run_notification_desktop(method, args),
            Method::NoticeMessengerCheck
            | Method::NoticeMessengerWait
            | Method::NoticeMessengerBackground
            | Method::NoticeMessengerMessage
            | Method::NoticeMessengerKey
            | Method::NoticeMessengerTap
            | Method::NoticeMessengerClick
            | Method::NoticeMessengerType
            | Method::NoticeMessengerRead
            | Method::NoticeMessengerFind
            | Method::NoticeMessengerConfirmRecipient
            | Method::NoticeMessengerClear => self.run_notification_messenger(method, args),
            Method::NoticeDeliveryImage
            | Method::NoticeDeliveryInitialize
            | Method::NoticeDeliverySend
            | Method::NoticeDeliveryEntry => self.run_notification_delivery(method, args),
            Method::NoticeClipboardCreate | Method::NoticeClipboardRestore => {
                self.run_notification_clipboard(method)
            }
            Method::NoticeWindowFindMessenger
            | Method::NoticeWindowPopups
            | Method::NoticeWindowPopupCompare
            | Method::NoticeWindowRender
            | Method::NoticeWindowRenderChild
            | Method::NoticeWindowCapture => self.run_notification_windows(method, args),
            Method::CommonReadJson
            | Method::CommonWriteJson
            | Method::CommonConfigLoad
            | Method::CommonConfigNormalize
            | Method::CommonConfigSave
            | Method::CommonConfigGet
            | Method::CommonConfigSet
            | Method::CommonConfigValidateCapture
            | Method::CommonConfigValidateBox => self.run_common_config(method),
            Method::CommonFrameImage | Method::CommonPreviewImage => {
                self.run_common_preview(method)
            }
            Method::ResourceMemoryFailure
            | Method::ResourceReserveCreate
            | Method::ResourceReserveRelease
            | Method::ResourceReserveRead
            | Method::ResourceFailureText
            | Method::ResourceRetryNonmovable => self.run_resource_failure(method),
            Method::SaveDiagnostic => self.run_diagnostics(method),
            Method::CaptureFreeType | Method::CaptureBitmap | Method::CapturePeriod => {
                self.run_capture_types(method, args)
            }
            Method::CaptureCapabilities => self.run_capture_capabilities(method),
            Method::CapturePublish
            | Method::CaptureVerifyUnchanged
            | Method::CaptureStart
            | Method::CaptureStop
            | Method::CaptureError
            | Method::CaptureDesktopRecovery
            | Method::CaptureRecovering
            | Method::CaptureLatest
            | Method::CaptureActual
            | Method::CaptureFps
            | Method::CaptureNext
            | Method::CaptureNextReady
            | Method::CaptureRun
            | Method::CaptureStartReady
            | Method::CaptureStopReady => self.run_capture_flow(method, args),
            Method::CaptureDsDevices
            | Method::CaptureDsDevice
            | Method::CaptureDsPins
            | Method::CaptureMfDevices
            | Method::CaptureMfSource
            | Method::CaptureMfMode
            | Method::CaptureDevices => self.run_capture_devices(method, args),
            Method::CaptureModes | Method::CaptureModesSame => self.run_capture_modes(method),
            Method::CaptureRunDshow => self.run_capture_dshow(),
            Method::CaptureRunMediaFoundation => self.run_capture_mf(),
            Method::DesktopPacerCreate
            | Method::DesktopPacerDestroy
            | Method::DesktopPacerWait
            | Method::DesktopGdiCreate
            | Method::DesktopGdiDestroy
            | Method::DesktopGdiResize => self.run_capture_desktop(method),
            Method::DesktopDuplicationCreate | Method::DesktopDuplicationFrame => {
                self.run_capture_duplication(method)
            }
            Method::DesktopCaptureRun => self.run_capture_desktop_loop(),
            Method::DesktopProcessPath
            | Method::DesktopClientRect
            | Method::DesktopForegroundClient
            | Method::DesktopDisplayRefresh
            | Method::DesktopDxgiDisplay
            | Method::DesktopCaptureTarget
            | Method::DesktopInputEnvironment => self.run_desktop_platform(method),
            Method::DesktopIdentityCreate
            | Method::DesktopIdentityMonitor
            | Method::DesktopIdentityRect
            | Method::DesktopIdentityForeground
            | Method::DesktopIdentityMatch => self.run_desktop_identity(method),
            Method::DesktopTargets
            | Method::DesktopTargetsMonitor
            | Method::DesktopTargetsWindow => self.run_desktop_targets(method, args),
            Method::PhysicalHook
            | Method::PhysicalCreate
            | Method::PhysicalDestroy
            | Method::PhysicalWorker
            | Method::PhysicalDown
            | Method::PhysicalHotkey => self.run_desktop_physical(method, args),
            Method::ControllerLatency
            | Method::ControllerRecord
            | Method::ControllerExchange
            | Method::ControllerMouseReport
            | Method::ControllerKeyboardReport
            | Method::ControllerState => self.run_controller_protocol(method),
            Method::ControllerConnect
            | Method::ControllerRelease
            | Method::ControllerClose
            | Method::ControllerProbe => self.run_controller_lifecycle(method),
            Method::ControllerMove
            | Method::ControllerAbsolute
            | Method::ControllerButton
            | Method::ControllerKey
            | Method::ControllerWheel => self.run_controller_input(method),
            Method::ControllerLocalSend => self.run_controller_local(),
            Method::BackendCreate
            | Method::BackendDestroy
            | Method::BackendStart
            | Method::BackendConfigMatches
            | Method::BackendStop => self.run_backend_lifecycle(method),
            Method::BackendFrame
            | Method::BackendRawFrame
            | Method::BackendRecoverCapture
            | Method::BackendDisarmFault
            | Method::BackendPreview
            | Method::BackendSnapshot => self.run_backend_frames(method),
            Method::BackendArm
            | Method::BackendHeartbeat
            | Method::BackendTestMove
            | Method::BackendPhysicalDown
            | Method::BackendPhysicalHotkey => self.run_backend_control(method),
            Method::BackendMonitor => self.run_backend_monitor(),
            Method::BackendDispatch => self.run_backend_dispatch(),
            Method::BackendServe | Method::BackendServeParseLine => self.run_backend_serve(method),
            Method::CountConcertoRing
            | Method::DetectConcerto
            | Method::GaugeColorPipeline
            | Method::GaugeRingPipeline
            | Method::GaugePhoebePipeline
            | Method::GaugeStripePipeline
            | Method::GaugeStripedPipeline => self.run_combat_images(method, args),
            Method::MonthlyWindowKey | Method::ContinueMarkerPipeline => {
                self.run_text_helpers(method)
            }
            _ => self.run_navigation(method, args),
        }
    }
}
