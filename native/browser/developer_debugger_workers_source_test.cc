// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <set>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "developer_debugger_worker_targets.h"
#include "developer_debugger_worker_pause.h"
#include "coverage_script_inventory.h"
using Dict=CefRefPtr<CefDictionaryValue>;
static double clock_now=1;
static double Now(){return clock_now;}
static Dict JSON(const std::string&s){const auto v=CefParseJSON(s,JSON_PARSER_RFC);assert(v);return v->GetDictionary();}
// Actual production Source adapter + controllers and CEF graphs. Inspector
// transport/worker observations are controlled, not a running browser proof.
class Source{
 public:
  using Domains=suitecut::DebuggerDomains;
  struct WorkerTarget{std::string id,session,parent_session,kind;int context=0;uint64_t generation=0;bool published=false,bootstrap_failed=false,context_destroyed=false;};
  struct Operation{uint64_t generation;double deadline_ms;};
  struct Operations{std::map<int,std::shared_ptr<Operation>>items;auto Get(int id)const{auto i=items.find(id);return i==items.end()?std::shared_ptr<Operation>{}:i->second;}bool IsCurrent(int id,uint64_t gen)const{auto p=Get(id);return p&&p->generation==gen;}}operations_;
  struct Result{bool ok;std::string error,json;};
  struct Call{std::string session,method;Dict params;Domains::Reply reply;};
  std::vector<Call>calls;std::map<int,Result>results;std::vector<std::string>fatal;
  uint64_t next_lease=0,next_op=0,native_frame_debugger_observer_=4;
  std::function<bool(int)>worker_debugger_retains_;std::set<int>native_debugger_calls_;
  bool observer_=true,native_frame_debugger_registering_=false,native_frame_debugger_registration_traffic_=false,closing_page_=false,native_debugger_closing_requested_=false;
  std::string handle_namespace_="controlled-worker-source";
  std::map<std::string,WorkerTarget>worker_targets_;std::map<std::string,std::string>worker_sessions_;
  suitecut::CoverageScriptInventory native_coverage_inventory_;std::shared_ptr<Domains>domains;
  void WakeDeadlines(){}
  void Complete(int id,bool ok,const std::string&error,const std::string&json=""){assert(operations_.Get(id));operations_.items.erase(id);assert(!results.contains(id));results.emplace(id,Result{ok,error,json});}
  uint64_t NativeDebuggerLease(){return ++next_lease;}
  auto NativeDebuggerDomains(){return domains;}
  bool NativeDebuggerSend(const std::string&s,const std::string&m,Dict p,double,Domains::Reply reply){calls.push_back({s,m,p,std::move(reply)});return true;}
  void NativeDebuggerRequestClose(const std::string&e){fatal.push_back(e);native_debugger_closing_requested_=true;}
#include "controllers/workers/developer_debugger_worker_documents_source.inc"
#include "controllers/workers/developer_debugger_workers_source.inc"
  Source(){Domains::Hooks h;h.now=[]{return Now();};h.send=[this](const auto&s,const auto&m,Dict p,double d,auto r){return NativeDebuggerSend(s,m,p,d,std::move(r));};h.close_channel=[this](const auto&,const auto&e){NativeDebuggerRequestClose(e);};domains=std::make_shared<Domains>(std::move(h));NativeWorkerDebuggerObserved();}
  ~Source(){domains->Closed();if(native_worker_developer_debuggers_)native_worker_developer_debuggers_->Closed();calls.clear();}
  void Attach(const std::string&target="w",const std::string&session="worker-session",uint64_t gen=1,int context=7){
    worker_targets_[target]={target,session,"","worker",context,gen,true,false,false};worker_sessions_[session]=target;
    auto data=CefDictionaryValue::Create();data->SetString("sessionId",session);auto info=CefDictionaryValue::Create();info->SetString("targetId",target);info->SetString("type","worker");data->SetDictionary("targetInfo",info);NativeWorkerDebuggerAttached(data,gen-1);
    auto value=JSON("{\"context\":{\"id\":"+std::to_string(context)+",\"uniqueId\":\"runtime-"+std::to_string(gen)+"\"}}");NativeWorkerDebuggerRuntime("Runtime.executionContextCreated",value,session);
    auto script=JSON("{\"scriptId\":\"script-"+target+"\",\"executionContextId\":"+std::to_string(context)+",\"url\":\"http://controlled/worker.js\"}");native_coverage_inventory_.Observe("Debugger.scriptParsed",script,session);
  }
  void Run(int id,const std::string&action,const std::string&token="",const std::string&target="w",const std::string&gen="1",const std::string&extra="",double deadline=100){
    operations_.items.emplace(id,std::make_shared<Operation>(Operation{++next_op,deadline}));
    NativeDeveloperWorkerCommand(id,JSON("{\"action\":\""+action+"\",\"worker\":\""+target+"\",\"workerControl\":{\"owner\":\""+handle_namespace_+"\",\"generation\":\""+gen+"\"}"+(token.empty()?"":",\"session\":\""+token+"\"")+extra+"}"));
  }
  bool Has(const std::string&m,const std::string&s="worker-session")const{return std::any_of(calls.begin(),calls.end(),[&](const auto&c){return c.method==m&&c.session==s;});}
  void Ack(const std::string&m,Dict data=nullptr,const std::string&s="worker-session"){auto i=std::find_if(calls.begin(),calls.end(),[&](const auto&c){return c.method==m&&c.session==s;});if(i==calls.end()){std::cerr<<"Missing controlled ACK "<<s<<" "<<m<<"; pending:";for(const auto&call:calls)std::cerr<<" "<<call.session<<":"<<call.method;std::cerr<<"; results:";for(const auto&[id,result]:results)std::cerr<<" "<<id<<":"<<result.error;std::cerr<<"\n";}assert(i!=calls.end());auto cb=std::move(i->reply);calls.erase(i);cb(true,data?data:CefDictionaryValue::Create(),"");}
  void Ready(const std::string&s="worker-session"){Ack("Debugger.enable",nullptr,s);Ack("Debugger.setSkipAllPauses",nullptr,s);}
  std::string Token(int id){assert(results.at(id).ok);return JSON(results.at(id).json)->GetString("session").ToString();}
  void Pause(const std::string&script="script-w"){auto p=JSON(R"({"reason":"other","callFrames":[{"callFrameId":"actual-frame","functionName":"worker","location":{"scriptId":"","lineNumber":0},"scopeChain":[]}],"hitBreakpoints":[]})");p->GetList("callFrames")->GetDictionary(0)->GetDictionary("location")->SetString("scriptId",script);assert(domains->Event("Debugger.paused",p,"worker-session"));}
  void Close(int id,const std::string&token){Run(id,"close",token,"w","1","",0);if(Has("Debugger.resume"))Ack("Debugger.resume");if(Has("Debugger.disable"))Ack("Debugger.disable");assert(results.at(id).ok);}
};
int main(){assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
 {Source s;s.Attach();s.Run(1,"open");assert(s.worker_debugger_retains_);s.native_debugger_calls_.insert(901);assert(s.worker_debugger_retains_(901)&&!s.worker_debugger_retains_(902));s.Ready();const auto token=s.Token(1);s.Run(2,"scripts",token);assert(s.results.at(2).ok&&JSON(s.results.at(2).json)->GetList("scripts")->GetSize()==1);s.Run(3,"scriptSource",token,"w","1",",\"scriptId\":\"script-w\"");s.Ack("Debugger.getScriptSource",JSON(R"({"scriptSource":"const workerValue=42;"})"));assert(s.results.at(3).ok);s.Close(4,token);assert(!s.native_worker_developer_debuggers_->Live()&&s.fatal.empty());++groups;}
 {Source s;s.Attach();s.Run(1,"open");s.Ready();const auto token=s.Token(1);s.Run(2,"pause",token);s.Run(3,"stepInto",token);assert(!s.results.at(2).ok&&!s.results.at(3).ok&&s.calls.empty());s.Close(4,token);++groups;}
 {Source s;s.Attach();s.Run(1,"open");s.Ready();auto token=s.Token(1);s.Run(2,"waitForPause",token);s.Pause();auto pause=JSON(s.results.at(2).json)->GetString("pause").ToString();s.Run(3,"evaluateOnCallFrame",token,"w","1",",\"pause\":\""+pause+"\",\"callFrameId\":\"actual-frame\",\"expressionJSON\":\"\\\"workerValue\\\"\"");s.Ack("Debugger.evaluateOnCallFrame",JSON(R"({"result":{"type":"number","value":42}})"));assert(!s.results.contains(3)&&!s.native_worker_developer_debuggers_->AcceptedWorkReady());s.Ack("Runtime.releaseObjectGroup");assert(s.results.at(3).ok);s.Close(4,token);++groups;}
 {Source s;s.Attach();s.Run(1,"open");s.Ready();auto token=s.Token(1);s.Run(2,"setBreakpoint",token,"w","1",",\"location\":{\"scriptId\":\"script-w\",\"lineNumber\":0}");s.Run(3,"close",token,"w","1","",0);assert(!s.Has("Debugger.disable"));s.Ack("Debugger.setBreakpoint",JSON(R"({"breakpointId":"actual-late-id","actualLocation":{"scriptId":"script-w","lineNumber":0,"columnNumber":0}})"));assert(s.Has("Debugger.removeBreakpoint")&&!s.Has("Debugger.disable"));s.Ack("Debugger.removeBreakpoint");s.Ack("Debugger.disable");assert(s.results.at(3).ok);++groups;}
 {Source s;s.Attach();s.Run(1,"open");s.Ready();auto token=s.Token(1);s.Run(2,"scripts",token,"other");s.Run(3,"scripts",token,"w","2");assert(!s.results.at(2).ok&&!s.results.at(3).ok&&s.calls.empty());s.Close(4,token);++groups;}
 {Source s;s.Attach();s.Run(1,"open");s.Ready();s.Run(2,"scriptSource",s.Token(1),"w","1",",\"scriptId\":\"script-w\"");auto data=JSON(R"({"executionContextId":7})");s.NativeWorkerDebuggerRuntime("Runtime.executionContextDestroyed",data,"worker-session");s.NativeDeveloperWorkersChanged();assert(!s.results.at(2).ok&&s.Has("Debugger.getScriptSource")&&!s.Has("Debugger.disable"));s.Ack("Debugger.getScriptSource",JSON(R"({"scriptSource":"late"})"));s.Ack("Debugger.disable");assert(!s.native_worker_developer_debuggers_->Live());++groups;}
 {Source s;s.Attach();s.Run(1,"open");s.native_worker_developer_debuggers_->Cancel(1);s.Run(2,"open");assert(!s.results.at(2).ok&&s.results.at(2).error.find("physically retained")!=std::string::npos);s.Ack("Debugger.enable");assert(!s.Has("Debugger.setSkipAllPauses"));s.Ack("Debugger.disable");assert(!s.results.contains(1));s.Run(3,"open");s.Ready();assert(s.results.at(3).ok);s.Close(4,s.Token(3));++groups;}
 {Source s;s.Attach();s.Run(1,"open");s.Ready();s.Run(2,"scriptSource",s.Token(1),"w","1",",\"scriptId\":\"script-w\"");s.NativeDeveloperWorkerChannelClosed("worker-session",99,1);assert(!s.results.contains(2)&&s.native_worker_developer_debuggers_->Live());s.domains->Detached("worker-session");auto cb=std::move(s.calls.front().reply);s.calls.clear();cb(false,nullptr,"actual channel detached");s.NativeDeveloperWorkerChannelClosed("worker-session",4,1);assert(!s.results.at(2).ok&&!s.native_worker_developer_debuggers_->Live());++groups;}
 {Source s;s.Attach();s.worker_targets_.at("w").kind="shared_worker";s.Run(1,"open");assert(!s.results.at(1).ok&&s.calls.empty()&&!s.next_lease);++groups;}
 {Source s;s.Attach();s.Run(1,"open","","w","1","",2);clock_now=2;s.native_worker_developer_debuggers_->Tick(Now());assert(!s.results.at(1).ok&&s.Has("Debugger.enable"));s.Ack("Debugger.enable");if(s.Has("Debugger.disable"))s.Ack("Debugger.disable");clock_now=1;++groups;}
 {Source s;s.Attach();s.Run(1,"open");s.Ready();const auto token=s.Token(1);s.Run(2,"waitForPause",token);auto foreign=JSON(R"({"scriptId":"foreign-script","executionContextId":99,"url":"http://controlled/other.js"})");s.native_coverage_inventory_.Observe("Debugger.scriptParsed",foreign,"worker-session");s.Pause("foreign-script");assert(!s.results.contains(2)&&s.Has("Debugger.resume"));s.Ack("Debugger.resume");s.Pause();assert(s.results.at(2).ok);s.Close(3,token);++groups;}
 {Source s;s.Attach();assert(s.native_coverage_inventory_.MarkNative("worker-session",7,"script-w"));s.Run(1,"open");s.Ready();auto token=s.Token(1);s.Run(2,"setBreakpoint",token,"w","1",",\"location\":{\"scriptId\":\"script-w\",\"lineNumber\":0}");assert(!s.results.at(2).ok&&s.calls.empty());s.Close(3,token);++groups;}
 {Source s;s.Attach();auto clear=JSON(R"({"executionContextId":7})");s.NativeWorkerDebuggerRuntime("Runtime.executionContextDestroyed",clear,"worker-session");s.Run(1,"open");assert(!s.results.at(1).ok&&s.calls.empty()&&!s.next_lease);++groups;}
 {Source s;s.Attach();s.Run(1,"open");s.Ready();const auto old=s.Token(1);const auto channel=s.NativeWorkerDebuggerChannel("worker-session");s.domains->Detached("worker-session");s.NativeDeveloperWorkerChannelClosed("worker-session",channel.observer,channel.generation);s.NativeWorkerDebuggerChannelDrained("worker-session",channel);s.worker_targets_.clear();s.worker_sessions_.clear();s.Attach("w","replacement-session",2);s.Run(2,"open","","w","2");s.Ready("replacement-session");assert(s.results.at(2).ok&&s.Token(2)!=old);s.Run(3,"scripts",old,"w","1");assert(!s.results.at(3).ok);++groups;}
 std::cout<<"native included worker debugger adapter "<<groups<<" controlled groups passed\n";
}
