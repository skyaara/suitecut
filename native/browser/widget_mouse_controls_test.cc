// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "widget_mouse_controls.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <deque>
#include <iostream>
using Mouse=suitecut::NativeWidgetMouse;
struct Call{std::string method;Mouse::Dict params;Mouse::Reply done;};
int main(){assert(cef_api_hash(CEF_API_VERSION,0));
 std::deque<Call>calls;bool current=true;int fatal=0;
 Mouse::Hooks hooks;hooks.now=[] {return 100.;};hooks.current=[&](const auto&){return current;};hooks.fatal=[&](const auto&){++fatal;};
 hooks.send=[&](const std::string&,const std::string&method,auto params,auto done,double){calls.push_back({method,params,std::move(done)});};
 auto mouse=std::make_shared<Mouse>(hooks);Mouse::Pin pin{"frame","loader","","context",1};
 auto reply=[&](const std::string&method,const std::string&json,bool ok=true){
  if(method=="Input.acquireMouseWidget"&&!calls.empty()&&calls.front().method=="Input.setInterceptDrags"){
   auto setup=std::move(calls.front());calls.pop_front();assert(!setup.params->GetBool("enabled"));setup.done(true,"{}");
  }
  assert(!calls.empty());auto call=std::move(calls.front());calls.pop_front();assert(call.method==method);call.done(ok,json);
 };
 const auto probe=R"({"schemaVersion":1,"protocol":"suitecut-widget-mouse-v1","maxLeases":64,"maxPending":64})";
 bool ready=false;mouse->Probe([&](bool ok,const auto&){ready=ok;});assert(!ready&&mouse->physical());reply("Input.getMouseWidgetReceiptCapabilities",probe);assert(ready&&mouse->available());
 auto event=[](const char*type,const char*button="left"){auto p=CefDictionaryValue::Create();p->SetString("type",type);p->SetString("button",button);p->SetDouble("x",50);p->SetDouble("y",60);p->SetInt("clickCount",1);return p;};
 auto ack=[](int sequence,const char*outcome="rendererAck",int source=2){return std::string("{\"receipt\":{\"lease\":\"lease\",\"sequence\":")+std::to_string(sequence)+",\"documentToken\":\"document\",\"outcome\":\""+outcome+"\",\"ackObserved\":true,\"ackSource\":"+std::to_string(source)+",\"ackState\":1}}";};
 bool dispatched=false,finished=false;
 mouse->Dispatch(1,pin,event("mousePressed"),1000,[&](bool ok,const auto&){dispatched=ok;});reply("Input.acquireMouseWidget",R"({"lease":"lease","documentToken":"document"})");assert(!dispatched);
 mouse->Finish(1,true,[&](bool ok,const auto&){finished=ok;});assert(!finished&&calls.size()==1);
 reply("Input.dispatchMouseEventToWidget",ack(1));assert(dispatched&&!finished&&mouse->held()&&calls.size()==1);
 assert(calls.front().params->GetString("lease")=="lease"&&calls.front().params->GetInt("sequence")==2&&calls.front().params->GetString("type")=="mouseReleased"&&calls.front().params->GetDouble("x")==50);
 reply("Input.dispatchMouseEventToWidget",ack(2));assert(!finished&&!mouse->held()&&calls.front().method=="Input.releaseMouseWidget");reply("Input.releaseMouseWidget","{}");assert(finished&&!mouse->physical());
 // A logically canceled acquisition remains physically owned. Its eventual
 // identity is released directly without ever submitting the canceled press.
 dispatched=true;finished=false;mouse->Dispatch(2,pin,event("mousePressed"),1000,[&](bool ok,const auto&){dispatched=ok;});reply("Input.setInterceptDrags","{}");mouse->Finish(2,true,[&](bool ok,const auto&){finished=ok;});reply("Input.acquireMouseWidget",R"({"lease":"lease","documentToken":"document"})");assert(!dispatched&&!finished&&calls.front().method=="Input.releaseMouseWidget");reply("Input.releaseMouseWidget","{}");assert(finished);
 // Native nonblocking browser movement ACK is allowed as browser evidence;
 // it must never satisfy a press/release renderer-delivery requirement.
 dispatched=false;mouse->Dispatch(3,pin,event("mouseMoved","none"),1000,[&](bool ok,const auto&){dispatched=ok;});reply("Input.acquireMouseWidget",R"({"lease":"lease","documentToken":"document"})");reply("Input.dispatchMouseEventToWidget",ack(1,"browserAck",3));assert(dispatched);mouse->Finish(3,false,[](bool ok,const auto&){assert(ok);});reply("Input.releaseMouseWidget","{}");
 // A successful down retains the immutable native lease across SDK calls.
 dispatched=false;mouse->Dispatch(4,pin,event("mousePressed"),1000,[&](bool ok,const auto&){dispatched=ok;});reply("Input.acquireMouseWidget",R"({"lease":"lease","documentToken":"document"})");reply("Input.dispatchMouseEventToWidget",ack(1));assert(dispatched&&mouse->held());mouse->Finish(4,false,[](bool ok,const auto&){assert(ok);});assert(calls.empty());
 current=false;finished=false;mouse->Finish(4,true,[&](bool ok,const auto&){assert(!ok);finished=true;});assert(fatal==1&&!finished&&calls.empty());mouse->ChannelClosed();assert(finished&&!mouse->held());
 auto stock=std::make_shared<Mouse>(hooks);ready=true;stock->Probe([&](bool ok,const auto&){ready=ok;});reply("Input.getMouseWidgetReceiptCapabilities","method absent",false);assert(!ready&&!stock->available());bool refused=false;stock->Dispatch(5,pin,event("mousePressed"),1000,[&](bool ok,const auto&error){assert(!ok&&!error.empty());refused=true;});assert(refused&&calls.empty());
 current=true;auto closing=std::make_shared<Mouse>(hooks);closing->Probe([](bool ok,const auto&){assert(ok);});reply("Input.getMouseWidgetReceiptCapabilities",probe);
 bool raw_retired=false,finish_retired=false;
 closing->Dispatch(6,pin,event("mousePressed"),1000,[&](bool ok,const auto&){assert(!ok);raw_retired=true;});
 reply("Input.setInterceptDrags","{}");closing->Finish(6,true,[&](bool ok,const auto&){assert(!ok);finish_retired=true;});closing->ChannelClosed();
 assert(closing->physical()&&!raw_retired&&!finish_retired);
 reply("Input.acquireMouseWidget","actual inspector channel closed",false);
 assert(!closing->physical()&&raw_retired&&finish_retired&&calls.empty());
 std::cout<<"Native widget mouse: strict live capability contract, exact canceled native ACK ownership, original-lease cleanup, nonblocking distinction, resident held gesture, changed-document fail-close and stock unsupported PASS\n";
}
