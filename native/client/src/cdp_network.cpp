// SPDX-License-Identifier: MIT
#include "cdp_network.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace suitecut::detail {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t count_limit = 1024, cache_limit = 8 * 1024 * 1024;
std::string text(const Json& v, const char* key, std::string fallback = {}) {
  return v.contains(key) ? v.at(key).as_string() : std::move(fallback);
}
bool flag(const Json& v, const char* key, bool fallback = false) {
  return v.contains(key) ? v.at(key).as_bool() : fallback;
}
std::string iso_time(double seconds) {
  const auto value=static_cast<std::time_t>(seconds);std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm,&value);
#else
  gmtime_r(&value,&tm);
#endif
  std::ostringstream out;out<<std::put_time(&tm,"%Y-%m-%dT%H:%M:%S")<<'.'<<std::setw(3)<<std::setfill('0')<<static_cast<int>((seconds-value)*1000)<<'Z';return out.str();
}
double numeric(const Json& value,const char* key,double fallback=-1) {
  return value.contains(key)&&value.at(key).is_number()?value.at(key).as_double():fallback;
}
std::string header_value(const Json& input,const std::string& wanted) {
  if(!input.is_object())return {};
  for(const auto&[key,value]:input.as_object()) {
    auto lower=key;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(lower==wanted&&value.is_string())return value.as_string();
  }
  return {};
}
bool glob(const std::string& pattern, const std::string& value,std::size_t& budget) {
  std::size_t p=0,v=0,star=std::string::npos,mark=0;
  while(v<value.size()) {
    if(!budget--)throw std::runtime_error("Route glob matching work budget exceeded");
    if(p<pattern.size()&&(pattern[p]=='?'||pattern[p]==value[v])) { ++p;++v; }
    else if(p<pattern.size()&&pattern[p]=='*') { star=p++;mark=v; }
    else if(star!=std::string::npos) { p=star+1;v=++mark; }
    else return false;
  }
  while(p<pattern.size()&&pattern[p]=='*')++p;
  return p==pattern.size();
}
std::string base64(const std::string& data) {
  constexpr char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;out.reserve((data.size()+2)/3*4);
  for(std::size_t i=0;i<data.size();i+=3) {
    const auto a=static_cast<unsigned char>(data[i]);
    const auto b=i+1<data.size()?static_cast<unsigned char>(data[i+1]):0;
    const auto c=i+2<data.size()?static_cast<unsigned char>(data[i+2]):0;
    out+=digits[a>>2];out+=digits[((a&3)<<4)|(b>>4)];
    out+=i+1<data.size()?digits[((b&15)<<2)|(c>>6)]:'=';out+=i+2<data.size()?digits[c&63]:'=';
  }
  return out;
}
Json headers(const Json& input) {
  if(!input.is_object()||input.as_object().size()>256)throw std::invalid_argument("Headers must be an object of at most256 string entries");
  for(const auto&[key,value]:input.as_object()) {
    if(key.empty()||key.find_first_of("\r\n\0",0,3)!=std::string::npos||!value.is_string()||
       value.as_string().find_first_of("\r\n\0",0,3)!=std::string::npos)
      throw std::invalid_argument("HTTP header names/values must be strings without line breaks or nulls");
  }
  return input;
}
Json header_array(const Json& input) {
  Json::array out;
  if(input.is_object())for(const auto&[name,value]:input.as_object())
    out.emplace_back(Json{{"name",name},{"value",value.is_string()?value.as_string():value.dump()}});
  return out;
}
Json bounded(const Json& value) {
  try {value.dump({32768,32,16384,32768});return value;}
  catch(...) {return Json{{"truncated",true}};}
}
struct Ring {
  std::deque<std::pair<Json,std::size_t>> values;
  std::size_t bytes=0,dropped=0;
  void add(Json value) {
    value=bounded(value);const auto n=value.dump().size();
    while(!values.empty()&&(values.size()>=count_limit||bytes+n>cache_limit)) {
      bytes-=values.front().second;values.pop_front();++dropped;
    }
    values.emplace_back(std::move(value),n);bytes+=n;
  }
  Json take(bool clear=false) {
    Json::array out;out.reserve(values.size());for(const auto& v:values)out.push_back(v.first);
    if(clear){values.clear();bytes=0;dropped=0;}return out;
  }
};
}
struct CdpNetwork::Impl {
  mutable std::mutex mutex;
  std::function<void(CdpAutomatic)> enqueue;
  struct Route {int id=0;Json fields;};
  struct Settings {Json extra_headers=Json::object{},credentials;bool offline=false;std::vector<Route> routes;std::map<std::string,std::string> scripts;};
  struct Page {
    int id=0,context=0;std::set<std::string> inflight,auth;std::map<std::string,int> execution_contexts;
    std::map<std::string,std::string> script_ids;Clock::time_point activity=Clock::now();Ring requests,responses,console,errors,dialogs;
    bool har=false,har_embed=true,har_truncated=false;
    std::map<std::string,Json> har_entries;
    std::map<std::string,std::string> har_current;
    std::map<std::string,std::size_t> har_sizes;
    std::deque<std::string> har_order;
    std::size_t har_bytes=0,har_dropped=0;
    std::uint64_t har_generation=0,har_serial=0;
  };
  std::map<int,Settings> settings;
  std::map<std::string,Page> pages;
  Ring browser;
  int next_route=0,next_script=0;
  std::uint64_t event_sequence=0;
  explicit Impl(std::function<void(CdpAutomatic)> value):enqueue(std::move(value)){}
  static constexpr std::size_t body_limit=2*1024*1024;
  static std::string current(const Page& page,const std::string& id) {
    const auto found=page.har_current.find(id);return found==page.har_current.end()?std::string{}:found->second;
  }
  static void store(Page& page,const std::string& key,Json entry) {
    std::size_t bytes;
    try{bytes=entry.dump({6*1024*1024,48,262144,3*1024*1024}).size();}
    catch(const std::exception&) {
      // UTF-8 byte counts and base64 decoded counts are not JSON storage
      // counts: escaped controls can expand sixfold. Preserve metadata with an
      // explicit body error when a retained payload exceeds the entry budget.
      bool removed=false;
      if(entry.contains("response")&&entry.at("response").contains("content")) {
        auto& content=entry["response"]["content"];
        if(content.contains("text")){content.as_object().erase("text");content.as_object().erase("encoding");content["_bodyError"]="Serialized body exceeds HAR entry storage budget";removed=true;}
      }
      if(entry.contains("request")&&entry.at("request").contains("postData")) {
        entry["request"].as_object().erase("postData");entry["request"]["_postDataError"]="Serialized POST data exceeds HAR entry storage budget";removed=true;
      }
      if(!removed)throw;
      page.har_truncated=true;bytes=entry.dump({6*1024*1024,48,262144,3*1024*1024}).size();
    }
    const auto old=page.har_sizes.contains(key)?page.har_sizes.at(key):0;
    while(!page.har_order.empty()&&((page.har_entries.size()>=count_limit&&!page.har_entries.contains(key))||page.har_bytes-old+bytes>cache_limit)) {
      auto oldest=std::find_if(page.har_order.begin(),page.har_order.end(),[&](const auto& id){return id!=key;});
      if(oldest==page.har_order.end())break;
      const auto removed=*oldest;page.har_order.erase(oldest);page.har_bytes-=page.har_sizes.at(removed);
      page.har_sizes.erase(removed);page.har_entries.erase(removed);
      std::erase_if(page.har_current,[&](const auto& item){return item.second==removed;});
      page.har_truncated=true;++page.har_dropped;
    }
    if(page.har_bytes-old+bytes>cache_limit)throw std::runtime_error("HAR entry exceeds bounded archive storage");
    if(!page.har_entries.contains(key))page.har_order.push_back(key);
    page.har_bytes=page.har_bytes-old+bytes;page.har_sizes[key]=bytes;page.har_entries.insert_or_assign(key,std::move(entry));
  }
  static void apply_response(Json& entry,const Json& response) {
    auto& out=entry["response"];
    const auto status=numeric(response,"status",0);
    if(status<0||status>999||std::trunc(status)!=status)throw std::runtime_error("Invalid HTTP status in native CDP response");
    out["status"]=static_cast<int>(status);out["statusText"]=text(response,"statusText");
    out["httpVersion"]=text(response,"protocol");
    if(response.contains("headers")) {
      const auto safe=bounded(response.at("headers"));
      if(safe.contains("truncated")&&safe.at("truncated").is_bool())entry["_headersTruncated"]=true;
      else out["headers"]=header_array(safe);
    }
    out["content"]["mimeType"]=text(response,"mimeType");
    if(response.contains("headersText"))out["headersSize"]=response.at("headersText").as_string().size();
    const auto location=response.contains("headers")?header_value(response.at("headers"),"location"):std::string{};
    if(!location.empty())out["redirectURL"]=location;
    if(response.contains("timing")) {
      const auto& t=response.at("timing");entry["_cdpTiming"]=t;
      auto span=[&](const char* begin,const char* end) {
        const auto a=numeric(t,begin),b=numeric(t,end);return a>=0&&b>=a?b-a:-1;
      };
      auto& timing=entry["timings"];
      timing["dns"]=span("dnsStart","dnsEnd");timing["connect"]=span("connectStart","connectEnd");
      timing["ssl"]=span("sslStart","sslEnd");timing["send"]=span("sendStart","sendEnd");
      timing["wait"]=span("sendEnd","receiveHeadersStart");
      double first=-1;
      for(const auto* name:{"proxyStart","dnsStart","connectStart","sendStart"}) {
        const auto n=numeric(t,name);if(n>=0&&(first<0||n<first))first=n;
      }
      timing["blocked"]=first;
    }
  }
  static void finish(Json& entry,const Json& params) {
    const auto end=numeric(params,"timestamp");const auto begin=numeric(entry,"_timestamp");
    entry["_finished"]=true;
    if(end>=0&&begin>=0){entry["time"]=std::max(0.0,(end-begin)*1000);entry["_timeSource"]="CDP monotonic request/finish timestamps";}
    if(entry.contains("_cdpTiming")) {
      const auto& timing=entry.at("_cdpTiming");
      const auto request=numeric(timing,"requestTime"),headers=numeric(timing,"receiveHeadersEnd");
      if(end>=0&&request>=0&&headers>=0)entry["timings"]["receive"]=std::max(0.0,(end-request)*1000-headers);
    }
    if(params.contains("encodedDataLength"))entry["_transferSize"]=params.at("encodedDataLength");
  }
  Json har(const Page& page)const {
    Json::array entries;for(const auto& key:page.har_order)if(page.har_entries.contains(key))entries.push_back(page.har_entries.at(key));
    return Json{{"log",Json{{"version","1.2"},{"creator",Json{{"name","SuiteCut native Chromium"},{"version","1"}}},{"pages",Json::array{}},{"entries",std::move(entries)},
      {"_truncated",page.har_truncated},{"_droppedEntries",page.har_dropped},{"_retainedBytes",page.har_bytes},{"_maxBytes",cache_limit},
      {"_bodyPolicy",page.har_embed?"embed":"omit"},{"_bodyByteLimit",body_limit}}}};
  }
};
CdpNetwork::CdpNetwork(std::function<void(CdpAutomatic)> enqueue):impl_(std::make_unique<Impl>(std::move(enqueue))){}
CdpNetwork::~CdpNetwork()=default;
void CdpNetwork::observe(const Json& event) {
  const auto method=text(event,"method");
  const auto session=text(event,"sessionId");
  if(!event.contains("params"))return;
  const auto& params=event.at("params");
  std::optional<CdpAutomatic> action;
  {
    std::lock_guard lock(impl_->mutex);
    if(method.starts_with("Browser.download")||method=="Input.dragIntercepted") {
      auto copy=event;copy["_suitecutEventSequence"]=++impl_->event_sequence;impl_->browser.add(std::move(copy));return;
    }
    const auto found=impl_->pages.find(session);if(found==impl_->pages.end())return;
    auto& page=found->second;auto& settings=impl_->settings[page.context];
    if(method=="Runtime.executionContextCreated") {
      const auto& context=params.at("context");
      if(context.contains("auxData")&&flag(context.at("auxData"),"isDefault")&&context.at("auxData").contains("frameId")) {
        if(page.execution_contexts.size()>=1024)throw std::runtime_error("Frame execution context capacity exceeded");
        page.execution_contexts.insert_or_assign(context.at("auxData").at("frameId").as_string(),static_cast<int>(context.at("id").as_int()));
      }
    } else if(method=="Runtime.executionContextsCleared")page.execution_contexts.clear();
    else if(method=="Runtime.executionContextDestroyed")std::erase_if(page.execution_contexts,[&](const auto& entry){return entry.second==params.at("executionContextId").as_int();});
    else if(method=="Fetch.requestPaused") {
      const auto request_id=text(params,"requestId");
      const auto& url=params.at("request").at("url").as_string();
      CdpAutomatic next{"Fetch.continueRequest",session,Json{{"requestId",request_id}}};
      if(!settings.routes.empty()&&url.size()>65536)throw std::runtime_error("Routed request URL exceeds65536 bytes");
      std::size_t budget=1024*1024;
      for(auto it=settings.routes.rbegin();it!=settings.routes.rend();++it) {
        if(!glob(it->fields.at("pattern").as_string(),url,budget))continue;
        const auto behavior=text(it->fields,"behavior");
        if(behavior=="abort") {next.method="Fetch.failRequest";next.params["errorReason"]=text(it->fields,"errorReason","BlockedByClient");}
        else if(behavior=="mock") {
          next.method="Fetch.fulfillRequest";next.params["responseCode"]=it->fields.contains("status")?it->fields.at("status"):Json(200);
          next.params["body"]=base64(text(it->fields,"body"));
          next.params["responseHeaders"]=header_array(it->fields.contains("headers")?it->fields.at("headers"):Json::object{});
        }
        break;
      }
      action=std::move(next);
    } else if(method=="Fetch.authRequired") {
      const auto request_id=text(params,"requestId");
      Json reply{{"response","CancelAuth"}};
      if(!settings.credentials.is_null()&&!page.auth.contains(request_id)) {
        if(page.auth.size()>=count_limit)page.auth.erase(page.auth.begin());
        page.auth.insert(request_id);reply=Json{{"response","ProvideCredentials"},{"username",settings.credentials.at("username")},{"password",settings.credentials.at("password")}};
      }
      action=CdpAutomatic{"Fetch.continueWithAuth",session,Json{{"requestId",request_id},{"authChallengeResponse",std::move(reply)}}};
    } else if(method=="Network.requestWillBeSent") {
      const auto request_id=text(params,"requestId");page.activity=Clock::now();
      if(page.inflight.size()>=4096&&!page.inflight.contains(request_id))throw std::runtime_error("Network inflight tracking capacity exceeded");
      page.inflight.insert(request_id);page.requests.add(params);
      if(page.har) {
        const auto old=Impl::current(page,request_id);
        std::int64_t hop=0;
        if(!old.empty()&&page.har_entries.contains(old)) {
          auto previous=page.har_entries.at(old);hop=previous.at("_redirectHop").as_int()+1;
          if(params.contains("redirectResponse")) {
            Impl::apply_response(previous,params.at("redirectResponse"));Impl::finish(previous,params);
            previous["request"]["httpVersion"]=previous.at("response").at("httpVersion");
            previous["_redirect"]=true;previous["_bodyPending"]=false;
            previous["response"]["content"]["_bodyError"]="CDP does not retain redirect response bodies";
            previous["response"]["redirectURL"]=params.at("request").at("url");
            if(flag(previous,"_postDataPending")){previous["_postDataPending"]=false;previous["request"]["_postDataError"]="CDP no longer retains this redirect hop POST data";}
          } else previous["_error"]="CDP reused a request ID before a matching final response";
          Impl::store(page,old,std::move(previous));
        }
        const auto& request=params.at("request");
        const auto safe_headers=bounded(request.contains("headers")?request.at("headers"):Json::object{});
        const bool truncated=safe_headers.contains("truncated")&&safe_headers.at("truncated").is_bool();
        const auto key=request_id+":"+std::to_string(++page.har_serial);
        Json entry{{"startedDateTime",params.contains("wallTime")?iso_time(params.at("wallTime").as_double()):iso_time(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count())},{"time",0},
          {"request",Json{{"method",text(request,"method")},{"url",text(request,"url")},{"httpVersion",""},{"headers",truncated?Json(Json::array{}):header_array(safe_headers)},{"queryString",Json::array{}},{"cookies",Json::array{}},{"headersSize",-1},{"bodySize",-1}}},
          {"response",Json{{"status",0},{"statusText",""},{"httpVersion",""},{"headers",Json::array{}},{"cookies",Json::array{}},{"content",Json{{"size",0},{"mimeType",""}}},{"redirectURL",""},{"headersSize",-1},{"bodySize",-1}}},
          {"cache",Json::object{}},{"timings",Json{{"blocked",-1},{"dns",-1},{"connect",-1},{"ssl",-1},{"send",-1},{"wait",-1},{"receive",-1}}},
          {"_requestId",request_id},{"_redirectHop",hop},{"_timestamp",params.contains("timestamp")?params.at("timestamp"):Json(0)},
          {"_finished",false},{"_bodyPending",page.har_embed}};
        if(truncated)entry["_headersTruncated"]=true;
        if(request.contains("postData")&&request.at("postData").is_string()&&request.at("postData").as_string().size()<=Impl::body_limit) {
          entry["request"]["postData"]=Json{{"mimeType",request.contains("headers")?header_value(request.at("headers"),"content-type"):std::string{}},{"text",request.at("postData")}};
          entry["request"]["bodySize"]=request.at("postData").as_string().size();
        }else if(flag(request,"hasPostData"))entry["_postDataPending"]=true;
        // Preserve exact CDP wall time without inventing a human timestamp.
        if(params.contains("wallTime"))entry["_wallTime"]=params.at("wallTime");
        entry["_wallTimeSource"]=params.contains("wallTime")?"CDP wallTime":"observer receipt wall clock";
        Impl::store(page,key,std::move(entry));page.har_current[request_id]=key;
      }
    } else if(method=="Network.responseReceived") {
      page.responses.add(params);const auto key=Impl::current(page,text(params,"requestId"));
      if(page.har_entries.contains(key)) {
        auto entry=page.har_entries.at(key);Impl::apply_response(entry,params.at("response"));
        entry["request"]["httpVersion"]=entry.at("response").at("httpVersion");Impl::store(page,key,std::move(entry));
      }
    } else if(method=="Network.dataReceived") {
      const auto key=Impl::current(page,text(params,"requestId"));
      if(page.har_entries.contains(key)&&params.contains("dataLength")) {
        auto entry=page.har_entries.at(key);entry["response"]["content"]["size"]=
          numeric(entry.at("response").at("content"),"size",0)+params.at("dataLength").as_double();Impl::store(page,key,std::move(entry));
      }
    } else if(method=="Network.loadingFinished"||method=="Network.loadingFailed") {
      const auto id=text(params,"requestId"),key=Impl::current(page,id);page.inflight.erase(id);page.activity=Clock::now();
      if(page.har_entries.contains(key)) {
        auto entry=page.har_entries.at(key);Impl::finish(entry,params);
        if(method=="Network.loadingFailed") {
          entry["_error"]=text(params,"errorText");entry["_bodyPending"]=false;
          entry["response"]["content"]["_bodyError"]=text(params,"errorText");
        }
        Impl::store(page,key,std::move(entry));
      }
    } else if(method=="Runtime.consoleAPICalled") page.console.add(params);
    else if(method=="Runtime.exceptionThrown")page.errors.add(params);
    else if(method=="Page.javascriptDialogOpening"||method=="Page.javascriptDialogClosed")page.dialogs.add(Json{{"method",method},{"params",params}});
  }
  if(action)impl_->enqueue(std::move(*action));
}
void CdpNetwork::setup(CdpRuntime& runtime,CdpPage& page,Deadline deadline) {
  Impl::Settings settings;
  {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->pages.contains(page.session)&&impl_->pages.size()>=4096)throw std::runtime_error("Network page capacity exceeded");
    auto& state=impl_->pages[page.session];state.id=page.id;state.context=page.context;
    settings=impl_->settings[page.context];
  }
  runtime.call(page,"Network.enable",Json{{"maxTotalBufferSize",8*1024*1024},{"maxResourceBufferSize",2*1024*1024}},deadline);
  if(!settings.extra_headers.as_object().empty())runtime.call(page,"Network.setExtraHTTPHeaders",Json{{"headers",settings.extra_headers}},deadline);
  if(settings.offline)runtime.call(page,"Network.emulateNetworkConditions",Json{{"offline",true},{"latency",0},{"downloadThroughput",-1},{"uploadThroughput",-1}},deadline);
  if(!settings.routes.empty()||!settings.credentials.is_null())runtime.call(page,"Fetch.enable",Json{{"patterns",Json::array{Json{{"urlPattern","*"},{"requestStage","Request"}}}},{"handleAuthRequests",!settings.credentials.is_null()}},deadline);
  for(const auto&[id,source]:settings.scripts) {
    const auto result=runtime.call(page,"Page.addScriptToEvaluateOnNewDocument",Json{{"source",source}},deadline);
    std::lock_guard lock(impl_->mutex);impl_->pages.at(page.session).script_ids[id]=result.at("identifier").as_string();
  }
}
void CdpNetwork::forget(const std::string& session){std::lock_guard lock(impl_->mutex);impl_->pages.erase(session);}
void CdpNetwork::forget_context(int context){std::lock_guard lock(impl_->mutex);impl_->settings.erase(context);}
bool CdpNetwork::idle(const std::string& session,std::chrono::milliseconds quiet)const {
  std::lock_guard lock(impl_->mutex);const auto page=impl_->pages.find(session);
  return page==impl_->pages.end()||(page->second.inflight.empty()&&Clock::now()-page->second.activity>=quiet);
}
Json CdpNetwork::events(const std::string& method,std::uint64_t after)const {
  std::lock_guard lock(impl_->mutex);Json::array out;
  for(const auto&[event,size]:impl_->browser.values){(void)size;if(text(event,"method")==method&&event.contains("_suitecutEventSequence")&&event.at("_suitecutEventSequence").as_int()>static_cast<std::int64_t>(after))out.push_back(event);}return out;
}
int CdpNetwork::execution_context(const std::string& session,const std::string& frame)const {
  std::lock_guard lock(impl_->mutex);const auto page=impl_->pages.find(session);
  if(page==impl_->pages.end())return 0;
  const auto context=page->second.execution_contexts.find(frame);
  return context==page->second.execution_contexts.end()?0:context->second;
}
Json CdpNetwork::command(CdpRuntime& runtime,CdpPage& page,const Json& fields,Deadline deadline) {
  cdp_check_active(deadline);
  const auto family=fields.at("command").as_string(),action=text(fields,"action");
  if(family=="network"&&(action=="harStart"||action=="harStop"||action=="exportHar")) {
    struct Body {std::string key,id;bool response=false,post=false;};
    std::vector<Body> pending;std::uint64_t generation;
    {
      std::lock_guard lock(impl_->mutex);const auto found=impl_->pages.find(page.session);
      if(found==impl_->pages.end())throw std::runtime_error("HAR page is closed or unknown");
      auto& state=found->second;
      if(action=="harStart") {
        for(const auto&[key,value]:fields.as_object()) {
          (void)value;
          if(key!="command"&&key!="action"&&key!="pageId"&&key!="contextId"&&key!="timeoutMs"&&key!="id"&&key!="version"&&key!="api"&&key!="content")
            throw std::invalid_argument("Unsupported HAR recording option: "+key);
        }
        const auto policy=text(fields,"content","embed");
        if(policy!="embed"&&policy!="omit")throw std::invalid_argument("HAR content must be embed or omit");
        state.har=true;state.har_embed=policy=="embed";state.har_entries.clear();state.har_order.clear();state.har_current.clear();
        state.har_sizes.clear();state.har_bytes=state.har_dropped=0;state.har_truncated=false;++state.har_generation;
        return Json::object{};
      }
      if(action=="harStop")state.har=false;
      generation=state.har_generation;
      for(const auto& key:state.har_order) {
        const auto& entry=state.har_entries.at(key);
        const bool response=flag(entry,"_bodyPending")&&flag(entry,"_finished")&&!flag(entry,"_redirect");
        if(response||flag(entry,"_postDataPending"))pending.push_back({key,entry.at("_requestId").as_string(),response,flag(entry,"_postDataPending")});
      }
    }
    // CDP retains a bounded response cache. Read finished resources on export,
    // without performing I/O on the connection observer or fetching the URL
    // again. Evicted/oversized/unavailable bodies retain explicit error markers.
    for(const auto& item:pending) {
      cdp_check_active(deadline);Json response,post;std::string response_error,post_error;
      if(item.response)try{response=runtime.call(page,"Network.getResponseBody",Json{{"requestId",item.id}},deadline);}
      catch(const std::exception& error){cdp_check_active(deadline);response_error=error.what();}
      if(item.post)try{post=runtime.call(page,"Network.getRequestPostData",Json{{"requestId",item.id}},deadline);}
      catch(const std::exception& error){cdp_check_active(deadline);post_error=error.what();}
      std::lock_guard lock(impl_->mutex);const auto found=impl_->pages.find(page.session);
      if(found==impl_->pages.end()||found->second.har_generation!=generation)throw std::runtime_error("HAR recording changed while collecting native bodies");
      auto& state=found->second;if(!state.har_entries.contains(item.key))continue;
      auto entry=state.har_entries.at(item.key);
      if(item.response) {
        entry["_bodyPending"]=false;auto& body=entry["response"]["content"];
        if(response_error.empty()) {
          const auto value=text(response,"body");const bool encoded=flag(response,"base64Encoded");
          const auto padding=encoded&&!value.empty()?static_cast<std::size_t>(value.back()=='=')+static_cast<std::size_t>(value.size()>1&&value[value.size()-2]=='='):0;
          const auto size=encoded?value.size()/4*3-padding:value.size();
          if(size>Impl::body_limit)response_error="Response body exceeds 2 MiB HAR resource limit";
          else {body["text"]=value;body["size"]=size;if(encoded)body["encoding"]="base64";}
        }
        if(!response_error.empty())body["_bodyError"]=response_error;
      }
      if(item.post) {
        entry["_postDataPending"]=false;const auto value=text(post,"postData");
        if(post_error.empty()&&value.size()>Impl::body_limit)post_error="POST data exceeds 2 MiB HAR resource limit";
        if(post_error.empty()){entry["request"]["postData"]=Json{{"mimeType",""},{"text",value}};entry["request"]["bodySize"]=value.size();}
        else entry["request"]["_postDataError"]=post_error;
      }
      Impl::store(state,item.key,std::move(entry));
    }
    std::lock_guard lock(impl_->mutex);auto& state=impl_->pages.at(page.session);
    if(action=="harStop") {
      const auto order=state.har_order;
      for(const auto& key:order)if(state.har_entries.contains(key)&&flag(state.har_entries.at(key),"_bodyPending")) {
        auto entry=state.har_entries.at(key);entry["_bodyPending"]=false;
        entry["response"]["content"]["_bodyError"]="Response was incomplete at the HAR stop boundary";Impl::store(state,key,std::move(entry));
      }
    }
    return impl_->har(state);
  }
  const int context=fields.contains("contextId")?static_cast<int>(fields.at("contextId").as_int()):page.context;
  std::vector<std::string> sessions;Json config;int route_id=0;
  {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->settings.contains(context)&&impl_->settings.size()>=4096)throw std::runtime_error("Network context capacity exceeded");
    auto& settings=impl_->settings[context];
    auto found=impl_->pages.find(page.session);
    if(family=="console"||family=="dialog"||(family=="network"&&(action=="requests"||action=="responses"))) {
      if(found==impl_->pages.end())throw std::runtime_error("Network page is closed or unknown");
      auto& state=found->second;const bool clear=flag(fields,"clear");
      if(family=="console") {
        if(action=="list") {
          if(text(fields,"filter","all")!="all")throw std::runtime_error("Console list currently supports filter=all");
          return state.console.take(clear);
        }
        if(action=="messages")return Json{{"messages",state.console.take(clear)},{"dropped",state.console.dropped}};
        if(action=="errors")return Json{{"errors",state.errors.take(clear)},{"dropped",state.errors.dropped}};
        if(action=="clear"){state.console.take(true);state.errors.take(true);return Json::object{};}
      }
      if(family=="dialog"&&action=="list")return Json{{"dialogs",state.dialogs.take(clear)}};
      if(family=="network") {
        if(action=="requests")return Json{{"requests",state.requests.take(clear)},{"dropped",state.requests.dropped}};
        if(action=="responses")return Json{{"responses",state.responses.take(clear)},{"dropped",state.responses.dropped}};
      }
    }
    if(family=="dialog"&&action=="handle")config=Json{{"accept",fields.at("accept")}};
    else if(family=="network"&&action=="body")config=Json{{"requestId",fields.at("requestId")}};
    else if(family=="initScript") {
      if(action=="list"){Json::array scripts;for(const auto&[id,source]:settings.scripts)scripts.emplace_back(Json{{"identifier",id},{"source",source}});return Json{{"scripts",std::move(scripts)}};}
      if(action=="add") {
        const auto source=fields.at("source").as_string();
        std::size_t total=0;for(const auto&[id,state]:impl_->settings){(void)id;total+=state.scripts.size();}
        if(source.size()>49152||settings.scripts.size()>=32||total>=128||impl_->next_script==INT_MAX)throw std::runtime_error("Init script storage limit exceeded");
        const auto id="script-"+std::to_string(++impl_->next_script);settings.scripts.emplace(id,source);config=Json{{"source",source},{"_identifier",id}};
      } else if(action=="remove") {
        const auto id=fields.at("identifier").as_string();settings.scripts.erase(id);config=Json{{"_identifier",id},{"remove",true}};
      }
      else throw std::invalid_argument("Unsupported initScript action: "+action);
    } else if(family=="network") {
      if(action=="route") {
        std::size_t total=0;for(const auto&[id,state]:impl_->settings){(void)id;total+=state.routes.size();}
        if(total>=256||impl_->next_route==INT_MAX)throw std::runtime_error("Route capacity exceeded");
        const auto pattern=fields.at("pattern").as_string(),behavior=fields.at("behavior").as_string();
        if(pattern.empty()||pattern.size()>2048||(behavior!="abort"&&behavior!="mock"&&behavior!="continue"))throw std::invalid_argument("Route requires bounded glob pattern and abort/mock/continue behavior");
        if(fields.contains("headers"))headers(fields.at("headers"));
        if(fields.contains("status")&&(fields.at("status").as_int()<100||fields.at("status").as_int()>599))throw std::invalid_argument("Mock status must be100..599");
        route_id=++impl_->next_route;settings.routes.push_back({route_id,fields});
      } else if(action=="unroute") {
        int route_id=0;
        if(fields.contains("routeId")) {
          if(fields.at("routeId").is_string()) {std::size_t end=0;route_id=std::stoi(fields.at("routeId").as_string(),&end);if(end!=fields.at("routeId").as_string().size())throw std::invalid_argument("Invalid routeId");}
          else route_id=static_cast<int>(fields.at("routeId").as_int());
        }
        std::erase_if(settings.routes,[&](const auto& route){return (!fields.contains("routeId")&&!fields.contains("pattern"))||(fields.contains("routeId")&&route_id==route.id)||(fields.contains("pattern")&&fields.at("pattern")==route.fields.at("pattern"));});
      } else if(action=="routes") {Json::array routes;for(const auto& route:settings.routes){auto out=route.fields;out["routeId"]=route.id;routes.push_back(std::move(out));}return Json{{"routes",std::move(routes)}};}
      else if(action=="headers")settings.extra_headers=headers(fields.at("headers"));
      else if(action=="offline")settings.offline=fields.at("offline").as_bool();
      else if(action=="credentials") {
        const auto& credentials=fields.at("credentials");
        if(!credentials.is_null()){const auto username=credentials.at("username").as_string(),password=credentials.at("password").as_string();if(username.size()>4096||password.size()>4096)throw std::invalid_argument("Credentials exceed limit");}
        settings.credentials=credentials;
      } else throw std::invalid_argument("Unsupported network action: "+action);
      config=Json{{"headers",settings.extra_headers},{"offline",settings.offline},{"fetch",!settings.routes.empty()||!settings.credentials.is_null()},{"auth",!settings.credentials.is_null()}};
    } else if(family!="dialog")throw std::invalid_argument("Unsupported observer command family: "+family);
    for(const auto&[session,state]:impl_->pages)if(state.context==context)sessions.push_back(session);
  }
  if(family=="dialog") {
    if(action!="handle")throw std::invalid_argument("Unsupported dialog action: "+action);
    if(fields.contains("promptText"))config["promptText"]=fields.at("promptText");
    return runtime.wire->call("Page.handleJavaScriptDialog",std::move(config),page.session,deadline);
  }
  if(family=="network"&&action=="body")return runtime.call(page,"Network.getResponseBody",std::move(config),deadline);
  if(family=="initScript") {
    const auto id=config.at("_identifier").as_string();
    for(const auto& session:sessions) {
      cdp_check_active(deadline);
      if(flag(config,"remove")) {
        std::string actual;
        {std::lock_guard lock(impl_->mutex);const auto page=impl_->pages.find(session);if(page!=impl_->pages.end()&&page->second.script_ids.contains(id))actual=page->second.script_ids.at(id);}
        if(!actual.empty())runtime.wire->call("Page.removeScriptToEvaluateOnNewDocument",Json{{"identifier",actual}},session,deadline);
        std::lock_guard lock(impl_->mutex);if(impl_->pages.contains(session))impl_->pages.at(session).script_ids.erase(id);
      } else {
        const auto result=runtime.wire->call("Page.addScriptToEvaluateOnNewDocument",Json{{"source",config.at("source")}},session,deadline);
        std::lock_guard lock(impl_->mutex);if(impl_->pages.contains(session))impl_->pages.at(session).script_ids[id]=result.at("identifier").as_string();
      }
    }
    return flag(config,"remove")?Json::object{}:Json{{"identifier",id}};
  }
  for(const auto& session:sessions) {
    cdp_check_active(deadline);
    if(action=="headers")runtime.wire->call("Network.setExtraHTTPHeaders",Json{{"headers",config.at("headers")}},session,deadline);
    else if(action=="offline")runtime.wire->call("Network.emulateNetworkConditions",Json{{"offline",config.at("offline")},{"latency",0},{"downloadThroughput",-1},{"uploadThroughput",-1}},session,deadline);
    else {
      if(config.at("fetch").as_bool())runtime.wire->call("Fetch.enable",Json{{"patterns",Json::array{Json{{"urlPattern","*"},{"requestStage","Request"}}}},{"handleAuthRequests",config.at("auth")}},session,deadline);
      else runtime.wire->call("Fetch.disable",Json::object{},session,deadline);
    }
  }
  return route_id?Json{{"routeId",route_id}}:Json::object{};
}
} // namespace suitecut::detail
