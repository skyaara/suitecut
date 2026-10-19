// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_values.h"
#include "curl_transport.h"
#include "http_storage_visitor.h"
#include "http_trace_controls.h"
#include "http_request_policy.h"
#include "http_upload_preparations.h"
#include "api_cookie_jar.h"
#include "api_origins_snapshot.h"

// UI-thread native HTTP ownership. libcurl makes requests; the actual CEF
// cookie manager remains the browser-context jar. No SDK networking is used.
class SuiteCutHTTPControls : public std::enable_shared_from_this<SuiteCutHTTPControls> {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  struct BrowserContext {CefRefPtr<CefRequestContext> cef;int id=0;};
  struct Hooks {
    std::function<BrowserContext(int)> page_context;
    std::function<BrowserContext(int)> browser_context;
    std::function<void(const suitecut::HTTPLookupControls::Event&)> lookup_event;
    std::function<void()> wake;
    std::function<double()> now;
    std::function<Dict(int)> context_policy;
    std::function<Dict(int)> context_headers;
    std::function<std::string()> default_user_agent;
    std::function<void(std::function<void(BrowserContext,const std::string&)>)> create_context;
    std::function<void(int,Reply)> close_context;
    std::function<void(int,int,const std::string&,Dict,Reply)> context_command;
    std::function<void(int)> cancel_context_command;
    std::function<void(int,int,Dict,CefRefPtr<CefListValue>,double,Reply)> match_cookies;
    std::function<bool(int)> physical_owner;
    std::function<void(int)> settled_context;
  };
  explicit SuiteCutHTTPControls(Hooks hooks):hooks_(std::move(hooks)){}
  void Handle(int owner,Dict params,Reply reply,double original_deadline=0,std::shared_ptr<void> physical_lease={});
  using RouteFetchReply=std::function<bool(bool,Dict,const std::string&)>;
  void RouteFetch(int owner,int page,Dict request,Dict options,RouteFetchReply reply,double deadline,std::shared_ptr<void> physical_lease);
  void RouteFetchContext(int owner,BrowserContext expected,Dict request,Dict options,RouteFetchReply reply,double deadline,std::shared_ptr<void> physical_lease);
  void SetTracing(std::shared_ptr<suitecut::HTTPTraceControls> tracing){tracing_=std::move(tracing);}
  bool ContextExists(int context)const{return contexts_.contains(context);}
  size_t TracePhysical()const{return tracing_?tracing_->Physical():0;}
  bool InternalCookieCommand(int owner,int browser_context,const std::string&action,Dict params,double deadline,Reply reply);
  bool ValidateInternalCookies(int owner,int browser_context,CefRefPtr<CefListValue> cookies,double deadline,std::string& error);
  bool ValidateInternalStorage(int owner,int browser_context,Dict state,double deadline,std::string&error);
  void Cancel(int owner);
  std::string TimeoutError(int owner)const;
  void ContextClosed(int context);
  void ContextClosing(int context);
  bool ContextReady(int context)const;
  size_t PhysicalProfiles()const;
  void Clear();
  void Tick(double now);
  bool AcceptedWorkReady()const;
  bool NeedsTick() const {return !uploads_.empty() || !active_.empty() || !storage_queries_.empty() || HasProfileWork() || (tracing_&&tracing_->NeedsTick());}
  void Diagnostics(Dict output) const;
 private:
  struct Context {
    CefRefPtr<CefRequestContext> cef;int browser_context=0;
    CefRequest::HeaderMap headers;suitecut::NativeHTTPRequestPolicy policy;Dict policy_overrides;
    std::shared_ptr<suitecut::NativeAPICookieJar> jar;
    std::shared_ptr<const suitecut::APIOriginsSnapshot> origins;
    bool owned=false,closing=false,close_started=false,actually_closed=false,initializing=false;size_t physical_visits=0;
    double creation_deadline=0;std::string close_error;
    std::vector<std::pair<int,Reply>> dispose_replies;std::set<int> physical_owners;
  };
  suitecut::NativeAPICookieJar::Clock JarClock(double deadline) const;
  std::shared_ptr<suitecut::NativeAPICookieJar::Budget> jar_budget_=std::make_shared<suitecut::NativeAPICookieJar::Budget>();
  struct ScopeCommand{int context;uint64_t generation;Reply reply;double deadline;bool initialize;std::shared_ptr<const suitecut::APIOriginsSnapshot> origins;};
  std::shared_ptr<suitecut::APIOriginsBudget> origins_budget_=std::make_shared<suitecut::APIOriginsBudget>();
  bool PublishOrigins(int context,Dict result,bool indexed_db,bool opfs,double deadline,std::string&error);
  std::map<int,ScopeCommand> scope_commands_;
  std::shared_ptr<std::atomic_size_t> metadata_bytes_=std::make_shared<std::atomic_size_t>(0);
  void BeginDispose(int context,Reply reply={},int owner=0);
  void FinishDispose(int context);
  void ScopeCommandStart(int owner,int context,const std::string&action,Dict params,Reply reply,bool initialize=false,double deadline=0);
  void OrderCookies(int context,CefRefPtr<CefListValue>cookies,std::function<void(CefRefPtr<CefListValue>,const std::string&)>done,size_t retained_bytes=0);
  void ProfileTick(double now);
  bool HasProfileWork()const;
  struct State {
    int owner=0,context=0,remaining_redirects=20;
    std::string lookup_key,preparation_token;double deadline=0;bool preparing=false,stream_prepared=false;
    uint64_t generation=0;
    suitecut::NativeHTTPRequestPolicy policy;
    std::string url,method,error,api_name="fetch";int timeout_ms=30000,retries=0;bool ignore_https_errors=false;
    CefRequest::HeaderMap headers;
    std::vector<uint8_t> upload,body;
    Reply reply;std::shared_ptr<void> physical_lease;
    bool fail_status=false,follow_redirects=true;
    std::vector<std::pair<std::string,Dict>> cookie_actions;size_t cookie_index=0;std::optional<suitecut::NativeCurlTransport::Response> pending_response;
  };
  struct StorageQuery {int context;uint64_t generation;double deadline;std::shared_ptr<suitecut::NativeHTTPStorageVisitor::Control> canceled;Reply reply;};
  void StopStorage(int owner,const std::string& error,bool reply);
  struct Response {int context=0,status=0;std::string url,status_text,method;std::vector<std::pair<std::string,std::string>> headers;std::vector<uint8_t> body;std::string remote_ip;int remote_port=0;std::optional<suitecut::HTTPConnectionSecurity> security;suitecut::HTTPConnectionTiming timing;};
  struct Upload {int context=0;double updated=0;std::vector<uint8_t> bytes;};
  static constexpr size_t kMaxUpload=8*1048576,kUploadBudget=16*1048576;
  static constexpr size_t kMaxBody=64*1048576,kBodyBudget=128*1048576;
  static bool URL(const std::string& url);
  static std::string Origin(const std::string& url);
  static bool Headers(CefRefPtr<CefListValue> input,CefRequest::HeaderMap& output);
  static std::string Lower(std::string value);
  static void Merge(CefRequest::HeaderMap& target,const CefRequest::HeaderMap& extra);
  static void Remove(CefRequest::HeaderMap& target,const std::string& name);
  bool Multipart(const std::shared_ptr<State>&state,CefRefPtr<CefListValue> entries,size_t available,std::string&error);
  void PreparedMultipart(int owner,int context,Dict params,Reply reply);
  size_t RetainedUploadCapacity()const;
  size_t RetainedResponseCapacity()const;
  suitecut::NativeHTTPUploadPreparations preparations_;
  void Start(const std::shared_ptr<State>& state);
  void SendRequest(const std::shared_ptr<State>& state,suitecut::NativeCurlTransport::Request request);
  void CookiesReady(int owner,uint64_t generation,std::string cookies,const std::string& error);
  bool Data(int owner,uint64_t generation,const void* bytes,size_t size);
  void Complete(int owner,uint64_t generation,suitecut::NativeCurlTransport::Response response);
  void CommitCookies(const std::shared_ptr<State>& state);
  void Finish(const std::shared_ptr<State>& state,suitecut::NativeCurlTransport::Response response);
  void Stop(int owner,const std::string& error,Dict details=nullptr);
  Dict Metadata(const std::string& id,const Response& response) const;
  std::shared_ptr<suitecut::HTTPTraceControls> tracing_;
  Hooks hooks_;bool closed_=false;
  suitecut::HTTPLookupControls lookups_{[this](const suitecut::HTTPLookupControls::Event&event){if(hooks_.lookup_event)hooks_.lookup_event(event);}};
  suitecut::NativeCurlTransport transport_;
  int next_context_=0;uint64_t next_response_=0,generation_=0;
  std::map<int,Context> contexts_;
  std::map<int,int> route_contexts_;
  std::map<int,std::shared_ptr<State>> active_;
  std::map<int,StorageQuery> storage_queries_;
  std::shared_ptr<std::atomic<size_t>> storage_cookie_bytes_=std::make_shared<std::atomic<size_t>>(0);
  std::map<std::string,Response> responses_;
  std::map<std::string,Upload> uploads_;
  size_t upload_bytes_=0,active_upload_bytes_=0,body_bytes_=0,active_body_bytes_=0,pending_cookie_saves_=0,pending_cookie_visits_=0;
};
