// SPDX-License-Identifier: MIT
#include "bidi_backend.hpp"
#include "bidi_runtime.hpp"
#include "bidi_dom.hpp"
#include "bidi_features.hpp"
#include "bidi_har.hpp"
#include "bidi_permissions.hpp"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace suitecut::detail {
namespace {
using Clock=std::chrono::steady_clock;
thread_local const std::atomic<bool>* canceled=nullptr;
thread_local unsigned cleanup_depth=0;
constexpr std::size_t limit=4096,command_limit=65536,queue_limit=4*1024*1024;
std::string text(const Json& j,const char* key,std::string fallback={}){return j.contains(key)?j.at(key).as_string():std::move(fallback);}
bool flag(const Json& j,const char* key,bool fallback=false){return j.contains(key)?j.at(key).as_bool():fallback;}
void duration(std::chrono::milliseconds timeout,bool zero=false){if(timeout.count()<(zero?0:1)||timeout>std::chrono::hours(24))throw std::invalid_argument("BiDi timeout must be positive and at most24 hours");}
int integer(const Json& j,const char* key,int fallback=0){if(!j.contains(key))return fallback;const auto n=j.at(key).as_int();if(n<0||n>INT_MAX)throw std::invalid_argument(std::string("Invalid BiDi ")+key);return static_cast<int>(n);}
Packet pack(Json header,const Json& body,std::size_t budget){const auto data=body.is_null()?std::string{}:body.dump({budget,64,1024*1024,budget});header["version"]=1;header["bytes"]=data.size();Packet p{header.dump(),{}};p.payload.assign(data.begin(),data.end());return p;}
Json decode(const Json& remote,int depth=0){
  if(depth>64)throw std::runtime_error("BiDi remote value nesting exceeded");
  const auto type=remote.at("type").as_string();
  if(type=="undefined"||type=="null")return Json{};
  if(type=="string"||type=="boolean")return remote.at("value");
  if(type=="number"){if(!remote.at("value").is_number())throw std::runtime_error("Nonfinite BiDi number is not JSON-compatible");return remote.at("value");}
  if(type=="array"||type=="set") {Json::array values;const auto& source=remote.at("value").as_array();if(source.size()>65536)throw std::runtime_error("BiDi array exceeds entry budget");for(const auto& value:source)values.push_back(decode(value,depth+1));return values;}
  if(type=="object"||type=="map") {Json::object values;const auto& source=remote.at("value").as_array();if(source.size()>65536)throw std::runtime_error("BiDi object exceeds entry budget");for(const auto& pair:source){const auto key=pair.at(0).is_string()?pair.at(0).as_string():decode(pair.at(0),depth+1).as_string();if(!values.emplace(key,decode(pair.at(1),depth+1)).second)throw std::runtime_error("Duplicate BiDi object key");}return values;}
  throw std::runtime_error("BiDi value has no JSON representation: "+type);
}
Json normalized(const Json& remote){
  Json result{{"type",remote.at("type")}};
  try {result["value"]=decode(remote);}
  catch(const std::exception&){result["unserializableValue"]=remote.dump({65536,64,65536,65536});}
  if(remote.contains("sharedId"))result["sharedId"]=remote.at("sharedId");
  if(remote.contains("handle"))result["handle"]=remote.at("handle");
  return result;
}
Json target(const BidiPage& page,bool isolated){Json t{{"context",page.browsing_context}};if(isolated)t["sandbox"]=page.sandbox;return t;}
std::vector<std::string> unicode(const std::string& value){
  std::vector<std::string> out;for(std::size_t i=0;i<value.size();){const auto ch=static_cast<unsigned char>(value[i]);const std::size_t n=ch<128?1:ch>=0xc2&&ch<=0xdf?2:ch>=0xe0&&ch<=0xef?3:ch>=0xf0&&ch<=0xf4?4:0;if(!n||i+n>value.size())throw std::invalid_argument("Input text is not UTF8");for(std::size_t k=1;k<n;++k)if((static_cast<unsigned char>(value[i+k])&0xc0)!=0x80)throw std::invalid_argument("Input text is not UTF8");out.emplace_back(value.substr(i,n));i+=n;}return out;
}
std::string key_value(const std::string& key){
  static const std::map<std::string,std::string> keys{{"Enter","\xee\x80\x87"},{"Return","\xee\x80\x86"},{"Tab","\xee\x80\x84"},{"Backspace","\xee\x80\x83"},{"Escape","\xee\x80\x8c"},{"Delete","\xee\x80\x97"},{"Space"," "},{"Shift","\xee\x80\x88"},{"Control","\xee\x80\x89"},{"ControlOrMeta","\xee\x80\x89"},{"Alt","\xee\x80\x8a"},{"Meta","\xee\x80\xbd"},{"ArrowLeft","\xee\x80\x92"},{"ArrowUp","\xee\x80\x93"},{"ArrowRight","\xee\x80\x94"},{"ArrowDown","\xee\x80\x95"},{"Home","\xee\x80\x91"},{"End","\xee\x80\x90"},{"PageUp","\xee\x80\x8e"},{"PageDown","\xee\x80\x8f"}};
  const auto found=keys.find(key);if(found!=keys.end())return found->second;
  if(unicode(key).size()!=1)throw std::invalid_argument("Unsupported BiDi key: "+key);
  return key;
}
int modifier_bit(const std::string& key){return key==key_value("Alt")?1:key==key_value("Control")?2:key==key_value("Meta")?4:key==key_value("Shift")?8:0;}
bool matches(const std::string& pattern,const std::string& value){std::size_t p=0,v=0,star=std::string::npos,mark=0,budget=1024*1024;while(v<value.size()){if(!budget--)throw std::runtime_error("Route matching budget exceeded");if(p<pattern.size()&&(pattern[p]=='?'||pattern[p]==value[v])){++p;++v;}else if(p<pattern.size()&&pattern[p]=='*'){star=p++;mark=v;}else if(star!=std::string::npos){p=star+1;v=++mark;}else return false;}while(p<pattern.size()&&pattern[p]=='*')++p;return p==pattern.size();}
Json byte_headers(const Json& headers){if(!headers.is_object()||headers.as_object().size()>256)throw std::invalid_argument("BiDi headers must be a bounded object");Json::array out;for(const auto&[name,value]:headers.as_object()){if(name.empty()||name.find_first_of("\r\n\0",0,3)!=std::string::npos||value.as_string().find_first_of("\r\n\0",0,3)!=std::string::npos)throw std::invalid_argument("Invalid HTTP header");out.emplace_back(Json{{"name",name},{"value",Json{{"type","string"},{"value",value}}}});}return out;}
Json merged_headers(const Json& original,const Json& overrides){
  const auto lower=[](std::string name){std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return name;};
  std::set<std::string> replaced;for(const auto&[name,value]:overrides.as_object()){(void)value;replaced.insert(lower(name));}
  Json::array out;if(original.is_array())for(const auto& header:original.as_array())if(!replaced.contains(lower(header.at("name").as_string())))out.push_back(header);
  const auto extra=byte_headers(overrides);for(const auto& header:extra.as_array())out.push_back(header);return out;
}
struct Ring {std::deque<Json> values;void add(const Json& value){try {value.dump({32768,32,16384,32768});}catch(...){if(values.size()>=1024)values.pop_front();values.push_back(Json{{"truncated",true}});return;}if(values.size()>=1024)values.pop_front();values.push_back(value);}Json array(bool clear=false){Json::array out(values.begin(),values.end());if(clear)values.clear();return out;}};
}
void bidi_check_active(Deadline deadline){if(Clock::now()>=deadline)throw std::runtime_error("BiDi original deadline expired");if(!cleanup_depth&&canceled&&canceled->load(std::memory_order_relaxed))throw std::runtime_error("BiDi request canceled; dispatched operation outcome is unknown");}
BidiCleanupScope::BidiCleanupScope(){++cleanup_depth;}BidiCleanupScope::~BidiCleanupScope(){--cleanup_depth;}
Json BidiRuntime::call(const std::string& method,Json params,Deadline deadline){bidi_check_active(deadline);try{return wire->call(method,std::move(params),"",deadline);}catch(const std::exception& error){throw std::runtime_error("Firefox "+method+" failed: "+error.what());}}
Json BidiRuntime::evaluate(BidiPage& page,const std::string& expression,Deadline deadline,bool isolated){
  const auto result=call("script.evaluate",Json{{"expression",expression},{"target",target(page,isolated)},{"awaitPromise",true},{"resultOwnership","none"},{"serializationOptions",Json{{"maxObjectDepth",64},{"maxDomDepth",0}}}},deadline);
  if(text(result,"type")=="exception")return Json{{"exceptionDetails",result.at("exceptionDetails")},{"result",Json{{"type","undefined"}}}};
  if(text(result,"type")!="success")throw std::runtime_error("Invalid BiDi script result");
  return Json{{"result",normalized(result.at("result"))}};
}
Json BidiRuntime::value(BidiPage& page,const std::string& expression,Deadline deadline,bool isolated){auto reply=evaluate(page,expression,deadline,isolated);if(reply.contains("exceptionDetails"))throw std::runtime_error("Firefox JavaScript failed: "+reply.at("exceptionDetails").dump());const auto& remote=reply.at("result");if(remote.contains("unserializableValue"))throw std::runtime_error("Firefox value is not JSON-compatible; use evaluate_raw");return remote.contains("value")?remote.at("value"):Json{};}
Json bidi_input_command(BidiRuntime& runtime,BidiPage& page,const Json& fields,Deadline deadline){
  const auto kind=text(fields,"kind"),action=text(fields,"action");Json::array steps;std::string type,id;Json source;
  if(kind=="keyboard") {
    type="key";id="suitecut-keyboard";
    if(action=="insertText"||action=="type") {
      const auto value=fields.contains("text")?fields.at("text").as_string():text(fields,"value");if(value.size()>65536)throw std::invalid_argument("BiDi input text exceeds64KiB");
      for(const auto& point:unicode(value)){const auto key=point=="\n"||point=="\r"?key_value("Enter"):point=="\t"?key_value("Tab"):point;steps.emplace_back(Json{{"type","keyDown"},{"value",key}});steps.emplace_back(Json{{"type","keyUp"},{"value",key}});}
    } else if(action=="down"||action=="up")steps.emplace_back(Json{{"type",action=="down"?"keyDown":"keyUp"},{"value",key_value(fields.at("key").as_string())}});
    else if(action=="press") {
      const auto chord=fields.at("key").as_string();std::vector<std::string> keys;std::size_t begin=0;
      for(;;){const auto end=chord.find('+',begin);auto key=chord.substr(begin,end==std::string::npos?end:end-begin);if(key.empty()&&end==std::string::npos)key="+";keys.push_back(key_value(key));if(end==std::string::npos)break;begin=end+1;}
      std::vector<std::string> added;
      for(const auto& key:keys){if(modifier_bit(key)&&(page.modifiers&modifier_bit(key)))continue;steps.emplace_back(Json{{"type","keyDown"},{"value",key}});added.push_back(key);}
      for(auto it=added.rbegin();it!=added.rend();++it)steps.emplace_back(Json{{"type","keyUp"},{"value",*it}});
    } else throw std::invalid_argument("Unsupported BiDi keyboard action: "+action);
  } else if(kind=="mouse") {
    const auto coordinate=[&](const char* key,double fallback){const double value=fields.contains(key)?fields.at(key).as_double():fallback;if(!std::isfinite(value)||std::abs(value)>1000000)throw std::invalid_argument("Invalid BiDi pointer coordinate");return static_cast<int>(std::round(value));};
    const auto button=text(fields,"button","left");const int index=button=="left"?0:button=="middle"?1:button=="right"?2:-1;if(index<0)throw std::invalid_argument("Unsupported pointer button");
    type="pointer";id="suitecut-mouse";source["parameters"]=Json{{"pointerType","mouse"}};
    if(action=="move"||action=="click") {
      const auto count=action=="move"?integer(fields,"steps",1):1;
      if(count<1||count>10000)throw std::invalid_argument("BiDi mouse steps must be1..10000");
      const auto start_x=page.mouse_x,start_y=page.mouse_y;
      const auto end_x=coordinate("x",start_x),end_y=coordinate("y",start_y);
      // One bounded action sequence preserves interpolation without a protocol
      // round trip per point. Existing chunking bounds wire-message size.
      for(int step=1;step<=count;++step)steps.emplace_back(Json{{"type","pointerMove"},
        {"x",static_cast<int>(std::round(start_x+(end_x-start_x)*step/count))},
        {"y",static_cast<int>(std::round(start_y+(end_y-start_y)*step/count))},
        {"duration",0},{"origin","viewport"}});
      page.mouse_x=end_x;page.mouse_y=end_y;
    }
    if(action=="click") {const auto count=integer(fields,"clickCount",1);if(count<1||count>3)throw std::invalid_argument("BiDi click count must be1..3");for(int n=0;n<count;++n){steps.emplace_back(Json{{"type","pointerDown"},{"button",index}});steps.emplace_back(Json{{"type","pointerUp"},{"button",index}});}}
    else if(action=="down")steps.emplace_back(Json{{"type","pointerDown"},{"button",index}});
    else if(action=="up")steps.emplace_back(Json{{"type","pointerUp"},{"button",index}});
    if(action=="wheel") {type="wheel";id="suitecut-wheel";source=Json::object{};steps.emplace_back(Json{{"type","scroll"},{"x",coordinate("x",page.mouse_x)},{"y",coordinate("y",page.mouse_y)},{"deltaX",coordinate("deltaX",0)},{"deltaY",coordinate("deltaY",0)},{"duration",0},{"origin","viewport"}});}
    if(steps.empty())throw std::invalid_argument("Unsupported BiDi mouse action: "+action);
  } else throw std::invalid_argument("Unsupported BiDi input kind");
  const auto delay=fields.contains("delay")?fields.at("delay").as_int():0;if(delay<0||delay>120000)throw std::invalid_argument("BiDi input delay must be0..120000ms");
  try {
    // Chunk Unicode actions to bound protocol frames independently of input.
    for(std::size_t start=0;start<steps.size();start+=256){Json::array batch;const auto end=std::min(start+256,steps.size());for(auto i=start;i<end;++i)batch.push_back(steps[i]);source["type"]=type;source["id"]=id;source["actions"]=std::move(batch);runtime.call("input.performActions",Json{{"context",page.browsing_context},{"actions",Json::array{source}}},deadline);}
    if(kind=="keyboard"&&(action=="down"||action=="up")){const auto bit=modifier_bit(key_value(fields.at("key").as_string()));if(action=="down")page.modifiers|=bit;else page.modifiers&=~bit;}
    if(kind=="mouse"&&(action=="down"||action=="up"||action=="click")){const auto button=text(fields,"button","left");const int bit=button=="left"?1:button=="right"?2:4;if(action=="down")page.mouse_buttons|=bit;else page.mouse_buttons&=~bit;}
    const auto until=Clock::now()+std::chrono::milliseconds(delay);while(Clock::now()<until){bidi_check_active(deadline);std::this_thread::sleep_until(std::min(until,Clock::now()+std::chrono::milliseconds{10}));}bidi_check_active(deadline);
  }catch(...){const auto failure=std::current_exception();try{BidiCleanupScope cleanup;runtime.call("input.releaseActions",Json{{"context",page.browsing_context}},Clock::now()+std::chrono::milliseconds{150});page.modifiers=0;page.mouse_buttons=0;}catch(...){}std::rethrow_exception(failure);}
  return Json{{"acted",true}};
}

struct BidiBackend::Impl {
  BidiRuntime runtime;
  std::unique_ptr<BidiFeatures> features;
  std::unique_ptr<BidiHar> har;
  BidiPermissions permissions;
  bool har_drain_requested=false;
  mutable std::mutex mutex;std::mutex close_mutex;std::condition_variable changed;
  struct Work{int id=0,page=1;Json fields;Deadline deadline;std::size_t bytes=0;std::string context;std::promise<Packet> promise;std::atomic<bool> canceled{false};};
  struct Automatic{std::string method;Json params;std::size_t bytes=0;};
  std::map<int,std::shared_ptr<Work>> pending;std::deque<std::shared_ptr<Work>> normal,controls;std::deque<Automatic> automatic;std::size_t queued=0;
  std::map<int,Packet> ready_pages;std::map<std::string,int> page_ids;std::map<int,std::string> page_contexts;std::map<std::string,std::string> parents;std::set<int> removed;
  struct Settings{Json headers=Json::object{},credentials;bool offline=false;std::vector<Json> routes;std::map<std::string,std::string> scripts;};
  struct State{int context=0;Ring requests,responses,console,errors,dialogs;std::string intercept;std::set<std::string> auth;};
  std::map<int,Settings> settings;std::map<int,State> states;
  std::set<std::string> owned_contexts,owned_pages;std::map<std::string,int> intercept_contexts;
  int next_id=0,next_route=0;bool closing=false,closed=false,attached=false,owns_session=false;std::string terminal_error;Packet initial;ClientStats counters;
  std::string version,session_id,profile,logs;bool owned_profile=false;
  std::thread worker,continuation,timer;
#ifndef _WIN32
  pid_t pid=-1;int log_fd=-1;bool reaped=false;
#endif
  explicit Impl(const LaunchOptions& options):features(std::make_unique<BidiFeatures>()),har(std::make_unique<BidiHar>()){runtime.options=options;attached=!options.cdp_endpoint.empty();har->set_wakeup([this]{std::lock_guard lock(mutex);if(!closing){har_drain_requested=true;changed.notify_all();}});}
  Packet ready_for(int page)const{return pack(Json{{"type","ready"},{"pageId",page},{"backend","firefox"},{"browserVersion",version},{"cefVersion",version},{"capabilities",Json{{"nativeProtocol",1},{"webDriverBiDi",true},{"cdp",false},{"contexts",true},{"evaluate",true},{"nativeInput",true},{"screenshots",true},{"downloads",true},{"har",true},{"mediaEmulation",true},{"contrastEmulation",false},{"crossFrameDrag",false},{"html5Drag",false},{"attached",attached},{"frameSubscription",false}}}},Json{},runtime.options.max_payload_bytes);}
  void remember(BidiPage page){std::lock_guard lock(mutex);if(runtime.pages.size()>=limit)throw std::runtime_error("BiDi page capacity exceeded");page_ids[page.browsing_context]=page.id;page_contexts[page.id]=page.browsing_context;ready_pages[page.id]=ready_for(page.id);states[page.id].context=page.context;runtime.pages.emplace(page.id,std::move(page));changed.notify_all();}
  void forget(int id){const auto found=runtime.pages.find(id);if(found==runtime.pages.end())return;const auto name=found->second.browsing_context;if(features)features->forget(name);if(har)har->forget(name);runtime.pages.erase(found);std::lock_guard lock(mutex);page_ids.erase(name);page_contexts.erase(id);ready_pages.erase(id);states.erase(id);owned_pages.erase(name);}
  void synchronize(){std::set<int> closed_pages;{std::lock_guard lock(mutex);closed_pages.swap(removed);}for(int page:closed_pages)forget(page);}
  void enqueue(std::string method,Json params){const auto bytes=params.dump({65536,32,16384,65536}).size()+method.size();if(automatic.size()+pending.size()>=runtime.options.max_pending||bytes>queue_limit-queued)throw std::runtime_error("BiDi automatic continuation budget exceeded");automatic.push_back({std::move(method),std::move(params),bytes});queued+=bytes;changed.notify_all();}
  void observe_har(const Json& event) {
    if(!har||!event.contains("method")||!event.at("method").as_string().starts_with("network.")||!event.contains("params"))return;
    const auto& params=event.at("params");if(!params.contains("context")||!params.at("context").is_string())return;
    std::string owner=params.at("context").as_string(),root;
    {
      std::lock_guard lock(mutex);if(closing)return;
      for(int depth=0;depth<65;++depth){const auto found=page_ids.find(owner);if(found!=page_ids.end()){root=page_contexts.at(found->second);break;}const auto parent=parents.find(owner);if(parent==parents.end())break;owner=parent->second;}
    }
    if(!root.empty())har->observe(event,root);
  }
  void observe(const Json& event){observe_har(event);if(features)features->observe(event);const auto method=text(event,"method");if(!event.contains("params"))return;const auto& params=event.at("params");std::lock_guard lock(mutex);if(closing||!terminal_error.empty())return;int page=0;const auto context=text(params,"context");
    if(method=="browsingContext.contextCreated"){if(params.contains("parent")&&params.at("parent").is_string()){if(parents.size()>=4096&&!parents.contains(context))throw std::runtime_error("BiDi frame parent tracking capacity exceeded");parents[context]=params.at("parent").as_string();}return;}
    const auto direct=page_ids.find(context);if(direct!=page_ids.end())page=direct->second;
    if(method=="browsingContext.contextDestroyed"){if(page){removed.insert(page);ready_pages.erase(page);}parents.erase(context);return;}
    auto owner=context;for(int depth=0;!page&&depth<64&&parents.contains(owner);++depth){owner=parents.at(owner);const auto found=page_ids.find(owner);if(found!=page_ids.end())page=found->second;}
    if(!page)return;
    auto& state=states.at(page);auto& config=settings[state.context];
    if(method=="network.beforeRequestSent") {
      state.requests.add(params);
      if(flag(params,"isBlocked")) {
        const auto& request=params.at("request");const auto request_id=request.at("request").as_string();const auto url=text(request,"url");
        std::string command="network.continueRequest";Json args{{"request",request_id}};
        if(!config.headers.as_object().empty())args["headers"]=merged_headers(request.at("headers"),config.headers);
        if(config.offline)command="network.failRequest";
        else for(auto it=config.routes.rbegin();it!=config.routes.rend();++it)if(matches(it->at("pattern").as_string(),url)) {
          const auto behavior=it->at("behavior").as_string();if(behavior=="abort")command="network.failRequest";
          if(behavior=="mock"){command="network.provideResponse";args=Json{{"request",request_id},{"statusCode",it->contains("status")?it->at("status"):Json(200)},{"body",Json{{"type","string"},{"value",text(*it,"body")}}}};if(it->contains("headers"))args["headers"]=byte_headers(it->at("headers"));}break;
        }
        if(command=="network.failRequest")args=Json{{"request",request_id}};
        enqueue(command,std::move(args));
      }
    }else if(method=="network.authRequired"&&flag(params,"isBlocked")) {
      const auto request=params.at("request").at("request").as_string();Json args{{"request",request},{"action","cancel"}};
      if(!config.credentials.is_null()&&!state.auth.contains(request)){if(state.auth.size()>=1024)state.auth.erase(state.auth.begin());state.auth.insert(request);args["action"]="provideCredentials";args["credentials"]=Json{{"type","password"},{"username",config.credentials.at("username")},{"password",config.credentials.at("password")}};}enqueue("network.continueWithAuth",std::move(args));
    }else if(method=="network.responseStarted"||method=="network.responseCompleted"||method=="network.fetchError")state.responses.add(params);
    else if(method=="log.entryAdded"){state.console.add(params);if(text(params,"level")=="error")state.errors.add(params);}
    else if(method=="browsingContext.userPromptOpened"||method=="browsingContext.userPromptClosed")state.dialogs.add(Json{{"method",method},{"params",params}});
  }
  void configure(BidiPage& page,Deadline deadline){
    Settings config;std::string previous;{std::lock_guard lock(mutex);config=settings[page.context];previous=states[page.id].intercept;}
    if(!previous.empty()) {
      runtime.call("network.removeIntercept",Json{{"intercept",previous}},deadline);
      std::lock_guard lock(mutex);states[page.id].intercept.clear();
    }
    if(!config.routes.empty()||!config.credentials.is_null()||config.offline||!config.headers.as_object().empty()) {
      Json::array phases{Json("beforeRequestSent")};if(!config.credentials.is_null())phases.push_back("authRequired");
      const auto result=runtime.call("network.addIntercept",Json{{"phases",std::move(phases)},{"contexts",Json::array{Json(page.browsing_context)}}},deadline);
      std::lock_guard lock(mutex);states[page.id].intercept=result.at("intercept").as_string();
    }else {std::lock_guard lock(mutex);states[page.id].intercept.clear();}
  }
  Json cookies(const std::string& user,Deadline deadline){const auto reply=runtime.call("storage.getCookies",Json{{"partition",Json{{"type","storageKey"},{"userContext",user}}}},deadline);Json::array out;for(const auto& value:reply.at("cookies").as_array()){Json cookie=value;cookie["value"]=value.at("value").at("value");if(cookie.contains("expiry"))cookie["expires"]=cookie.at("expiry");const auto site=text(cookie,"sameSite","lax");cookie["sameSite"]=site=="strict"?"Strict":site=="none"?"None":"Lax";out.push_back(std::move(cookie));}return Json{{"cookies",std::move(out)}};}
  void set_cookies(const std::string& user,const Json& values,Deadline deadline){if(!values.is_array()||values.as_array().size()>4096)throw std::invalid_argument("Cookies exceed4096 entries");for(const auto& input:values.as_array()){Json cookie{{"name",input.at("name")},{"value",Json{{"type","string"},{"value",input.at("value")}}},{"domain",input.at("domain")},{"path",text(input,"path","/")},{"secure",flag(input,"secure")},{"httpOnly",flag(input,"httpOnly")}};auto site=text(input,"sameSite","Lax");std::transform(site.begin(),site.end(),site.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});cookie["sameSite"]=site;if(input.contains("expires")&&input.at("expires").as_double()>=0)cookie["expiry"]=input.at("expires");runtime.call("storage.setCookie",Json{{"partition",Json{{"type","storageKey"},{"userContext",user}}},{"cookie",std::move(cookie)}},deadline);}}
  Json context_command(const Json& fields,Deadline deadline){const auto action=fields.at("action").as_string();const auto params=fields.contains("params")?fields.at("params"):Json(Json::object{});int id=integer(params,"contextId");if(params.contains("pageId")){const auto found=runtime.pages.find(integer(params,"pageId"));if(found==runtime.pages.end())throw std::runtime_error("BiDi context owner page is closed");id=found->second.context;}
    if(action=="create"){if(runtime.contexts.size()>=limit||runtime.next_context==INT_MAX)throw std::runtime_error("BiDi context capacity exceeded");const auto result=runtime.call("browser.createUserContext",Json::object{},deadline);const auto name=result.at("userContext").as_string();const int created=++runtime.next_context;runtime.contexts.emplace(created,name);{std::lock_guard lock(mutex);owned_contexts.insert(name);}return Json{{"contextId",created}};}
    if(action=="list"){Json::array out;for(const auto&[key,name]:runtime.contexts){(void)name;out.emplace_back(Json{{"contextId",key},{"closing",false}});}return Json{{"contexts",std::move(out)},{"pageCount",runtime.pages.size()}};}
    if(action=="closePage"){const int page=integer(params,"pageId");const auto found=runtime.pages.find(page);if(found!=runtime.pages.end()){runtime.call("browsingContext.close",Json{{"context",found->second.browsing_context}},deadline);forget(page);}return Json{};}
    const auto found=runtime.contexts.find(id);if(found==runtime.contexts.end()){if(action=="close")return Json{};throw std::runtime_error("BiDi context is closed or unknown");}const auto user=found->second;
    if(action=="newPage"){if(runtime.pages.size()>=limit||runtime.next_page==INT_MAX)throw std::runtime_error("BiDi page capacity exceeded");if(attached&&id==0)throw std::runtime_error("Attached Firefox requires an SDK-owned context for new pages");const auto result=runtime.call("browsingContext.create",Json{{"type","tab"},{"userContext",user},{"background",false}},deadline);BidiPage page;page.id=++runtime.next_page;page.context=id;page.browsing_context=result.at("context").as_string();page.sandbox="suitecut-private-"+session_id;{std::lock_guard lock(mutex);owned_pages.insert(page.browsing_context);}remember(page);configure(runtime.pages.at(page.id),deadline);features->setup(runtime,runtime.pages.at(page.id),deadline);har->setup(runtime,runtime.pages.at(page.id),deadline);return Json{{"contextId",id},{"pageId",page.id}};}
    if(action=="grantPermissions"||action=="clearPermissions")return permissions.command(runtime,id,user,action,params,deadline);
    if(action=="close"){if(id==0)throw std::runtime_error("Close the browser to retire its default context");permissions.command(runtime,id,user,"clearPermissions",Json::object{},deadline);runtime.call("browser.removeUserContext",Json{{"userContext",user}},deadline);std::vector<int> ids;for(const auto&[key,page]:runtime.pages)if(page.context==id)ids.push_back(key);for(int key:ids)forget(key);features->forget_context(id);permissions.forget(id);runtime.contexts.erase(found);{std::lock_guard lock(mutex);owned_contexts.erase(user);settings.erase(id);}return Json{};}
    if(action=="pages"){Json::array out;for(const auto&[key,page]:runtime.pages)if(page.context==id)out.emplace_back(Json{{"pageId",key},{"contextId",id}});return Json{{"pages",std::move(out)}};}
    if(action=="cookies")return cookies(user,deadline);
    if(action=="addCookies"){set_cookies(user,params.at("cookies"),deadline);return Json{};}
    if(action=="clearCookies"){Json args{{"partition",Json{{"type","storageKey"},{"userContext",user}}}};if(params.contains("filter")&&!params.at("filter").as_object().empty())args["filter"]=params.at("filter");return runtime.call("storage.deleteCookies",args,deadline);}
    auto page=params.contains("pageId")?runtime.pages.find(integer(params,"pageId")):runtime.pages.end();if(!params.contains("pageId"))for(auto it=runtime.pages.begin();it!=runtime.pages.end();++it)if(it->second.context==id){page=it;break;}
    if(action=="storageState"||action=="setStorageState"){if(page==runtime.pages.end())throw std::runtime_error("Storage state requires an open page");Json request=params;request["command"]="storage";request["action"]=action=="storageState"?"getState":"setState";return page_command(page->second,request,deadline);}
    if(action=="setExtraHTTPHeaders"||action=="setOffline"||action=="setHTTPCredentials"||action=="addInitScript"||action=="removeInitScript"){Json request=params;request["command"]=(action=="addInitScript"||action=="removeInitScript")?"initScript":"network";request["action"]=action=="setExtraHTTPHeaders"?"headers":action=="setOffline"?"offline":action=="setHTTPCredentials"?"credentials":action=="addInitScript"?"add":"remove";BidiPage owner;owner.context=id;if(page!=runtime.pages.end())owner=page->second;return page_command(owner,request,deadline);}
    throw std::runtime_error("Unsupported native Firefox context action: "+action);
  }
  Json console_command(int page,const Json& fields){
    std::lock_guard lock(mutex);
    const auto found=states.find(page);
    if(found==states.end())throw std::runtime_error("Firefox console page is closed or unknown");
    auto& state=found->second;const auto action=text(fields,"action");
    if(action=="clear"){state.console.values.clear();state.errors.values.clear();return Json{};}
    if(action=="errors")return Json{{"errors",state.errors.array(flag(fields,"clear"))}};
    if(action=="list")return state.console.array(flag(fields,"clear"));
    if(action=="messages")return Json{{"messages",state.console.array(flag(fields,"clear"))}};
    throw std::invalid_argument("Unsupported Firefox console action: "+action);
  }
  Json page_command(BidiPage& page,const Json& fields,Deadline deadline){const auto command=fields.at("command").as_string(),action=text(fields,"action");
    if(command=="download"||command=="emulation")return features->command(runtime,page,fields,deadline);
    if(command=="network"&&(action=="harStart"||action=="harStop"||action=="exportHar"||action=="body"))return har->command(runtime,page,fields,deadline);
    std::unique_ptr<BidiFrameScope> frame;
    if(command=="evaluate"||command=="navigateFrame"||command=="navigate")frame=bidi_resolve_frame(runtime,page,fields,deadline);
    if(command=="evaluate")return runtime.evaluate(page,fields.at("expression").as_string(),deadline,flag(fields,"isolated"));
    if(command=="devtools")throw std::runtime_error("Chromium CDP is unavailable on Firefox; use the native BiDi API");
    if(command=="bidi"||command=="protocol"){const auto method=fields.at("method").as_string();if(method.find('.')==std::string::npos)throw std::invalid_argument("BiDi raw method must include a module");return runtime.call(method,fields.contains("params")?fields.at("params"):Json(Json::object{}),deadline);}
    if(command=="input")return bidi_input_command(runtime,page,fields,deadline);
    if(command=="navigateFrame"||command=="navigate"){auto wait=text(fields,"waitUntil","load");wait=wait=="load"?"complete":wait=="domcontentloaded"?"interactive":wait=="commit"?"none":wait;if(wait!="complete"&&wait!="interactive"&&wait!="none")throw std::runtime_error("Unsupported native Firefox navigation wait: "+wait);return runtime.call("browsingContext.navigate",Json{{"context",page.browsing_context},{"url",fields.at("url")},{"wait",wait}},deadline);}
    if(command=="reload")return runtime.call("browsingContext.reload",Json{{"context",page.browsing_context},{"wait","complete"},{"ignoreCache",flag(fields,"ignoreCache")}},deadline);
    if(command=="historyBack"||command=="historyForward")return runtime.call("browsingContext.traverseHistory",Json{{"context",page.browsing_context},{"delta",command=="historyBack"?-1:1}},deadline);
    if(command=="bringToFront")return runtime.call("browsingContext.activate",Json{{"context",page.browsing_context}},deadline);
    if(command=="frames")return runtime.call("browsingContext.getTree",Json{{"root",page.browsing_context}},deadline);
    if(command=="viewport"){const int width=integer(fields,"width"),height=integer(fields,"height");if(width<1||height<1||width>32768||height>32768)throw std::invalid_argument("Invalid Firefox viewport");return runtime.call("browsingContext.setViewport",Json{{"context",page.browsing_context},{"viewport",Json{{"width",width},{"height",height}}}},deadline);}
    if(command=="artifact"||command=="pdf") {
      if(command=="pdf"||action=="pdf")return runtime.call("browsingContext.print",Json{{"context",page.browsing_context}},deadline);
      if(action!="screenshot")throw std::runtime_error("Unsupported Firefox artifact action");
      const auto format=text(fields,"format","png");if(format!="png"&&format!="jpeg"&&format!="webp")throw std::invalid_argument("Invalid Firefox screenshot format");Json args{{"context",page.browsing_context},{"origin",flag(fields,"fullPage")?"document":"viewport"},{"format",Json{{"type","image/"+format}}}};if(fields.contains("quality"))args["format"]["quality"]=fields.at("quality").as_double()/100;return runtime.call("browsingContext.captureScreenshot",args,deadline);
    }
    if(command=="locator"||command=="form"||command=="queryEvaluate"||command=="snapshotReferences"||command=="waitFor"||command=="waitForSelector"||command=="waitForText"||command=="waitForFunction"||command=="waitLoadState"||command=="waitForURL")return bidi_dom_command(runtime,page,fields,deadline);
    if(command=="dialog") {if(action=="handle"){Json args{{"context",page.browsing_context},{"accept",fields.at("accept")}};if(fields.contains("promptText"))args["userText"]=fields.at("promptText");return runtime.call("browsingContext.handleUserPrompt",args,deadline);}std::lock_guard lock(mutex);return Json{{"dialogs",states[page.id].dialogs.array(flag(fields,"clear"))}};}
    if(command=="console")return console_command(page.id,fields);
    if(command=="initScript") {
      if(action=="add"){const auto source=fields.at("source").as_string();if(source.size()>49152)throw std::invalid_argument("Firefox init script exceeds48KiB");{std::lock_guard lock(mutex);if(settings[page.context].scripts.size()>=32)throw std::runtime_error("Firefox init script capacity exceeded");}const auto result=runtime.call("script.addPreloadScript",Json{{"functionDeclaration","()=>{\n"+source+"\n}"},{"userContexts",Json::array{Json(runtime.contexts.at(page.context))}}},deadline);const auto id=result.at("script").as_string();{std::lock_guard lock(mutex);settings[page.context].scripts[id]=source;}return Json{{"identifier",id}};}
      if(action=="remove"){const auto id=fields.at("identifier").as_string();runtime.call("script.removePreloadScript",Json{{"script",id}},deadline);std::lock_guard lock(mutex);settings[page.context].scripts.erase(id);return Json{};}
      std::lock_guard lock(mutex);Json::array out;for(const auto&[id,source]:settings[page.context].scripts)out.emplace_back(Json{{"identifier",id},{"source",source}});return Json{{"scripts",std::move(out)}};
    }
    if(command=="storage") {
      const auto user=runtime.contexts.at(page.context);
      if(action=="getState"){auto origin=runtime.value(page,"(()=>{const read=s=>Array.from({length:s.length},(_,i)=>{const name=s.key(i);return {name,value:s.getItem(name)}});return {origin:location.origin,localStorage:read(localStorage),sessionStorage:read(sessionStorage)}})()",deadline);return Json{{"cookies",cookies(user,deadline).at("cookies")},{"origins",Json::array{std::move(origin)}}};}
      if(action!="setState")throw std::runtime_error("Unsupported Firefox storage action");
      const auto& state=fields.at("state");const auto current=runtime.value(page,"location.origin",deadline).as_string();if(!state.at("origins").is_array()||state.at("origins").as_array().size()>128)throw std::invalid_argument("Invalid storage origins");for(const auto& origin:state.at("origins").as_array())if(origin.at("origin").as_string()!=current)throw std::invalid_argument("Firefox storage restoration supports only the current page origin; deferred origins unsupported");runtime.call("storage.deleteCookies",Json{{"partition",Json{{"type","storageKey"},{"userContext",user}}}},deadline);set_cookies(user,state.at("cookies"),deadline);runtime.value(page,"(()=>{const entries="+state.at("origins").dump()+";for(const e of entries)if(location.origin===e.origin)for(const key of ['localStorage','sessionStorage'])if(Array.isArray(e[key])){globalThis[key].clear();for(const item of e[key])globalThis[key].setItem(item.name,item.value)}})()",deadline);return Json{};
    }
    if(command=="network") {
      if(action=="requests"||action=="responses"){std::lock_guard lock(mutex);return action=="requests"?Json{{"requests",states[page.id].requests.array(flag(fields,"clear"))}}:Json{{"responses",states[page.id].responses.array(flag(fields,"clear"))}};}
      if(action=="body"){const auto result=runtime.call("network.getData",Json{{"request",fields.at("requestId")},{"dataType","response"}},deadline);const auto& bytes=result.at("bytes");return Json{{"body",bytes.at("value")},{"base64Encoded",text(bytes,"type")=="base64"}};}
      {std::lock_guard lock(mutex);auto& config=settings[page.context];
        if(action=="route"){if(config.routes.size()>=256||next_route==INT_MAX)throw std::runtime_error("Firefox route capacity exceeded");if(fields.contains("headers"))byte_headers(fields.at("headers"));if(fields.contains("body")&&fields.at("body").as_string().size()>49152)throw std::invalid_argument("Firefox mock body exceeds48KiB");if(fields.contains("status")&&(fields.at("status").as_int()<100||fields.at("status").as_int()>599))throw std::invalid_argument("Firefox mock status must be100..599");const auto pattern=fields.at("pattern").as_string(),behavior=fields.at("behavior").as_string();if(pattern.empty()||pattern.size()>2048||(behavior!="continue"&&behavior!="abort"&&behavior!="mock"))throw std::invalid_argument("Invalid Firefox route");auto route=fields;route["routeId"]=++next_route;config.routes.push_back(route);}
        else if(action=="unroute")std::erase_if(config.routes,[&](const auto& route){return (!fields.contains("routeId")&&!fields.contains("pattern"))||(fields.contains("routeId")&&fields.at("routeId")==route.at("routeId"))||(fields.contains("pattern")&&fields.at("pattern")==route.at("pattern"));});
        else if(action=="routes")return Json{{"routes",config.routes}};
        else if(action=="headers"){byte_headers(fields.at("headers"));config.headers=fields.at("headers");}
        else if(action=="offline")config.offline=fields.at("offline").as_bool();
        else if(action=="credentials"){const auto& creds=fields.at("credentials");if(!creds.is_null()){creds.at("username").as_string();creds.at("password").as_string();}config.credentials=creds;}
        else throw std::runtime_error("Unsupported native Firefox network action: "+action);
      }
      for(auto&[id,owned]:runtime.pages){(void)id;if(owned.context==page.context)configure(owned,deadline);}return action=="route"?Json{{"routeId",next_route}}:Json{};
    }
    throw std::runtime_error("Unsupported native Firefox command family: "+command);
  }
  Json dispatch(const Work& work){synchronize();if(work.fields.at("command").as_string()=="context")return context_command(work.fields,work.deadline);const auto page=runtime.pages.find(work.page);if(page==runtime.pages.end())throw std::runtime_error("Firefox page is closed or unknown");return page_command(page->second,work.fields,work.deadline);}
  void cancel_locked(int id,const std::string& reason){const auto found=pending.find(id);if(found==pending.end())return;found->second->canceled.store(true);found->second->promise.set_exception(std::make_exception_ptr(std::runtime_error(reason)));pending.erase(found);for(auto* lane:{&normal,&controls})for(auto it=lane->begin();it!=lane->end();++it)if((*it)->id==id){queued-=(*it)->bytes;lane->erase(it);goto done;}done:counters.pending=pending.size();changed.notify_all();}
  void run(bool control){auto& lane=control?controls:normal;for(;;){std::shared_ptr<Work> work;std::optional<Automatic> internal;bool drain_har=false;{std::unique_lock lock(mutex);changed.wait(lock,[&]{return closing||!terminal_error.empty()||!lane.empty()||(control&&(!automatic.empty()||har_drain_requested));});if(closing||!terminal_error.empty())return;if(control&&!automatic.empty()){internal=std::move(automatic.front());queued-=internal->bytes;automatic.pop_front();}else if(control&&lane.empty()&&har_drain_requested){har_drain_requested=false;drain_har=true;}else{work=lane.front();lane.pop_front();queued-=work->bytes;if(!pending.contains(work->id))continue;if(Clock::now()>=work->deadline){cancel_locked(work->id,"BiDi request timed out in queue");continue;}}}
      if(drain_har){try{if(har->drain(runtime,Clock::now()+std::chrono::seconds{2})){std::lock_guard lock(mutex);har_drain_requested=true;changed.notify_all();}}catch(const std::exception& error){std::lock_guard lock(mutex);if(!closing){terminal_error="Firefox HAR collection failed: "+std::string(error.what());while(!pending.empty())cancel_locked(pending.begin()->first,terminal_error);normal.clear();controls.clear();automatic.clear();har_drain_requested=false;queued=0;changed.notify_all();}return;}continue;}
      if(internal){try{runtime.wire->call(internal->method,internal->params,"",Clock::now()+std::chrono::seconds{5});}catch(const std::exception& error){std::lock_guard lock(mutex);if(!closing){terminal_error="Firefox automatic "+internal->method+" failed: "+error.what();while(!pending.empty())cancel_locked(pending.begin()->first,terminal_error);normal.clear();controls.clear();automatic.clear();queued=0;changed.notify_all();}return;}continue;}
      canceled=&work->canceled;Packet reply;try{Json value;if(control){if(work->context.empty())throw std::runtime_error("Firefox continuation page is closed or unknown");BidiPage page;page.id=work->page;page.browsing_context=work->context;const auto family=work->fields.at("command").as_string();if(family=="bidi"||family=="protocol")value=runtime.call(work->fields.at("method").as_string(),work->fields.contains("params")?work->fields.at("params"):Json(Json::object{}),work->deadline);else if(family=="dialog"&&text(work->fields,"action")=="handle"){Json args{{"context",work->context},{"accept",work->fields.at("accept")}};if(work->fields.contains("promptText"))args["userText"]=work->fields.at("promptText");value=runtime.call("browsingContext.handleUserPrompt",args,work->deadline);}else if(family=="console")value=console_command(work->page,work->fields);else{std::lock_guard lock(mutex);value=Json{{"dialogs",states.at(work->page).dialogs.array(flag(work->fields,"clear"))}};}}else value=dispatch(*work);reply=pack(Json{{"type","response"},{"id",work->id},{"pageId",work->page},{"ok",true}},value,runtime.options.max_payload_bytes);}catch(const std::exception& error){reply=pack(Json{{"type","response"},{"id",work->id},{"pageId",work->page},{"ok",false},{"error",std::string(error.what()).substr(0,16384)}},Json{},runtime.options.max_payload_bytes);}canceled=nullptr;{std::lock_guard lock(mutex);++counters.responses;const auto found=pending.find(work->id);if(found!=pending.end()){if(Clock::now()>=work->deadline)cancel_locked(work->id,"BiDi request timed out");else{found->second->promise.set_value(std::move(reply));pending.erase(found);counters.pending=pending.size();}}changed.notify_all();}}
  }
#ifndef _WIN32
  void drain_logs(){if(log_fd<0)return;char buffer[4096];for(int n=0;n<16;++n){const auto count=::read(log_fd,buffer,sizeof(buffer));if(count<=0)break;logs.append(buffer,static_cast<std::size_t>(count));if(logs.size()>16384)logs.erase(0,logs.size()-16384);}}
  std::string spawn(Deadline deadline){
    const auto& options=runtime.options;if(options.executable.empty()||!std::filesystem::path(options.executable).is_absolute()||options.executable.find('\0')!=std::string::npos||::access(options.executable.c_str(),X_OK))throw std::invalid_argument("Firefox executable must be an executable absolute path");
    if(options.profile.empty()){auto name=(std::filesystem::temp_directory_path()/"suitecut-firefox-XXXXXX").string();std::vector<char> writable(name.begin(),name.end());writable.push_back(0);const auto created=mkdtemp(writable.data());if(!created)throw std::runtime_error("Could not create Firefox profile");profile=created;owned_profile=true;}else{if(!std::filesystem::path(options.profile).is_absolute())throw std::invalid_argument("Firefox profile must be absolute");profile=options.profile;std::filesystem::create_directories(profile);}
    std::vector<std::string> args{options.executable};std::size_t total=0;for(const auto& arg:options.arguments){if(arg.find('\0')!=std::string::npos||arg=="--"||arg.starts_with("--profile")||arg.starts_with("-profile")||arg.starts_with("-P")||arg.starts_with("--remote-debugging")||arg.starts_with("--marionette"))throw std::invalid_argument("Firefox argument overrides a managed switch");total+=arg.size();if(total>512*1024||args.size()>1024)throw std::invalid_argument("Firefox arguments exceed budget");args.push_back(arg);}args.insert(args.end(),{"--no-remote","--remote-debugging-port=0","-remote-allow-system-access","--profile",profile});if(options.headless)args.push_back("--headless");
    int pipefd[2];if(pipe(pipefd))throw std::runtime_error("Could not create Firefox log pipe");log_fd=pipefd[0];fcntl(log_fd,F_SETFL,fcntl(log_fd,F_GETFL)|O_NONBLOCK);fcntl(log_fd,F_SETFD,FD_CLOEXEC);fcntl(pipefd[1],F_SETFD,FD_CLOEXEC);
    posix_spawn_file_actions_t files;posix_spawnattr_t attrs;posix_spawn_file_actions_init(&files);posix_spawnattr_init(&attrs);posix_spawn_file_actions_addopen(&files,0,"/dev/null",O_RDONLY,0);posix_spawn_file_actions_adddup2(&files,pipefd[1],1);posix_spawn_file_actions_adddup2(&files,pipefd[1],2);posix_spawn_file_actions_addclose(&files,pipefd[0]);posix_spawn_file_actions_addclose(&files,pipefd[1]);posix_spawnattr_setflags(&attrs,POSIX_SPAWN_SETPGROUP);posix_spawnattr_setpgroup(&attrs,0);std::vector<char*> argv;for(auto& arg:args)argv.push_back(arg.data());argv.push_back(nullptr);const int error=posix_spawn(&pid,options.executable.c_str(),&files,&attrs,argv.data(),environ);posix_spawnattr_destroy(&attrs);posix_spawn_file_actions_destroy(&files);::close(pipefd[1]);if(error)throw std::runtime_error("Could not spawn Firefox: "+std::to_string(error));
    while(Clock::now()<deadline){drain_logs();const std::string marker="WebDriver BiDi listening on ";const auto begin=logs.find(marker);if(begin!=std::string::npos){const auto start=begin+marker.size();const auto end=logs.find_first_of(" \r\n\t",start);if(end!=std::string::npos){auto endpoint=logs.substr(start,end-start);if(!endpoint.starts_with("ws://127.0.0.1:")&&!endpoint.starts_with("ws://localhost:"))throw std::runtime_error("Firefox announced a nonlocal endpoint");if(endpoint.back()=='/')endpoint.pop_back();return endpoint+"/session";}}int status=0;if(waitpid(pid,&status,WNOHANG)==pid){reaped=true;throw std::runtime_error("Firefox exited before BiDi readiness: "+logs);}struct pollfd poller{log_fd,POLLIN,0};::poll(&poller,1,25);}throw std::runtime_error("Firefox RemoteAgent startup timed out: "+logs);
  }
  void retire(){
    std::string retirement_failure;
    if(pid>0){
      const auto owned_group=pid;
      const auto wait_main=[&](Deadline deadline){
        while(!reaped&&Clock::now()<deadline){
          int status=0;const auto result=waitpid(pid,&status,WNOHANG);
          if(result==pid||(result<0&&errno==ECHILD)){reaped=true;break;}
          if(result<0&&errno!=EINTR){retirement_failure="Cannot reap owned Firefox parent";break;}
          drain_logs();std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
      };
      wait_main(Clock::now()+runtime.options.shutdown_timeout);
      if(!reaped){kill(-owned_group,SIGTERM);wait_main(Clock::now()+std::chrono::milliseconds{250});}
      if(!reaped){
        kill(-owned_group,SIGKILL);int status=0;pid_t result;
        do{result=waitpid(pid,&status,0);}while(result<0&&errno==EINTR);
        reaped=result==pid||(result<0&&errno==ECHILD);
        if(!reaped)retirement_failure="Cannot reap killed Firefox parent";
      }
      // Only an embedding process that explicitly acts as a subreaper can own
      // Firefox's orphaned helpers. Never change that process-wide policy here,
      // and never consume unrelated children with waitpid(-1).
      bool escalated=false;
      auto helper_deadline=Clock::now()+std::chrono::milliseconds{500};
      for(;;){
        int status=0;const auto child=waitpid(-owned_group,&status,WNOHANG);
        if(child>0){
          if(!(WIFEXITED(status)&&WEXITSTATUS(status)==0)&&
             !(WIFSIGNALED(status)&&WTERMSIG(status)==SIGTERM))
            retirement_failure="Owned Firefox helper exited unexpectedly: pid="+std::to_string(child)+" status="+std::to_string(status);
          continue;
        }
        if(child<0&&errno==ECHILD)break;
        if(child<0&&errno!=EINTR){retirement_failure="Cannot reap owned Firefox helper group";break;}
        if(kill(-owned_group,0)<0&&errno==ESRCH)break;
        if(Clock::now()>=helper_deadline){
          if(escalated){retirement_failure="Owned Firefox process group did not retire before its physical deadline";break;}
          kill(-owned_group,SIGKILL);escalated=true;helper_deadline=Clock::now()+std::chrono::milliseconds{250};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
      pid=-1;
    }
    if(log_fd>=0){::close(log_fd);log_fd=-1;}
    if(owned_profile){std::error_code error;std::filesystem::remove_all(profile,error);if(error)throw std::runtime_error("Could not remove Firefox profile: "+error.message());owned_profile=false;}
    if(!retirement_failure.empty())throw std::runtime_error(retirement_failure);
  }
#endif
  void expire(){std::unique_lock lock(mutex);while(!closing){auto next=Clock::now()+std::chrono::milliseconds{50};std::vector<int> expired;for(const auto&[id,work]:pending){if(work->deadline<=Clock::now())expired.push_back(id);else next=std::min(next,work->deadline);}for(int id:expired)cancel_locked(id,"BiDi request timed out");lock.unlock();
#ifndef _WIN32
      drain_logs();
#endif
      lock.lock();changed.wait_until(lock,next);}}
};
BidiBackend::BidiBackend(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
BidiBackend::~BidiBackend(){try{close();}catch(...){}}
std::unique_ptr<BidiBackend> BidiBackend::launch(const LaunchOptions& options){duration(options.startup_timeout);duration(options.shutdown_timeout);if(!options.max_pending||options.max_pending>limit||!options.max_payload_bytes||options.max_payload_bytes>192*1024*1024||!options.max_event_bytes||options.max_event_bytes>256*1024*1024)throw std::invalid_argument("Invalid BiDi storage limits");auto backend=std::unique_ptr<BidiBackend>(new BidiBackend(std::make_unique<Impl>(options)));auto& state=*backend->impl_;const auto deadline=Clock::now()+options.startup_timeout;auto connection=options;
  if(!state.attached){
#ifdef _WIN32
    throw std::runtime_error("Native Firefox process launch currently requires POSIX");
#else
    connection.cdp_endpoint=state.spawn(deadline);
#endif
  }
  connection.profile.clear();connection.arguments.clear();state.runtime.wire=CdpConnection::connect_websocket(connection);state.runtime.wire->set_event_handler([&state](const Json& event){state.observe(event);});const auto session=state.runtime.call("session.new",Json{{"capabilities",Json{{"alwaysMatch",Json{{"acceptInsecureCerts",false},{"unhandledPromptBehavior","ignore"}}}}}},deadline);state.owns_session=true;state.session_id=session.at("sessionId").as_string();const auto& caps=session.at("capabilities");state.version=text(caps,"browserName","Firefox")+" "+text(caps,"browserVersion");state.runtime.contexts.emplace(0,"default");state.runtime.call("session.subscribe",Json{{"events",Json::array{Json("browsingContext.contextCreated"),Json("browsingContext.contextDestroyed"),Json("browsingContext.userPromptOpened"),Json("browsingContext.userPromptClosed"),Json("network.beforeRequestSent"),Json("network.responseStarted"),Json("network.responseCompleted"),Json("network.fetchError"),Json("network.authRequired"),Json("log.entryAdded")}}},deadline);const auto tree=state.runtime.call("browsingContext.getTree",Json{{"maxDepth",0},{"moz:scope","content"}},deadline);std::string main;for(const auto& item:tree.at("contexts").as_array())if(state.attached&&text(item,"userContext","default")=="default"){const auto url=text(item,"url");if(url=="about:blank"||url.starts_with("http:")||url.starts_with("https:")||url.starts_with("data:")||url.starts_with("file:")||url.starts_with("blob:")){main=item.at("context").as_string();break;}}if(main.empty()){const auto created=state.runtime.call("browsingContext.create",Json{{"type","tab"}},deadline);main=created.at("context").as_string();state.owned_pages.insert(main);}BidiPage page;page.browsing_context=main;page.sandbox="suitecut-private-"+state.session_id;state.remember(page);state.features->setup(state.runtime,state.runtime.pages.at(page.id),deadline);state.har->setup(state.runtime,state.runtime.pages.at(page.id),deadline);if(!state.attached)state.runtime.call("browsingContext.setViewport",Json{{"context",main},{"viewport",Json{{"width",options.width},{"height",options.height}}}},deadline);state.initial=state.ready_for(1);state.worker=std::thread([&state]{state.run(false);});state.continuation=std::thread([&state]{state.run(true);});state.timer=std::thread([&state]{state.expire();});return backend;}
Request BidiBackend::submit(Json fields,std::chrono::milliseconds timeout){duration(timeout);const auto start=Clock::now();if(!fields.is_object()||!fields.contains("command")||!fields.at("command").is_string())throw std::invalid_argument("BiDi command must be an object with command");if(fields.contains("timeoutMs")){auto requested=std::chrono::milliseconds{fields.at("timeoutMs").as_int()};duration(requested);timeout=std::min(timeout,requested);}auto& state=*impl_;std::lock_guard lock(state.mutex);if(state.closing)throw std::runtime_error("Firefox backend closed");if(!state.terminal_error.empty())throw std::runtime_error(state.terminal_error);if(state.pending.size()+state.automatic.size()>=state.runtime.options.max_pending||state.next_id==INT_MAX)throw std::runtime_error("BiDi pending request budget exceeded");const int id=++state.next_id,page=integer(fields,"pageId",1);if(page<1)throw std::invalid_argument("BiDi pageId must be positive");fields["version"]=1;fields["id"]=id;fields["pageId"]=page;fields["timeoutMs"]=timeout.count();const auto bytes=fields.dump({command_limit-1,64,command_limit,command_limit-1}).size()+1;if(bytes>queue_limit-state.queued)throw std::runtime_error("BiDi command writer full");auto work=std::make_shared<Impl::Work>();work->id=id;work->page=page;work->fields=std::move(fields);work->deadline=start+timeout;work->bytes=bytes;const auto context=state.page_contexts.find(page);if(context!=state.page_contexts.end())work->context=context->second;const auto family=work->fields.at("command").as_string(),method=text(work->fields,"method");const bool control=family=="dialog"||family=="console"||((family=="bidi"||family=="protocol")&&(method.starts_with("network.continue")||method=="network.failRequest"||method=="network.provideResponse"||method=="browsingContext.handleUserPrompt"));Request request{id,work->promise.get_future()};state.pending.emplace(id,work);try{(control?state.controls:state.normal).push_back(std::move(work));}catch(...){state.pending.erase(id);throw;}state.queued+=bytes;++state.counters.commands;state.counters.pending=state.pending.size();state.changed.notify_all();return request;}
void BidiBackend::cancel(int id){std::lock_guard lock(impl_->mutex);impl_->cancel_locked(id,"BiDi request canceled; dispatched operation outcome is unknown");}
std::optional<Packet> BidiBackend::poll(std::chrono::milliseconds timeout){
  duration(timeout,true);const auto deadline=Clock::now()+timeout;
  for(;;){
    {std::lock_guard lock(impl_->mutex);if(impl_->closing)return std::nullopt;if(!impl_->terminal_error.empty())throw std::runtime_error(impl_->terminal_error);}
    const auto remaining=std::max(std::chrono::milliseconds{0},std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()));
    const auto event=impl_->runtime.wire->poll(std::min(remaining,std::chrono::milliseconds{25}));
    {std::lock_guard lock(impl_->mutex);if(impl_->closing)return std::nullopt;if(!impl_->terminal_error.empty())throw std::runtime_error(impl_->terminal_error);}
    if(event){
      Json header{{"type","bidi"},{"backend","firefox"}};
      if(event->contains("params")&&event->at("params").contains("context")){std::lock_guard lock(impl_->mutex);const auto page=impl_->page_ids.find(event->at("params").at("context").as_string());if(page!=impl_->page_ids.end())header["pageId"]=page->second;}
      return pack(std::move(header),*event,impl_->runtime.options.max_payload_bytes);
    }
    if(Clock::now()>=deadline)return std::nullopt;
  }
}
Packet BidiBackend::ready()const{std::lock_guard lock(impl_->mutex);return impl_->initial;}
Packet BidiBackend::wait_ready(int page,std::chrono::milliseconds timeout){duration(timeout);std::unique_lock lock(impl_->mutex);if(!impl_->changed.wait_for(lock,timeout,[&]{return impl_->closing||!impl_->terminal_error.empty()||impl_->ready_pages.contains(page);}))throw std::runtime_error("BiDi page readiness timed out");if(!impl_->terminal_error.empty())throw std::runtime_error(impl_->terminal_error);const auto found=impl_->ready_pages.find(page);if(found==impl_->ready_pages.end())throw std::runtime_error("Firefox closed before page readiness");return found->second;}
ClientStats BidiBackend::stats()const{ClientStats out;{std::lock_guard lock(impl_->mutex);out=impl_->counters;}if(impl_->runtime.wire){const auto wire=impl_->runtime.wire->stats();out.queued_event_bytes+=wire.queued_event_bytes;out.dropped_events+=wire.dropped_events;}return out;}
void BidiBackend::close(){if(!impl_)return;auto& state=*impl_;std::lock_guard closing_lock(state.close_mutex);if(state.closed)return;std::vector<std::string> pages,contexts;{std::lock_guard lock(state.mutex);state.closing=true;while(!state.pending.empty())state.cancel_locked(state.pending.begin()->first,"Firefox backend closed");state.normal.clear();state.controls.clear();state.automatic.clear();state.queued=0;pages.assign(state.owned_pages.begin(),state.owned_pages.end());contexts.assign(state.owned_contexts.begin(),state.owned_contexts.end());state.changed.notify_all();}std::exception_ptr failure;const auto deadline=Clock::now()+state.runtime.options.shutdown_timeout;if(state.runtime.wire){
    try{if(state.har)state.har->cleanup(state.runtime,deadline);}catch(...){failure=std::current_exception();}
    try{state.permissions.close(state.runtime,deadline);}catch(...){if(!failure)failure=std::current_exception();}
    try{if(state.features)state.features->close(state.runtime,deadline);}catch(...){if(!failure)failure=std::current_exception();}
    if(state.attached&&state.owns_session){for(const auto& page:pages)try{state.runtime.wire->call("browsingContext.close",Json{{"context",page}},"",deadline);}catch(...){}for(const auto& context:contexts)try{state.runtime.wire->call("browser.removeUserContext",Json{{"userContext",context}},"",deadline);}catch(...){}try{state.runtime.wire->call("session.end",Json::object{},"",deadline);}catch(...){failure=std::current_exception();}}else if(!state.attached)try{state.runtime.wire->call("browser.close",Json::object{},"",deadline);}catch(...){}try{state.runtime.wire->close();}catch(...){if(!failure)failure=std::current_exception();}}
  for(auto* thread:{&state.worker,&state.continuation,&state.timer})try{if(thread->joinable())thread->join();}catch(...){if(!failure)failure=std::current_exception();}
#ifndef _WIN32
  if(!state.attached)try{state.retire();}catch(...){if(!failure)failure=std::current_exception();}
#endif
  state.runtime.pages.clear();state.runtime.contexts.clear();{std::lock_guard lock(state.mutex);state.states.clear();state.settings.clear();state.ready_pages.clear();state.page_ids.clear();state.page_contexts.clear();state.closed=true;}if(failure)std::rethrow_exception(failure);
}
} // namespace suitecut::detail
