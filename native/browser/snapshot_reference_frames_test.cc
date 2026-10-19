// SPDX-License-Identifier: MIT
// Production frame-coordinator include with controlled Inspector/capture
// boundaries and the real reference registry. This is not a live-browser proof.
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "native_aria_json.h"
#include "native_aria_formatter.h"
#include "operation_registry.h"
#include "snapshot_reference_capture.h"
#include "snapshot_reference_frames.h"
#include "snapshot_reference_projection.h"
#include "snapshot_reference_publish.h"
class Task {public:explicit Task(std::function<void()> run):run_(std::move(run)){}void Run(){run_();}private:std::function<void()>run_;};
std::vector<std::unique_ptr<Task>> tasks;
bool CefPostDelayedTask(int,Task*task,int){tasks.emplace_back(task);return true;}
class Harness final:public CefBaseRefCounted {
 public:
 using Source=Harness;using Dict=CefRefPtr<CefDictionaryValue>;
 using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;
 using SnapshotReferenceCapture=suitecut::SnapshotReferenceCapture;
 struct Frame {std::string parent;SnapshotReferenceDocument document;};
 struct Call {std::shared_ptr<SnapshotReferenceCapture> work;std::string method;Dict params;std::function<void(Dict)> done;std::shared_ptr<void> borrow;};
 struct Capture {Dict config;std::function<void(const std::string&)> done;};
 SuiteCutOperationRegistry operations_;std::map<std::string,Frame>frame_states_;
 std::vector<Call>calls;std::vector<Capture>captures;
 std::vector<std::function<void(bool,const std::string&)>>release_ack;
 std::vector<std::shared_ptr<SnapshotReferenceCapture>>install;
 std::vector<std::string>errors,responses;double now=0;bool hold_publication=false;std::string held_result;
 suitecut::SnapshotReferences refs{"actualFrameHost",[this](int owner,uint64_t generation,const auto&doc){const auto current=ReferenceDocument(doc.frame);return operations_.IsCurrent(owner,generation)&&!operations_.IsExpired(owner,generation,now)&&current&&*current==doc;},[this](const auto&,auto done){release_ack.push_back(std::move(done));},[this](const auto&error){errors.push_back(error);}};
 Harness(){for(const auto frame:{"main","child","nested"})frame_states_[frame]={std::string(frame)=="main"?"":std::string(frame)=="child"?"main":"child",Doc(frame)};}
 static SnapshotReferenceDocument Doc(const std::string& frame){return{frame,frame=="main"?"":"actualOOPIF",frame+"Loader",frame+"Main",frame+"Utility",4,5,7};}
 double Now()const{return now;}
 suitecut::SnapshotReferences& SnapshotReferences(){return refs;}
 std::optional<SnapshotReferenceDocument> ReferenceDocument(const std::string& frame){const auto found=frame_states_.find(frame);if(found==frame_states_.end()||found->second.document.main<=0||found->second.document.utility<=0||found->second.document.main_unique.empty()||found->second.document.utility_unique.empty())return{};return found->second.document;}
 void EnsureUtilityWorld(const std::string&){}
 std::string Json(Dict dict){auto value=CefValue::Create();value->SetDictionary(dict);return CefWriteJSON(value,JSON_WRITER_DEFAULT);}
 void Complete(int owner,bool ok,const std::string&error="",const std::string&json=""){
  if(ok&&!ReferenceFrameOperationCurrent(owner))return;
  if(ok&&hold_publication){held_result=json;return;}
  if(ok&&!PublishReferenceFrameBatch(owner))return;
  const auto operation=operations_.Get(owner);if(!operation||!operations_.Finish(owner))return;
  if(ok)responses.push_back(json);else errors.push_back(error);
  refs.Cancel(owner,operation->generation,now);reference_frame_batches_.erase(owner);
 }

#include "snapshot_reference_current.inc"
 void ReferenceRequest(const std::shared_ptr<SnapshotReferenceCapture>&work,const std::string&method,Dict params,std::function<void(Dict)>done){
  if(!ReferenceCurrent(work))return;
  if(work->before_request&&!work->before_request())return;
  assert(++work->requests<=work->request_limit);auto borrow=refs.Use(work->ticket);if(work->ticket)assert(borrow);
  calls.push_back({work,method,params,std::move(done),std::move(borrow)});
 }
 bool ReferenceTemporaryGroups(int){return true;}
 void CaptureDOMSemantic(int,Dict config,std::function<void(const std::string&,const std::string&)>,std::function<void(const std::string&)>done){captures.push_back({config,std::move(done)});}
 void ReadReferenceSnapshot(const std::shared_ptr<SnapshotReferenceCapture>& work){
  const int backend=work->document.frame=="child"?20:30;
  work->ticket=refs.Begin(work->owner,work->generation,work->document,{{backend,1}},1000,now);assert(work->ticket&&refs.Dispatched(work->ticket));install.push_back(work);
 }
 void Installed(){assert(!install.empty());auto work=install.front();install.erase(install.begin());const bool retained=refs.Installed(work->ticket,true,"actual-child-retained-array",now);if(!ReferenceCurrent(work))return;assert(retained);auto done=std::move(work->installed_done);done();}
 void Ack(Dict result){assert(!calls.empty());auto call=std::move(calls.front());calls.erase(calls.begin());if(ReferenceCurrent(call.work))call.done(result);call.borrow.reset();}
 void String(const std::string& value){auto result=CefDictionaryValue::Create(),remote=CefDictionaryValue::Create();remote->SetString("value",value);result->SetDictionary("result",remote);Ack(result);}
 void Count(int count=1){auto result=CefDictionaryValue::Create(),remote=CefDictionaryValue::Create();remote->SetInt("value",count);result->SetDictionary("result",remote);Ack(result);}
 void Node(const std::string& frame="child",int backend=10){auto result=CefDictionaryValue::Create(),node=CefDictionaryValue::Create();node->SetInt("backendNodeId",backend);node->SetInt("nodeType",1);node->SetString("nodeName","IFRAME");if(!frame.empty())node->SetString("frameId",frame);result->SetDictionary("node",node);Ack(result);}
 std::shared_ptr<SnapshotReferenceCapture> Start(bool json=true){const auto op=operations_.Begin(1,0,1000);assert(op);auto work=std::make_shared<SnapshotReferenceCapture>();work->owner=1;work->generation=op->generation;work->document=Doc("main");work->json=json;work->ticket=refs.Begin(1,op->generation,work->document,{{10,1}},1000,0);assert(work->ticket&&refs.Dispatched(work->ticket)&&refs.Installed(work->ticket,true,"actual-root-array",0));BeginReferenceFrameBatch(work);return work;}
 uint64_t Old(const std::string& frame="main"){const auto op=operations_.Begin(2,now,1000);assert(op);const auto ticket=refs.Begin(2,op->generation,Doc(frame),{{99,1}},1000,now);assert(ticket&&refs.Dispatched(ticket)&&refs.Installed(ticket,true,"actual-previous-array",now)&&refs.Commit(ticket,now));operations_.Finish(2);return ticket;}
 std::string Forest(const std::shared_ptr<SnapshotReferenceCapture>&work,const std::string&role){return "[{\"role\":\""+role+"\",\"ref\":\""+refs.Prefix(work->ticket)+"0\"}]";}
 void AckReleases(){for(size_t i=0;i<release_ack.size();++i)release_ack[i](true,"");}
 void Close(){calls.clear();captures.clear();install.clear();tasks.clear();refs.Destroyed();assert(!refs.Resident()&&!refs.Borrows()&&!refs.InstallPhysical()&&!refs.Physical());}
#include "snapshot_reference_frames.inc"
 private:IMPLEMENT_REFCOUNTING(Harness);
};
int main(){cef_api_hash(CEF_API_VERSION,0);int groups=0;
 {CefRefPtr<Harness> h=new Harness();auto root=h->Start();h->String(h->Forest(root,"iframe"));assert(h->calls.size()==1&&h->calls[0].method=="DOM.describeNode"&&h->calls[0].work->document.session.empty()&&h->calls[0].params->GetInt("backendNodeId")==10);h->Node();assert(h->captures.size()==1&&h->captures[0].config->GetString("frame")=="child");auto capture=std::move(h->captures[0]);h->captures.clear();capture.done("actual-child-capture");h->Count();h->Installed();auto child=h->reference_frame_batches_.at(1)->frames[1].work;h->String(h->Forest(child,"button"));assert(h->calls[0].params->GetString("functionDeclaration")==kSuiteCutSnapshotReferenceMerge);const auto forest="[{\"role\":\"iframe\",\"ref\":\""+h->refs.Prefix(root->ticket)+"0\",\"children\":"+h->Forest(child,"button")+"}]";h->String(forest);assert(h->refs.Active()==0);h->String("[{\"role\":\"iframe\",\"children\":[{\"role\":\"button\"}]}]");assert(h->errors.empty()&&h->responses.size()==1&&h->refs.Active()==2&&h->refs.Reference(root->ticket,0).size()&&h->refs.Reference(child->ticket,0).size()&&h->reference_frame_batches_.empty());h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();auto root=h->Start();h->String(h->Forest(root,"iframe"));h->Node("child",999);assert(h->responses.empty()&&h->captures.empty()&&h->errors.size()==1&&h->refs.Active()==0);h->AckReleases();h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();h->frame_states_["child"].parent="other";auto root=h->Start();h->String(h->Forest(root,"iframe"));h->Node();assert(h->captures.empty()&&h->errors.size()==1&&h->refs.Active()==0);h->AckReleases();h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();auto root=h->Start();h->String(h->Forest(root,"iframe"));h->Node("");assert(tasks.size()==1&&h->calls.empty());h->now=1000;auto task=std::move(tasks.front());tasks.clear();task->Run();assert(h->errors.size()==1&&h->errors[0]=="Native operation timed out"&&h->calls.empty()&&h->refs.Active()==0);h->AckReleases();h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();auto root=h->Start();h->String(h->Forest(root,"iframe"));h->frame_states_["main"].document.main_unique="newUniqueSameNumeric";h->Node();assert(h->captures.empty()&&h->errors.size()==1&&h->refs.Active()==0);h->AckReleases();h->Close();++groups;}
 for(bool late:{false,true}){CefRefPtr<Harness> h=new Harness();const auto old=h->Old();const auto token=h->refs.Reference(old,0);auto root=h->Start();h->String(h->Forest(root,"button"));assert(h->refs.Active()==1&&h->calls.size()==1);if(late)h->now=1000;h->String(late?"[]":"{}");assert(h->responses.empty()&&h->errors.size()==1&&h->refs.Active()==1&&h->refs.Lookup(token,Harness::Doc("main")));h->AckReleases();h->Close();}++groups;
 {CefRefPtr<Harness> h=new Harness();const auto old=h->Old();const auto token=h->refs.Reference(old,0);auto root=h->Start();h->String(h->Forest(root,"iframe"));h->Node();auto capture=std::move(h->captures.front());h->captures.clear();capture.done("actual-child-capture");h->Count();assert(h->refs.InstallPhysical()==1);h->Complete(1,false,"Actual caller abort");assert(h->refs.InstallPhysical()==1&&h->refs.Active()==1);h->Installed();assert(h->refs.InstallPhysical()==0&&h->responses.empty()&&h->refs.Lookup(token,Harness::Doc("main"))&&h->calls.empty());h->AckReleases();h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();h->frame_states_["child"].document.utility=0;auto root=h->Start();h->String(h->Forest(root,"iframe"));h->Node();assert(tasks.size()==1&&h->captures.empty());h->frame_states_["child"].document.utility=5;auto task=std::move(tasks.front());tasks.clear();task->Run();assert(h->captures.size()==1&&h->captures.front().config->GetString("frame")=="child");h->Complete(1,false,"Controlled end");h->AckReleases();h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();const auto old=h->Old();const auto token=h->refs.Reference(old,0);auto root=h->Start();h->String(h->Forest(root,"iframe"));h->Node();auto capture=std::move(h->captures.front());h->captures.clear();capture.done("actual-empty-child-capture");h->Count(0);assert(tasks.size()==1&&h->install.empty());h->frame_states_["child"].document.loader="actualNavigation";auto task=std::move(tasks.front());tasks.clear();task->Run();assert(h->captures.empty()&&h->responses.empty()&&h->errors.size()==1&&h->refs.Lookup(token,Harness::Doc("main")));h->AckReleases();h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();auto root=h->Start();assert(root->ticket);h->String("[{\"role\":\"button\",\"ref\":\"rForeign_1_0\"}]");assert(h->responses.empty()&&h->errors.size()==1&&h->refs.Active()==0);h->AckReleases();h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();h->frame_states_.erase("child");auto root=h->Start();h->String(h->Forest(root,"iframe"));h->Node();assert(tasks.size()==1&&h->captures.empty()&&h->errors.empty());h->frame_states_["child"]={"main",Harness::Doc("child")};auto task=std::move(tasks.front());tasks.clear();task->Run();assert(h->captures.size()==1&&h->errors.empty());h->Complete(1,false,"Controlled end");h->AckReleases();h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();h->frame_states_["child"].document.utility=0;auto root=h->Start();h->String(h->Forest(root,"iframe"));h->Node();assert(tasks.size()==1&&h->captures.empty());h->frame_states_.erase("child");auto task=std::move(tasks.front());tasks.clear();task->Run();assert(h->responses.empty()&&h->errors.size()==1&&h->errors[0].find("detached")!=std::string::npos&&h->captures.empty());h->AckReleases();h->Close();++groups;}
 for(int condition=0;condition<3;++condition){CefRefPtr<Harness> h=new Harness();const auto old=h->Old();const auto token=h->refs.Reference(old,0);auto root=h->Start();h->String(h->Forest(root,"iframe"));h->Node();auto capture=std::move(h->captures.front());h->captures.clear();capture.done("actual-held-child-capture");h->Count();h->Installed();auto child=h->reference_frame_batches_.at(1)->frames[1].work;h->String(h->Forest(child,"button"));const auto forest="[{\"role\":\"iframe\",\"ref\":\""+h->refs.Prefix(root->ticket)+"0\",\"children\":"+h->Forest(child,"button")+"}]";h->String(forest);h->hold_publication=true;h->String("[{\"role\":\"iframe\",\"children\":[{\"role\":\"button\"}]}]");assert(h->refs.Active()==1&&h->responses.empty()&&h->operations_.Contains(1)&&!h->held_result.empty()&&h->refs.Lookup(token,Harness::Doc("main"))&&h->reference_frame_batches_.size()==1);if(condition==1)h->frame_states_["child"].document.loader="actual-new-child-loader";if(condition==2)h->now=1000;h->hold_publication=false;h->Complete(1,true,"",h->held_result);if(!condition){assert(h->refs.Active()==2&&h->responses.size()==1&&!h->refs.Lookup(token,Harness::Doc("main")));}else{assert(h->refs.Active()==1&&h->responses.empty()&&h->errors.size()==1&&h->refs.Lookup(token,Harness::Doc("main")));if(condition==2)assert(h->errors[0]=="Native operation timed out");}assert(h->reference_frame_batches_.empty());h->AckReleases();h->Close();++groups;}
 // YAML uses the same actual multi-frame coordinator and final CommitBatch.
 {CefRefPtr<Harness> h=new Harness();auto root=h->Start(false);root->boxes=true;root->depth=3;h->String(h->Forest(root,"iframe"));h->Node();assert(!h->captures.front().config->HasKey("referenceFormat"));auto capture=std::move(h->captures.front());h->captures.clear();capture.done("actual-yaml-child-capture");h->Count();h->Installed();auto child=h->reference_frame_batches_.at(1)->frames[1].work;assert(!child->json&&child->boxes&&child->depth==3);h->String(h->Forest(child,"button"));const auto forest="[{\"role\":\"iframe\",\"ref\":\""+h->refs.Prefix(root->ticket)+"0\",\"children\":"+h->Forest(child,"button")+"}]";h->String(forest);const auto fn=h->calls.front().params->GetString("functionDeclaration").ToString();assert(fn.find(kSuiteCutNativeAriaFormatter)!=std::string::npos&&fn.find("boxes:true,depth:3")!=std::string::npos);const std::string yaml="- iframe [ref="+h->refs.Prefix(root->ticket)+"0]:\n  - button [ref="+h->refs.Prefix(child->ticket)+"0]";auto value=CefValue::Create();value->SetString(yaml);h->String(CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString());assert(h->errors.empty()&&h->refs.Active()==2&&h->responses.size()==1);const auto envelope=CefParseJSON(h->responses.front(),JSON_PARSER_RFC)->GetDictionary();assert(envelope->GetType("yamlJSON")==VTYPE_STRING&&!envelope->HasKey("snapshotJSON"));assert(CefParseJSON(envelope->GetString("yamlJSON"),JSON_PARSER_RFC)->GetString()==yaml);h->Close();++groups;}
 for(bool invalid:{false,true}){CefRefPtr<Harness> h=new Harness();const auto old=h->Old();const auto token=h->refs.Reference(old,0);auto root=h->Start(false);h->String(h->Forest(root,"button"));h->String(invalid?"[]":"\""+std::string(393218,'x')+"\"");assert(h->responses.empty()&&h->errors.size()==1&&h->refs.Active()==1&&h->refs.Lookup(token,Harness::Doc("main")));h->AckReleases();h->Close();++groups;}
 {CefRefPtr<Harness> h=new Harness();auto root=h->Start(false);h->String(h->Forest(root,"button"));h->hold_publication=true;h->String("\"- button\"");assert(h->refs.Active()==0&&h->responses.empty()&&h->reference_frame_batches_.size()==1);h->hold_publication=false;h->Complete(1,true,"",h->held_result);assert(h->refs.Active()==1&&h->responses.size()==1&&h->reference_frame_batches_.empty());h->Close();++groups;}
 assert(suitecut::SnapshotReferences::GlobalResident()==0&&suitecut::SnapshotReferences::GlobalInstallPhysical()==0&&suitecut::SnapshotReferences::GlobalReleasePhysical()==0);
 std::cout<<"Native frame snapshot coordinator "<<groups<<" controlled groups PASS; compiled browser pipeline pending\n";
}
