// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "artifact_controls.h"
#include <cassert>
#include <deque>
#include <iostream>
using Controller=SuiteCutArtifactController;
struct Harness {
 using Dict=Controller::Dict;using Stage=Controller::ResponseStage;
 struct Call{int owner;std::string method;Dict params;Stage stage;Controller::Callback done;};
 std::deque<Call>calls;std::vector<Controller::AssertionCleanupPlan>plans;std::vector<Controller::Callback>cleanups;
 unsigned fires=0,finishes=0,documents=0,view_releases=0;bool handled=true,sync_cleanup=false;
 Controller controller{[this](int owner,const auto&method,auto params,auto done){calls.push_back({owner,method,params,Stage::Unknown,std::move(done)});},[this](const auto&,auto){++fires;},[this](int,bool,const auto&,const auto&){++finishes;}};
 Harness(){
  controller.SetAssertionSend([this](int owner,const auto&method,auto params,Stage stage,auto done){calls.push_back({owner,method,params,stage,std::move(done)});},[](int){return true;});
  controller.SetAssertionCleanup([this](int,auto plan,auto done){if(!handled)return false;plans.push_back(std::move(plan));cleanups.push_back(done);if(sync_cleanup)done(true,"");return true;});
 }
 Dict Config(bool full=false){auto config=CefDictionaryValue::Create();config->SetString("action","screenshot");config->SetString("format","png");config->SetString("scale","css");config->SetString("caret","hide");config->SetString("animations","disabled");config->SetString("style","body { color: red }");config->SetBool("fullPage",full);config->SetBool("omitBackground",false);return config;}
 void Start(int owner,bool full=false){controller.Begin(owner,Config(full),11,1);}
 void Ack(){assert(!calls.empty());auto call=std::move(calls.front());calls.pop_front();std::string reply="{}";
  if(call.stage==Stage::Boolean)reply="{\"result\":{\"type\":\"boolean\",\"value\":true}}";
  if(call.stage==Stage::Viewport)reply="{\"result\":{\"type\":\"object\",\"value\":{\"width\":200,\"height\":120}}}";
  if(call.stage==Stage::Layout)reply="{\"cssVisualViewport\":{\"pageX\":0,\"pageY\":0},\"cssContentSize\":{\"x\":0,\"y\":0,\"width\":200,\"height\":240}}";
  if(call.stage==Stage::PNG)reply="{\"data\":\"controlled transport payload; not a captured image\"}";
  call.done(true,reply);
 }
 void Capture(){for(unsigned n=0;n<32&&!calls.empty()&&calls.front().stage!=Stage::PNG;++n)Ack();assert(!calls.empty()&&calls.front().stage==Stage::PNG);}
 void Plan(){assert(plans.size()==1);const auto&plan=plans[0];assert(plan.root.size()==2&&plan.root[0].method=="Emulation.setDefaultBackgroundColorOverride"&&plan.root[0].stage==Stage::Empty);const auto&style=plan.root[1];assert(style.method=="Runtime.evaluate"&&style.stage==Stage::Boolean&&style.params->GetInt("contextId")==11&&style.params->GetString("expression")==Controller::RestoreExpression(7)&&style.params->GetBool("returnByValue"));}
};
int main(){cef_api_hash(CEF_API_VERSION,0);unsigned groups=0;
 {Harness h;h.Start(7);h.Capture();h.Start(8);h.controller.Cancel(7);h.Plan();assert(h.fires==0&&h.controller.Size()==1&&h.controller.RestorationPhysicalOwned(7)&&h.controller.CapturePhysicalOwned(7)&&h.controller.PhysicalCount()==2);h.cleanups[0](true,"");assert(!h.controller.RestorationPhysicalOwned(7)&&h.controller.CapturePhysicalOwned(7)&&h.calls.size()==1);h.Ack();assert(!h.controller.CapturePhysicalOwned(7)&&h.calls.size()==1&&h.calls.front().owner==8);++groups;}
 {Harness h;h.Start(7);h.Capture();h.Start(8);h.controller.Cancel(7);h.Ack();assert(h.calls.empty()&&h.controller.RestorationPhysicalOwned(7)&&!h.controller.ExternalCaptureAvailable());h.cleanups[0](true,"");assert(h.calls.size()==1&&h.calls.front().owner==8);++groups;}
 {Harness h;h.Start(7);h.Capture();h.Start(8);h.controller.Cancel(7);h.Ack();h.cleanups[0](false,"actual restore rejection");assert(h.calls.empty()&&h.controller.RestorationPhysicalOwned(7)&&h.controller.PhysicalCount()==1);h.controller.Clear();assert(h.controller.PhysicalCount()==1);h.controller.AssertionChannelClosed();assert(!h.controller.PhysicalCount());++groups;}
 {Harness h;h.sync_cleanup=true;h.Start(7);h.Capture();h.Start(8);h.controller.Cancel(7);assert(!h.controller.RestorationPhysicalOwned(7)&&h.controller.CapturePhysicalOwned(7)&&h.calls.size()==1);h.Ack();assert(h.calls.size()==1&&h.calls.front().owner==8);++groups;}
 {Harness h;h.Start(7);h.Capture();h.controller.Cancel(7);h.Ack();h.cleanups[0](true,"");assert(!h.controller.PhysicalCount());h.Start(7);h.Capture();h.controller.Cancel(7);h.Ack();assert(h.cleanups.size()==2&&h.controller.RestorationPhysicalOwned(7));h.cleanups[0](true,"");assert(h.controller.RestorationPhysicalOwned(7));h.cleanups[1](true,"");assert(!h.controller.PhysicalCount());++groups;}
 {Harness h;h.controller.SetDocumentPreparation([&](int,const auto&,auto done){++h.documents;done(true,"");},[&](int,bool){assert(false);});h.controller.SetViewLease([](int,int width,int height,auto done){assert(width==200&&height==240);done(true,"");},[&](int){++h.view_releases;});h.Start(7,true);h.Capture();h.controller.Cancel(7);assert(h.plans.size()==1&&h.plans[0].documents&&h.plans[0].physical_view&&h.plans[0].root.size()==3&&!h.view_releases);const auto&viewport=h.plans[0].root[0];assert(viewport.method=="Emulation.setDeviceMetricsOverride"&&viewport.params->GetInt("width")==200&&viewport.params->GetInt("height")==120&&!viewport.params->GetBool("mobile"));h.Ack();h.cleanups[0](true,"");assert(!h.controller.PhysicalCount());++groups;}
 {Harness h;h.handled=false;h.Start(7);h.Capture();h.controller.Cancel(7);assert(h.plans.empty()&&h.cleanups.empty()&&h.fires==2&&!h.controller.RestorationPhysicalOwned(7));h.Ack();assert(!h.controller.PhysicalCount());++groups;}
 {Harness h;h.Start(7);h.controller.Cancel(7);assert(h.plans.size()==1&&h.plans[0].root.empty()&&h.controller.RestorationPhysicalOwned(7));h.Ack();assert(h.calls.empty()&&h.finishes==0);h.cleanups[0](true,"");assert(!h.controller.PhysicalCount());++groups;}
 {Harness h;h.Start(7);h.Capture();h.controller.Clear();assert(h.controller.CapturePhysicalOwned(7)&&h.controller.PhysicalCount()==1);h.Ack();assert(!h.controller.PhysicalCount());++groups;}
 {Harness h;unsigned cleanup_calls=0;h.controller.SetAssertionCleanup([&](int owner,auto,auto done){++cleanup_calls;h.controller.Cancel(owner);done(true,"");return true;});h.Start(7);h.Capture();h.Start(8);h.controller.Cancel(7);assert(cleanup_calls==1&&h.controller.CapturePhysicalOwned(7)&&h.controller.Size()==1);h.Ack();assert(h.calls.size()==1&&h.calls.front().owner==8);++groups;}
 {Harness h;h.controller.SetAssertionCleanup([](int,auto,auto)->bool{throw 17;});h.Start(7);h.Capture();h.Start(8);h.controller.Cancel(7);h.Ack();assert(h.finishes==1&&h.controller.RestorationPhysicalOwned(7)&&h.calls.empty()&&!h.fires);h.controller.AssertionChannelClosed();++groups;}
 {Harness h;unsigned retries=0;h.controller.SetRetry([&](int){++retries;});h.Start(7);h.Capture();auto capture=std::move(h.calls.front());h.calls.pop_front();capture.done(false,"{\"message\":\"Execution context was destroyed\"}");assert(!retries&&h.finishes==1&&!h.controller.CapturePhysicalOwned(7));h.controller.Cancel(7);h.cleanups[0](true,"");++groups;}
 {Harness h;unsigned receipts=0;uint64_t actual_token=0;h.controller.SetAssertionCaptured([&](int owner,uint64_t token,bool ok,const auto&){assert(owner==7&&token==actual_token&&ok&&!h.controller.CapturePhysicalOwned(7));++receipts;return true;});h.Start(7);actual_token=h.controller.Generation(7);h.Capture();h.controller.Cancel(7);h.Ack();assert(receipts==1&&h.finishes==0&&h.controller.RestorationPhysicalOwned(7));h.cleanups[0](true,"");++groups;}
 {Harness h;unsigned receipts=0;h.controller.SetAssertionCaptured([&](int owner,uint64_t token,bool,const auto&){assert(owner==7&&token==h.controller.Generation(7));++receipts;h.controller.Cancel(7);return true;});h.Start(7);h.Capture();h.Ack();assert(receipts==1&&h.finishes==0&&h.plans.size()==1&&h.controller.RestorationPhysicalOwned(7));h.cleanups[0](true,"");++groups;}
 std::cout<<"PASS "<<groups<<" actual artifact controller cleanup-plan/barrier/token controls; controlled sender/view receipts, no browser capture claim\n";
}
