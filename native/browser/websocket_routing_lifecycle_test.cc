// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"

// Drives actual adapter state with out-of-order native protocol receipts.
// No renderer, transport delivery or custom CEF runtime acceptance is claimed.
class RoutingLifecycle {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Operations {
    std::map<int,uint64_t> live;
    std::set<int> expired;
    bool IsCurrent(int id,uint64_t generation) const {const auto it=live.find(id);return it!=live.end()&&it->second==generation;}
    bool IsExpired(int id,uint64_t,double) const {return expired.contains(id);}
  } operations_;
  struct Call {std::function<void(bool,const std::string&)> reply;};
  std::vector<Call> calls;
  std::map<int,bool> results;
  bool closing_page_=false,retired=false;
  bool reject_dispatch=false;
  bool NativeWebSocketPolicyRootOwned()const{return false;}
  static double Now(){return 1;}
  static std::string Json(Dict value){auto wrapped=CefValue::Create();wrapped->SetDictionary(value);return CefWriteJSON(wrapped,JSON_WRITER_DEFAULT);}
  void Complete(int id,bool ok,const std::string& = "",const std::string& = ""){results[id]=ok;operations_.live.erase(id);}
  void WakeDeadlines(){}
  void NetworkFatal(const std::string&){retired=true;}
  void NativeNetworkSend(const std::string&,const std::string&,Dict,std::function<void(bool,const std::string&)> reply,double){
    if(reject_dispatch){reply(false,"Native network command dispatch failed");return;}
    calls.push_back({std::move(reply)});
  }
  void DevTools(int,const std::string&,Dict,std::function<void(bool,const std::string&)>,std::optional<std::string>,bool,size_t){}
#include "controllers/network/websocket_routing.inc"
  void Enable(int id){operations_.live[id]=1;auto input=CefDictionaryValue::Create();input->SetString("action","routeEnable");NativeWebSocketRouteCommand(id,1,input);}
  void Disable(int id){operations_.live[id]=1;auto input=CefDictionaryValue::Create();input->SetString("action","routeDisable");input->SetString("routingToken",std::to_string(native_routed_epoch_));NativeWebSocketRouteCommand(id,1,input);}
  static std::string Supported(){return "{\"revision\":1,\"maxSockets\":128,\"maxPendingMessages\":64,\"maxPendingBytes\":2097152}";}
  void Ack(size_t index,bool ok=true,const std::string& value=Supported()){auto done=std::move(calls.at(index).reply);assert(done);done(ok,value);}
};
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  {
    RoutingLifecycle h;h.Enable(1);auto late=h.calls.at(0).reply;h.operations_.live.erase(1);
    h.Enable(2);assert(!h.results.at(2)&&h.calls.size()==1);
    h.Ack(0);assert(h.calls.size()==2&&h.native_routed_configuring_&&!h.native_routed_active_);
    h.Enable(3);assert(!h.results.at(3)&&h.calls.size()==2);
    h.Ack(1);assert(!h.native_routed_configuring_);
    h.Enable(4);assert(h.native_routed_epoch_==2);h.Ack(2);assert(h.results.at(4)&&h.native_routed_active_);
    late(true,RoutingLifecycle::Supported());assert(h.native_routed_epoch_==2&&h.native_routed_active_&&h.calls.size()==3);
  }
  {
    RoutingLifecycle h;h.Enable(1);h.NativeWebSocketRouteFail("queue failure while enabling");
    assert(h.calls.size()==2&&h.native_routed_configuring_);
    h.Ack(1);assert(h.native_routed_configuring_&&h.native_routed_enable_pending_);
    h.Enable(2);assert(!h.results.at(2));
    h.Ack(0);assert(!h.native_routed_active_&&!h.native_routed_configuring_&&!h.results.at(1));
    h.Enable(3);h.Ack(2);assert(h.results.at(3)&&h.native_routed_active_);
  }
  {
    RoutingLifecycle h;h.Enable(1);h.Ack(0);h.Disable(2);
    assert(h.native_routed_configuring_&&!h.native_routed_active_);
    h.Ack(1,false,"{\"code\":-32000,\"message\":\"uncertain cleanup\"}");
    assert(h.retired&&h.native_routed_retiring_&&!h.results.at(2));
    h.Enable(3);assert(!h.results.at(3)&&h.calls.size()==2);
  }
  {
    RoutingLifecycle h;h.Enable(1);h.Ack(0);h.Disable(2);auto late=h.calls.at(1).reply;
    h.operations_.expired.insert(2);h.Ack(1);
    assert(!h.results.at(2)&&!h.native_routed_configuring_&&!h.retired);
    h.Enable(3);h.Ack(2);assert(h.results.at(3)&&h.native_routed_active_);
    late(true,RoutingLifecycle::Supported());assert(h.native_routed_active_&&h.native_routed_epoch_==2);
  }
  {
    RoutingLifecycle h;h.reject_dispatch=true;h.Enable(1);
    assert(!h.results.at(1)&&!h.native_routed_configuring_&&!h.native_routed_enable_pending_);
    h.reject_dispatch=false;h.Enable(2);h.Ack(0);assert(h.results.at(2));
  }
  {
    RoutingLifecycle h;h.Enable(1);h.operations_.live.clear();h.closing_page_=true;
    h.Ack(0,false,"actual channel closed");assert(h.calls.size()==1&&h.native_routed_retiring_);
  }
  {
    RoutingLifecycle h;h.Enable(1);h.Ack(0,false,"{\"code\":-32601,\"message\":\"Method not found\"}");
    assert(!h.retired&&!h.native_routed_configuring_&&!h.results.at(1)&&h.calls.size()==1);
  }
}
