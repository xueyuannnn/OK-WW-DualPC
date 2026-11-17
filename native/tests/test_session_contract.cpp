#include "reference_session_core.hpp"
#include "reference_session_detection.hpp"
#include "reference_session_battle.hpp"
#include "reference_session_switch.hpp"
#include "reference_session_recovery.hpp"
#include "session_party_probe.hpp"
#include "reference_session_helpers.hpp"
#include "runtime/combat_session.hpp"
#include "runtime/diagnostics.hpp"
#include "runtime/interaction.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
using O=rust::SessionOp;
using B=reference_session::Bool;
using T=reference_session::Time;
struct Probe {
    int scene,frames=0,checks=0,observations=0,activeCalls=0,releases=0,method=-1,choices=0,target=-1,conReads=0,closeWaits=0;
    bool switchTapped=false,postCalled=false;
    QJsonArray candidates;
    std::array<double,6> areas{},beforeAreas{};
    int candidate=-1,best=-1,ring=0;
    double currentCon=.25,savedCon=0;
    QJsonObject savedAreas,taskConfig;
    QVector<int> memberPointers;
    QString keyResult;
    bool imageExtracted=false,combatLogged=false;
    bool resultChecked=false;
    double resultCheckedAt=-1;
    std::array<int,2> counters{};
    std::array<bool,3> memberCurrent{},liberation{};
    double time=10;
    std::array<bool,15> booleans{};
    std::array<double,10> times{};
    uint64_t observed,revision;
    QJsonArray events;
    explicit Probe(int input,int selectedMethod=-1):scene(input),method(selectedMethod),observed((uint64_t(1)<<60)+input),revision(observed-((input&2)?1:0)){
        for(int i=0;i<int(booleans.size());++i)booleans[i]=(input&(1<<(i%8)))!=0;
        booleans[int(B::CheckingDialog)]=input%7==0;
        times[int(T::Deadline)]=input%5==0?9:input%5==1?11:0;
        times[int(T::LastInterval)]=9.95;
        times[int(T::LastDeathCheck)]=input%3==0?9.5:0;
        times[int(T::LastHealthCheck)]=input%3==0?9.9:-1;
        times[int(T::LastWorkshopObservation)]=input%3==0?9.95:input%3==1?-1:9.8;
        times[int(T::TargetEnemyTimeout)]=input%4*.3;
        counters[1]=input%4==0?1:0;
        for(int i=0;i<3;++i)memberCurrent[i]=i==scene%5||((scene&128)&&i==2);
        for(int i=0;i<6;++i)areas[i]=(scene+i)%4==0?0:100+i;
        ring=scene%9-2;
    }
    void event(O op,double v=0){events.append(QJsonArray{int(op),v,time});}
    void invoke(O op,const double *a,double *out,const std::exception_ptr &error){
        switch(op){
        case O::GetBool:out[0]=booleans.at(int(a[0]));break;
        case O::SetBool:booleans.at(int(a[0]))=a[1]!=0;events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::GetTime:out[0]=times.at(int(a[0]));break;
        case O::SetTime:times.at(int(a[0]))=a[1];events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::GetCount:out[0]=counters.at(int(a[0]));break;
        case O::SetCount:counters.at(int(a[0]))=int(a[1]);events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::Now:out[0]=time;break;
        case O::Checkpoint:++checks;event(op);if(scene%17==0)throw Cancelled();break;
        case O::Wait:time+=a[0];event(op,a[0]);break;
        case O::Refresh:++frames;event(op);if(scene%29==0)throw CombatEnded("refresh interruption");break;
        case O::AutoTask:out[0]=(scene&1)!=0;break;
        case O::AutoEnabled:out[0]=(scene&4)!=0;break;
        case O::RevisionChanged:out[0]=revision!=observed;break;
        case O::StoreRevision:revision=observed;event(op);break;
        case O::MacroExists:case O::PhysicalFExists:out[0]=(scene&8)!=0;break;
        case O::PhysicalFRead:out[0]=(scene&16)!=0;event(op);break;
        case O::PartySize:out[0]=method==30?3:scene%4;break;
        case O::MemberCurrent:out[0]=memberCurrent.at(int(a[0]));break;
        case O::Interval:out[0]=(scene&1)!=0;event(op,a[0]);break;
        case O::Sleep:time+=a[0];event(op,a[0]);if(scene%37==0)throw Cancelled();break;
        case O::InputKey:case O::InputClick:case O::InputEscape:case O::ClickMiddle:case O::Log:
            event(op,a[0]);break;
        case O::NextFrame:time+=.3;++frames;event(op);break;
        case O::CurrentExists:out[0]=scene%5!=0;break;
        case O::CurrentIndex:out[0]=scene%3;break;
        case O::RefreshCooldowns:event(op,a[0]);break;
        case O::Cooldown:out[0]=scene%3*.2;events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::Highlighted:out[0]=(scene&2)?1:0;event(op,a[0]);break;
        case O::HasCd:out[0]=(scene&4)!=0;events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::HealthBarNative:out[0]=(scene&1)!=0;event(op,a[0]);break;
        case O::ShiftedHealthBar:out[0]=(scene&4)!=0;event(op);break;
        case O::SessionHealthBar:out[0]=(scene&4)!=0||frames>1;event(op);break;
        case O::InTeam:out[0]=(scene&2)!=0||frames>1;break;
        case O::TargetLocked:out[0]=(scene&8)!=0||frames>2;event(op);break;
        case O::ReadRunDialog:out[0]=(scene&16)!=0&&frames<(scene/16)%5;event(op);break;
        case O::DeathDialog:out[0]=method==33?((scene&512)&&activeCalls>=2):(scene&32)!=0;event(op);break;
        case O::HudVisible:out[0]=(scene&64)!=0;event(op);break;
        case O::ChallengeResult:out[0]=scene%3;event(op);break;
        case O::CalibrationExists:out[0]=(scene&1)!=0;break;
        case O::ReadCalibration:event(op);if(scene%19==0)throw std::runtime_error("calibration read failed");break;
        case O::CalibrationValue:event(op,a[0]);break;
        case O::MacroReset:event(op);if(scene%31==0)throw std::runtime_error("macro reset failed");break;
        case O::PythonMacro:out[0]=(scene&64)!=0;event(op);break;
        case O::PythonShortTarget:out[0]=(scene&128)!=0;event(op);break;
        case O::BuiltinShortTarget:out[0]=(scene&32)!=0;event(op);break;
        case O::ObserveTarget:++observations;out[0]=(scene&1)!=0;out[1]=(scene&2)!=0;out[2]=1;event(op,a[0]);if(observations==2&&scene%5==0)out[0]=0;break;
        case O::TapEscape:case O::TapF:time+=a[0];event(op,a[0]);break;
        case O::FullBreakFeature:case O::BreakFeature:case O::DialogInteract:case O::VisibleBreakPrompt:case O::FindBreakPrompt:
            out[0]=(scene&(1<<(int(op)%8)))!=0;event(op,op==O::VisibleBreakPrompt||op==O::FindBreakPrompt?a[0]:0);break;
        case O::CheckBreak:out[0]=(scene&128)!=0;event(op);break;
        case O::CombatActive:{
            ++activeCalls;event(op);const int mode=(scene/8)%8;
            if(mode==0){out[0]=0;break;}
            if(activeCalls<((scene&64)?1:3)){out[0]=1;break;}
            switch(mode){case 1:throw CharacterDead();case 2:throw CombatInterrupted("interrupted");case 3:throw CombatEnded("ended");
                case 4:throw Cancelled();case 5:throw std::runtime_error("combat failure");case 6:throw 123;default:out[0]=0;break;}
            break;
        }
        case O::ReadHud:
            if(method==30){out[0]=scene%11!=0;out[1]=3;out[2]=switchTapped&&scene%13!=0?target:scene%3;}
            else {out[0]=(scene&8)!=0;out[1]=scene%4;out[2]=(scene/4+frames)%4-1;}
            event(op);break;
        case O::MemberIndex:out[0]=a[0];break;
        case O::SetMemberCurrent:memberCurrent.at(int(a[0]))=a[1]!=0;events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::SetActorPerform:event(op,a[0]);break;
        case O::MacroPerform:out[0]=(scene&16)!=0;event(op);if(scene%41==0)throw CombatEnded("macro interrupted");break;
        case O::MemberRing:out[0]=(scene+int(a[0]))%8-1;break;
        case O::MemberLiberationReady:out[0]=liberation.at(int(a[0]));break;
        case O::FindLiberationPortrait:out[0]=(scene&32)!=0;event(op,a[0]);break;
        case O::SetMemberLiberationReady:liberation.at(int(a[0]))=true;event(op,a[0]);break;
        case O::TaskIdMatch:out[0]=scene%4==int(a[0]);break;
        case O::HealerTaskSetting:out[0]=(scene&16)!=0;event(op);break;
        case O::HealerGlobalSetting:out[0]=(scene&32)!=0;event(op);break;
        case O::PythonHealer:out[0]=(scene+(a[0]<0?scene%3:int(a[0])))%5==0;event(op,a[0]);break;
        case O::MemberRole:out[0]=(scene+(a[0]<0?scene%3:int(a[0])))%4;event(op,a[0]);break;
        case O::TapMember:event(op,a[0]);break;
        case O::CheckedVersionMatches:out[0]=(scene&32)!=0;break;
        case O::WaitHudRestore:out[0]=(scene&128)!=0;events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::ResultChecked:out[0]=resultChecked;break;
        case O::SetResultChecked:resultChecked=a[0]!=0;event(op,a[0]);break;
        case O::ResultCheckedAt:out[0]=resultCheckedAt;break;
        case O::SetResultCheckedAt:resultCheckedAt=a[0];event(op,a[0]);break;
        case O::PythonSkipCombatCheck:case O::BuiltinSkipCombatCheck:case O::HasOnCombatCheck:case O::OnCombatCheck:
        case O::HasEndCondition:case O::EndCondition:case O::MonthlyCard:case O::LoadParty:case O::AutoTargetSetting:
        case O::BossFeatures:case O::LevitatorFeature:case O::EarthFeature:case O::RealmExitFeature:case O::UseLiberationSetting:
        case O::TargetEnemy:case O::SelectedPython:out[0]=(scene&(1<<(int(op)%8)))!=0;event(op);break;
        case O::ReleaseInput:++releases;event(op);if(scene%23==0)throw std::runtime_error("release failure");break;
        case O::ResetCombat:booleans[int(B::Engaged)]=false;event(op);if(scene%19==0)throw std::runtime_error("reset failure");break;
        case O::MacroCombatEnd:case O::ActorCombatEnd:case O::PerformCurrent:event(op);if(scene%13==0)throw std::runtime_error("action failure");break;
        case O::ResetMember:event(op,a[0]);break;
        case O::LogException:try{std::rethrow_exception(error);}catch(const std::exception &e){events.append(QJsonArray{int(op),errorText(e),time});}break;
        case O::PreviousCon:{++conReads;const double values[]{0,.79,.8,.9,1,std::numeric_limits<double>::quiet_NaN()};out[0]=conReads>1&&(scene&16)?1:values[(scene/8)%6];event(op);break;}
        case O::ClearCandidates:candidates={};event(op);break;
        case O::PythonPriority:case O::NativeMacroPriority:out[0]=scene%3==0||int(a[0])%2==0;out[1]=(scene+int(a[0]))%9-4;events.append(QJsonArray{int(op),a[0],a[1],a[2],time});break;
        case O::BuiltinPriority:out[0]=(scene&128)?999:a[0]*3;events.append(QJsonArray{int(op),a[0],a[1],a[2],time});break;
        case O::AppendCandidate:candidates.append(QJsonArray{a[0],a[1]});events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::ChooseDecision:++choices;out[0]=scene%5==0||(scene%5==3&&choices>1)?scene%3:(scene%3+1)%3;event(op,a[0]);break;
        case O::DecisionMatchesPrevious:out[0]=scene%5==0||(scene%5==3&&choices>1);break;
        case O::DisposeCandidates:candidates={};event(op);break;
        case O::ClearTarget:target=-1;event(op);break;
        case O::SetTarget:target=int(a[0]);event(op,a[0]);break;
        case O::PreviousIndex:out[0]=scene%3;break;
        case O::TargetIndex:out[0]=target;break;
        case O::TargetSkipCheck:out[0]=(scene&4)!=0;event(op,a[0]);break;
        case O::PreviousConFull:out[0]=(scene&32)!=0;event(op);break;
        case O::PreviousCheckF:out[0]=(scene&64)!=0;break;
        case O::TargetWaitSwitch:out[0]=scene%7==0||((scene&128)&&time<10.4);event(op);break;
        case O::TapTargetIndex:switchTapped=true;event(op);break;
        case O::HasSwitchPost:out[0]=(scene&256)!=0;break;
        case O::RunSwitchPost:postCalled=true;event(op,a[0]);if(scene%17==0)throw std::runtime_error("switch post failed");break;
        case O::PreviousNormalAttack:case O::ApplyIntro:case O::PreviousSwitchOut:case O::SetTargetSwitchIn:case O::SetPreviousOutro:event(op,a[0]);break;
        case O::UpdatePortraits:case O::LogSwitch:case O::SetTargetCurrent:case O::FreezeSwitchIntro:case O::BreakF:event(op);break;
        case O::InRealm:out[0]=(scene&8)!=0;event(op);break;
        case O::HasRecovery:out[0]=(scene&256)!=0;break;
        case O::CustomRecovery:event(op);if(scene%13==0)throw std::runtime_error("custom recovery failed");break;
        case O::ExitRealm:case O::ContextEscape:case O::CombatCountInfo:case O::WaitTeamAfterCombat:event(op,a[0]);break;
        case O::ContextSleep:time+=a[0];++frames;event(op,a[0]);break;
        case O::WaitDeathClosed:++closeWaits;out[0]=(scene&(closeWaits==1?4:8))!=0;event(op,a[0]);break;
        case O::ClosePopupClick:events.append(QJsonArray{int(op),a[0],a[1],a[2],time});break;
        case O::HasInfo:out[0]=(scene&16)!=0;break;
        case O::ResumeInterrupted:out[0]=(scene&32)!=0;break;
        case O::RecoveryRetryLimit:{const int limits[]{-1,0,1,3,10,11};out[0]=limits[(scene/4)%6];break;}
        case O::WaitTeamOrDeath:out[0]=(scene&128)!=0;event(op,a[0]);break;
        case O::LogRetry:events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::RecoverDead:event(op);throw CharacterRevived();
        case O::CloseRevivalDialog:case O::CreateNavigation:case O::HealAtBeacon:event(op);break;
        case O::NamedRegionEmpty:case O::FeatureBoxEmpty:out[0]=(scene&1)!=0;break;
        case O::MakeSnapshotDirectory:case O::EncodeSnapshot:case O::OpenSnapshot:case O::WriteSnapshot:case O::CommitSnapshot:
            out[0]=(scene&(1<<(int(op)%8)))!=0;event(op);break;
        case O::AppendMemberPointer:memberPointers.append(int(a[0]));event(op,a[0]);break;
        case O::ReadHotkey:keyResult=QString("configured-%1").arg(int(a[0]));event(op,a[0]);break;
        case O::ExtraHotkey:keyResult="4";event(op);break;
        case O::FindFeature:out[0]=(scene&2)!=0;event(op);break;
        case O::CandidateNameCount:out[0]=scene%5;break;
        case O::FindCandidate:candidate=int(a[0]);out[0]=(scene+candidate)%3!=0;event(op,a[0]);break;
        case O::CandidateConfidence:{const double values[]{0,.5,.6,.8,.8,1,std::numeric_limits<double>::quiet_NaN()};out[0]=values[(scene+candidate)%7];break;}
        case O::StoreBestCandidate:best=int(a[0]);event(op,a[0]);break;
        case O::CloneNamedImage:case O::ExtractRegionImage:imageExtracted=true;event(op);break;
        case O::MeasureSegmentedGauge:out[0]=scene%10-1;event(op);break;
        case O::CaptureConcertoState:beforeAreas=areas;savedCon=currentCon;event(op);break;
        case O::DetectConcerto:currentCon=.75;out[0]=scene&1?.75:.3;if(scene&2)areas[scene%6]+=1;event(op);if(scene%31==0)throw std::runtime_error("concerto detection failed");break;
        case O::RestoreConcertoCache:currentCon=savedCon;event(op);break;
        case O::BeforeCalibrationArea:out[0]=beforeAreas.at(int(a[0]));break;
        case O::CurrentCalibrationArea:out[0]=areas.at(int(a[0]));break;
        case O::StoreCalibrationArea:savedAreas[QString::number(int(a[0]))]=a[1];events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::WriteCalibration:event(op);if(scene%37==0)throw std::runtime_error("calibration write failed");break;
        case O::CharacterRing:out[0]=ring;break;
        case O::CallCharacterConcerto:ring=scene%6;event(op);break;
        case O::ConcertoRegionInside:out[0]=(scene&8)!=0;break;
        case O::FrameSize:out[0]=scene%3==0?1600:1920;out[1]=scene%3==0?900:1080;break;
        case O::CountConcertoRing:out[0]=scene%300;events.append(QJsonArray{int(op),a[0],a[1],time});break;
        case O::ReadTaskConfig:taskConfig={{"base",5}};event(op);break;
        case O::OverrideCount:out[0]=scene%4;break;
        case O::MergeOverride:taskConfig[QString("key%1").arg(int(a[0]))]=scene;event(op,a[0]);break;
        case O::EmitCombatLog:combatLogged=true;event(op);break;
        case O::SnapshotPath:case O::SanitizeSnapshotName:case O::CreateSnapshotFile:case O::FeatureSearchBox:case O::BuildConcertoRegion:event(op);break;
        case O::ClickDefault:case O::ClearCheckedVersion:case O::VisionReset:case O::ActorPerform:case O::PublishParty:
        case O::SwitchHealer:case O::SetCheckedVersionFrame:event(op);break;
        case O::CreateConcerto:case O::InitialRevision:case O::Watchdog:case O::CheckCombat:case O::RefreshObservation:event(op);break;
        case O::Throw:event(op,a[0]);switch(int(a[0])){
            case 0:throw CombatEnded("disabled");case 1:throw CombatEnded("rearm");case 2:throw std::runtime_error("invalid wait");
            case 3:throw NavigationFailure("deadline");case 4:throw ExecutionFailure("run dialog","exhausted");case 5:throw CharacterDead();
            case 6:throw CombatEnded("challenge success");case 7:throw CombatInterrupted("challenge failure");
            case 8:throw CombatEnded("combat ended");case 9:throw CombatInterrupted("current actor unknown");
            case 10:throw CombatEnded("challenge succeeded");case 11:throw CombatInterrupted("challenge failed");case 12:throw CombatInterrupted("party not restored");
            case 13:throw CombatInterrupted("switch timeout");case 14:throw CombatInterrupted("switch HUD missing");
            case 15:throw NavigationFailure("combat wait timeout");
            case 16:try{std::rethrow_exception(error);}catch(const std::exception &e){throw NavigationFailure("retry limit: "+errorText(e));}
            case 17:throw NavigationFailure("party did not recover");case 18:throw CharacterRevived();
            case 19:throw std::runtime_error("revival popup did not close");case 20:throw std::runtime_error("invalid combat options");
        }break;
        default:throw std::runtime_error("unexpected session operation");
        }
    }
    double value(O op,double a=0){double out[3]{};invoke(op,&a,out,{});return out[0];}
    bool flag(O op,double a=0){return value(op,a)!=0;}
    void action(O op,double a=0){value(op,a);}
    bool get(B field){return flag(O::GetBool,int(field));}
    double get(T field){return value(O::GetTime,int(field));}
    void set(B field,bool v){const double a[]{double(int(field)),double(v)};double out[3]{};invoke(O::SetBool,a,out,{});}
    void set(T field,double v){const double a[]{double(int(field)),v};double out[3]{};invoke(O::SetTime,a,out,{});}
    double pair(O op,double first,double second){const double a[]{first,second};double out[3]{};invoke(op,a,out,{});return out[0];}
    double now(){return value(O::Now);}
    std::array<double,3> observe(O op,double value=0){std::array<double,3> out{};invoke(op,&value,out.data(),{});return out;}
    void logException(std::exception_ptr e){double out[3]{};invoke(O::LogException,nullptr,out,e);}
    std::array<double,3> priority(O op,int index,bool intro,bool lowCon){const double a[]{double(index),double(intro),double(lowCon)};std::array<double,3> out{};invoke(op,a,out.data(),{});return out;}
    void closePopup(double x,double y,double after){const double a[]{x,y,after};double out[3]{};invoke(O::ClosePopupClick,a,out,{});}
    void interruptionLimit(std::exception_ptr e){const double a[]{16.};double out[3]{};invoke(O::Throw,a,out,e);}
    void logRetry(int iteration,int limit){const double a[]{double(iteration),double(limit)};double out[3]{};invoke(O::LogRetry,a,out,{});}
    QJsonObject trace() const{
        QJsonArray flags,seconds,current,ready,counts;for(auto v:booleans)flags.append(v);for(auto v:times)seconds.append(v);
        for(auto v:memberCurrent)current.append(v);for(auto v:liberation)ready.append(v);for(auto v:counters)counts.append(v);
        return {{"events",events},{"booleans",flags},{"times",seconds},{"now",time},{"frames",frames},{"checks",checks},{"revision",QString::number(qulonglong(revision))},
            {"members",current},{"ready",ready},{"counts",counts},{"observations",observations},{"active_calls",activeCalls},{"releases",releases},
            {"result_checked",resultChecked},{"checked_at",resultCheckedAt},{"candidates",candidates},{"choices",choices},{"target",target},{"con_reads",conReads},
            {"tapped",switchTapped},{"post_called",postCalled},{"close_waits",closeWaits},{"best",best},{"ring",ring},{"current_con",currentCon},{"saved_areas",savedAreas},
            {"key_result",keyResult},{"image_extracted",imageExtracted},{"task_config",taskConfig},{"combat_logged",combatLogged},{"pointer_count",memberPointers.size()}};
    }
};
std::vector<double> args(int method,int scene){
    const double times[]{-1,0,.001,.1,.4,1,std::numeric_limits<double>::quiet_NaN(),86401};
    if(method==3||method==5||method==8)return {times[scene%8]};
    if(method==4)return {scene&1?.2:0,scene&2?.1:-1,.01+scene%3*.05};
    if(method==9||method==15)return {double(scene%4),double(scene%5-2)};
    if(method==10)return {double(scene%4),double(scene%5-2),double((scene&1)!=0),double((scene&2)!=0)};
    if(method==19)return {.7+scene%3*.1};
    if(method==20)return {double((scene&1)!=0)};
    if(method==30)return {double((scene&1)!=0),double((scene&2)!=0)};
    if(method==31||method==34)return {double((scene&1)!=0)};
    if(method==33){
        const double waits[]{0,.02,.04,-1,std::numeric_limits<double>::quiet_NaN()};
        const double limits[]{1,30,0,7201,std::numeric_limits<double>::quiet_NaN()};
        return {waits[(scene/4)%5],double((scene&1)!=0),double((scene&2)!=0),double((scene&4)!=0),double((scene&8)!=0),limits[(scene/16)%5]};
    }
    if(method==42)return {double(scene%6-1)};
    if(method==44){const double thresholds[]{0,.5,.6,.8,std::numeric_limits<double>::quiet_NaN()};return {thresholds[scene%5]};}
    if(method==45||method==46)return {double(scene%4-1),double((scene/4)%4-1),double(1918+scene%3),double(1078+(scene/4)%3),1920,1080};
    if(method==52){const double widths[]{1600,1920,2560,0,-1};return {widths[scene%5]};}
    return {};
}
QJsonObject replay(int method,int scene,bool migrated){
    if(method==37||method==38)return test::partyReplay<Probe>(method,scene,migrated);
    Probe probe(scene,method);const auto input=args(method,scene);QString error;int kind=0;double result=0;
    try{
        if(migrated)result=rust::sessionRun(rust::SessionMethod(method),input,[&](O op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else if(method<16)result=reference_session::run(probe,method,input);
        else if(method<26)result=reference_session::detectionRun(probe,method,input);
        else if(method<30)result=reference_session::battleRun(probe,method);
        else if(method==30)reference_session::switchNext(probe,input[0]!=0,input[1]!=0);
        else if(method<37)result=reference_session::recoveryRun(probe,method,input);
        else result=reference_session::helpersRun(probe,method,input);
    }catch(const std::exception &e){error=errorText(e);kind=rust::sessionException(std::current_exception());}catch(...){error="unknown";kind=6;}
    auto report=probe.trace();report["error"]=error;report["error_kind"]=kind;report["result"]=result;return report;
}
}
class SessionContractTests final:public QObject {
    Q_OBJECT
  private slots:
    void controlFlowMatchesFrozenCpp_data(){
        QTest::addColumn<int>("method");QTest::addColumn<int>("scene");
        for(int method=0;method<53;++method)for(int scene=0;scene<(method==30||method==37||method==38?512:method==33?1024:256);++scene)
            QTest::newRow(qPrintable(QString::number(method)+"-"+QString::number(scene)))<<method<<scene;
    }
    void controlFlowMatchesFrozenCpp(){
        QFETCH(int,method);QFETCH(int,scene);const auto expected=replay(method,scene,false),actual=replay(method,scene,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
};
QTEST_GUILESS_MAIN(SessionContractTests)
#include "test_session_contract.moc"
