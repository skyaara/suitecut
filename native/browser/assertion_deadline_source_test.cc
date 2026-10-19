// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "assertion_deadline_controls.h"
#include "devtools_utf16.h"
#include "operation_registry.h"
#include "selector_wait_controls.h"
#include "snapshot_reference_controls.h"
#include <cassert>
#include <iostream>
#include <optional>
struct Host {
  using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;
  using Dict=CefRefPtr<CefDictionaryValue>;using Callback=std::function<void(bool,const std::string&)>;
  struct DevToolsCall{int owner;Callback callback;std::string session;bool retain_after_cancel=false;size_t response_limit=0;bool retained_control=false,selector_group_release=false;};
  static constexpr size_t kMaxResponseBytes=192*1048576;
  static inline double clock=0;double& now=clock;bool browser_=true;int next=0;
  std::map<int,DevToolsCall>devtools_;std::set<std::string>sessions_;
  SuiteCutOperationRegistry operations_;
std::optional<SnapshotReferenceDocument> document=SnapshotReferenceDocument{"main","","loader","mainUnique","utilityUnique",1,2,1};
  int publications=0,wakes=0;bool success=false;std::string error,payload;std::vector<std::string>fatal;
  std::map<int,std::string>expect_results_;std::map<int,double>first_observations_;
  std::vector<std::function<void()>>release_acks;
  Host(){clock=0;}
  int AllocateDevToolsId(){return ++next;}
  bool SubmitDevTools(int id,const std::string& method,Dict,const std::string&){
    assert(method=="Runtime.releaseObjectGroup");
    release_acks.push_back([this,id]{auto callback=std::move(devtools_.at(id).callback);devtools_.erase(id);callback(true,"{}");});return true;
  }
  void NativeTargetSessionDetached(const std::string& session){sessions_.erase(session);}
  std::vector<int> screenshot_release_failures;
  void ScreenshotAssertionReleaseFailed(int owner){screenshot_release_failures.push_back(owner);}
#include "selector_wait_controller.inc"
  bool ReferenceTemporaryGroups(int id){return SelectorWaits().Group(id,"suitecut-automation-"+std::to_string(id),"");}
  std::optional<SnapshotReferenceDocument>ReferenceDocument(const std::string&)const{return document;}
  static double Now(){return clock;}
  void WakeDeadlines(){++wakes;}
  void NetworkFatal(const std::string&e){fatal.push_back(e);}
  static std::string Json(CefRefPtr<CefDictionaryValue>value){auto wrapper=CefValue::Create();wrapper->SetDictionary(value);return CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).ToString();}
  void Complete(int id,bool ok,const std::string&e="",const std::string&json=""){
    if(ok&&!AssertionDeadlinePublicationCurrent(id))return;
    assert(operations_.Contains(id));AssertionDeadlineForget(id);assert(operations_.Finish(id));
    ++publications;success=ok;error=e;payload=json;expect_results_.erase(id);SelectorWaits().Cancel(id);
  }
  uint64_t Start(double timeout=40){const auto entry=operations_.Begin(1,now,timeout);assert(entry&&document);assert(AssertionDeadlineDispatch(1,entry->generation,"toHaveText","main"));first_observations_[1]=now+1000;return entry->generation;}
  void AckRelease(){assert(!release_acks.empty());auto ack=std::move(release_acks.front());release_acks.erase(release_acks.begin());ack();}
#include "expectation_deadline.inc"
#include "assertion_deadline_source.inc"
};
const std::string actual=R"({"result":{"type":"object","value":{"waiting":false,"value":{"matches":false,"count":1,"actual":["primitive","late actual"],"preview":"p#status"}}}})";
const std::string waiting=R"({"result":{"type":"object","value":{"waiting":true}}})";
int main(){cef_api_hash(CEF_API_VERSION,0);
  // Hold the genuine first query, then the actual group release. A native
  // deadline decision must not make either physical ownership count zero.
  {Host h;auto generation=h.Start();auto query=h.SelectorWaits().Begin(1);assert(query);h.now=40;h.AssertionDeadlineTick();assert(!h.publications&&h.SelectorWaits().PhysicalSize()==1&&h.release_acks.empty());h.now=55;query->Settle();assert(h.AssertionDeadlineObserve(1,generation,true,actual));assert(h.SelectorWaits().PhysicalSize()==1&&h.release_acks.size()==1);h.AssertionDeadlineTick();assert(!h.publications);h.AckRelease();assert(!h.SelectorWaits().PhysicalSize());h.AssertionDeadlineTick();assert(h.publications==1&&!h.success&&h.payload.find("native-assertion-deadline")!=std::string::npos&&h.payload.find("late actual")==std::string::npos&&h.payload.find("Received")==std::string::npos);assert(!h.operations_.Size()&&!h.SelectorWaits().Size()&&!h.assertion_deadlines_.Size());}
  {Host h;auto generation=h.Start();auto query=h.SelectorWaits().Begin(1);h.now=40;h.AssertionDeadlineTick();query->Settle();assert(h.AssertionDeadlineObserve(1,generation,true,waiting));h.AckRelease();h.AssertionDeadlineTick();assert(!h.payload.empty()&&!h.success);}
  {Host h;auto generation=h.Start();auto query=h.SelectorWaits().Begin(1);h.now=41;h.AssertionDeadlineTick();query->Settle();assert(!h.AssertionDeadlineObserve(1,generation,true,"not JSON"));h.AckRelease();h.AssertionDeadlineTick();assert(h.payload.empty()&&h.error.find("protocol response failed")!=std::string::npos);}
  {Host h;auto generation=h.Start();auto query=h.SelectorWaits().Begin(1);h.now=41;h.AssertionDeadlineTick();query->Settle();assert(!h.AssertionDeadlineObserve(1,generation,true,R"({"exceptionDetails":{"text":"Actual selector syntax error"}})"));h.AckRelease();h.AssertionDeadlineTick();assert(h.payload.empty()&&h.error=="Actual selector syntax error");}
  {Host h;auto generation=h.Start();auto query=h.SelectorWaits().Begin(1);h.now=40;h.AssertionDeadlineTick();query->Settle();assert(!h.AssertionDeadlineObserve(1,generation,true,R"({"result":{"type":"object","value":{"strictError":"Actual strict node ambiguity"}}})"));h.AckRelease();h.AssertionDeadlineTick();assert(h.payload.empty()&&h.error=="Actual strict node ambiguity");}
  {Host h;auto generation=h.Start();auto query=h.SelectorWaits().Begin(1);h.now=40;h.AssertionDeadlineTick();query->Settle();h.document->utility_unique="reused-context-number";assert(!h.AssertionDeadlineObserve(1,generation,true,actual));h.AckRelease();h.AssertionDeadlineTick();assert(h.error=="Native assertion document changed"&&h.payload.empty());}
  {Host h;auto generation=h.Start();auto query=h.SelectorWaits().Begin(1);h.now=40;h.AssertionDeadlineTick();query->Settle();assert(h.AssertionDeadlineObserve(1,generation,true,actual));assert(h.devtools_.size()==1);auto callback=std::move(h.devtools_.begin()->second.callback);h.devtools_.clear();h.release_acks.clear();callback(false,"{}");h.AssertionDeadlineTick();assert(h.error=="Native selector wait object-group release failed"&&h.payload.empty()&&h.fatal.size()==1);}
  {Host h;h.Start();auto query=h.SelectorWaits().Begin(1);h.now=40;h.AssertionDeadlineTick();h.now=1040;h.AssertionDeadlineTick();assert(h.fatal.size()==1&&h.payload.empty()&&h.SelectorWaits().PhysicalSize()==1);query->Settle();h.AckRelease();assert(!h.SelectorWaits().PhysicalSize()&&!h.SelectorWaits().Size());}
  {Host h;auto generation=h.Start();auto query=h.SelectorWaits().Begin(1);h.Complete(1,false,"Actual caller abort");h.now=50;query->Settle();assert(h.AssertionDeadlineObserve(1,generation,true,actual));h.AckRelease();h.AssertionDeadlineTick();assert(h.publications==1&&h.error=="Actual caller abort"&&h.payload.empty());}
  {Host h;auto old=h.Start();h.Complete(1,false,"old canceled");h.AckRelease();auto fresh=h.Start();assert(old!=fresh);assert(h.AssertionDeadlineObserve(1,old,true,actual));assert(!h.assertion_deadlines_.Get(1,fresh)->validated&&h.operations_.IsCurrent(1,fresh));h.Complete(1,false,"fresh canceled");h.AckRelease();}
  {Host h;auto generation=h.Start();h.expect_results_[1]="genuine cached mismatch bytes";h.now=40;assert(h.AssertionDeadlineExpire(1,generation));assert(h.success&&h.payload=="genuine cached mismatch bytes");h.AckRelease();}
  {Host h;auto generation=h.Start();assert(h.AssertionDeadlineObserve(1,generation,true,actual));h.now=39;h.AssertionDeadlineTick();assert(!h.publications&&!h.SelectorWaits().Retrying(1));h.Complete(1,true,"","genuine predeadline result");h.AckRelease();}
  {Host h;h.Start();assert(h.operations_.Finish(1));h.AssertionDeadlineTick();assert(!h.assertion_deadlines_.Size()&&h.assertion_deadline_documents_.empty());h.SelectorWaits().Cancel(1);h.AckRelease();}
  {Host h;auto generation=h.Start();auto query=h.SelectorWaits().Begin(1);h.now=40;h.AssertionDeadlineTick();query->Settle();assert(!h.AssertionDeadlineObserve(1,generation,false,R"({"error":{"code":-32000,"message":"Actual context lost"}})"));h.AckRelease();h.AssertionDeadlineTick();assert(h.payload.empty()&&h.error.find("Actual context lost")!=std::string::npos);}
  {Host h;auto generation=h.Start();assert(h.AssertionDeadlineObserve(1,generation,true,actual));h.now=30;assert(h.assertion_deadlines_.Admitted(1,generation));h.now=40;h.Complete(1,true,"","actual predeadline success held by publication");assert(!h.publications);h.AckRelease();h.AssertionDeadlineTick();assert(h.publications==1&&!h.success&&h.error=="Native assertion result publication exceeded its original deadline"&&h.payload.empty());}
  {Host h;h.document.reset();const auto entry=h.operations_.Begin(1,0,40);assert(entry&&!h.assertion_deadlines_.Size());h.now=10;h.document=Host::SnapshotReferenceDocument{"main","","loader","mainUnique","utilityUnique",1,2,1};assert(h.AssertionDeadlineDispatch(1,entry->generation,"toHaveText","main")&&h.assertion_deadlines_.Get(1,entry->generation)->deadline==40);h.Complete(1,false,"Actual caller abort after context admission");h.AckRelease();}
  {Host h;const auto entry=h.operations_.Begin(1,0,40);assert(entry);h.now=40;assert(!h.AssertionDeadlineDispatch(1,entry->generation,"toHaveText","main"));assert(h.publications==1&&h.payload.empty()&&h.error=="Native operation timed out before native assertion query dispatch"&&!h.assertion_deadlines_.Size()&&h.devtools_.empty());}
  {Host h;auto old=h.Start();assert(h.operations_.Finish(1));const auto fresh=h.operations_.Begin(1,0,80);assert(fresh&&fresh->generation!=old);h.AssertionDeadlineTick();assert(!h.assertion_deadlines_.Size()&&h.operations_.IsCurrent(1,fresh->generation)&&!h.publications);h.Complete(1,false,"Actual fresh command abort");h.AckRelease();}
  std::cout<<"PASS18 actual included Source assertion-deadline/CEF schema/SelectorWait held-query and release-ACK controls; no compiled browser feature claim\n";
}
