// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "operation_registry.h"
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
struct Host {
  using Dict=CefRefPtr<CefDictionaryValue>;using Callback=std::function<void(bool,const std::string&)>;
  SuiteCutOperationRegistry operations_;bool browser_=true,closing_page_=false;double now=1;
  struct Wire {int owner;std::string method,session;Dict params;bool retained;size_t response_limit;Callback callback;};
  std::vector<Wire> pending;size_t publications=0;bool success=false;std::string error;
  double Now()const{return now;}
  void DevTools(int owner,const std::string& method,Dict params,Callback callback,std::optional<std::string> session={},bool retained=false,size_t response_limit=0){pending.push_back({owner,method,session.value_or("missing"),params,retained,response_limit,std::move(callback)});}
  void Complete(int id,bool ok,const std::string& message="",const std::string& =""){assert(operations_.Finish(id));++publications;success=ok;error=message;}
  uint64_t Begin(double timeout=20){const auto entry=operations_.Begin(1,now,timeout);assert(entry);NativeBringToFront(1,entry->generation);return entry->generation;}
  void Ack(bool ok=true,const std::string& json="{}"){assert(!pending.empty());auto call=std::move(pending.front());pending.erase(pending.begin());assert(call.method=="Page.bringToFront"&&call.params&&call.params->GetSize()==0&&call.session.empty()&&call.retained&&call.response_limit==8192);call.callback(ok,json);}
#include "page_activation_source.inc"
};
int main(){cef_api_hash(CEF_API_VERSION,0);size_t checks=0;
  {Host h;h.Begin();assert(h.pending.size()==1&&h.operations_.Size()==1&&!h.publications);h.Ack();assert(h.publications==1&&h.success&&h.pending.empty()&&!h.operations_.Size());++checks;}
  {Host h;h.Begin();h.Ack(false,R"({"code":-32000,"message":"Actual target rejected activation"})");assert(!h.success&&h.error=="Actual target rejected activation");++checks;}
  {Host h;h.Begin(0);h.now=1000000;h.Ack();assert(h.success);++checks;}
  {Host h;const auto entry=h.operations_.Begin(1,1,20);h.now=21;h.NativeBringToFront(1,entry->generation);assert(!h.success&&h.pending.empty()&&h.error.find("original deadline")!=h.error.npos);++checks;}
  {Host h;h.Begin();h.now=21;h.Ack();assert(!h.success&&h.error.find("original deadline")!=h.error.npos);++checks;}
  {Host h;h.Begin();h.Complete(1,false,"Actual caller cancellation");assert(h.pending.size()==1);h.Ack();assert(h.publications==1&&h.error=="Actual caller cancellation");++checks;}
  {Host h;const auto old=h.Begin();h.Complete(1,false,"Old canceled owner");const auto fresh=h.operations_.Begin(1,2,30);assert(fresh&&fresh->generation!=old);h.Ack();assert(h.publications==1&&h.operations_.IsCurrent(1,fresh->generation));h.NativeBringToFront(1,fresh->generation);h.Ack();assert(h.success&&h.publications==2);++checks;}
  {Host h;h.browser_=false;h.Begin();assert(!h.success&&h.pending.empty()&&h.error=="Native page is closed");++checks;}
  {Host h;h.Begin();h.closing_page_=true;h.Ack();assert(!h.success&&h.error=="Native page is closed");++checks;}
  {Host h;h.Begin();h.Ack(true,"not JSON");assert(!h.success&&h.error=="Invalid native page activation acknowledgement");++checks;}
  {Host a,b;a.Begin();b.Begin();a.Ack();assert(a.success&&!b.publications&&b.pending.size()==1&&b.operations_.Size()==1);b.Ack();assert(b.success);++checks;}
  {Host h;h.NativeBringToFront(1,999);assert(!h.publications&&h.pending.empty());++checks;}
  std::cout<<"Native page activation: "<<checks<<" included Source controls PASS; controlled DevTools ingress, no live focus/OS proof\n";
}
