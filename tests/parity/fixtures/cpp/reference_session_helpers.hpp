#pragma once
#include "reference_session_party.hpp"
namespace okww::runtime::reference_session {
template<class H> double helpersRun(H &h,int method,std::span<const double> a){
    using O=rust::SessionOp;
    switch(method){
    case 39:if(!h.flag(O::NamedRegionEmpty))h.action(O::CloneNamedImage);break;
    case 40:
        h.action(O::SnapshotPath);if(!h.flag(O::MakeSnapshotDirectory)){h.action(O::Log,6);break;}
        h.action(O::SanitizeSnapshotName);if(!h.flag(O::EncodeSnapshot))break;h.action(O::CreateSnapshotFile);
        if(!h.flag(O::OpenSnapshot)||!h.flag(O::WriteSnapshot)||!h.flag(O::CommitSnapshot))h.action(O::Log,7);break;
    case 41:{const int count=int(h.value(O::PartySize));for(int i=0;i<count;++i)h.action(O::AppendMemberPointer,i);break;}
    case 42:if(a[0]==0||a[0]==1||a[0]==2)h.action(O::ReadHotkey,a[0]);else h.action(O::ExtraHotkey);break;
    case 43:if(!h.flag(O::FeatureBoxEmpty))h.action(O::FeatureSearchBox);return h.flag(O::FindFeature);
    case 44:{
        double score=a[0];const int count=int(h.value(O::CandidateNameCount));
        for(int i=0;i<count;++i)if(h.flag(O::FindCandidate,i)){const double confidence=h.value(O::CandidateConfidence);if(confidence>=score){h.action(O::StoreBestCandidate,i);score=confidence;}}
        break;
    }
    case 45:case 46:
        if(a[0]<0||a[1]<0||a[2]>=a[4]||a[3]>=a[5])return 0;
        if(method==45)h.action(O::ExtractRegionImage);else return h.value(O::MeasureSegmentedGauge);break;
    case 47:{
        h.action(O::CaptureConcertoState);const auto percent=h.value(O::DetectConcerto);h.action(O::RestoreConcertoCache);
        bool changed=false;for(int i=0;i<6;++i)if(h.value(O::BeforeCalibrationArea,i)!=h.value(O::CurrentCalibrationArea,i)){changed=true;break;}
        if(changed){for(int i=0;i<6;++i)h.pair(O::StoreCalibrationArea,i,h.value(O::CurrentCalibrationArea,i));h.action(O::WriteCalibration);}
        return percent;
    }
    case 48:{
        if(h.value(O::CharacterRing)<0)h.action(O::CallCharacterConcerto);const int index=int(h.value(O::CharacterRing));
        if(index<0||index>=6||h.value(O::CurrentCalibrationArea,index)<=0)return 0;
        h.action(O::BuildConcertoRegion);if(!h.flag(O::ConcertoRegionInside))return 0;
        const auto size=h.observe(O::FrameSize);const double minimum=1500./3840/2160*size[0]*size[1];
        return h.pair(O::CountConcertoRing,index,minimum)/h.value(O::CurrentCalibrationArea,index);
    }
    case 49:{h.action(O::ReadTaskConfig);const int count=int(h.value(O::OverrideCount));for(int i=0;i<count;++i)h.action(O::MergeOverride,i);break;}
    case 50:if(h.value(O::CharacterRing)<0)h.action(O::CallCharacterConcerto);break;
    case 51:if(h.flag(O::HasInfo))h.action(O::EmitCombatLog);break;
    case 52:return a[0]==1600;
    }
    return 0;
}
}
