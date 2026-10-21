// SPDX-License-Identifier: MIT
// Actual Source sender and rejection helper over controlled submission/receipt
// boundaries; actual CEF dictionaries/parser, no browser integration claim.
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "selector_wait_controls.h"
struct Harness {
 using Dict=CefRefPtr<CefDictionaryValue>;using Callback=std::function<void(bool,const std::string&)>;
 struct DevToolsCall{int owner;Callback callback;std::string session;bool retain_after_cancel=false;size_t response_limit=0;bool retained_control=false;bool selector_group_release=false;};
 struct Sent{int id;std::string method,group,session;};
 static constexpr size_t kMaxResponseBytes=192*1048576;
 bool browser_=true,submit=true;int next=0;std::map<int,DevToolsCall> devtools_;std::set<std::string> sessions_{"actual-child"};std::vector<Sent> sent;std::vector<std::string> errors,detached;
 static double Now(){return 0;}
 std::vector<int> deadline_release_failures,screenshot_release_failures;
 void AssertionDeadlineReleaseFailed(int owner){deadline_release_failures.push_back(owner);}
 void ScreenshotAssertionReleaseFailed(int owner){screenshot_release_failures.push_back(owner);}
 void NetworkFatal(const std::string&error){errors.push_back(error);}
 int AllocateDevToolsId(){return ++next;}
 bool SubmitDevTools(int id,const std::string&method,Dict params,const std::string&session){sent.push_back({id,method,params->GetString("objectGroup").ToString(),session});return submit;}
 void NativeTargetSessionDetached(const std::string&session){detached.push_back(session);sessions_.erase(session);}
 void Ack(int id,const std::string&session){const auto found=devtools_.find(id);assert(found!=devtools_.end()&&found->second.session==session);auto callback=std::move(found->second.callback);devtools_.erase(found);callback(true,"{}");}
 bool Reject(int id,bool success=false,const std::string&session="",const std::string&json=R"({"code":-32001,"message":"Session with given id not found."})"){return SelectorWaitRejectedSession(id,success,json.data(),json.size(),session);}
 void Release(const std::string&session="actual-child",bool with_main=false){auto& owner=SelectorWaits();assert(owner.Admit(1,1)&&owner.Group(1,"actual-group",session));if(with_main)assert(owner.Group(1,"actual-group",""));owner.Cancel(1);}
#include "controllers/dom/selector_wait_controller.inc"
};
int main(){cef_api_hash(CEF_API_VERSION,0);int groups=0;
 // A real child close while the prior main group is awaiting ACK must prevent
 // a new command to that dead channel; logical cancellation alone must not.
 for(bool actual_close:{false,true}){Harness h;auto& owner=h.SelectorWaits();assert(owner.Admit(1,1)&&owner.Group(1,"actual-group","")&&owner.Group(1,"actual-group","actual-child"));auto held=owner.Begin(1);owner.Cancel(1);assert(owner.PhysicalSize()==1&&h.sent.empty());held.reset();assert(h.sent.size()==1&&h.sent.front().session.empty()&&owner.PhysicalSize()==1);if(actual_close)h.NativeTargetSessionDetached("actual-child");h.Ack(1,"");if(actual_close){assert(h.sent.size()==1&&h.devtools_.empty()&&!owner.Size()&&!owner.PhysicalSize());}else{assert(h.sent.size()==2&&h.sent.back().session=="actual-child"&&owner.Size()==1&&owner.PhysicalSize()==1);h.Ack(2,"actual-child");assert(!owner.Size()&&!owner.PhysicalSize());}assert(h.errors.empty());++groups;}
 {Harness h;h.Release();assert(h.sent.size()==1&&h.devtools_.at(1).selector_group_release&&h.SelectorWaits().PhysicalSize()==1);assert(h.Reject(1));assert(h.detached==std::vector<std::string>{"actual-child"}&&h.devtools_.empty()&&h.errors.empty()&&!h.SelectorWaits().PhysicalSize()&&!h.SelectorWaits().Size());assert(!h.Reject(1));++groups;}
 for(int negative=0;negative<8;++negative){Harness h;h.Release();bool consumed=false;
  switch(negative){case 0:consumed=h.Reject(1,true);break;case 1:consumed=h.Reject(1,false,"other-actual-session");break;case 2:consumed=h.Reject(1,false,"",R"({"code":-32000,"message":"Session with given id not found."})");break;case 3:consumed=h.Reject(1,false,"",R"({"code":-32001,"message":"Author error"})");break;case 4:consumed=h.Reject(2);break;case 5:h.devtools_.at(1).selector_group_release=false;consumed=h.Reject(1);break;case 6:consumed=h.Reject(1,false,"",R"({"code":"-32001","message":"Session with given id not found."})");break;case 7:consumed=h.Reject(1,false,"",std::string(513,' '));break;}
  assert(!consumed&&h.detached.empty()&&h.devtools_.size()==1&&h.SelectorWaits().PhysicalSize()==1&&h.sessions_.contains("actual-child"));h.Ack(1,"actual-child");assert(!h.SelectorWaits().PhysicalSize()&&h.errors.empty());++groups;}
 {Harness h;h.Release("");assert(!h.Reject(1)&&h.devtools_.contains(1)&&h.SelectorWaits().PhysicalSize()==1);h.Ack(1,"");assert(!h.SelectorWaits().PhysicalSize());++groups;}
 {Harness h;h.sessions_.erase("actual-child");h.Release();assert(h.sent.empty()&&h.devtools_.empty()&&!h.SelectorWaits().Size()&&!h.SelectorWaits().PhysicalSize()&&h.errors.empty());++groups;}
 {Harness h;h.submit=false;h.Release();assert(h.sent.size()==1&&h.devtools_.empty()&&!h.SelectorWaits().Size()&&!h.SelectorWaits().PhysicalSize()&&h.errors.size()==1);assert(h.deadline_release_failures==std::vector<int>{1}&&h.screenshot_release_failures==std::vector<int>{1});++groups;}
 std::cout<<"Native selector Source sender "<<groups<<" controlled groups PASS; actual browser regression pending\n";
}
