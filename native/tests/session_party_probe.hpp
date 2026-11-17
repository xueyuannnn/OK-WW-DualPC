#pragma once
#include "reference_session_party.hpp"
namespace okww::runtime::test {
template<class Base> struct PartyProbe:Base {
    using O=rust::SessionOp;
    QVector<QJsonObject> members,definitions;
    QVector<std::optional<QJsonObject>> identities;
    std::optional<QJsonObject> observed;
    QJsonObject definition,identity,report,lastReport,bridge;
    QJsonArray reportMembers,emitted;
    QString selected,selection;
    QByteArray key,lastKey;
    bool macro=false,scanMatches=false;
    explicit PartyProbe(int scene,int method):Base(scene,method){
        for(int i=0;i<(scene/16)%4;++i)members.append(QJsonObject{{"name","A"+QString::number(i)},{"class","BaseChar"},{"index",i},{"current",false},{"resets",0}});
        definitions.resize(members.size());
        selected=scene&64?"python:x":scene&32?"native:x":"";
        macro=(scene&8)!=0;scanMatches=(scene&2)!=0;bridge={{"samples",scene}};
        QJsonArray rows;
        for(const auto &member:members)rows.append(QJsonObject{{"class",member["class"]},{"name",member["name"]},{"index",member["index"]},{"current",(scene&1)&&member["index"].toInt()==scene%4-1}});
        const QJsonObject expected{{"members",rows},{"mode",selected.startsWith("python:")?"python":selected.isEmpty()?"builtin":"native-team"},{"code_state","ready"},{"verified",(scene&1)!=0}};
        lastReport=expected;if(selected.startsWith("python:"))lastReport["bridge"]=bridge;
        if(scene%4!=0)lastReport={{"old",true}};
        lastKey=scene%3==0?QJsonDocument(expected).toJson(QJsonDocument::Compact):QByteArray("old");
        this->times[int(reference_session::Time::LastPartyReport)]=scene%3==0?9.9:9;
    }
    void invoke(O op,const double *a,double *out,const std::exception_ptr &error={}){
        const auto event=[&](double v=0){this->event(op,v);};
        switch(op){
        case O::ReadHud:out[0]=(this->scene&1)!=0;out[1]=(this->scene/4)%4;out[2]=this->scene%4-1;event();break;
        case O::PartySize:out[0]=double(members.size());break;
        case O::PartyScanMatches:out[0]=scanMatches;break;
        case O::StorePartyScan:scanMatches=true;event();break;
        case O::ReadIdentity:{
            const int i=int(a[0]),variant=(this->scene+i)%3;
            observed=variant==0?std::nullopt:std::optional<QJsonObject>({{"name",(variant==1?"A":"newA")+QString::number(i)},{"class","BaseChar"},{"index",i},{"supported",!((this->scene&256)&&i==1)}});
            this->events.append(QJsonArray{int(op),a[0],a[1],this->time});break;
        }
        case O::ObservedIdentityExists:out[0]=observed.has_value();break;
        case O::ObservedIdentitySupported:out[0]=observed->value("supported").toBool();break;
        case O::UnsupportedCharacter:event(a[0]);throw std::runtime_error("unsupported character");
        case O::AppendIdentity:identities.append(observed);event();break;
        case O::IdentityExists:out[0]=identities.at(int(a[0])).has_value();break;
        case O::IdentityCount:out[0]=double(identities.size());break;
        case O::MemberExists:out[0]=!members.at(int(a[0])).isEmpty();break;
        case O::MemberMatchesIdentity:out[0]=members.at(int(a[0]))["name"]==identities.at(int(a[0]))->value("name");break;
        case O::MemberIndex:out[0]=members.at(int(a[0]))["index"].toInt();break;
        case O::SetMemberCurrent:members[int(a[0])]["current"]=a[1]!=0;this->events.append(QJsonArray{int(op),a[0],a[1],this->time});break;
        case O::DropMacro:macro=false;event();break;
        case O::ClearSelectedTeam:selected.clear();event();break;
        case O::ResizeParty:members.resize(int(a[0]));event(a[0]);break;
        case O::ResizeDefinitions:definitions.resize(int(a[0]));event(a[0]);break;
        case O::CopyIdentityDefinition:definition=*identities.at(int(a[0]));event(a[0]);break;
        case O::CreateIdentifiedMember:event(a[0]);if(this->scene%23==0)throw std::runtime_error("character creation failed");members[int(a[0])]=definition;members[int(a[0])]["resets"]=0;break;
        case O::StoreIdentityDefinition:definitions[int(a[0])]=definition;event(a[0]);break;
        case O::CreateUnknownMember:members[int(a[0])]=QJsonObject{{"name","unknown"},{"class","BaseChar"},{"index",int(a[0])},{"resets",0}};event(a[0]);break;
        case O::ResetMember:{auto &member=members[int(a[0])];member["resets"]=member["resets"].toInt()+1;event(a[0]);break;}
        case O::TeamsExist:out[0]=(this->scene&16)!=0;break;
        case O::ReadTeamSelection:event();if(this->scene%19==0)throw std::runtime_error("team selection failed");selection=this->scene&128?"python:y":"native:y";break;
        case O::EmptyTeamSelection:selection.clear();event();break;
        case O::TeamSelectionMatches:out[0]=selected==selection;break;
        case O::StoreTeamSelection:selected=selection;event();break;
        case O::ClearBridgeInfo:bridge={};event();break;
        case O::SelectionPython:out[0]=selection.startsWith("python:");break;
        case O::PublishPartyState:emitted.append(a[0]!=0?"loading":"ready");event(a[0]);break;
        case O::CreateSelectedMacro:event();if(this->scene%37==0)throw std::runtime_error("macro creation failed");macro=(this->scene&64)!=0;break;
        case O::MacroExists:out[0]=macro;break;
        case O::MacroReset:event();if(this->scene%31==0)throw std::runtime_error("macro reset failed");break;
        case O::PublishParty:emitted.append("ready");event();break;
        case O::HasCombatInfo:out[0]=(this->scene&2)!=0;break;
        case O::AppendPartyMember:{const auto &member=members.at(int(a[0]));reportMembers.append(QJsonObject{{"class",member["class"]},{"name",member["name"]},{"index",member["index"]},{"current",a[1]!=0}});this->events.append(QJsonArray{int(op),a[0],a[1],this->time});break;}
        case O::SelectedPython:out[0]=selected.startsWith("python:");break;
        case O::SelectedTeamEmpty:out[0]=selected.isEmpty();break;
        case O::BuildPartyIdentity:{const char *modes[]{"builtin","python","native-team"};identity={{"members",reportMembers},{"mode",QLatin1String(modes[int(a[0])])},{"code_state","ready"},{"verified",a[1]!=0}};this->events.append(QJsonArray{int(op),a[0],a[1],this->time});break;}
        case O::BuildPartyKey:key=QJsonDocument(identity).toJson(QJsonDocument::Compact);event();break;
        case O::InitializePartyReport:report=identity;event();break;
        case O::AppendPartyBridge:report["bridge"]=bridge;event();break;
        case O::PartyReportMatches:out[0]=report==lastReport;break;
        case O::PartyKeyMatches:out[0]=key==lastKey;break;
        case O::StorePartyKey:lastKey=key;event();break;
        case O::StorePartyReport:lastReport=report;event();break;
        case O::EmitPartyInfo:emitted.append(report);event();if(this->scene%43==0)throw std::runtime_error("party callback failed");break;
        default:Base::invoke(op,a,out,error);break;
        }
    }
    double value(O op,double a=0){double out[3]{};invoke(op,&a,out);return out[0];}
    bool flag(O op,double a=0){return value(op,a)!=0;}
    void action(O op,double a=0){value(op,a);}
    double pair(O op,double first,double second){const double a[]{first,second};double out[3]{};invoke(op,a,out);return out[0];}
    std::array<double,3> observe(O op,double value=0){std::array<double,3> out{};invoke(op,&value,out.data());return out;}
    QJsonObject trace()const{
        auto output=Base::trace();QJsonArray roster,defs,ids;for(const auto &v:members)roster.append(v);for(const auto &v:definitions)defs.append(v);
        for(const auto &v:identities)ids.append(v?QJsonValue(*v):QJsonValue());
        output["roster"]=roster;output["definitions"]=defs;output["identities"]=ids;output["selected"]=selected;output["macro"]=macro;
        output["scan_matches"]=scanMatches;output["emitted"]=emitted;output["report"]=lastReport;output["key"]=QString::fromLatin1(lastKey.toHex());return output;
    }
};
template<class Base> QJsonObject partyReplay(int method,int scene,bool migrated){
    PartyProbe<Base> probe(scene,method);QString error;int kind=0;double result=0;
    try{
        if(migrated)result=rust::sessionRun(rust::SessionMethod(method),{},[&](rust::SessionOp op,const double *a,double *out,const std::exception_ptr &e){probe.invoke(op,a,out,e);});
        else if(method==37)result=reference_session::loadParty(probe);else reference_session::publishParty(probe);
    }catch(const std::exception &e){error=errorText(e);kind=rust::sessionException(std::current_exception());}catch(...){error="unknown";kind=6;}
    auto output=probe.trace();output["error"]=error;output["error_kind"]=kind;output["result"]=result;return output;
}
}
