// SPDX-License-Identifier: MIT
#include "cdp_backend.hpp"
#include "cdp_internal.hpp"
#include "cdp_network.hpp"
#include "cdp_media.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace suitecut::detail {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t command_limit = 65536;
constexpr std::size_t queued_limit = 4 * 1024 * 1024;
constexpr std::size_t resource_limit = 4096;
// Only the command worker sets this pointer. Cancellation changes the pointed
// flag atomically, so a multi-step helper stops before its next CDP operation.
thread_local const std::atomic<bool>* active_cancellation = nullptr;
thread_local unsigned cleanup_depth = 0;
void check_active(Deadline deadline) {
  if (Clock::now() >= deadline) throw std::runtime_error("Chromium command original deadline expired");
  if (!cleanup_depth && active_cancellation && active_cancellation->load(std::memory_order_relaxed))
    throw std::runtime_error("Chromium request canceled; dispatched operation outcome is unknown");
}
void duration_valid(std::chrono::milliseconds timeout, bool zero = false) {
  if (timeout.count() < (zero ? 0 : 1) || timeout > std::chrono::hours(24))
    throw std::invalid_argument("Chromium timeout must be positive and at most 24 hours");
}
int positive_id(const Json& fields, const char* name, int fallback = 0) {
  if (!fields.contains(name)) return fallback;
  const auto id = fields.at(name).as_int();
  if (id < 1 || id > INT_MAX) throw std::invalid_argument(std::string("Invalid Chromium ") + name);
  return static_cast<int>(id);
}
Packet packet(Json header, const Json& body, std::size_t limit) {
  // A missing payload is the existing native representation of JSON null.
  // This also leaves error responses usable with very small payload budgets.
  const auto text = body.is_null() ? std::string{} : body.dump({limit, 64, 1024 * 1024, limit});
  header["version"] = 1;
  header["bytes"] = text.size();
  Packet result;
  result.header = header.dump();
  result.payload.assign(text.begin(), text.end());
  return result;
}
Packet response(int id, int page, bool ok, const Json& body, std::size_t limit,
                const std::string& error = {}) {
  Json header{{"version", 1}, {"type", "response"}, {"id", id}, {"pageId", page}, {"ok", ok}};
  if (!ok) header["error"] = error.substr(0, 16384);
  return packet(std::move(header), body, limit);
}
Json unwrap(Json evaluated) {
  if (evaluated.contains("exceptionDetails"))
    throw std::runtime_error("Chromium JavaScript evaluation failed: " + evaluated.at("exceptionDetails").dump());
  auto& remote = evaluated.at("result");
  if (remote.contains("unserializableValue"))
    throw std::runtime_error("JavaScript value is not JSON-compatible; use evaluate_raw");
  if (remote.contains("value")) return std::move(remote.at("value"));
  if ((remote.contains("type") && remote.at("type").as_string() == "undefined") ||
      (remote.contains("subtype") && remote.at("subtype").as_string() == "null")) return Json{};
  throw std::runtime_error("JavaScript result has no JSON value; use evaluate_raw");
}
void reject_unknown(const Json& params, std::initializer_list<const char*> allowed) {
  if (!params.is_object()) throw std::invalid_argument("Chromium context params must be an object");
  for (const auto& [key, value] : params.as_object()) {
    (void)value;
    if (std::none_of(allowed.begin(), allowed.end(), [&](const char* name) { return key == name; }))
      throw std::runtime_error("Chromium backend does not support context option: " + key);
  }
}
bool continuation_method(const Json& fields) {
  const auto family=fields.at("command").as_string();
  if(family=="dialog"||family=="console")return true;
  if(family=="network") {
    const auto action=fields.contains("action")?fields.at("action").as_string():std::string{};
    if(action=="requests"||action=="responses"||action=="exportHar"||action=="harStop")return true;
  }
  if (fields.at("command").as_string() != "devtools" || !fields.contains("method") ||
      !fields.at("method").is_string()) return false;
  const auto& method = fields.at("method").as_string();
  return method == "Page.handleJavaScriptDialog" || method == "Runtime.terminateExecution" ||
         method == "Runtime.runIfWaitingForDebugger" || method.starts_with("Fetch.") ||
         method.starts_with("Debugger.");
}
Json context_params(const std::string& context) {
  return context.empty()?Json(Json::object{}):Json{{"browserContextId",context}};
}
bool cdp_error_is(const std::exception& error,int code,const std::string& message) {
  const std::string text=error.what();constexpr std::string_view prefix="CDP error: ";
  if(!text.starts_with(prefix))return false;
  try {
    const auto detail=Json::parse(std::string_view(text).substr(prefix.size()),{65536,16,1024,65536});
    return detail.at("code").as_int()==code&&detail.at("message").as_string()==message;
  } catch(...) {return false;}
}
} // namespace

void cdp_check_active(Deadline deadline) { check_active(deadline); }
CdpCleanupScope::CdpCleanupScope() { ++cleanup_depth; }
CdpCleanupScope::~CdpCleanupScope() { --cleanup_depth; }

Json CdpRuntime::call(CdpPage& page, const std::string& method, Json params, Deadline deadline) {
  check_active(deadline);
  return wire->call(method, std::move(params), page.session, deadline);
}
Json CdpRuntime::evaluate(CdpPage& page, const std::string& expression, Deadline deadline, bool isolated) {
  const auto once=[&]() -> Json {
  Json params{{"expression", expression}, {"awaitPromise", true}, {"returnByValue", true},
              {"userGesture", false}};
  if (isolated) {
    const auto tree = call(page, "Page.getFrameTree", Json::object{}, deadline);
    const Json* selected=nullptr;
    std::function<void(const Json&)> find=[&](const Json& node){
      if(selected)return;
      if(page.frame_id.empty()||node.at("frame").at("id").as_string()==page.frame_id){selected=&node.at("frame");return;}
      if(node.contains("childFrames"))for(const auto& child:node.at("childFrames").as_array())find(child);
    };
    find(tree.at("frameTree"));
    if(!selected)throw std::runtime_error("Targeted Chromium frame is detached");
    const auto& frame=*selected;
    const auto loader = frame.contains("loaderId") ? frame.at("loaderId").as_string() : std::string{};
    if (page.isolated_context == 0 || page.isolated_loader != loader || page.isolated_frame!=frame.at("id").as_string()) {
      const auto world = call(page, "Page.createIsolatedWorld",
          Json{{"frameId", frame.at("id")},
               {"worldName", "suitecut-private-" + page.session},
               {"grantUniveralAccess", false}}, deadline);
      page.isolated_context = positive_id(world, "executionContextId");
      if (!page.isolated_context) throw std::runtime_error("Chromium isolated world has no execution context");
      page.isolated_loader = loader;
      page.isolated_frame=frame.at("id").as_string();
    }
    params["contextId"] = page.isolated_context;
  } else if(!page.frame_id.empty()) {
    const int context=network?network->execution_context(page.session,page.frame_id):0;
    if(!context)throw std::runtime_error("Targeted frame default execution context is unavailable or detached");
    params["contextId"]=context;
  }
  return call(page, "Runtime.evaluate", std::move(params), deadline);
  };
  for(int attempt=0;;++attempt) {
    try {return once();}
    catch(const std::exception& error) {
      // This exact CDP rejection occurs before the expression is executed.
      // Never replay a context-destroyed error or a JavaScript exception.
      if(!isolated||attempt!=0||!cdp_error_is(error,-32000,"Cannot find context with specified id"))throw;
      page.isolated_context=0;page.isolated_loader.clear();page.isolated_frame.clear();
      check_active(deadline);
      std::this_thread::sleep_until(std::min(deadline,Clock::now()+std::chrono::milliseconds{20}));
      check_active(deadline);
    }
  }
}
Json CdpRuntime::value(CdpPage& page, const std::string& expression, Deadline deadline, bool isolated) {
  return unwrap(evaluate(page, expression, deadline, isolated));
}
bool CdpRuntime::network_idle(CdpPage& page,Deadline deadline)const {
  check_active(deadline);
  if(!network||!network->idle(page.session,std::chrono::milliseconds{500}))return false;
  for(const auto&[frame,session]:page.frame_sessions){(void)frame;if(!network->idle(session,std::chrono::milliseconds{500}))return false;}
  return true;
}
Json CdpRuntime::browser_events(const std::string& method,std::uint64_t after)const {return network?network->events(method,after):Json(Json::array{});}

struct CdpBackend::Impl {
  CdpRuntime runtime;
  mutable std::mutex mutex;
  std::mutex close_mutex;
  std::condition_variable changed;
  struct Work {
    int id = 0, page = 1;
    Json fields;
    Deadline deadline;
    std::size_t bytes = 0;
    // Immutable snapshot for the narrow continuation lane. That worker never
    // accesses the command worker's runtime pages, contexts, or DOM state.
    std::string continuation_session;
    std::promise<Packet> promise;
    std::atomic<bool> canceled{false};
  };
  std::map<int, std::shared_ptr<Work>> pending;
  std::deque<std::shared_ptr<Work>> queue;
  std::deque<std::shared_ptr<Work>> continuations;
  std::deque<std::pair<CdpAutomatic,std::size_t>> automatic;
  std::size_t queue_bytes = 0;
  std::deque<Packet> events;
  std::map<int, Packet> page_ready;
  std::map<std::string, int> session_pages;
  std::map<int, std::string> page_sessions;
  std::map<std::string, int> target_pages;
  std::set<int> invalidated, externally_closed;
  std::set<std::string> detached_sessions;
  // A created target remains owned here until attachment is committed or a
  // real close receipt arrives. Failed cleanup never discards that identity.
  std::map<std::string, int> target_debt;
  std::set<std::string> context_debt;
  std::set<std::string> owned_targets,owned_contexts;
  bool default_creation_inflight=false,uncertain_default_creation=false;
  std::exception_ptr terminal;
  Packet ready_packet;
  ClientStats counters;
  int next_id = 0;
  bool closing = false;
  bool attached_cleanup_started=false;
  // Four fixed threads per client including the connection's I/O thread.
  // No request allocates a thread.
  std::thread worker, continuation_worker, deadlines;
  std::string browser_version;

  std::size_t event_budget;
  explicit Impl(const LaunchOptions& options) : event_budget(options.max_event_bytes/2) { runtime.options = options; }
  void automatic_command(CdpAutomatic command) {
    const auto bytes=command.params.dump({65536,32,16384,65536}).size()+command.method.size()+command.session.size();
    std::lock_guard lock(mutex);
    if(closing)return;
    if(automatic.size()+pending.size()>=runtime.options.max_pending||bytes>queued_limit-queue_bytes)
      throw std::runtime_error("Automatic network continuation budget exceeded");
    automatic.emplace_back(std::move(command),bytes);queue_bytes+=bytes;changed.notify_all();
  }
  bool observe(const Json& event) {
    runtime.network->observe(event);
    const bool consumed=runtime.media && runtime.media->observe(event);
    const auto method=event.at("method").as_string();
    std::lock_guard lock(mutex);
    if(closing)return consumed;
    int page=0;
    if(event.contains("sessionId")){const auto found=session_pages.find(event.at("sessionId").as_string());if(found!=session_pages.end())page=found->second;}
    if(page&&(method=="Page.frameNavigated"||method=="Runtime.executionContextsCleared"))invalidated.insert(page);
    if((method=="Target.targetDestroyed"||method=="Target.detachedFromTarget")&&event.contains("params")) {
      const auto& params=event.at("params");
      // The envelope session can be the parent page reporting a child detach.
      // Lifecycle identity comes only from the event's affected target/session.
      page=0;
      if(params.contains("targetId")){const auto found=target_pages.find(params.at("targetId").as_string());if(found!=target_pages.end())page=found->second;}
      if(!page&&params.contains("sessionId")){const auto found=session_pages.find(params.at("sessionId").as_string());if(found!=session_pages.end())page=found->second;}
      if(page){externally_closed.insert(page);page_ready.erase(page);}
      if(method=="Target.detachedFromTarget"&&params.contains("sessionId")) {
        const auto& detached=params.at("sessionId").as_string();
        if(detached_sessions.size()>=resource_limit&&!detached_sessions.contains(detached))throw std::runtime_error("Detached session retirement queue capacity exceeded");
        detached_sessions.insert(detached);
      }
    }
    return consumed;
  }
  Packet ready_for(int id) const {
    Json header{{"version", 1}, {"type", "ready"}, {"pageId", id}, {"bytes", 0},
                {"browserVersion", browser_version}, {"cefVersion", browser_version},
                {"backend", "chromium"},
                {"capabilities", Json{{"nativeProtocol", 1}, {"evaluate", true}, {"isolatedWorld", true},
                                      {"devtools", true}, {"contexts", true}, {"screenshots", true},
                                      {"attached",!runtime.options.cdp_endpoint.empty()},
                                      {"defaultContextNewPages",runtime.options.cdp_endpoint.empty()},
                                      {"frameSubscription", false}, {"cef", false}}}};
    return Packet{header.dump(), {}};
  }
  void enqueue_event_locked(Packet event) {
    if (closing) return;
    const auto bytes = event.header.size() + event.payload.size();
    while (!events.empty() && (events.size() >= resource_limit ||
           bytes > event_budget - counters.queued_event_bytes)) {
      counters.queued_event_bytes -= events.front().header.size() + events.front().payload.size();
      events.pop_front(); ++counters.dropped_events;
    }
    if (bytes > event_budget) { ++counters.dropped_events; return; }
    events.push_back(std::move(event)); counters.queued_event_bytes += bytes;
    changed.notify_all();
  }
  void publish_page(const CdpPage& page, bool event = true) {
    std::lock_guard lock(mutex);
    if (closing) return;
    const auto ready = ready_for(page.id);
    page_ready.insert_or_assign(page.id, ready);
    session_pages.insert_or_assign(page.session, page.id);
    page_sessions.insert_or_assign(page.id, page.session);
    target_pages.insert_or_assign(page.target, page.id);
    if (event) enqueue_event_locked(ready);
    changed.notify_all();
  }
  void forget_page(int id, bool event = true) {
    const auto found = runtime.pages.find(id);
    if (found == runtime.pages.end()) return;
    const auto page = found->second;
    if(runtime.media)runtime.media->forget(page.session);
    if(runtime.network)runtime.network->forget(page.session);
    if(runtime.network)for(const auto&[frame,session]:page.frame_sessions){(void)frame;runtime.network->forget(session);}
    runtime.pages.erase(found);
    std::lock_guard lock(mutex);
    page_ready.erase(id); session_pages.erase(page.session); page_sessions.erase(id); target_pages.erase(page.target);
    invalidated.erase(id); externally_closed.erase(id);
    if (event) enqueue_event_locked(packet(Json{{"type", "context"}, {"pageId", id}},
        Json{{"event", "page-closed"}, {"contextId", page.context}, {"pageId", id}},
        runtime.options.max_payload_bytes));
    changed.notify_all();
  }
  CdpPage attach(int id, int context, const std::string& target, Deadline deadline) {
    CdpPage page; page.id = id; page.context = context; page.target = target;
    const auto attached = runtime.wire->call("Target.attachToTarget", Json{{"targetId", target}, {"flatten", true}}, "", deadline);
    page.session = attached.at("sessionId").as_string();
    runtime.call(page, "Page.enable", Json::object{}, deadline);
    if(runtime.network)runtime.network->setup(runtime,page,deadline);
    runtime.call(page, "Runtime.enable", Json::object{}, deadline);
    if(runtime.options.cdp_endpoint.empty()||id!=1)runtime.call(page, "Emulation.setDeviceMetricsOverride",
        Json{{"width", runtime.options.width}, {"height", runtime.options.height},
             {"deviceScaleFactor", 1}, {"mobile", false}}, deadline);
    return page;
  }
  void synchronize_pages() {
    std::set<int> reset, removed;
    std::set<std::string> detached;
    { std::lock_guard lock(mutex); reset.swap(invalidated); removed.swap(externally_closed);detached.swap(detached_sessions); }
    for (const int id : removed) forget_page(id, false);
    for(const auto& session:detached) {
      if(runtime.network)runtime.network->forget(session);
      for(auto&[id,page]:runtime.pages){(void)id;std::erase_if(page.frame_sessions,[&](const auto& cached){return cached.second==session;});}
    }
    for (const int id : reset) if (auto found = runtime.pages.find(id); found != runtime.pages.end()) {
      found->second.isolated_context = 0; found->second.isolated_loader.clear();found->second.isolated_frame.clear();
    }
  }
  void fail_terminal(const std::string& reason) {
    std::lock_guard lock(mutex);
    if (!terminal) terminal = std::make_exception_ptr(std::runtime_error(reason));
    while (!pending.empty()) cancel_locked(pending.begin()->first, reason);
    changed.notify_all();
  }
  Json storage_command(CdpPage& page,const Json& fields,Deadline deadline) {
    const auto action=fields.at("action").as_string();
    const auto context=runtime.contexts.find(page.context);
    if(context==runtime.contexts.end())throw std::runtime_error("Storage context is closed");
    auto args=context_params(context->second);
    if(action=="getState"||action=="storageState") {
      const auto cookies=runtime.wire->call("Storage.getCookies",args,"",deadline);
      auto origin=runtime.value(page,"(()=>{const read=s=>Array.from({length:s.length},(_,i)=>{const name=s.key(i);return {name,value:s.getItem(name)}});return {origin:location.origin,localStorage:read(localStorage),sessionStorage:read(sessionStorage)}})()",deadline);
      return Json{{"cookies",cookies.at("cookies")},{"origins",Json::array{std::move(origin)}}};
    }
    if(action!="setState"&&action!="setStorageState")throw std::invalid_argument("Unsupported storage action: "+action);
    const auto& state=fields.at("state");
    if(!state.is_object()||!state.at("cookies").is_array()||!state.at("origins").is_array()||state.at("cookies").as_array().size()>4096||state.at("origins").as_array().size()>128)throw std::invalid_argument("Invalid bounded storage state");
    const auto current_origin=runtime.value(page,"location.origin",deadline).as_string();
    std::set<std::string> restored_origins;
    for(const auto& origin:state.at("origins").as_array()) {
      const auto& target_origin=origin.at("origin").as_string();
      // Reject deferred work before touching cookies or either storage area.
      // A persistent init script would replay this snapshot over later app
      // writes and refreshed authentication state on every navigation.
      if(target_origin!=current_origin)
        throw std::invalid_argument("Storage restoration supports only the current page origin; navigate there first. Deferred origins are unsupported");
      if(!restored_origins.insert(target_origin).second)
        throw std::invalid_argument("Storage restoration contains a duplicate origin");
      for(const auto* kind:{"localStorage","sessionStorage"})if(origin.contains(kind)) {
        if(!origin.at(kind).is_array()||origin.at(kind).as_array().size()>4096)throw std::invalid_argument("Invalid storage entries");
        for(const auto& item:origin.at(kind).as_array()){item.at("name").as_string();item.at("value").as_string();}
      }
    }
    const auto script="(()=>{if(location.origin!=="+Json(current_origin).dump()+")throw new Error('Storage origin changed during restoration');const origins="+state.at("origins").dump()+";for(const o of origins){for(const kind of ['localStorage','sessionStorage'])if(Array.isArray(o[kind])){const store=globalThis[kind];store.clear();for(const item of o[kind])store.setItem(item.name,item.value);}}})()";
    // Apply once in the current document. No init-script registration or
    // future restoration remains, and repeated restores consume no quota.
    runtime.wire->call("Storage.clearCookies",args,"",deadline);
    args["cookies"]=state.at("cookies");runtime.wire->call("Storage.setCookies",args,"",deadline);
    runtime.value(page,script,deadline);return Json::object{};
  }
  Json context_command(const Json& fields, Deadline deadline) {
    check_active(deadline);
    const auto action = fields.at("action").as_string();
    const Json params = fields.contains("params") ? fields.at("params") : Json::object{};
    if (!params.is_object()) throw std::invalid_argument("Chromium context params must be an object");
    if (action == "list") {
      Json::array contexts;
      for (const auto& [id, context] : runtime.contexts) {
        (void)context; contexts.emplace_back(Json{{"contextId", id}, {"closing", false}});
      }
      return Json{{"contexts", std::move(contexts)}, {"pageCount", runtime.pages.size()},
                  {"unretiredTargets", target_debt.size()}, {"unretiredContexts", context_debt.size()}};
    }
    if (action == "create") {
      reject_unknown(params, {});
      if (runtime.contexts.size() + context_debt.size() >= resource_limit || runtime.next_context == INT_MAX)
        throw std::runtime_error("Chromium context capacity exceeded");
      Json result;
      try { result = runtime.wire->call("Target.createBrowserContext", Json{{"disposeOnDetach", true}}, "", deadline); }
      catch (...) {
        fail_terminal("Chromium context creation outcome is unknown; close the browser to retire owned resources");
        throw;
      }
      const auto created = result.at("browserContextId").as_string();
      {std::lock_guard lock(mutex);owned_contexts.insert(created);}
      context_debt.insert(created);
      const int id = ++runtime.next_context;
      try { runtime.contexts.emplace(id, created); }
      catch (...) {
        const auto failure = std::current_exception();
        try {
          CdpCleanupScope cleanup;
          runtime.wire->call("Target.disposeBrowserContext", Json{{"browserContextId", created}}, "",
                            Clock::now() + std::chrono::milliseconds{500});
          context_debt.erase(created);
          {std::lock_guard lock(mutex);owned_contexts.erase(created);}
        } catch (...) {
          fail_terminal("Chromium context cleanup failed; ownership retained until browser close");
        }
        std::rethrow_exception(failure);
      }
      context_debt.erase(created);
      return Json{{"contextId", id}};
    }
    if (action == "closePage") {
      reject_unknown(params, {"pageId", "contextId", "reason"});
      const int id = positive_id(params, "pageId");
      const auto found = runtime.pages.find(id);
      if (found == runtime.pages.end()) return Json{};
      const auto result = runtime.wire->call("Target.closeTarget", Json{{"targetId", found->second.target}}, "", deadline);
      if (result.contains("success") && !result.at("success").as_bool())
        throw std::runtime_error("Chromium refused to close the page");
      {std::lock_guard lock(mutex);owned_targets.erase(found->second.target);}
      forget_page(id); return Json{};
    }
    int id=0;
    if(params.contains("contextId")) {
      const auto value=params.at("contextId").as_int();if(value<0||value>INT_MAX)throw std::invalid_argument("Invalid Chromium contextId");id=static_cast<int>(value);
    } else if(params.contains("pageId")) {
      const auto owner=runtime.pages.find(positive_id(params,"pageId"));if(owner==runtime.pages.end())throw std::runtime_error("Context owner page is closed");id=owner->second.context;
    }
    const auto context = runtime.contexts.find(id);
    if (context == runtime.contexts.end()) {
      if (action == "close") return Json{};
      throw std::runtime_error("Chromium browser context is closed or unknown");
    }
    if (action == "close") {
      reject_unknown(params, {"contextId", "reason"});
      if(id==0)throw std::runtime_error("Close the browser to retire its default context");
      runtime.wire->call("Target.disposeBrowserContext", Json{{"browserContextId", context->second}}, "", deadline);
      {std::lock_guard lock(mutex);owned_contexts.erase(context->second);}
      std::vector<int> pages;
      for (const auto& [page_id, page] : runtime.pages) if (page.context == id) pages.push_back(page_id);
      for (int page_id : pages) {
        {std::lock_guard lock(mutex);owned_targets.erase(runtime.pages.at(page_id).target);}
        forget_page(page_id);
      }
      for (auto debt = target_debt.begin(); debt != target_debt.end();) {
        if (debt->second == id) debt = target_debt.erase(debt); else ++debt;
      }
      runtime.contexts.erase(context);runtime.network->forget_context(id); return Json{};
    }
    if(action=="storageState"||action=="setStorageState") {
      Json storage=params;storage["command"]="storage";storage["action"]=action=="storageState"?"getState":"setState";
      auto owner=runtime.pages.end();
      if(params.contains("pageId"))owner=runtime.pages.find(positive_id(params,"pageId"));
      else for(auto it=runtime.pages.begin();it!=runtime.pages.end();++it)if(it->second.context==id){owner=it;break;}
      if(owner==runtime.pages.end())throw std::runtime_error("Storage state requires an open context page");
      return storage_command(owner->second,storage,deadline);
    }
    if(action=="headers"||action=="setExtraHTTPHeaders"||action=="offline"||action=="setOffline"||action=="credentials"||action=="setHTTPCredentials"||action=="addInitScript"||action=="removeInitScript") {
      Json request=params;request["contextId"]=id;
      request["command"]=(action=="addInitScript"||action=="removeInitScript")?"initScript":"network";
      request["action"]=action=="setExtraHTTPHeaders"?"headers":action=="setOffline"?"offline":action=="setHTTPCredentials"?"credentials":action=="addInitScript"?"add":action=="removeInitScript"?"remove":action;
      CdpPage owner;owner.context=id;
      return runtime.network->command(runtime,owner,request,deadline);
    }
    if (action == "pages") {
      reject_unknown(params, {"contextId"}); Json::array pages;
      for (const auto& [page_id, page] : runtime.pages) if (page.context == id)
        pages.emplace_back(Json{{"contextId", id}, {"pageId", page_id}});
      return Json{{"contextId", id}, {"pages", std::move(pages)}};
    }
    if (action == "newPage") {
      reject_unknown(params, {"contextId"});
      if(id==0&&!runtime.options.cdp_endpoint.empty())
        throw std::runtime_error("Attached browsers require an SDK-owned context for new pages; create a context first");
      if (runtime.pages.size() + target_debt.size() >= resource_limit || runtime.next_page == INT_MAX)
        throw std::runtime_error("Chromium page capacity exceeded");
      Json result;
      try {
        if(id==0){std::lock_guard lock(mutex);default_creation_inflight=true;}
        auto args=context_params(context->second);args["url"]="about:blank";
        result = runtime.wire->call("Target.createTarget",std::move(args), "", deadline);
      } catch (...) {
        if(id==0){std::lock_guard lock(mutex);default_creation_inflight=false;uncertain_default_creation=true;}
        fail_terminal("Chromium target creation outcome is unknown; close the browser to retire owned resources");
        throw;
      }
      const auto target = result.at("targetId").as_string();
      {std::lock_guard lock(mutex);default_creation_inflight=false;owned_targets.insert(target);}
      target_debt.emplace(target, id);
      const int page_id = ++runtime.next_page;
      try {
        auto page = attach(page_id, id, target, deadline);
        runtime.pages.emplace(page_id, page); publish_page(page);
        target_debt.erase(target);
      } catch (...) {
        const auto failure = std::current_exception();
        // This is bounded retirement, not a retry of the expired operation.
        // The caller's promise can already be canceled while cleanup proceeds.
        try {
          CdpCleanupScope cleanup;
          const auto closed = runtime.wire->call("Target.closeTarget", Json{{"targetId", target}}, "",
                                                Clock::now() + std::chrono::milliseconds{500});
          if (closed.contains("success") && !closed.at("success").as_bool())
            throw std::runtime_error("Chromium refused target cleanup");
          target_debt.erase(target); forget_page(page_id);
          {std::lock_guard lock(mutex);owned_targets.erase(target);}
        } catch (...) {
          fail_terminal("Chromium target cleanup failed; ownership retained until browser close");
        }
        std::rethrow_exception(failure);
      }
      return Json{{"contextId", id}, {"pageId", page_id}};
    }
    if (action == "cookies") {
      reject_unknown(params, {"contextId"});
      return runtime.wire->call("Storage.getCookies", context_params(context->second), "", deadline);
    }
    if (action == "addCookies") {
      reject_unknown(params, {"contextId", "cookies"});
      const auto& cookies = params.at("cookies");
      if (!cookies.is_array() || cookies.as_array().size() > resource_limit)
        throw std::invalid_argument("Chromium cookies must be an array of at most 4096 entries");
      auto args=context_params(context->second);args["cookies"]=cookies;
      return runtime.wire->call("Storage.setCookies",std::move(args), "", deadline);
    }
    if (action == "clearCookies") {
      reject_unknown(params, {"contextId","filter"});
      if(!params.contains("filter")||(params.at("filter").is_object()&&params.at("filter").as_object().empty()))
        return runtime.wire->call("Storage.clearCookies", context_params(context->second), "", deadline);
      const auto& filter=params.at("filter");reject_unknown(filter,{"name","domain","path"});
      for(const auto&[key,value]:filter.as_object()){(void)key;value.as_string();}
      auto owner=runtime.pages.end();for(auto it=runtime.pages.begin();it!=runtime.pages.end();++it)if(it->second.context==id){owner=it;break;}
      if(owner==runtime.pages.end())throw std::runtime_error("Filtered cookie clearing requires an open context page");
      const auto result=runtime.wire->call("Storage.getCookies",context_params(context->second),"",deadline);
      for(const auto& cookie:result.at("cookies").as_array()) {
        bool match=true;for(const auto&[key,value]:filter.as_object())if(!cookie.contains(key)||cookie.at(key)!=value)match=false;
        if(!match)continue;
        Json args{{"name",cookie.at("name")},{"domain",cookie.at("domain")},{"path",cookie.at("path")}};
        if(cookie.contains("partitionKey"))args["partitionKey"]=cookie.at("partitionKey");
        runtime.call(owner->second,"Network.deleteCookies",std::move(args),deadline);
      }
      return Json::object{};
    }
    if (action == "grantPermissions") {
      reject_unknown(params, {"contextId", "permissions", "origin"});
      auto args=context_params(context->second);args["permissions"]=params.at("permissions");
      if (params.contains("origin")) args["origin"] = params.at("origin");
      return runtime.wire->call("Browser.grantPermissions", std::move(args), "", deadline);
    }
    if (action == "clearPermissions") {
      reject_unknown(params, {"contextId"});
      return runtime.wire->call("Browser.resetPermissions", context_params(context->second), "", deadline);
    }
    throw std::runtime_error("Unsupported Chromium context action: " + action);
  }
  Json dispatch(const Work& work) {
    synchronize_pages();
    const auto& fields = work.fields;
    const auto command = fields.at("command").as_string();
    if (command == "context") return context_command(fields, work.deadline);
    const auto found = runtime.pages.find(work.page);
    if (found == runtime.pages.end()) throw std::runtime_error("Chromium page is closed or unknown");
    auto& page = found->second;
    if(command=="network"||command=="dialog"||command=="console"||command=="initScript")return runtime.network->command(runtime,page,fields,work.deadline);
    if(command=="recording"||command=="tracing"||command=="profiler")return runtime.media->command(runtime,page,fields,work.deadline);
    if(command=="storage")return storage_command(page,fields,work.deadline);
    if(command=="frames")return runtime.call(page,"Page.getFrameTree",Json::object{},work.deadline);
    if (command == "evaluate") {
      if(fields.contains("frame")||(fields.contains("framePath")&&!fields.at("framePath").as_array().empty()))return cdp_page_command(runtime,page,fields,work.deadline);
      else return runtime.evaluate(page, fields.at("expression").as_string(), work.deadline,
          fields.contains("isolated") && fields.at("isolated").as_bool());
    }
    if (command == "devtools")
      return runtime.call(page, fields.at("method").as_string(),
          fields.contains("params") ? fields.at("params") : Json::object{}, work.deadline);
    if (command == "navigateFrame" || command == "navigate" || command == "viewport" ||
        command == "artifact" || command == "bringToFront" || command == "frameSubscription" || command=="historyBack" || command=="historyForward" ||command=="reload"||command=="navigationHistory"||command=="waitLoadState"||command=="waitForURL"||command=="waitForFunction"||command=="waitForSelector"||command=="waitForText"||command=="download"||command=="emulation"||command=="page"||command=="pdf"||command=="wait")
      return cdp_page_command(runtime, page, fields, work.deadline);
    if (command == "locator" || command == "form" || command == "queryEvaluate" || command == "snapshotReferences"||command=="waitFor")
      return cdp_dom_command(runtime, page, fields, work.deadline);
    if (command == "input") return cdp_input_command(runtime, page, fields, work.deadline);
    throw std::runtime_error("Unsupported Chromium command family: " + command);
  }
  void remove_queued_locked(int id) {
    for (auto* lane : {&queue, &continuations})
      for (auto it = lane->begin(); it != lane->end(); ++it) if ((*it)->id == id) {
        queue_bytes -= (*it)->bytes; lane->erase(it); return;
      }
  }
  void cancel_locked(int id, const std::string& reason) {
    const auto found = pending.find(id);
    if (found == pending.end()) return;
    found->second->canceled.store(true, std::memory_order_relaxed);
    found->second->promise.set_exception(std::make_exception_ptr(std::runtime_error(reason)));
    pending.erase(found); remove_queued_locked(id);
    counters.pending = pending.size(); changed.notify_all();
  }
  void run(bool continuation = false) {
    auto& lane = continuation ? continuations : queue;
    for (;;) {
      std::shared_ptr<Work> work;
      std::optional<CdpAutomatic> internal;
      {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return closing || !lane.empty() || (continuation&&!automatic.empty()); });
        if (closing) return;
        if(continuation&&!automatic.empty()) {
          internal=std::move(automatic.front().first);queue_bytes-=automatic.front().second;automatic.pop_front();
        } else {
        work = lane.front(); lane.pop_front(); queue_bytes -= work->bytes;
        if (!pending.contains(work->id)) continue;
        if (Clock::now() >= work->deadline) {
          cancel_locked(work->id, "Chromium request timed out in command queue"); continue;
        }
        }
      }
      if(internal) {
        try {runtime.wire->call(internal->method,std::move(internal->params),internal->session,Clock::now()+std::chrono::seconds{5});}
        catch(const std::exception& error){fail_terminal(std::string("Automatic network continuation failed: ")+error.what());}
        continue;
      }
      Packet result;
      active_cancellation = &work->canceled;
      try {
        Json value;
        if (continuation) {
          check_active(work->deadline);
          if (work->continuation_session.empty()) throw std::runtime_error("Chromium continuation page is closed or unknown");
          if(work->fields.at("command").as_string()!="devtools") {
            CdpPage copied;copied.id=work->page;copied.session=work->continuation_session;
            value=runtime.network->command(runtime,copied,work->fields,work->deadline);
          } else value = runtime.wire->call(work->fields.at("method").as_string(),
              work->fields.contains("params") ? work->fields.at("params") : Json::object{},
              work->continuation_session, work->deadline);
        } else value = dispatch(*work);
        result = response(work->id, work->page, true, value, runtime.options.max_payload_bytes);
      }
      catch (const std::exception& error) {
        try { result = response(work->id, work->page, false, Json{}, runtime.options.max_payload_bytes, error.what()); }
        catch (...) {
          active_cancellation = nullptr;
          std::lock_guard lock(mutex); cancel_locked(work->id, "Chromium response serialization failed"); continue;
        }
      } catch (...) {
        result = response(work->id, work->page, false, Json{}, runtime.options.max_payload_bytes,
                          "Unknown Chromium backend command failure");
      }
      active_cancellation = nullptr;
      std::lock_guard lock(mutex);
      ++counters.responses;
      const auto found = pending.find(work->id);
      if (found != pending.end()) {
        if (Clock::now() >= work->deadline) cancel_locked(work->id, "Chromium request timed out");
        else { found->second->promise.set_value(std::move(result)); pending.erase(found); counters.pending = pending.size(); }
      }
      changed.notify_all();
    }
  }
  void expire() {
    std::unique_lock lock(mutex);
    while (!closing) {
      auto next = Deadline::max();
      std::vector<int> expired;
      const auto now = Clock::now();
      for (const auto& [id, work] : pending) {
        if (work->deadline <= now) expired.push_back(id);
        else next = std::min(next, work->deadline);
      }
      for (int id : expired) cancel_locked(id, "Chromium request timed out");
      if (closing) return;
      if (next == Deadline::max()) changed.wait(lock);
      else changed.wait_until(lock, next);
    }
  }
};

CdpBackend::CdpBackend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CdpBackend::~CdpBackend() { try { close(); } catch (...) {} }
std::unique_ptr<CdpBackend> CdpBackend::launch(const LaunchOptions& options) {
  if (!options.max_pending || options.max_pending > resource_limit || !options.max_payload_bytes ||
      options.max_payload_bytes > 192 * 1024 * 1024 || !options.max_event_bytes ||
      options.max_event_bytes > 256 * 1024 * 1024)
    throw std::invalid_argument("Invalid Chromium backend storage limits");
  duration_valid(options.startup_timeout); duration_valid(options.shutdown_timeout);
  auto backend = std::unique_ptr<CdpBackend>(new CdpBackend(std::make_unique<Impl>(options)));
  auto& state = *backend->impl_;
  const auto deadline = Clock::now() + options.startup_timeout;
  auto wire_options=options;
  // One public event budget covers both backend lifecycle packets and raw wire
  // events; retained bytes reported by stats() must never exceed that budget.
  wire_options.max_event_bytes=options.max_event_bytes-state.event_budget;
  state.runtime.wire = CdpConnection::launch(wire_options);
  state.runtime.contexts.emplace(0,"");
  state.runtime.network=std::make_shared<CdpNetwork>([&state](CdpAutomatic command){state.automatic_command(std::move(command));});
  state.runtime.media=std::make_shared<CdpMedia>([&state](CdpAutomatic command){state.automatic_command(std::move(command));});
  state.runtime.wire->set_event_handler_with_disposition([&state](const Json& event){return state.observe(event);});
  const auto version = state.runtime.wire->call("Browser.getVersion", Json::object{}, "", deadline);
  state.browser_version = version.at("product").as_string();
  const auto targets = state.runtime.wire->call("Target.getTargets", Json::object{}, "", deadline);
  std::string target;
  for (const auto& entry : targets.at("targetInfos").as_array()) {
    if (entry.at("type").as_string() == "page") { target = entry.at("targetId").as_string(); break; }
  }
  if (target.empty()) target = state.runtime.wire->call("Target.createTarget", Json{{"url", "about:blank"}}, "", deadline).at("targetId").as_string();
  auto page = state.attach(1, 0, target, deadline);
  state.runtime.pages.emplace(1, page); state.publish_page(page, false);
  state.ready_packet = state.ready_for(1);
  state.worker = std::thread([&state] { state.run(); });
  state.continuation_worker = std::thread([&state] { state.run(true); });
  state.deadlines = std::thread([&state] { state.expire(); });
  return backend;
}
Request CdpBackend::submit(Json fields, std::chrono::milliseconds timeout) {
  duration_valid(timeout);
  const auto submitted = Clock::now();
  if (!fields.is_object() || !fields.contains("command") || !fields.at("command").is_string() ||
      fields.at("command").as_string().empty())
    throw std::invalid_argument("Chromium command must be an object with command");
  if (fields.contains("timeoutMs")) {
    const auto requested = std::chrono::milliseconds{fields.at("timeoutMs").as_int()};
    duration_valid(requested);
    timeout = std::min(timeout, requested);
  }
  const auto deadline = submitted + timeout;
  const int page = positive_id(fields, "pageId", 1);
  auto& state = *impl_;
  std::lock_guard lock(state.mutex);
  if (state.terminal) std::rethrow_exception(state.terminal);
  if (state.closing) throw std::runtime_error("Chromium backend is closed");
  if (state.pending.size()+state.automatic.size() >= state.runtime.options.max_pending)
    throw std::runtime_error("Chromium pending request budget exceeded");
  if (state.next_id == INT_MAX) throw std::runtime_error("Chromium request ID space exhausted");
  const int id = ++state.next_id;
  fields["id"] = id; fields["version"] = 1; fields["pageId"] = page;
  fields["timeoutMs"] = timeout.count();
  const auto encoded = fields.dump({command_limit - 1, 64, command_limit, command_limit - 1});
  const auto bytes = encoded.size() + 1;
  if (bytes > queued_limit - state.queue_bytes) throw std::runtime_error("Chromium command queue is full");
  auto work = std::make_shared<Impl::Work>();
  const bool continuation = continuation_method(fields);
  if (continuation) {
    const auto session = state.page_sessions.find(page);
    if (session != state.page_sessions.end()) work->continuation_session = session->second;
  }
  work->id = id; work->page = page; work->fields = std::move(fields); work->deadline = deadline; work->bytes = bytes;
  Request result{id, work->promise.get_future()};
  state.pending.emplace(id, work);
  try { (continuation ? state.continuations : state.queue).push_back(std::move(work)); }
  catch (...) { state.pending.erase(id); throw; }
  state.queue_bytes += bytes; ++state.counters.commands; state.counters.pending = state.pending.size();
  state.changed.notify_all(); return result;
}
void CdpBackend::cancel(int id) {
  std::lock_guard lock(impl_->mutex);
  impl_->cancel_locked(id, "Chromium request canceled; already dispatched input cannot be undone");
}
std::optional<Packet> CdpBackend::poll(std::chrono::milliseconds timeout) {
  duration_valid(timeout, true);
  auto& state = *impl_;
  const auto deadline = Clock::now() + timeout;
  for (;;) {
    {
      std::lock_guard lock(state.mutex);
      if (!state.events.empty()) {
        auto result = std::move(state.events.front()); state.events.pop_front();
        state.counters.queued_event_bytes -= result.header.size() + result.payload.size(); return result;
      }
      if (state.closing) return std::nullopt;
      if (state.terminal) std::rethrow_exception(state.terminal);
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    const auto event = state.runtime.wire->poll(std::max(std::chrono::milliseconds{0},
        std::min(remaining, std::chrono::milliseconds{25})));
    if (event) {
      int page = 0;
      const auto method = event->contains("method") ? event->at("method").as_string() : std::string{};
      {
        std::lock_guard lock(state.mutex);
        if (state.closing) return std::nullopt;
        if (event->contains("sessionId")) {
          const auto found = state.session_pages.find(event->at("sessionId").as_string());
          if (found != state.session_pages.end()) page = found->second;
        }
        // The internal observer owns lifecycle updates. Public polling only
        // assigns event identity and never replays stale invalidation/closure.
        if ((method=="Target.targetDestroyed"||method=="Target.detachedFromTarget")&&event->contains("params")) {
          page=0;
          const auto& params = event->at("params");
          if (params.contains("targetId")) {
            const auto found = state.target_pages.find(params.at("targetId").as_string());
            if (found != state.target_pages.end()) page=found->second;
          }
          if (!page&&params.contains("sessionId")) {
            const auto found = state.session_pages.find(params.at("sessionId").as_string());
            if (found != state.session_pages.end()) page=found->second;
          }
        }
      }
      Json header{{"type", "devtools"}, {"backend", "chromium"}};
      if (page) header["pageId"] = page;
      try { return packet(std::move(header), *event, state.runtime.options.max_payload_bytes); }
      catch (...) { std::lock_guard lock(state.mutex); ++state.counters.dropped_events; throw; }
    }
    if (Clock::now() >= deadline) return std::nullopt;
  }
}
Packet CdpBackend::ready() const { std::lock_guard lock(impl_->mutex); return impl_->ready_packet; }
Packet CdpBackend::wait_ready(int page, std::chrono::milliseconds timeout) {
  duration_valid(timeout);
  if (page < 1) throw std::invalid_argument("Chromium ready page must be positive");
  std::unique_lock lock(impl_->mutex);
  if (!impl_->changed.wait_for(lock, timeout, [&] { return impl_->closing || impl_->terminal || impl_->page_ready.contains(page); }))
    throw std::runtime_error("Chromium page readiness timed out");
  if (impl_->terminal) std::rethrow_exception(impl_->terminal);
  const auto found = impl_->page_ready.find(page);
  if (found == impl_->page_ready.end()) throw std::runtime_error("Chromium backend closed before page readiness");
  return found->second;
}
ClientStats CdpBackend::stats() const {
  ClientStats counters;
  { std::lock_guard lock(impl_->mutex); counters = impl_->counters; }
  // The connection owns the raw CDP event queue; the facade only owns its
  // synthesized ready/context events. Count both without consuming either.
  if (impl_->runtime.wire) {
    const auto wire = impl_->runtime.wire->stats();
    counters.queued_event_bytes += wire.queued_event_bytes;
    counters.dropped_events += wire.dropped_events;
  }
  return counters;
}
void CdpBackend::close() {
  if (!impl_) return;
  auto& state = *impl_;
  std::lock_guard closing_lock(state.close_mutex);
  std::vector<std::string> owned_targets,owned_contexts,sessions;
  bool attached=!state.runtime.options.cdp_endpoint.empty(),uncertain=false,run_attached_cleanup=false;
  {
    std::lock_guard lock(state.mutex);
    if (!state.closing) {
      state.closing = true;
      while (!state.pending.empty()) state.cancel_locked(state.pending.begin()->first, "Chromium backend closed");
      state.queue.clear(); state.continuations.clear();state.automatic.clear(); state.queue_bytes = 0;
      state.events.clear(); state.counters.queued_event_bytes = 0;
      state.page_ready.clear(); state.changed.notify_all();
    }
    if(attached&&!state.attached_cleanup_started) {
      state.attached_cleanup_started=true;run_attached_cleanup=true;
      owned_targets.assign(state.owned_targets.begin(),state.owned_targets.end());
      owned_contexts.assign(state.owned_contexts.begin(),state.owned_contexts.end());
      std::set<int> retiring_pages;
      for(const auto& target:owned_targets){const auto owner=state.target_pages.find(target);if(owner!=state.target_pages.end())retiring_pages.insert(owner->second);}
      // Closing owned targets/contexts also destroys their attached sessions.
      // Only detach sessions for caller-owned pages that remain alive.
      for(const auto&[id,session]:state.page_sessions)if(!retiring_pages.contains(id))sessions.push_back(session);
      uncertain=state.default_creation_inflight||state.uncertain_default_creation;
    }
  }
  if(state.runtime.media)state.runtime.media->cancel_all();
  // Physical connection retirement/profile cleanup may throw. The fixed
  // worker threads must still be joined, including partial launch where only
  // one thread was created, before a backend can be destroyed or retried.
  std::exception_ptr failure;
  if(run_attached_cleanup&&state.runtime.wire) {
    const auto cleanup=Clock::now()+std::chrono::milliseconds{500};
    auto retire=[&](const std::string& method,Json params) {
      try {state.runtime.wire->call(method,params,"",cleanup);}
      catch(const std::exception& error) {
        const bool gone_session=method=="Target.detachFromTarget"&&cdp_error_is(error,-32602,"No session with given id");
        const bool gone_target=method=="Target.closeTarget"&&cdp_error_is(error,-32602,"No target with given id found");
        const bool gone_context=method=="Target.disposeBrowserContext"&&
            cdp_error_is(error,-32000,"Failed to find context with id "+params.at("browserContextId").as_string());
        if(!gone_session&&!gone_target&&!gone_context&&!failure)failure=std::current_exception();
      }
      catch(...) {if(!failure)failure=std::current_exception();}
    };
    for(const auto& target:owned_targets)retire("Target.closeTarget",Json{{"targetId",target}});
    for(const auto& context:owned_contexts)retire("Target.disposeBrowserContext",Json{{"browserContextId",context}});
    for(const auto& session:sessions)retire("Target.detachFromTarget",Json{{"sessionId",session}});
  }
  try { if (state.runtime.media && state.runtime.wire) state.runtime.media->shutdown(state.runtime,Clock::now()+std::chrono::milliseconds{250}); }
  catch (...) { if(!failure)failure=std::current_exception(); }
  try { if (state.runtime.wire) state.runtime.wire->close(); }
  catch (...) { if(!failure)failure = std::current_exception(); }
  try { if (state.worker.joinable()) state.worker.join(); }
  catch (...) { if (!failure) failure = std::current_exception(); }
  try { if (state.continuation_worker.joinable()) state.continuation_worker.join(); }
  catch (...) { if (!failure) failure = std::current_exception(); }
  try { if (state.deadlines.joinable()) state.deadlines.join(); }
  catch (...) { if (!failure) failure = std::current_exception(); }
  // No I/O callback or command worker can retain these ledgers after joins.
  if(state.runtime.media)state.runtime.media->close();
  state.runtime.media.reset();state.runtime.network.reset();state.runtime.pages.clear();state.runtime.contexts.clear();
  if(attached) {
    std::lock_guard lock(state.mutex);
    for(const auto& target:state.owned_targets)if(std::find(owned_targets.begin(),owned_targets.end(),target)==owned_targets.end())uncertain=true;
    state.page_sessions.clear();state.session_pages.clear();state.target_pages.clear();
    state.owned_targets.clear();state.owned_contexts.clear();
  }
  if(uncertain&&!failure)failure=std::make_exception_ptr(std::runtime_error("Attached browser default-context target creation outcome is unknown; caller browser was preserved"));
  if (failure) std::rethrow_exception(failure);
}
} // namespace suitecut::detail
