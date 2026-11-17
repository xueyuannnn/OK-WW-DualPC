#pragma once
#include "engine_session_probe.hpp"
#include "runtime/diagnostics.hpp"
#include "reference_engine_reporting.hpp"
namespace okww::runtime::test {
struct ReportingProbe:SessionProbe{
    bool gate=true,exitAfter=true,released=false,workerFinished=false;
    int diagnostics=0;
    QString primary,errorDescription;
    explicit ReportingProbe(int scene):SessionProbe(scene),diagnostics((scene/16)%5){}
    QString describe(std::exception_ptr e){try{std::rethrow_exception(e);}catch(const std::exception &ex){return errorText(ex);}catch(...){return "non-standard exception";}}
    void invoke(O op,const double *a,double *out,const std::exception_ptr &error={}){
        switch(op){
        case O::ExecuteSession:event(op);switch((scene/8)%6){case 0:break;case 1:throw Cancelled();case 2:throw std::runtime_error("run failed");case 3:throw ExecutionFailure("bridge failed","bridge details");case 4:throw 123;case 5:throw std::bad_alloc();}break;
        case O::ReleaseEmergency:released=true;event(op);break;
        case O::ClassifyFailure:primary=describe(error);out[0]=(scene/8)%6==5;event(op);break;
        case O::LowMemory:out[0]=(scene&1)!=0;break;
        case O::IsExecutionFailure:out[0]=(scene/8)%6==3;break;
        case O::SetTaskFailure:errorDescription=primary;event(op);break;
        case O::SetCancelledFailure:errorDescription="已暂停或取消";event(op);break;
        case O::StopGate:gate=false;event(op);break;
        case O::SetPaused:paused=a[0]!=0;event(op,a[0]);break;
        case O::SetWorkerFinished:workerFinished=a[0]!=0;event(op,a[0]);break;
        case O::BackendArm:event(op,a[0]);failure(2);break;
        case O::LogException:case O::LogResultFailure:case O::LogStepFailure:case O::LogScreenshotFailure:case O::SetScreenshotError:
            events.append(QJsonArray{int(op),describe(error)});break;
        case O::CaptureErrorScreenshot:event(op);failure(3);break;
        case O::SetExitAfterFalse:exitAfter=false;event(op);break;
        case O::ExitAfter:out[0]=exitAfter;break;
        case O::ResultRecorded:out[0]=(scene&4)!=0;break;
        case O::WriteRunResult:event(op);failure(0);break;
        case O::StepFrameEmpty:out[0]=(scene&1)!=0;break;
        case O::CreateScreenshotDirectory:case O::ErrorFrameAvailable:out[0]=(scene&2)!=0;break;
        case O::EncodeStepImage:case O::EncodeErrorImage:out[0]=(scene&4)!=0;event(op);failure(1);break;
        case O::OpenScreenshot:out[0]=(scene&8)!=0;event(op);failure(4);break;
        case O::WriteScreenshot:out[0]=(scene&16)!=0;event(op);failure(5);break;
        case O::CommitScreenshot:out[0]=(scene&32)!=0;event(op);break;
        case O::StepDiagnosticCount:out[0]=diagnostics;break;
        case O::RemoveFirstStepDiagnostic:--diagnostics;event(op);break;
        case O::AppendStepDiagnostic:++diagnostics;event(op);break;
        case O::SaveStepDiagnostic:case O::SaveErrorDiagnostic:event(op);failure(6);break;
        case O::BuildStepPaths:case O::SetScreenshotPath:case O::LogStepDiagnostic:case O::DisposeScreenshot:
        case O::ReadErrorFrame:case O::PrepareErrorImage:case O::BuildErrorScreenshot:case O::LogScreenshot:
        case O::AppendNoticeImage:case O::LogLowMemoryError:case O::BuildErrorEvent:case O::SetErrorTraceback:
        case O::SetErrorType:case O::CreateErrorNotice:case O::AssignDiagnosticReport:case O::LogDiagnosticReport:
        case O::EmitErrorEvent:case O::NotifyError:case O::RecordSelectedResult:case O::QueueExit:case O::PostStatus:
        case O::LogWaitingExit:case O::StartExitWait:case O::StartExitTimer:event(op);break;
        default:SessionProbe::invoke(op,a,out,error);break;
        }
    }
    double value(O op,double a=0){double out[3]{};invoke(op,&a,out);return out[0];}
    bool flag(O op,double a=0){return value(op,a)!=0;}
    void action(O op,double a=0){value(op,a);}
    void reportException(O op,std::exception_ptr e){double out[3]{};invoke(op,nullptr,out,e);}
    bool classify(std::exception_ptr e){double out[3]{};invoke(O::ClassifyFailure,nullptr,out,e);return out[0]!=0;}
};
inline QJsonObject reportingTrace(int scene,int method,bool migrated){
    ReportingProbe probe(scene);QString error;
    try{
        if(migrated)rust::engineRun(rust::EngineMethod(method),[&](rust::EngineOp op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else switch(method){case 37:reference_engine::recordResult(probe);break;case 38:reference_engine::stepDiagnostic(probe);break;
            case 39:reference_engine::errorScreenshot(probe);break;case 40:reference_engine::sessionLifetime(probe);break;
            case 41:reference_engine::disarm(probe);break;case 42:reference_engine::scheduleExit(probe);break;}
    }catch(const std::exception &e){error=errorText(e);}catch(...){error="non-standard exception";}
    auto result=probe.trace();result["error"]=error;result["locked"]=probe.locked;result["warming"]=probe.warming;
    result["gate"]=probe.gate;result["released"]=probe.released;result["worker_finished"]=probe.workerFinished;
    result["exit_after"]=probe.exitAfter;result["primary"]=probe.primary;result["failure_text"]=probe.errorDescription;result["diagnostics"]=probe.diagnostics;return result;
}
}
