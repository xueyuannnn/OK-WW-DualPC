#include "reference_engine_loop.hpp"
#include "engine_lifecycle_probe.hpp"
#include "engine_setup_probe.hpp"
#include "engine_catalog_probe.hpp"
#include "engine_command_probe.hpp"
#include "engine_scripts_probe.hpp"
#include "engine_teams_probe.hpp"
#include "engine_session_probe.hpp"
#include "engine_reporting_probe.hpp"
#include <QtTest>
using namespace okww;
using namespace okww::runtime;
namespace {
using O=rust::EngineOp;
struct Probe{
    struct Task{bool trigger=false,custom=false,customTrigger=true,enabled=false,present=true;double interval=.007;};
    std::array<Task,5> tasks;
    std::array<double,5> next{};
    int scenario,selected=-1,current=0,catalog=0,name=-1,active=-1,contextTask=-1;
    int checkpoints=0,calls=0,resets=0,done=0,releases=0;
    double time=0,limit=20;
    bool gate=true,foreground=false,cancelForeground=false,locked=false,held=false,taskRunning=false,
        contextCombat=true,overrides=true,exitAfter=false,completed=false,recorded=false;
    QString failure;
    QJsonArray events;
    explicit Probe(int value):scenario(value){
        selected=(scenario%7)-1;foreground=selected>=0;
        for(int index=0;index<5;++index){tasks[index].trigger=index!=0&&index!=4;tasks[index].custom=index>=3;
            tasks[index].enabled=(scenario&(1<<index))!=0;tasks[index].present=(scenario%5)!=index;
            tasks[index].interval=.003+index*.007;}
        tasks[3].customTrigger=(scenario&32)!=0;
        cancelForeground=(scenario&64)!=0;
    }
    void event(O op,double argument=0){events.append(QJsonArray{int(op),argument,time});}
    void invoke(O op,const double *a,double *out,const std::exception_ptr &error={}){
        auto &task=tasks[current];
        switch(op){
            case O::Checkpoint:
                if(!gate)throw Cancelled();if(++checkpoints>20)throw std::runtime_error("replay complete");break;
            case O::HasSelected:out[0]=selected>=0;break;
            case O::CancelForeground:out[0]=cancelForeground;break;
            case O::Refresh:event(op);break;
            case O::TaskCount:out[0]=tasks.size();break;
            case O::SelectTask:current=int(a[0]);event(op,a[0]);break;
            case O::TaskSelected:out[0]=current==selected;break;
            case O::TaskTrigger:out[0]=task.trigger;break;
            case O::TaskCustom:out[0]=task.custom;break;
            case O::CustomTrigger:out[0]=task.customTrigger;break;
            case O::ScriptEnabled:out[0]=task.enabled;break;
            case O::ProfileEnabled:out[0]=task.present?task.enabled:a[0]!=0;break;
            case O::Now:out[0]=time;break;
            case O::NextTime:out[0]=next[current];break;
            case O::SetTaskContext:active=contextTask=current;event(op,current);break;
            case O::SetTaskRunning:taskRunning=a[0]!=0;event(op,a[0]);break;
            case O::RunTask:{
                event(op,current);held=true;++calls;time+=.003;
                if(calls==1)switch((scenario/8)%7){
                    case 1:throw Cancelled();
                    case 2:cancelForeground=true;throw Cancelled();
                    case 3:gate=false;throw Cancelled();
                    case 4:throw CombatEnded("combat interrupted");
                    case 5:tasks[1].present=true;tasks[1].enabled=false;throw CombatEnded("combat disabled");
                    case 6:throw std::runtime_error("task failed");
                    default:break;
                }
                out[0]=(scenario&1)!=0;break;
            }
            case O::GateActive:out[0]=gate;break;
            case O::TaskAutoCombat:out[0]=current==1;break;
            case O::ReleaseInput:held=false;++releases;event(op);if((scenario&256)&&releases==1)gate=false;break;
            case O::LogException:
                if(scenario%17==0)throw std::runtime_error("log failed");
                try{std::rethrow_exception(error);}catch(const std::exception &e){events.append(QJsonArray{int(op),errorText(e),time});}break;
            case O::SetNextTime:next[current]=a[0];event(op,a[0]);break;
            case O::TaskInterval:out[0]=task.interval;break;
            case O::ExitOption:out[0]=(scenario&128)!=0;break;
            case O::ExitProfile:out[0]=scenario%11==0;break;
            case O::SetExitAfter:exitAfter=a[0]!=0;event(op,a[0]);break;
            case O::RecordResult:event(op,a[0]);break;
            case O::SetResultRecorded:recorded=a[0]!=0;event(op,a[0]);break;
            case O::TaskDone:++done;event(op,selected);break;
            case O::CompletionNameReset:name=selected;event(op);break;
            case O::CatalogCount:out[0]=3;break;
            case O::SelectCatalog:catalog=int(a[0]);event(op,a[0]);break;
            case O::CatalogSelected:out[0]=(catalog==1?1:0)==selected;break;
            case O::CompletionNameSet:name=10+catalog;event(op,name);break;
            case O::NotifyDone:event(op,name);break;
            case O::SetCompleted:completed=a[0]!=0;event(op,a[0]);break;
            case O::StopGate:gate=false;event(op);break;
            case O::PostStatusSelected:event(op,selected);break;
            case O::TriggerInterval:out[0]=scenario%9==0?0:scenario%9==1?60001:scenario%9==2?.5:17;break;
            case O::Wait:event(op,a[0]);if(!gate)throw Cancelled();time+=a[0];break;
            case O::LockLifecycle:if(locked)throw std::runtime_error("lock reentered");locked=true;event(op);break;
            case O::UnlockLifecycle:if(!locked)throw std::runtime_error("lock not held");locked=false;event(op);break;
            case O::SetForeground:foreground=a[0]!=0;event(op,a[0]);break;
            case O::SetCancelForeground:cancelForeground=a[0]!=0;event(op,a[0]);break;
            case O::ClearSelected:selected=-1;event(op);break;
            case O::ClearActiveTask:active=-1;event(op);break;
            case O::ClearContextTask:contextTask=-1;event(op);break;
            case O::ClearOverrides:overrides=false;event(op);break;
            case O::ClearContextCombat:contextCombat=false;event(op);break;
            case O::ResetRunner:event(op);if(scenario%13==0)throw std::runtime_error("runner initialization failed");++resets;break;
            case O::CombatPending:event(op);break;
            case O::ClearLimit:limit=0;event(op);break;
            case O::ClearNext:next.fill(0);event(op);break;
            case O::PostStatus:event(op);break;
            case O::SetStoppedFailure:failure="当前任务已停止";event(op);break;
            case O::LogStopped:case O::LogCompleted:event(op);break;
            case O::ThrowCancelled:throw Cancelled();
            case O::ScriptTrigger:out[0]=(scenario&1)!=0;break;
            case O::ScriptRepeat:{const int values[]{-2,0,1,2,6,1000001};out[0]=values[scenario%6];break;}
            case O::ScriptContinuous:out[0]=(scenario&2)!=0;break;
            case O::HasInfo:out[0]=(scenario&4)!=0;break;
            case O::ScriptProgress:{
                const auto next=(uint64_t(a[4])<<32)|uint64_t(a[3]);
                events.append(QJsonArray{int(op),a[0],a[1],a[2],QString::number(qulonglong(next)),time});break;
            }
            case O::ScriptInfo:{
                const auto next=(uint64_t(a[1])<<32)|uint64_t(a[0]);
                events.append(QJsonArray{int(op),QString::number(qulonglong(next)),a[2],a[3],time});break;
            }
            case O::ScriptRun:
                event(op,a[0]);++calls;time+=.003;
                if(calls==1)switch((scenario/8)%5){
                    case 1:throw ScriptDisabled();
                    case 2:throw Cancelled();
                    case 3:throw std::runtime_error("script failed");
                    default:break;
                }
                out[0]=(scenario&16)!=0 || calls%2==0;break;
            case O::ScriptDisabledInfo:event(op);if(scenario%17==0)throw std::runtime_error("disabled info failed");break;
            case O::ScriptComplete:event(op,a[0]);break;
            default:throw std::runtime_error("unexpected probe operation");
        }
    }
    double value(O op,double argument=0){double out[3]{};invoke(op,&argument,out);return out[0];}
    bool flag(O op,double argument=0){return value(op,argument)!=0;}
    void action(O op,double argument=0){value(op,argument);}
    void logException(std::exception_ptr error){double out[3]{};invoke(O::LogException,nullptr,out,error);}
    void scriptProgress(uint64_t iteration,int repeat,bool continuous){
        const uint64_t next=iteration+1;
        const double values[]{double(iteration),double(repeat),double(continuous),double(uint32_t(next)),double(next>>32)};
        double out[3]{};invoke(O::ScriptProgress,values,out);
    }
    void scriptInfo(uint64_t next,int repeat,bool continuous){
        const double values[]{double(uint32_t(next)),double(next>>32),double(continuous),double(repeat)};
        double out[3]{};invoke(O::ScriptInfo,values,out);
    }
    QJsonObject trace() const{
        QJsonArray pending;for(auto value:next)pending.append(value);
        return {{"events",events},{"next",pending},{"selected",selected},{"current",current},{"active",active},
            {"context_task",contextTask},{"calls",calls},{"resets",resets},{"done",done},{"releases",releases},
            {"time",time},{"limit",limit},{"gate",gate},{"foreground",foreground},{"cancel",cancelForeground},
            {"locked",locked},{"held",held},{"task_running",taskRunning},{"combat",contextCombat},
            {"overrides",overrides},{"exit_after",exitAfter},{"completed",completed},{"recorded",recorded},{"failure",failure}};
    }
};
QJsonObject replay(int scenario,int method,bool rust){
    Probe probe(scenario);QString error;double result=0;
    try{
        if(rust)result=rust::engineRun(rust::EngineMethod(method),[&](O op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else if(method==0)reference_engine::run(probe);
        else if(method==1)reference_engine::resume(probe);
        else if(method==2)reference_engine::cancel(probe);
        else result=reference_engine::scriptIterations(probe);
    }catch(const std::exception &e){error=errorText(e);}
    auto report=probe.trace();report["error"]=error;report["result"]=result;return report;
}
}
class EngineContractTests final:public QObject{
    Q_OBJECT
  private slots:
    void reportingMatchesFrozenCpp_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<int>("method");
        for(int method=37;method<43;++method)for(int scenario=0;scenario<256;++scenario)
            QTest::newRow(qPrintable(QString::number(method)+"-"+QString::number(scenario)))<<scenario<<method;
    }
    void reportingMatchesFrozenCpp(){
        QFETCH(int,scenario);QFETCH(int,method);
        const auto expected=test::reportingTrace(scenario,method,false),actual=test::reportingTrace(scenario,method,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void sessionMatchesFrozenCpp_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<int>("method");
        for(int method=27;method<37;++method)for(int scenario=0;scenario<256;++scenario)
            QTest::newRow(qPrintable(QString::number(method)+"-"+QString::number(scenario)))<<scenario<<method;
    }
    void sessionMatchesFrozenCpp(){
        QFETCH(int,scenario);QFETCH(int,method);
        const auto expected=test::sessionTrace(scenario,method,false),actual=test::sessionTrace(scenario,method,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void teamsMatchFrozenCpp_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<int>("method");
        for(int method=20;method<27;++method)for(int scenario=0;scenario<256;++scenario)
            QTest::newRow(qPrintable(QString::number(method)+"-"+QString::number(scenario)))<<scenario<<method;
    }
    void teamsMatchFrozenCpp(){
        QFETCH(int,scenario);QFETCH(int,method);
        const auto expected=test::teamsTrace(scenario,method,false),actual=test::teamsTrace(scenario,method,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void scriptsMatchFrozenCpp_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<int>("method");
        for(int method=16;method<20;++method)for(int scenario=0;scenario<512;++scenario)
            QTest::newRow(qPrintable(QString::number(method)+"-"+QString::number(scenario)))<<scenario<<method;
    }
    void scriptsMatchFrozenCpp(){
        QFETCH(int,scenario);QFETCH(int,method);
        const auto expected=test::scriptsTrace(scenario,method,false),actual=test::scriptsTrace(scenario,method,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void commandMatchesFrozenCpp_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<int>("command");
        for(int command=0;command<21;++command)for(int scenario=0;scenario<256;++scenario)
            QTest::newRow(qPrintable(QString::number(command)+"-"+QString::number(scenario)))<<scenario<<command;
    }
    void commandMatchesFrozenCpp(){
        QFETCH(int,scenario);QFETCH(int,command);
        const auto expected=test::commandTrace(scenario,command,false),actual=test::commandTrace(scenario,command,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void catalogueMatchesFrozenCpp_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<int>("method");
        for(int method=13;method<15;++method)for(int scenario=0;scenario<256;++scenario)
            QTest::newRow(qPrintable(QString::number(method)+"-"+QString::number(scenario)))<<scenario<<method;
    }
    void catalogueMatchesFrozenCpp(){
        QFETCH(int,scenario);QFETCH(int,method);
        const auto expected=test::catalogTrace(scenario,method,false),actual=test::catalogTrace(scenario,method,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void setupMatchesFrozenCpp_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<int>("method");
        for(int method=8;method<13;++method)for(int scenario=0;scenario<256;++scenario)
            QTest::newRow(qPrintable(QString::number(method)+"-"+QString::number(scenario)))<<scenario<<method;
    }
    void setupMatchesFrozenCpp(){
        QFETCH(int,scenario);QFETCH(int,method);
        const auto expected=test::setupTrace(scenario,method,false),actual=test::setupTrace(scenario,method,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void lifecycleMatchesFrozenCpp_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<int>("method");
        for(int method=4;method<8;++method)for(int scenario=0;scenario<512;++scenario)
            QTest::newRow(qPrintable(QString::number(method)+"-"+QString::number(scenario)))<<scenario<<method;
    }
    void lifecycleMatchesFrozenCpp(){
        QFETCH(int,scenario);QFETCH(int,method);
        const auto expected=test::lifecycleTrace(scenario,method,false),actual=test::lifecycleTrace(scenario,method,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
    void matchesFrozenSchedulerAndRecovery_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<int>("method");
        for(int method=0;method<4;++method)for(int scenario=0;scenario<512;++scenario)
            QTest::newRow(qPrintable(QString::number(method)+"-"+QString::number(scenario)))<<scenario<<method;
    }
    void matchesFrozenSchedulerAndRecovery(){
        QFETCH(int,scenario);QFETCH(int,method);
        const auto expected=replay(scenario,method,false),actual=replay(scenario,method,true);
        QVERIFY2(actual==expected,qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{"expected",expected},{"actual",actual}}).toJson(QJsonDocument::Compact))));
    }
};
QTEST_GUILESS_MAIN(EngineContractTests)
#include "test_engine_contract.moc"
