// SPDX-License-Identifier: MIT
#pragma once
#include "context_har_routes.h"
#include "accepted_inspector_work.h"
#include "context_routes.h"
#include "websocket_routes.h"
#include "context_worker_cursor.h"
#include "context_worker_close_waits.h"
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <cstdint>
#include <string>
#include <vector>
#include "include/cef_client.h"
#include "include/cef_request_context.h"
#include "include/cef_values.h"
#include "network_authentication.h"
#include "context_authentication_controls.h"
#include "cookie_control_protocol.h"

// A real private CEF target channel scoped to an actual request context. It has
// no SDK Page/Frame identity and stays alive until that context is disposed.
class SuiteCutContextWorkers : public std::enable_shared_from_this<SuiteCutContextWorkers> {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  using NetworkResult=std::function<void(bool,const std::string&)>;
  using NetworkApply=std::function<void(bool,NetworkResult)>;
  using BodyFetch=std::function<void(const std::string&,const std::string&,double,std::function<void(bool,std::vector<uint8_t>,const std::string&)>)>;
  using PublicNetworkCommand=std::function<void(int,Dict,double,Reply)>;
  using HeaderApply=std::function<void(Dict,NetworkResult)>;
  struct PostReservation {std::function<void(bool,const std::vector<uint8_t>&,bool)> data;std::function<void()> receipt;};
  struct Hooks {
    std::function<bool(int)>websocket_required;
    std::function<void(int,const std::string&)>websocket_failure;
    std::function<bool(int,const std::string&,suitecut::NativeWebSocketRoutes::Send,suitecut::NativeWebSocketRoutes::Fatal,NetworkResult)>attach_websocket;
    std::function<void(const std::string&)>detach_websocket;
    std::function<bool(const std::string&,Dict)>websocket_event;
    std::function<bool(int,const std::string&,suitecut::NativeContextRoutes::Apply,NetworkResult)> attach_routes;
    std::function<void(const std::string&)> detach_routes;
    std::function<bool(int,const std::string&,suitecut::NativeContextHARRoutes::Apply,NetworkResult)> attach_har;
    std::function<void(const std::string&)> detach_har;
    std::function<bool(int,const std::string&,PublicNetworkCommand,BodyFetch,std::function<void(int)>)> network_owner;
    std::function<void(const std::string&)> network_closed;
    std::function<void(int,const std::string&,Dict,Dict)> network_event;
    std::function<bool(int,const std::string&,uint64_t,size_t,size_t,std::span<const uint8_t>,bool)> network_body;

    std::function<bool(int,const std::string&,BodyFetch)> resource_owner;
    std::function<PostReservation(int,const std::string&,const std::string&)> resource_post;
    std::function<void(const std::string&)> resource_closed;
    std::function<void(int,const std::string&,const std::string&,Dict)> resource_request;
    std::function<void(int,const std::string&,Dict)> resource_event;
    std::function<bool(int,const std::string&,HeaderApply,NetworkResult)> attach_headers;
    std::function<void(const std::string&)> detach_headers;
    std::function<suitecut::NativeNetworkAuthentication::Provider(int)> authentication;
    std::function<bool(int,const std::string&,suitecut::NativeContextAuthenticationControls::Apply,NetworkResult)> attach_authentication;
    std::function<void(const std::string&)> detach_authentication;
    std::function<bool(int,const std::string&,NetworkApply,NetworkResult)> attach_network;
    std::function<void(const std::string&)> detach_network;
    std::function<CefRefPtr<CefRequestContext>(int)> context;
    std::function<void(int,Dict)> event;
    std::function<void(int,Dict)> console;
    std::function<void(int,Dict)> predicate;
    std::function<void(int,const std::string&)> error;
    std::function<void(int)> idle;
    std::function<double()> now;
    std::shared_ptr<suitecut::AcceptedInspectorWork> inspector_work;
    // Actual native broker ownership, never a page or an SDK-provided context.
    std::function<void(int,int,CefRefPtr<CefRequestContext>,Dict,Dict,double,std::shared_ptr<void>,std::function<bool(bool,Dict,const std::string&)>)> route_fetch;
    std::function<void(int)> cancel_route_fetch;
  };
  explicit SuiteCutContextWorkers(Hooks hooks);
  ~SuiteCutContextWorkers();
  void Ensure(int context);
  void WebSocketPolicyReady(int context,NetworkResult ready);
  void Handle(int owner,int context,Dict params,double deadline,Reply reply);
  void CookieControl(int owner,int context,suitecut::CookieMethod method,Dict params,double deadline,Reply reply);
  bool NetworkBody(int context,const std::string& owner,const std::string& body,const std::string& stage);
  bool NetworkPolicyFailure(const std::string&owner,const std::string&error);
  void CancelCookie(int owner);
  void Cancel(int owner);
  void CloseContext(int context);
  // Called only after the real context registry has removed its ownership.
  void ContextGone(int context);
  void Clear();
  bool ContextClosed(int context) const;
  bool AcceptedWorkReady()const;
  size_t BrowserCount() const;
  void Diagnostics(Dict output) const;
 private:
  class Client;
  void Closed(Client* client);
  Hooks hooks_;
  std::map<int,CefRefPtr<Client>> clients_;
  suitecut::NativeContextWorkerCursors cursors_;
  suitecut::NativeContextWorkerCloseWaits close_waits_;
  bool closing_=false;
};
