#pragma once
#include "runtime/engine_abi.hpp"
namespace okww::runtime::reference_engine {
template<class H> void recordResult(H &h){using O=rust::EngineOp;try{h.action(O::WriteRunResult);}catch(const std::exception &){h.reportException(O::LogResultFailure,std::current_exception());}}
template<class H> void stepDiagnostic(H &h){
    using O=rust::EngineOp;
    try{
        h.action(O::BuildStepPaths);
        try{
            if(!h.flag(O::StepFrameEmpty)&&h.flag(O::CreateScreenshotDirectory)&&h.flag(O::EncodeStepImage)&&h.flag(O::OpenScreenshot)&&h.flag(O::WriteScreenshot)&&h.flag(O::CommitScreenshot))h.action(O::SetScreenshotPath);
        }catch(...){h.action(O::DisposeScreenshot);throw;}
        h.action(O::DisposeScreenshot);h.action(O::SaveStepDiagnostic);
        if(h.value(O::StepDiagnosticCount)>=3)h.action(O::RemoveFirstStepDiagnostic);
        h.action(O::AppendStepDiagnostic);h.action(O::LogStepDiagnostic);
    }catch(const std::exception &){h.reportException(O::LogStepFailure,std::current_exception());}
}
template<class H> void errorScreenshot(H &h){
    using O=rust::EngineOp;
    if(h.flag(O::LowMemory))return;
    h.action(O::ReadErrorFrame);if(!h.flag(O::ErrorFrameAvailable))return;
    h.action(O::PrepareErrorImage);if(!h.flag(O::EncodeErrorImage))return;
    h.action(O::BuildErrorScreenshot);
    if(h.flag(O::OpenScreenshot)&&h.flag(O::WriteScreenshot)&&h.flag(O::CommitScreenshot)){h.action(O::LogScreenshot);h.action(O::SetScreenshotPath);}
    h.action(O::DisposeScreenshot);h.action(O::AppendNoticeImage);
}
template<class H> void disarm(H &h){
    using O=rust::EngineOp;h.action(O::LockLifecycle);
    try{
        h.action(O::StopGate);h.action(O::SetPaused,1);h.action(O::SetWarming,0);
        try{h.action(O::BackendArm,0);}catch(const std::exception &){h.reportException(O::LogException,std::current_exception());}
    }catch(...){h.action(O::UnlockLifecycle);throw;}
    h.action(O::UnlockLifecycle);
}
template<class H> void sessionLifetime(H &h){
    using O=rust::EngineOp;
    try{h.action(O::ExecuteSession);}
    catch(const Cancelled &){h.action(O::ReleaseEmergency);h.action(O::SetCancelledFailure);}
    catch(const std::exception &){
        const auto exception=std::current_exception();h.action(O::ReleaseEmergency);
        const bool lowMemory=h.classify(exception);h.action(O::SetTaskFailure);if(lowMemory)h.action(O::LogLowMemoryError);
        disarm(h);h.action(O::BuildErrorEvent);
        if(h.flag(O::IsExecutionFailure))h.action(O::SetErrorTraceback);else h.action(O::SetErrorType);
        h.action(O::CreateErrorNotice);
        try{h.action(O::CaptureErrorScreenshot);}catch(const std::exception &){
            const auto capture=std::current_exception();h.reportException(O::LogScreenshotFailure,capture);h.reportException(O::SetScreenshotError,capture);
        }
        h.action(O::SaveErrorDiagnostic);h.action(O::AssignDiagnosticReport);h.action(O::LogDiagnosticReport);
        h.action(O::EmitErrorEvent);h.action(O::NotifyError);h.action(O::SetExitAfterFalse);
    }
    disarm(h);h.action(O::PostStatus);if(!h.flag(O::ResultRecorded))h.action(O::RecordSelectedResult);
    if(h.flag(O::ExitAfter))h.action(O::QueueExit);h.action(O::SetWorkerFinished,1);
}
template<class H> void scheduleExit(H &h){
    using O=rust::EngineOp;if(!h.flag(O::GenerationMatches)||h.flag(O::HasPending))return;
    h.action(O::LogWaitingExit);h.action(O::StartExitWait);h.action(O::StartExitTimer);
}
}
