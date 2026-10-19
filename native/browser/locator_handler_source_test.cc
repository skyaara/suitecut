// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "action_pauses.h"
#include "operation_registry.h"
#include "accepted_inspector_work.h"
#include "accepted_task_work.h"
#include "locator_handler_async_work.h"
#include "selector_wait_controls.h"
#include "inbound_string.h"
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
using Dict=CefRefPtr<CefDictionaryValue>;
template<class T>bool Contains(const std::vector<T>& values,const T& value){return std::find(values.begin(),values.end(),value)!=values.end();}
double clock_now=1;
struct Writer {
  struct ReplyRow {int id;bool ok;std::string error,json;};std::vector<ReplyRow> replies;
  void Reply(int id,bool ok,const std::string& error="",const std::vector<uint8_t>& bytes={}){replies.push_back({id,ok,error,std::string(bytes.begin(),bytes.end())});}
};
class Source;
struct ContextView {std::set<int> closing;bool PageClosePending(int page)const{return closing.contains(page);}bool ContextClosePending(int)const{return false;}};
class Engine {
 public:
  Writer& writer_;std::map<int,CefRefPtr<Source>> sources_,locator_handler_leases_;
  uint64_t locator_handler_invocation_=0;size_t wakes=0;
  struct LocatorHandlerCommand {suitecut::NativeWorkOrigin origin;int page;uint64_t generation;};
  std::map<int,LocatorHandlerCommand> locator_handler_commands_;
  std::shared_ptr<suitecut::AcceptedInspectorWork> inspector_work_=std::make_shared<suitecut::AcceptedInspectorWork>();
  std::shared_ptr<ContextView> contexts_=std::make_shared<ContextView>();
  explicit Engine(Writer& writer):writer_(writer){}
  void WakeTrace(){++wakes;}
  uint64_t RetainLocatorHandler(int,CefRefPtr<Source>);
  void WakeLocatorHandlers();void TickLocatorHandlers();void LocatorHandlerChannelClosed();
  bool LocatorHandlerAdmission(Dict,double&)const;bool LocatorHandlerControl(int,const std::string&,Dict);
  bool TrackLocatorHandlerCommand(int,int,uint64_t);void ForgetLocatorHandlerCommand(int);
  suitecut::NativeWorkOrigin LocatorHandlerCommandOrigin(int,int,uint64_t)const;
  bool LocatorHandlerDescendantsReady(uint64_t)const;
};
class Source:public CefBaseRefCounted {
 public:
  Writer& writer_;Engine& engine_;int page_id_;SuiteCutOperationRegistry operations_;
  struct Frame{};std::map<std::string,Frame> frame_states_{{"main",{}}};
  struct ObjectHandle{std::string frame_id,node_id;};std::map<int,ObjectHandle> handles_;
  std::map<int,std::function<void()>> context_retries_;std::set<int> context_retry_queued_;
  struct InputWork{suitecut::NativeWorkOrigin native_origin;};std::map<int,std::shared_ptr<InputWork>> input_work_;
  std::unique_ptr<suitecut::SelectorWaitControls> selector_waits_;
  struct Call{int owner;std::function<void(bool,const std::string&)> callback;suitecut::AcceptedInspectorWork::Ticket ticket;};
  std::map<int,Call> devtools_;int next_call=0;std::vector<Dict> events;size_t resumes=0;
  suitecut::AcceptedInspectorWork::Channel channel;
  Source(Writer& writer,Engine& engine,int page):writer_(writer),engine_(engine),page_id_(page){channel=*engine_.inspector_work_->Open(page,suitecut::AcceptedInspectorWork::Kind::Page);}
  double Now()const{return clock_now;}
  Dict Header(const char* type){auto d=CefDictionaryValue::Create();d->SetString("type",type);return d;}
  void SendPacket(Dict value){events.push_back(value->Copy(false));}
  void WakeDeadlines(){}
  std::string FrameId(Dict d){const auto frame=d->GetString("frame").ToString();return frame.empty()?"main":frame;}
  std::function<bool()> LocatorHandlerReferences(Dict,std::string&){return []{return true;};} // Actual snapshot identity controls live in Source.
  static bool ActionHookInteger(Dict d,const char* key,uint64_t& result){const auto type=d->GetType(key);if(type!=VTYPE_INT&&type!=VTYPE_DOUBLE)return false;const double n=type==VTYPE_INT?d->GetInt(key):d->GetDouble(key);if(!std::isfinite(n)||n<1||std::floor(n)!=n||n>SuiteCutActionPauseRegistry::kMaxWireInteger)return false;result=uint64_t(n);return true;}
  void Complete(int id,bool ok,const std::string& error=""){const auto op=operations_.Get(id);if(!op)return;const suitecut::NativeWorkOriginFrame scope(engine_.LocatorHandlerCommandOrigin(id,page_id_,op->generation));assert(operations_.Finish(id));engine_.ForgetLocatorHandlerCommand(id);LocatorHandlerCancelRemoval(id);LocatorHandlerCancel(id);if(selector_waits_)selector_waits_->Cancel(id);writer_.Reply(id,ok,error);}
  void ResolveFramePath(int,uint64_t,Dict d,size_t,const std::string&,std::function<void(Dict)> done,std::function<void()> unavailable){if(d->GetBool("missingFrame"))unavailable();else done(d);}
  void QueryInspect(int owner,Dict,const std::string&,bool,bool,std::function<void(bool,const std::string&)> callback){const int id=++next_call;const auto ticket=*engine_.inspector_work_->Reserve(channel,id,"");assert(engine_.inspector_work_->Dispatch(ticket,"")&&engine_.inspector_work_->SendReturned(ticket,true));devtools_.emplace(id,Call{owner,std::move(callback),ticket});}
  void EvaluateFixedNode(int owner,Dict target,const std::string& expression,std::function<void(bool,const std::string&)> callback){QueryInspect(owner,target,expression,true,true,std::move(callback));}
  void Observe(bool visible){assert(!devtools_.empty());auto it=devtools_.begin();auto call=std::move(it->second);devtools_.erase(it);assert(engine_.inspector_work_->ActualReply(channel,call.ticket.command,"")==suitecut::AcceptedInspectorWork::Reply::Observed);const auto frame=engine_.inspector_work_->BeginCallback(channel);call.callback(true,visible?R"({"result":{"value":true}})":R"({"result":{"value":false}})");assert(engine_.inspector_work_->CallbackReturned(frame));}
#include "locator_handler_source.inc"
  IMPLEMENT_REFCOUNTING(Source);
};
#include "locator_handler_engine.inc"
struct Fixture {
  Writer writer;Engine engine{writer};CefRefPtr<Source> a,b;
  Fixture(){clock_now=1;a=new Source(writer,engine,1);b=new Source(writer,engine,2);engine.sources_={{1,a},{2,b}};}
  Dict Register(Source& source,int request=100,int times=0,bool no_wait=true){auto target=CefDictionaryValue::Create();target->SetList("query",CefListValue::Create());target->SetBool("strict",true);auto d=CefDictionaryValue::Create();d->SetString("clientToken","actual-client");d->SetDictionary("target",target);d->SetBool("noWaitAfter",no_wait);if(times)d->SetInt("times",times);source.LocatorHandlerRegister(request,d);assert(writer.replies.back().ok);return d;}
  Dict Begin(Source& source,int id=1,double timeout=100){const auto op=source.operations_.Begin(id,clock_now,timeout);assert(op);auto config=CefDictionaryValue::Create();assert(source.LocatorHandlerBefore(id,op->generation,config,[&source]{++source.resumes;}));return config;}
  Dict Run(Source& source){source.Observe(true);assert(source.events.empty());source.LocatorHandlerTick();assert(source.events.size()==1&&source.events.back()->GetString("stage")=="locatorHandlerRun");auto identity=source.events.back()->Copy(false);identity->SetInt("target",identity->GetInt("id"));identity->SetInt("pageId",source.page_id_);return identity;}
  void Raw(Source& source,Dict identity,bool ok=true,bool pending=false){identity=identity->Copy(false);identity->SetBool("ok",ok);if(!ok)identity->SetString("error","controlled ordinary author failure");identity->SetBool("operationsPending",pending);assert(source.LocatorHandlerRaw(identity,false));}
  void Retire(Source& source){engine.TickLocatorHandlers();assert(source.events.back()->GetString("stage")=="locatorHandlerRetired");}
};
int main(){cef_api_hash(CEF_API_VERSION,0);size_t checks=0;
  {Fixture f;auto op=f.a->operations_.Begin(1,1,10);assert(!f.a->LocatorHandlerBefore(1,op->generation,CefDictionaryValue::Create(),[]{}));assert(f.a->devtools_.empty()&&f.engine.locator_handler_leases_.empty());++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);f.a->Observe(false);assert(f.a->resumes==1&&f.a->events.empty());++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);assert(!f.a->resumes);const auto owner=f.a->operations_.Get(1);assert(f.a->LocatorHandlerBefore(1,owner->generation,CefDictionaryValue::Create(),[]{assert(false);}));f.Raw(*f.a,id);f.Retire(*f.a);assert(f.a->resumes==1&&f.engine.locator_handler_leases_.empty());++checks;}
  {Fixture f;f.Register(*f.a,100,1);f.Begin(*f.a);auto id=f.Run(*f.a);f.Raw(*f.a,id);f.Retire(*f.a);assert(f.a->locator_handlers_.empty()&&f.a->events.back()->GetBool("removed"));++checks;}
  {Fixture f;f.Register(*f.a,100,0,false);f.Begin(*f.a);auto id=f.Run(*f.a);f.Raw(*f.a,id);f.Retire(*f.a);assert(!f.a->resumes&&f.a->devtools_.size()==1);f.a->Observe(true);assert(!f.a->resumes);clock_now+=21;f.a->LocatorHandlerTick();f.a->Observe(false);assert(f.a->resumes==1);++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);auto op=f.a->operations_.Begin(2,1,10);assert(!f.a->LocatorHandlerBefore(2,op->generation,CefDictionaryValue::Create(),[]{}));f.a->Complete(2,true);f.Raw(*f.a,id);f.Retire(*f.a);++checks;}
  {Fixture f;f.Register(*f.a);f.Register(*f.b,101);f.Begin(*f.a);auto a=f.Run(*f.a);f.Begin(*f.b,2);f.Run(*f.b);f.Raw(*f.a,a);f.Retire(*f.a);assert(f.a->resumes==1&&f.b->locator_handler_active_&&!f.b->locator_handler_active_->raw);f.engine.LocatorHandlerChannelClosed();++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);assert(f.b->operations_.Begin(99,1,0));f.b->context_retries_[99]=[]{};f.Raw(*f.a,id);f.Retire(*f.a);assert(f.b->operations_.Contains(99)&&f.a->resumes==1);++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a,1,10);auto id=f.Run(*f.a);clock_now=12;f.a->LocatorHandlerTick();assert(f.a->locator_handler_active_&&f.a->events.back()->GetString("stage")=="locatorHandlerAbort"&&!f.a->operations_.Contains(1));f.Raw(*f.a,id);f.Retire(*f.a);assert(!f.a->resumes);++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);f.Raw(*f.a,id,false);f.Retire(*f.a);assert(!f.a->resumes&&!f.a->operations_.Contains(1)&&!f.writer.replies.back().ok);++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);assert(f.a->LocatorHandlerOperationControl(110,id,false));auto response=CefParseJSON(f.writer.replies.back().json,JSON_PARSER_RFC)->GetDictionary();uint64_t token=0;assert(Source::ActionHookInteger(response,"operationToken",token));auto origin=id->Copy(false);origin->SetInt("page",1);origin->SetDouble("operationToken",token);double deadline=0;assert(f.a->LocatorHandlerOrigin(origin,deadline)&&deadline==101);f.Raw(*f.a,id,true,true);assert(!f.a->LocatorHandlerOperationControl(111,id,false));f.engine.TickLocatorHandlers();assert(f.a->locator_handler_active_);id->SetDouble("operationToken",token);assert(f.a->LocatorHandlerOperationControl(112,id,true));assert(f.a->LocatorHandlerRaw(id,true));f.Retire(*f.a);++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);const auto invocation=f.a->locator_handler_active_->token;auto origin=suitecut::CurrentNativeWorkOrigin();origin.locator_handler_invocation=invocation;{const suitecut::NativeWorkOriginFrame scope(origin);assert(f.engine.TrackLocatorHandlerCommand(20,2,9));}f.Raw(*f.a,id);f.engine.TickLocatorHandlers();assert(!f.a->resumes);assert(f.engine.LocatorHandlerCommandOrigin(20,2,8).locator_handler_invocation==0&&f.engine.LocatorHandlerCommandOrigin(20,2,9).locator_handler_invocation==invocation);f.engine.ForgetLocatorHandlerCommand(20);f.Retire(*f.a);++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);const auto invocation=f.a->locator_handler_active_->token;auto origin=suitecut::CurrentNativeWorkOrigin();origin.locator_handler_invocation=invocation;std::unique_ptr<suitecut::AcceptedTaskReceipt> held;{const suitecut::NativeWorkOriginFrame scope(origin);held=std::make_unique<suitecut::AcceptedTaskReceipt>();}f.Raw(*f.a,id);f.engine.TickLocatorHandlers();assert(!f.a->resumes);held.reset();f.Retire(*f.a);++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);f.a->locator_handler_active_->close_pages.insert(1);f.engine.contexts_->closing.insert(1);f.a->Complete(1,false,"actual page close");f.engine.sources_.erase(1);f.a->frame_states_.clear();f.Raw(*f.a,id);f.engine.TickLocatorHandlers();assert(f.a->locator_handler_active_);f.engine.contexts_->closing.erase(1);f.Retire(*f.a);assert(!f.a->resumes);++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);auto remove=id->Copy(false);remove->SetDouble("invocationToken",id->GetDouble("token"));assert(f.a->operations_.BeginControl(120,1,20));f.a->LocatorHandlerRemove(120,remove);assert(!f.writer.replies.back().ok&&!f.a->locator_handlers_.begin()->second->enabled&&f.a->locator_handler_active_);f.Raw(*f.a,id);f.Retire(*f.a);assert(f.a->locator_handlers_.empty());++checks;}
  {Fixture f;f.Register(*f.a);f.Begin(*f.a);auto id=f.Run(*f.a);assert(f.a->operations_.BeginControl(120,1,20));f.a->LocatorHandlerRemove(120,id);assert(f.a->locator_handler_removals_.size()==1&&f.a->operations_.Contains(120));f.a->Complete(120,false,"actual remover cancellation");assert(f.a->locator_handler_removals_.empty()&&f.a->locator_handler_active_);f.Raw(*f.a,id);f.Retire(*f.a);++checks;}
  std::cout<<"Native locator handlers: "<<checks<<" included Source/Engine controls PASS; controlled visibility/close/backend ingress, no live browser claim\n";
}
