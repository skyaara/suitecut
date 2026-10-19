// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "include/cef_values.h"
#include "operation_registry.h"
#include "selector_wait_controls.h"
#include "snapshot_reference_controls.h"
#include "snapshot_reference_dispatch.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <optional>
#include <set>
struct Harness {
  using Dict=CefRefPtr<CefDictionaryValue>;
  using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;
  using Callback=std::function<void(bool,const std::string&)>;
  struct DevToolsCall {int owner;Callback callback;std::string session;bool retain_after_cancel=false;size_t response_limit=0;bool retained_control=false,lossless_scalars=false;};
  SuiteCutOperationRegistry operations_;bool browser_=true;double now=0;int next=1,submissions=0,receipts=0;
  bool submit_ok=true,synchronous=false,synchronous_ok=true;std::string main_frame_id_="main";
  std::map<int,DevToolsCall>devtools_;std::set<std::string>sessions_{"session"};std::vector<std::string>errors,released;
  std::vector<std::function<void()>> release_ack;
  std::map<std::string,SnapshotReferenceDocument>documents;
  suitecut::SelectorWaitControls selectors{[this]{return now;},[this](const auto& error){errors.push_back(error);},[this](int,const auto& group,const auto&,auto done){released.push_back(group);release_ack.push_back(std::move(done));}};
  suitecut::SelectorWaitControls* selector_waits_=&selectors;
  std::map<int,std::set<std::string>> evaluation_groups_;
  std::unique_ptr<suitecut::SnapshotReferences> snapshot_references_;
  Harness(){snapshot_references_=std::make_unique<suitecut::SnapshotReferences>("controlledSource",[this](int owner,uint64_t generation,const auto& document){return ReferencePlanCurrent(owner,generation,document);},[this](const auto& group,auto ack){released.push_back(group.name);release_ack.push_back([ack]{ack(true,"");});},[this](const auto& error){errors.push_back(error);});documents["main"]=Doc("main");documents["child"]=Doc("child");}
  static SnapshotReferenceDocument Doc(std::string frame){return{frame,"session","loader","mainUnique","utilityUnique",4,5,1};}
  double Now()const{return now;}
  int AllocateDevToolsId(){return next?next++:0;}
  void WakeDeadlines(){}
  bool SubmitDevTools(int id,const std::string&,Dict params,const std::string&){
    ++submissions;assert(params);assert(devtools_.contains(id));
    const auto& call=devtools_.at(id);assert(call.retain_after_cancel&&call.retained_control&&call.lossless_scalars&&call.response_limit==1048576);
    if(synchronous)Ack(id,synchronous_ok);
    return submit_ok;
  }
  void Ack(int id,bool ok=true){const auto found=devtools_.find(id);assert(found!=devtools_.end());auto callback=std::move(found->second.callback);devtools_.erase(found);callback(ok,"{}");}
  void Complete(int id,bool,const std::string&error){errors.push_back(error);operations_.Finish(id);}
  std::optional<SnapshotReferenceDocument>ReferenceDocument(const std::string&frame)const{const auto found=documents.find(frame);return found==documents.end()?std::nullopt:std::optional(found->second);}
  suitecut::SnapshotReferences& SnapshotReferences(){return *snapshot_references_;}
  uint64_t Start(int id=1,double timeout=1000){const auto entry=operations_.Begin(id,now,timeout);assert(entry);return entry->generation;}
  uint64_t BeginGroup(int owner,uint64_t generation,std::string frame="main"){return SnapshotReferences().Begin(owner,generation,documents.at(frame),{{41,1}},1000,now);}
  uint64_t Active(std::string frame="child"){const auto generation=Start();const auto ticket=BeginGroup(1,generation,std::move(frame));assert(ticket&&SnapshotReferences().Dispatched(ticket)&&SnapshotReferences().Installed(ticket,true,"actual-retained-object",now)&&SnapshotReferences().Commit(ticket,now));return ticket;}
  void Install(uint64_t ticket,int owner,uint64_t generation,bool publish=false){
    auto params=CefDictionaryValue::Create();params->SetString("objectGroup",SnapshotReferences().GroupFor(ticket)->name);
    auto temporary=selectors.Contains(owner)?selectors.Begin(owner):nullptr;
    ReferenceSubmit(owner,generation,"session","Runtime.callFunctionOn",params,[this,ticket]{return SnapshotReferences().Dispatched(ticket);},
      [this,ticket,temporary,publish](bool submitted,bool ok,const std::string&){++receipts;if(!submitted)SnapshotReferences().Rejected(ticket);else if(SnapshotReferences().Installed(ticket,ok,ok?"actual-installed-object":"",now)&&publish)assert(SnapshotReferences().Commit(ticket,now));});
  }
  Dict Query(const std::string&token,bool prefix=false,bool root_handle=false){auto message=CefDictionaryValue::Create();auto query=CefListValue::Create();size_t index=0;if(prefix){auto step=CefDictionaryValue::Create();step->SetString("engine","css");step->SetString("selector","#container");query->SetDictionary(index++,step);}auto ref=CefDictionaryValue::Create();ref->SetString("engine","reference");ref->SetString("ref",token);query->SetDictionary(index,ref);message->SetList("query",query);if(root_handle)message->SetInt("nativeRootHandle",7);return message;}
#include "snapshot_reference_dispatch.inc"
#include "snapshot_reference_scope.inc"
};
int main(){cef_api_hash(CEF_API_VERSION,0);
  {Harness h;const auto generation=h.Start();assert(h.selectors.Admit(1,generation)&&h.selectors.Group(1,"temporary-operation","session"));const auto ticket=h.BeginGroup(1,generation);h.Install(ticket,1,generation,true);assert(h.reference_dispatch_.Pending()==1&&h.SnapshotReferences().InstallPhysical()==1&&h.selectors.PhysicalSize()==1);h.Ack(1);assert(h.receipts==1&&h.SnapshotReferences().Active()==1&&!h.reference_dispatch_.Pending());h.operations_.Finish(1);h.selectors.Cancel(1);assert(h.released.size()==1&&h.released[0]=="temporary-operation");h.release_ack[0]();assert(!h.selectors.Size()&&h.SnapshotReferences().Active()==1&&h.SnapshotReferences().Groups()==1);}
  {Harness h;const auto generation=h.Start();assert(h.selectors.Admit(1,generation)&&h.selectors.Group(1,"temporary-operation","session"));const auto ticket=h.BeginGroup(1,generation);h.Install(ticket,1,generation);h.operations_.Finish(1);h.selectors.Cancel(1);h.SnapshotReferences().Cancel(1,generation,h.now);assert(h.released.empty()&&h.reference_dispatch_.Pending()==1&&h.SnapshotReferences().InstallPhysical()==1);h.Ack(1);assert(h.receipts==1&&!h.reference_dispatch_.Pending()&&!h.SnapshotReferences().InstallPhysical()&&h.released.size()==2);for(auto& ack:h.release_ack)ack();assert(!h.SnapshotReferences().Groups()&&!h.selectors.Size());}
  {Harness h;const auto generation=h.Start();assert(h.selectors.Admit(1,generation));const auto ticket=h.BeginGroup(1,generation);h.Install(ticket,1,generation);h.selectors.Pause(1);h.Ack(1);assert(h.receipts==1&&!h.SnapshotReferences().InstallPhysical()&&h.SnapshotReferences().GroupFor(ticket)->installed&&h.selectors.Retrying(1));h.SnapshotReferences().Cancel(1,generation,h.now);h.release_ack[0]();assert(!h.SnapshotReferences().Groups());}
  {Harness h;const auto generation=h.Start();const auto ticket=h.BeginGroup(1,generation);h.submit_ok=false;h.Install(ticket,1,generation);assert(h.receipts==1&&h.submissions==1&&h.devtools_.empty()&&!h.reference_dispatch_.Pending()&&!h.SnapshotReferences().Groups()&&!h.SnapshotReferences().InstallPhysical()&&h.released.empty());}
  {Harness h;const auto generation=h.Start();const auto ticket=h.BeginGroup(1,generation);h.next=0;h.Install(ticket,1,generation);assert(h.receipts==1&&!h.submissions&&h.devtools_.empty()&&!h.reference_dispatch_.Pending()&&!h.SnapshotReferences().Groups());}
  {Harness h;const auto generation=h.Start();const auto ticket=h.BeginGroup(1,generation);h.now=1000;h.Install(ticket,1,generation);assert(h.receipts==1&&!h.submissions&&!h.SnapshotReferences().Groups());}
  {Harness h;const auto generation=h.Start();const auto ticket=h.BeginGroup(1,generation);h.operations_.Finish(1);h.Start();h.Install(ticket,1,generation);assert(h.receipts==1&&!h.submissions&&!h.SnapshotReferences().Groups());}
  {Harness h;const auto generation=h.Start();const auto ticket=h.BeginGroup(1,generation);h.synchronous=true;h.submit_ok=false;h.Install(ticket,1,generation,true);assert(h.receipts==1&&h.submissions==1&&h.devtools_.empty()&&!h.reference_dispatch_.Pending()&&h.SnapshotReferences().Active()==1);}
  {Harness h;const auto generation=h.Start();const auto ticket=h.BeginGroup(1,generation);h.browser_=false;h.Install(ticket,1,generation);assert(h.receipts==1&&!h.submissions&&!h.SnapshotReferences().Groups());}
  {Harness h;const auto generation=h.Start();auto params=CefDictionaryValue::Create();int done=0;h.ReferenceSubmit(1,generation,"session","Runtime.callFunctionOn",params,[]{return false;},[&](bool submitted,bool ok,const auto&){assert(!submitted&&!ok);++done;});assert(done==1&&!h.submissions&&h.devtools_.empty()&&!h.reference_dispatch_.Pending());}
  {Harness h;const auto ticket=h.Active("main");auto use=h.SnapshotReferences().Use(ticket);assert(use&&h.SnapshotReferences().Borrows()==1);const auto generation=h.operations_.Get(1)->generation;auto params=CefDictionaryValue::Create();h.ReferenceSubmit(1,generation,"session","Runtime.callFunctionOn",params,[]{return true;},[&,use=std::move(use)](bool submitted,bool ok,const auto&)mutable{assert(submitted&&ok);++h.receipts;use.reset();});h.SnapshotReferences().InvalidateFrame("main",0);h.operations_.Finish(1);assert(h.released.empty()&&h.SnapshotReferences().Borrows()==1);h.Ack(1);assert(h.receipts==1&&h.SnapshotReferences().Borrows()==0&&h.released.size()==1);h.release_ack[0]();assert(!h.SnapshotReferences().Groups());}
  {Harness h;const auto ticket=h.Active();const auto token=h.SnapshotReferences().Reference(ticket,0);auto direct=h.Query(token);assert(h.PrepareSnapshotReferenceScope(1,direct)&&direct->GetString("frame")=="child");auto prefix=h.Query(token,true);assert(!h.PrepareSnapshotReferenceScope(1,prefix)&&!prefix->HasKey("frame"));}
  {Harness h;const auto ticket=h.Active();const auto token=h.SnapshotReferences().Reference(ticket,0);auto retained=h.Query(token,false,true);assert(!h.PrepareSnapshotReferenceScope(1,retained)&&!retained->HasKey("frame"));}
  {Harness h;const auto ticket=h.Active("main");const auto token=h.SnapshotReferences().Reference(ticket,0);auto prefix=h.Query(token,true);assert(h.PrepareSnapshotReferenceScope(1,prefix)&&!prefix->HasKey("frame"));}
  {Harness h;const auto ticket=h.Active();const auto token=h.SnapshotReferences().Reference(ticket,0);auto explicit_main=h.Query(token);explicit_main->SetString("frame","main");assert(!h.PrepareSnapshotReferenceScope(1,explicit_main)&&explicit_main->GetString("frame")=="main");}
  {Harness h;const auto ticket=h.Active();const auto token=h.SnapshotReferences().Reference(ticket,0);auto path=h.Query(token,true);auto stages=CefListValue::Create();stages->SetList(0,CefListValue::Create());path->SetList("framePath",stages);assert(h.PrepareSnapshotReferenceScope(1,path)&&!path->HasKey("frame"));path->Remove("framePath");path->SetString("frame","child");assert(h.PrepareSnapshotReferenceScope(1,path));}
  {Harness h;const auto generation=h.Start();const auto original=h.documents.at("main");assert(h.ReferencePlanCurrent(1,generation,original));h.documents["main"].main_unique="reused-numeric-context";assert(!h.ReferencePlanCurrent(1,generation,original));h.documents["main"]=original;h.documents["main"].context_generation++;assert(!h.ReferencePlanCurrent(1,generation,original));h.documents["main"]=original;h.now=1000;assert(!h.ReferencePlanCurrent(1,generation,original));}
  {Harness first,second,third;const auto a=first.Start(),b=second.Start(),c=third.Start();auto params=CefDictionaryValue::Create();int done=0;for(int i=0;i<128;++i){first.ReferenceSubmit(1,a,"session","Runtime.callFunctionOn",params,[]{return true;},[&](bool submitted,bool ok,const auto&){assert(submitted&&ok);++done;});second.ReferenceSubmit(1,b,"session","Runtime.callFunctionOn",params,[]{return true;},[&](bool submitted,bool ok,const auto&){assert(submitted&&ok);++done;});}assert(first.reference_dispatch_.Pending()==128&&second.reference_dispatch_.Pending()==128&&suitecut::SnapshotReferenceDispatch::ProcessPending()==256);int rejected=0;third.ReferenceSubmit(1,c,"session","Runtime.callFunctionOn",params,[]{return true;},[&](bool submitted,bool ok,const auto&){assert(!submitted&&!ok);++rejected;});assert(rejected==1&&!third.submissions);first.operations_.Finish(1);assert(suitecut::SnapshotReferenceDispatch::ProcessPending()==256);while(!first.devtools_.empty())first.Ack(first.devtools_.begin()->first);while(!second.devtools_.empty())second.Ack(second.devtools_.begin()->first);assert(done==256&&suitecut::SnapshotReferenceDispatch::ProcessPending()==0);}
  {Harness h;const auto generation=h.Start(1,10);const auto document=h.documents.at("main");auto params=CefDictionaryValue::Create();bool published=false;h.ReferenceSubmit(1,generation,"session","Runtime.callFunctionOn",params,[]{return true;},[&](bool submitted,bool ok,const auto&){assert(submitted&&ok);if(!h.ReferencePlanCurrent(1,generation,document))return;published=true;});h.now=10;h.Ack(1);assert(!published&&!h.operations_.Contains(1)&&h.errors.size()==1&&h.errors[0]=="Native operation timed out"&&!h.reference_dispatch_.Pending());}
  {Harness h;const auto generation=h.Start();assert(h.selectors.Admit(1,generation));h.evaluation_groups_[1].insert("session");h.evaluation_groups_[1].insert("");assert(h.ReferenceTemporaryGroups(1)&&h.evaluation_groups_.empty());const auto ticket=h.BeginGroup(1,generation);h.Install(ticket,1,generation);h.operations_.Finish(1);h.selectors.Cancel(1);h.SnapshotReferences().Cancel(1,generation,0);assert(h.released.empty()&&h.selectors.PhysicalSize()==1);h.Ack(1);assert(h.receipts==1&&h.released.size()==2);for(size_t i=0;i<h.release_ack.size();++i){auto ack=h.release_ack[i];ack();}assert(h.released.size()==3);for(const auto& group:h.released)assert(group=="suitecut-automation-1"||group.starts_with("suitecut-snapshot-"));assert(!h.selectors.Size()&&!h.SnapshotReferences().Groups());}
  {Harness h;h.Start();h.evaluation_groups_[1].insert("session");assert(!h.ReferenceTemporaryGroups(1)&&h.evaluation_groups_.size()==1);}
  {Harness h;const auto generation=h.Start();assert(h.selectors.Admit(1,generation));for(int i=0;i<63;++i)assert(h.selectors.Group(1,"existing-"+std::to_string(i),"session"));h.evaluation_groups_[1]={"first","second","third"};assert(!h.ReferenceTemporaryGroups(1)&&h.evaluation_groups_[1].size()==3);h.selectors.Cancel(1);for(size_t i=0;i<h.release_ack.size();++i){auto ack=h.release_ack[i];ack();}assert(h.released.size()==64&&!h.selectors.Size());}
  assert(!suitecut::SnapshotReferenceDispatch::ProcessPending()&&!suitecut::SnapshotReferences::GlobalInstallPhysical()&&!suitecut::SnapshotReferences::GlobalReleasePhysical()&&!suitecut::SnapshotReferences::GlobalResident());
  std::cout<<"PASS22 exact Source reference dispatch/scope wrappers: persistent ownership, actual ACK after cancellation/retry, no-submit rollback, synchronous ACK, staging read leases, scope, full pins, global physical caps; no actual browser-reference integration claim\n";
}
