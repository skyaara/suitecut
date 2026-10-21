// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "developer_debugger_documents.h"
#include "accepted_inspector_work.h"
#include "developer_debugger_initial_trees.h"
static double Now(){return 1.;}
template<class T>struct CefRefPtr{T*value;explicit CefRefPtr(T*input):value(input){}T*operator->()const{return value;}};
struct Task{std::function<void()>run;explicit Task(std::function<void()>input):run(std::move(input)){}};
static std::deque<std::unique_ptr<Task>>tasks;
static constexpr int TID_UI=0;
static void CefPostTask(int,Task*task){tasks.emplace_back(task);}
static void DrainTasks(){size_t count=0;while(!tasks.empty()){assert(++count<16);auto task=std::move(tasks.front());tasks.pop_front();task->run();}}
struct BrowserHost{
  std::vector<std::string>sent;std::function<void()>on_send;int closes=0;
  bool SendDevToolsMessage(const void*bytes,size_t size){sent.emplace_back(static_cast<const char*>(bytes),size);if(on_send)on_send();return true;}
  void CloseBrowser(bool){++closes;}
};
struct Browser{BrowserHost host;BrowserHost*GetHost(){return &host;}};
// Actual included production adapter, with controlled Source facts and posted
// close observations. This is not a CEF browser/observer or physical ACK proof.
class Source{
 public:
  struct FrameState{std::string id,session,loader,context_identity;int context=0;};
  struct Domains{std::set<uint64_t>leases;bool HasLease(uint64_t lease)const{return leases.contains(lease);}};
  bool observer_=true,closing_page_=false,renderer_failed_=false,native_debugger_closing_requested_=false;
  std::string handle_namespace_="actual-controlled-source",main_frame_id_="root";
  std::map<std::string,FrameState>frame_states_;
  std::shared_ptr<Domains>native_debugger_domains_=std::make_shared<Domains>();
  std::shared_ptr<Browser>browser_=std::make_shared<Browser>();
  struct Call{int owner=0;bool retain_after_cancel=false,retained_control=false;std::function<void(bool,const std::string&)>callback;bool selector_group_release=false;};
  struct Artifacts{void PhysicalDispatchFailed(int){}};std::unique_ptr<Artifacts>artifacts_;
  struct Operations{bool Contains(int)const{return false;}}operations_;
  std::map<int,Call>devtools_,network_calls_,binding_calls_;
  std::map<int,int>native_trace_protocol_,utility_worlds_;
  std::map<int,std::string>session_trees_;
  int changes=0,wakes=0;std::vector<std::string>close_requests;
  // Controlled consumer seams record only the exact adapter notification.
  // They do not manufacture a controller close, Inspector ACK or lease release.
  std::vector<suitecut::FrameDebuggerScopes::Pin>frame_changes;
  std::vector<std::pair<suitecut::FrameDebuggerScopes::Channel,uint64_t>>frame_channel_closures;
  void NativeDeveloperFrameChanged(const suitecut::FrameDebuggerScopes::Pin&pin){frame_changes.push_back(pin);}
  void NativeDeveloperFrameChannelClosed(const suitecut::FrameDebuggerScopes::Channel&channel,uint64_t attachment){frame_channel_closures.emplace_back(channel,attachment);}
  void NativeDeveloperChanged(){++changes;}
  void NativeDebuggerRequestClose(const std::string&error){if(native_debugger_closing_requested_)return;native_debugger_closing_requested_=true;close_requests.push_back(error);}
  void WakeDeadlines(){++wakes;}
  bool NativeDropDispatchRefused(int,const std::string&){return false;}
  bool NativeDropDispatchReady(int){return true;}
  int refused_idle=0;
  bool NativeIdleDispatchCurrent(int id)const{return id!=refused_idle;}
  bool NativeTraceDispatchReady(int){return true;}
  void NativeTraceDispatchFailed(int){}
  bool keyboard_physical=false;
  bool SendKeysPhysicalPending(int){return keyboard_physical;}
  bool SendKeysCleanupCommand(int){return false;}
  void MarkFinalReleaseSubmitted(int){}
  void Error(const char*error){close_requests.push_back(error);}
#include "controllers/debugger/developer_debugger_documents_source.inc"
#ifndef SUITECUT_DEVTOOLS_DISPATCH_TEST_INCLUDE
#define SUITECUT_DEVTOOLS_DISPATCH_TEST_INCLUDE "controllers/debugger/devtools_dispatch.inc"
#endif
#include "controllers/work/accepted_inspector_owner.inc"
#include SUITECUT_DEVTOOLS_DISPATCH_TEST_INCLUDE
  explicit Source(bool initialize=true){if(initialize){native_frame_debugger_registering_=true;assert(NativeFrameDebuggerObserved());NativeFrameDebuggerMainObserved();}}
  ~Source(){assert(tasks.empty());NativeFrameDebuggerClosed();}
  void Default(const std::string&frame="root",const std::string&session="",int context=7,const std::string&unique="actual-default"){
    frame_states_[frame]={frame,session,"",unique,context};NativeFrameDebuggerDefaultObserved(frame_states_.at(frame));
  }
  void Associate(int command,const std::string&frame="root",const std::string&session=""){
    frame_states_.at(frame).loader="actual-tree-loader";NativeFrameDebuggerInitialAssociated(command,frame,session);
  }
};
int main(){size_t groups=0;
 {Source source;assert(source.NativeFrameDebuggerTreePrepare(10,""));source.Default();const auto document=source.native_frame_debugger_documents_->DocumentStamp("root");
  assert(source.NativeFrameDebuggerBeforeSend(10,"",true,source.NativeFrameDebuggerAttachment("")));
  source.Associate(10);suitecut::FrameDebuggerScopes::Pin pin;assert(source.native_frame_debugger_documents_->Resolve("root",pin)&&pin.document>document&&source.NativeFrameDebuggerPinCurrent(pin));
  {const std::string session;Source::NativeFrameDebuggerTreeReceipt receipt{&source,10,session,source.NativeFrameDebuggerAttachment(session)};}
  assert(!source.native_frame_debugger_trees_.Trees()&&source.close_requests.empty());++groups;}
 {Source source;assert(source.NativeFrameDebuggerTreePrepare(10,""));source.Default();const auto before=source.native_frame_debugger_documents_->DocumentStamp("root");
  // Runtime.enable is ahead of the queued tree. These actual facts arrive
  // before the real sender boundary and must not be frozen at queue admission.
  source.NativeFrameDebuggerFrameInvalidated("root","");source.Default("root","",8,"replacement-before-real-send");
  assert(source.NativeFrameDebuggerBeforeSend(10,"",true,source.NativeFrameDebuggerAttachment("")));
  const auto captured=source.native_frame_debugger_trees_.InitialDocument(10,"",source.NativeFrameDebuggerAttachment(""),"root");assert(captured&&captured!=before);
  source.Associate(10);suitecut::FrameDebuggerScopes::Pin pin;assert(source.native_frame_debugger_documents_->Resolve("root",pin)&&pin.context==8&&source.NativeFrameDebuggerPinCurrent(pin)&&source.close_requests.empty());++groups;}
 {Source source;assert(source.NativeFrameDebuggerTreePrepare(10,"")&&source.NativeFrameDebuggerBeforeSend(10,"",true,source.NativeFrameDebuggerAttachment("")));
  source.NativeFrameDebuggerFrameInvalidated("root","");source.Default();source.Associate(10);suitecut::FrameDebuggerScopes::Pin pin;
  assert(source.native_frame_debugger_documents_->Resolve("root",pin)&&pin.loader.empty()&&!source.NativeFrameDebuggerPinCurrent(pin)&&source.close_requests.empty());++groups;}
 {Source source(false);source.native_frame_debugger_registering_=true;assert(source.NativeFrameDebuggerRegistrationEvent("Runtime.executionContextCreated"));
  assert(!source.NativeFrameDebuggerObserved()&&!source.native_frame_debugger_documents_&&source.close_requests.size()==1);++groups;}
 {Source source(false);source.native_frame_debugger_registering_=true;assert(!source.NativeFrameDebuggerRegistrationEvent("Runtime.consoleAPICalled")&&!source.NativeFrameDebuggerRegistrationEvent("Debugger.scriptParsed"));
  assert(source.NativeFrameDebuggerObserved()&&source.close_requests.empty());++groups;}
 {Source source;source.Default();assert(source.NativeFrameDebuggerAttached("actual-child","","remote"));source.Default("remote","actual-child",8,"remote-default");
  suitecut::FrameDebuggerScopes::Pin root,remote;assert(source.native_frame_debugger_documents_->Resolve("root",root)&&source.native_frame_debugger_documents_->Resolve("remote",remote));
  source.NativeFrameDebuggerRootRealmInvalidated();assert(!source.NativeFrameDebuggerPinCurrent(root)&&source.NativeFrameDebuggerPinCurrent(remote)&&source.changes==0&&source.close_requests.empty());++groups;}
 {Source source;assert(source.NativeFrameDebuggerAttached("actual-child","","remote")&&source.NativeFrameDebuggerTreePrepare(10,"actual-child"));const auto old=source.NativeFrameDebuggerAttachment("actual-child");
  source.NativeFrameDebuggerDetached("actual-child",old);assert(!source.NativeFrameDebuggerBeforeSend(10,"actual-child",true,old)&&source.close_requests.empty());
  assert(source.NativeFrameDebuggerAttached("actual-child","","remote")&&source.NativeFrameDebuggerAttachment("actual-child")!=old);
  assert(!source.NativeFrameDebuggerBeforeSend(10,"actual-child",true,old)&&source.close_requests.empty()&&source.NativeFrameDebuggerTreePrepare(11,"actual-child"));
  assert(source.NativeFrameDebuggerBeforeSend(11,"actual-child",true,source.NativeFrameDebuggerAttachment("actual-child")));++groups;}
 {Source source;assert(source.NativeFrameDebuggerTreePrepare(10,""));assert(!source.NativeFrameDebuggerBeforeSend(10,"wrong-session",true,source.NativeFrameDebuggerAttachment(""))&&source.close_requests.size()==1);++groups;}
 {Source source;assert(source.NativeFrameDebuggerTreePrepare(10,""));assert(!source.NativeFrameDebuggerBeforeSend(10,"",false,0)&&source.close_requests.size()==1);++groups;}
 {Source source;assert(source.NativeFrameDebuggerTreePrepare(10,""));source.Default();const auto old=source.native_frame_debugger_documents_->DocumentStamp("root"),attachment=source.NativeFrameDebuggerAttachment("");
  assert(source.QueueDeferredDevTools(10,R"({"id":10,"method":"Page.getFrameTree"})",false,"",true,attachment));
  assert(source.browser_->host.sent.empty()&&!source.native_frame_debugger_trees_.InitialDocument(10,"",attachment,"root"));
  source.NativeFrameDebuggerFrameInvalidated("root","");source.Default("root","",8,"actual-before-deferred-send");
  const auto current=source.native_frame_debugger_documents_->DocumentStamp("root");assert(current!=old);
  source.browser_->host.on_send=[&]{assert(source.native_frame_debugger_trees_.InitialDocument(10,"",attachment,"root")==current);};
  DrainTasks();assert(source.browser_->host.sent.size()==1&&source.devtools_dispatch_bytes_==0&&source.close_requests.empty());++groups;}
 {Source source;assert(source.NativeFrameDebuggerAttached("actual-child","","remote")&&source.NativeFrameDebuggerTreePrepare(10,"actual-child"));const auto old=source.NativeFrameDebuggerAttachment("actual-child");
  assert(source.QueueDeferredDevTools(10,R"({"id":10,"method":"Page.getFrameTree","sessionId":"actual-child"})",false,"actual-child",true,old));
  source.NativeFrameDebuggerDetached("actual-child",old);assert(source.NativeFrameDebuggerAttached("actual-child","","remote")&&source.NativeFrameDebuggerTreePrepare(11,"actual-child"));
  DrainTasks();assert(source.browser_->host.sent.empty()&&source.devtools_dispatch_bytes_==0&&source.close_requests.empty()&&source.native_frame_debugger_trees_.Trees()==1);++groups;}
 {Source source;source.Default();suitecut::FrameDebuggerScopes::Pin pin;
  assert(source.native_frame_debugger_documents_->Resolve("root",pin));
  source.native_debugger_domains_->leases.insert(101);std::string error;
  const auto token=source.native_frame_debugger_scopes_->Reserve(pin,101,100,error);assert(token&&error.empty());
  assert(source.native_frame_debugger_scopes_->BeginAcquire(token));source.native_frame_debugger_scopes_->AcquireReturned(token);
  assert(source.native_frame_debugger_scopes_->Ready(token,true));const auto receipt=source.native_frame_debugger_scopes_->Hold(token);assert(receipt);
  source.NativeFrameDebuggerFrameInvalidated("root","");
  assert(source.changes==1&&source.frame_changes.size()==1&&source.frame_changes.front()==pin);
  assert(!source.native_frame_debugger_scopes_->Current(token)&&source.native_frame_debugger_scopes_->Retains(token)&&source.native_frame_debugger_scopes_->Physical()==1);
  source.native_frame_debugger_scopes_->Tick();assert(source.frame_changes.size()==1);
  // Logical invalidation and its consumer notification cannot retire either
  // the held actual-callback slot or the controlled domain lease.
  assert(source.native_frame_debugger_scopes_->Retire(receipt));assert(source.native_frame_debugger_scopes_->Retains(token));
  source.native_debugger_domains_->leases.erase(101);source.native_frame_debugger_scopes_->Poll(token);
  assert(!source.native_frame_debugger_scopes_->Retains(token)&&source.close_requests.empty());++groups;}
 {Source source;assert(source.NativeFrameDebuggerAttached("actual-child","","remote"));source.Default("remote","actual-child",8,"remote-default");
  suitecut::FrameDebuggerScopes::Pin pin;assert(source.native_frame_debugger_documents_->Resolve("remote",pin));
  source.native_debugger_domains_->leases.insert(102);std::string error;
  const auto token=source.native_frame_debugger_scopes_->Reserve(pin,102,100,error);assert(token&&error.empty());
  assert(source.native_frame_debugger_scopes_->BeginAcquire(token));source.native_frame_debugger_scopes_->AcquireReturned(token);
  assert(source.native_frame_debugger_scopes_->Ready(token,true));const auto receipt=source.native_frame_debugger_scopes_->Hold(token);assert(receipt);
  // The adapter is invoked only after its externally controlled real channel
  // drain. Assert the exact original attachment notification, not a latest pin.
  source.native_debugger_domains_->leases.erase(102);source.NativeFrameDebuggerDetached("actual-child",pin.attachment);
  assert(source.frame_channel_closures.size()==1&&source.frame_channel_closures.front().first==pin.channel&&source.frame_channel_closures.front().second==pin.attachment);
  assert(!source.native_frame_debugger_scopes_->Retains(token)&&source.native_frame_debugger_scopes_->Physical()==0&&!source.native_frame_debugger_scopes_->Retire(receipt));
  assert(source.NativeFrameDebuggerAttached("actual-child","","remote"));const auto replacement=source.NativeFrameDebuggerAttachment("actual-child");assert(replacement!=pin.attachment);
  source.NativeFrameDebuggerDetached("actual-child",pin.attachment);
  assert(source.NativeFrameDebuggerAttachment("actual-child")==replacement&&source.frame_channel_closures.back().second==pin.attachment&&source.close_requests.empty());++groups;}
 {Source source;bool failed=false;source.refused_idle=77;source.devtools_.emplace(77,Source::Call{1,true,true,[&](bool ok,const auto&){assert(!ok);failed=true;}});
  assert(source.QueueDeferredDevTools(77,R"({"id":77,"method":"Emulation.setIdleOverride"})",false,"",false,0));DrainTasks();
  assert(failed&&source.browser_->host.sent.empty()&&source.devtools_.empty()&&source.devtools_dispatch_bytes_==0&&source.close_requests.empty());++groups;}
 {Source source;source.keyboard_physical=true;bool failed=false;
  source.devtools_.emplace(81,Source::Call{1,true,true,[&](bool ok,const auto&){assert(!ok);failed=true;}});
  assert(source.QueueDeferredDevTools(81,R"({"id":81,"method":"Runtime.callFunctionOn"})",false,"",false,0));DrainTasks();
  assert(failed&&source.browser_->host.sent.empty()&&source.devtools_.empty());++groups;}
 {Source source;source.keyboard_physical=true;bool failed=false;
  Source::Call release{1,true,true,[&](bool,const auto&){failed=true;}};release.selector_group_release=true;source.devtools_.emplace(82,std::move(release));
  assert(source.QueueDeferredDevTools(82,R"({"id":82,"method":"Runtime.releaseObjectGroup"})",false,"",false,0));DrainTasks();
  assert(!failed&&source.browser_->host.sent.size()==1&&source.devtools_.contains(82)&&source.close_requests.empty());++groups;}
 std::cout<<"native included frame-document Source adapter "<<groups<<" groups passed\n";
}
