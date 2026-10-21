// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "include/cef_parser.h"
#include "include/cef_api_hash.h"
#include "include/base/cef_ref_counted.h"
#include "inbound_string.h"
#include "accepted_work_origin.h"
#include "operation_registry.h"
#include "send_keys_sequence.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Actual production admission, queue, retained-command and retirement includes.
// Controlled DevTools callbacks/CEF values; no browser, renderer or focus claim.
class Task {
 public:
  explicit Task(std::function<void()> fn):fn_(std::move(fn)){}
  std::function<void()> fn_;
};
static std::deque<std::function<void()>> tasks;
static bool ControlledPostTask(int,Task* task){tasks.push_back(std::move(task->fn_));delete task;return true;}
static void Pump(){while(!tasks.empty()){auto fn=std::move(tasks.front());tasks.pop_front();fn();}}
#define CefPostTask ControlledPostTask
#define TID_UI 0
class Source:public CefBaseRefCounted {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,const std::string&)>;
  static constexpr size_t kMaxResponseBytes=1048576;
  struct DevToolsCall{int owner;Reply callback;std::string session;bool retain_after_cancel;size_t response_limit;bool retained_control;bool lossless_scalars;};
  struct Host{int closes=0;void CloseBrowser(bool){++closes;}};
  struct Browser{Host host;Host* GetHost(){return &host;}};
  struct Call{int command,owner;std::string method;Dict params;};
  struct Result{int owner;bool ok;std::string error;};
  #include "controllers/input/input_work.inc"
  double now=0;
  double Now()const{return now;}
  bool NativeDropBusy()const{return false;}
  bool NativeDropOwns(int)const{return false;}
  double Number(Dict data,const char* key){return data->GetType(key)==VTYPE_INT?data->GetInt(key):data->GetDouble(key);}
  std::string Literal(const std::string& value){return value;}
  void ArmActionNavigation(const std::shared_ptr<InputWork>& work){admitted.push_back(work->owner);}
  void InputStep(const std::shared_ptr<InputWork>& work){
    if(!operations_.IsCurrent(work->owner,work->generation)||work->busy)return;
    if(PrepareTargetedKeyboard(work))return;
    ready.push_back(work->owner);
  }
  void ContinueInput(const std::shared_ptr<InputWork>& work,int=0){InputStep(work);}
  void LocatorAction(int id,uint64_t generation,const std::string&,const std::string&,const std::string&,Dict config){
    assert(keyboard_input_==id);focus.push_back(id);targets.push_back(config);
    auto params=CefDictionaryValue::Create();
    RetainedCall(id,"Runtime.callFunctionOn",params,[this,id,generation](bool ok,const std::string&){
      if(ok)TargetedKeyboardPrepared(id,generation);else Complete(id,false,"actual focus reply failed");
    });
  }
  void RetainedCall(int owner,const std::string& method,Dict params,Reply callback){
    assert(RetainsKeyboardCall(owner,method));
    if(!RetainKeyboardCall(owner,method,params,callback))return;
    const int command=AllocateDevToolsId();
    devtools_.emplace(command,DevToolsCall{owner,std::move(callback),"",true,kMaxResponseBytes,true,false});
    assert(SubmitDevTools(command,method,params,""));
  }
  int AllocateDevToolsId(){return ++next_command;}
  bool SubmitDevTools(int command,const std::string& method,Dict params,const std::string&){
    if(refuse)return false;
    calls.push_back({command,devtools_.at(command).owner,method,params});return true;
  }
  void Ack(bool ok=true){
    assert(!calls.empty());const auto call=calls.front();calls.pop_front();
    auto callback=std::move(devtools_.at(call.command).callback);devtools_.erase(call.command);
    callback(ok,"{}");
  }
  void Complete(int id,bool ok,const std::string& error=""){
    if(!operations_.Finish(id))return;
    results.push_back({id,ok,error});CleanupInput(id);
  }
  Dict KeyPacket(const std::string&,bool){assert(false);return nullptr;}
  Dict MousePacket(const std::string&,const std::string&,int){assert(false);return nullptr;}
  void DispatchCefMouse(Dict){assert(false);}
  void SendDevTools(const std::string&,Dict){assert(false);}
  void InputCall(const std::shared_ptr<InputWork>& work,const std::string& method,Dict params,std::function<void()> done){
    TargetedKeyboardSubmitted(work,method,params);
    RetainedCall(work->owner,method,params,[done=std::move(done)](bool ok,const std::string&){if(ok)done();});
  }
  uint64_t Begin(int id,double timeout=100){auto entry=operations_.Begin(id,now,timeout);assert(entry);return entry->generation;}
  Dict Target(const std::string& selector="input",const std::string& key="Control+a"){
    auto config=CefDictionaryValue::Create();config->SetString("selector",selector);config->SetString("key",key);return config;
  }
  void Targeted(int id,double timeout=100,Dict config=nullptr){const auto gen=Begin(id,timeout);BeginTargetedKeyboard(id,gen,config?config:Target(),"press","");}
  void Down(int id){
    const auto work=input_work_.at(id);const auto key=*SuiteCutDescribeKey("Control",0);
    InputCall(work,"Input.dispatchKeyEvent",SendKeysPacket(key,true),[]{});
  }
  void Closed(){operations_.Clear();devtools_.clear();calls.clear();send_keys_physical_.clear();send_keys_physical_count_=0;input_work_.clear();keyboard_input_=0;keyboard_queue_.clear();}
  #include "controllers/input/input_admission.inc"
  #include "controllers/input/input_queue.inc"
  #include "controllers/input/input_retirement.inc"
  #include "controllers/input/send_keys.inc"
  #include "controllers/input/targeted_keyboard.inc"
  #include "controllers/input/send_keys_sequence.inc"
  SuiteCutOperationRegistry operations_;
  std::shared_ptr<Browser> browser_=std::make_shared<Browser>();
  std::map<int,std::shared_ptr<InputWork>> input_work_;
  std::deque<int> keyboard_queue_,mouse_queue_;
  int keyboard_input_=0,mouse_input_=0,key_modifiers_=0,next_command=0;
  double mouse_x_=0,mouse_y_=0;
  std::map<std::string,SuiteCutKey> pressed_keys_;
  std::map<int,int> context_retries_;
  std::map<int,DevToolsCall> devtools_;
  std::string closed_reason_;
  bool refuse=false;
  std::deque<Call> calls;
  std::vector<int> focus,ready,admitted;
  std::vector<Dict> targets;
  std::vector<Result> results;
 private:
  IMPLEMENT_REFCOUNTING(Source);
};
#undef CefPostTask
#undef TID_UI
int main(int argc,char** argv){
  assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
  // PumpInput may run before the general Expired scan. A prepared target must
  // stop before InputStep's focus/packet stage, not only at later InputCall.
  {auto s=CefRefPtr<Source>(new Source);s->Targeted(1);s->Ack();s->Down(1);s->Ack();
    const auto work=s->input_work_.at(1);assert(work->keyboard_target_prepared&&s->ready.size()==1);
    s->now=100;s->InputStep(work);
    assert(s->ready.size()==1&&!s->operations_.Contains(1)&&!s->results.back().ok);
    assert(s->keyboard_input_==1&&s->calls.size()==1&&s->calls.front().params->GetString("type")=="keyUp");
    assert(s->SendKeysPhysicalPending(1)&&!s->pressed_keys_.empty());s->Ack();Pump();
    assert(!s->keyboard_input_&&s->pressed_keys_.empty()&&s->input_work_.empty());++groups;}
  if(argc==2&&std::string(argv[1])=="--prepared-expiry-only"){
    assert(tasks.empty());std::printf("Native prepared keyboard original-deadline guard: %d controlled group PASS\n",groups);return 0;
  }
  assert(argc==1);
  {auto s=CefRefPtr<Source>(new Source);s->Targeted(1);s->Targeted(2);
    assert((s->focus==std::vector<int>{1}));s->Ack();assert((s->ready==std::vector<int>{1}));
    s->Complete(1,true);assert((s->focus==std::vector<int>{1}));Pump();
    assert((s->focus==std::vector<int>{1,2}));s->Ack();s->Complete(2,true);Pump();++groups;}
  {auto s=CefRefPtr<Source>(new Source);s->Targeted(1);s->Targeted(2,10);s->Ack();s->now=10;
    s->Complete(1,true);Pump();assert((s->focus==std::vector<int>{1}));
    assert(!s->operations_.Contains(2)&&!s->results.back().ok);assert(s->keyboard_input_==0);++groups;}
  {auto s=CefRefPtr<Source>(new Source);s->Targeted(1);const auto gen=s->input_work_.at(1)->generation;
    s->Complete(1,false,"actual cancel");s->Targeted(2);Pump();
    assert(s->keyboard_input_==1&&s->focus.size()==1&&s->SendKeysPhysicalPending(1));
    s->TargetedKeyboardPrepared(1,gen);assert(s->ready.empty());s->Ack();Pump();
    assert(s->keyboard_input_==2&&s->focus.size()==2);s->Ack();s->Complete(2,true);Pump();++groups;}
  {auto s=CefRefPtr<Source>(new Source);s->Targeted(1);s->Ack();s->Down(1);s->Targeted(2);
    s->Complete(1,false,"actual key cancellation");assert(s->calls.size()==1&&s->keyboard_input_==1);
    s->Ack();assert(s->calls.size()==1&&s->calls.front().params->GetString("type")=="keyUp");
    assert(s->keyboard_input_==1&&s->pressed_keys_.size()==1);s->Ack();Pump();
    assert(s->pressed_keys_.empty()&&s->keyboard_input_==2);s->Ack();s->Complete(2,true);Pump();++groups;}
  {auto s=CefRefPtr<Source>(new Source);s->Targeted(1);s->Ack();s->Down(1);s->Ack();
    s->Complete(1,false,"cancel");s->Targeted(2);s->Ack(false);Pump();
    assert(s->browser_->host.closes==1&&s->keyboard_input_==1&&s->focus.size()==1);
    assert(s->input_work_.at(1)->send_keys_cleanup_failed&&!s->pressed_keys_.empty());s->Closed();++groups;}
  {auto s=CefRefPtr<Source>(new Source);auto config=s->Target("fixed","Enter");
    config->SetInt("nativeHandle",17);config->SetString("frame","actual-child");config->SetBool("strict",false);
    config->SetString("diagnosticExpressionJSON","actual diagnostic");s->Targeted(1,57,config);
    assert(s->targets.front()==config&&s->operations_.Get(1)->deadline_ms==57);
    s->now=57;s->Ack();assert(s->ready.empty()&&!s->results.back().ok);Pump();++groups;}
  {auto s=CefRefPtr<Source>(new Source);s->Targeted(1,100,s->Target("input","UnknownKeyboardKey"));
    assert(s->focus.empty()&&s->input_work_.empty()&&!s->results.back().ok);++groups;}
  {auto s=CefRefPtr<Source>(new Source);auto config=s->Target();config->SetDouble("delay",0.5);
    s->Targeted(1,100,config);assert(s->focus.empty()&&!s->results.back().ok);++groups;}
  {auto s=CefRefPtr<Source>(new Source);auto message=CefDictionaryValue::Create();
    message->SetString("kind","keyboard");message->SetString("action","press");message->SetString("key","Enter");
    const auto gen=s->Begin(1);s->BeginInput(1,gen,message);s->Targeted(2);
    assert(s->focus.empty()&&(s->ready==std::vector<int>{1}));s->Complete(1,true);Pump();
    assert((s->focus==std::vector<int>{2}));s->Ack();s->Complete(2,true);Pump();++groups;}
  {auto s=CefRefPtr<Source>(new Source);s->Targeted(1);s->Ack();s->Down(1);s->Ack();
    const auto work=s->input_work_.at(1);const auto key=work->send_keys_held.back();
    s->InputCall(work,"Input.dispatchKeyEvent",s->SendKeysPacket(key,false),[]{});
    assert(!work->send_keys_held.empty());s->Ack();assert(work->send_keys_held.empty());
    s->Complete(1,true);assert(s->calls.empty());Pump();++groups;}
  assert(tasks.empty());std::printf("Native targeted keyboard queue/retirement: %d controlled groups PASS\n",groups);
}
