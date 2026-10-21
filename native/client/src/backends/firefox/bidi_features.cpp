// SPDX-License-Identifier: MIT
#include "bidi_features.hpp"
#include "bidi_privileged.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace suitecut::detail {
namespace {
using Clock=std::chrono::steady_clock;
constexpr std::size_t max_records=4096,max_record_bytes=8*1024*1024;
std::string text(const Json& value,const char* key,const std::string& fallback={}) {
  return value.contains(key)?value.at(key).as_string():fallback;
}
std::filesystem::path absolute_path(const std::string& value,const char* label) {
  if(value.empty()||value.size()>65536||value.find('\0')!=std::string::npos||
     !std::filesystem::path(value).is_absolute())
    throw std::invalid_argument(std::string(label)+" must be an absolute filesystem path");
  return std::filesystem::path(value).lexically_normal();
}
bool local_browser(const LaunchOptions& options) {
  const auto& endpoint=options.cdp_endpoint;
  if(endpoint.empty())return true;
  const auto begin=endpoint.find("://");
  if(begin==std::string::npos)return false;
  const auto host=endpoint.substr(begin+3,endpoint.find_first_of("/?#",begin+3)-(begin+3));
  return host=="localhost"||host.starts_with("localhost:")||host=="127.0.0.1"||
    host.starts_with("127.0.0.1:")||host=="[::1]"||host.starts_with("[::1]:");
}
bool beneath(const std::filesystem::path& file,const std::filesystem::path& directory) {
  auto item=file.begin();
  for(const auto& part:directory)if(item==file.end()||*item++!=part)return false;
  return item!=file.end();
}
void validate_fields(const Json& fields,std::set<std::string> allowed) {
  allowed.insert({"command","action","pageId","id","version","timeoutMs","api"});
  for(const auto&[key,value]:fields.as_object()) {
    (void)value;
    if(!allowed.contains(key))throw std::invalid_argument("Unsupported Firefox feature field: "+key);
  }
}
double finite(const Json& fields,const char* key,double fallback=0) {
  const auto value=fields.contains(key)?fields.at(key).as_double():fallback;
  if(!std::isfinite(value))throw std::invalid_argument(std::string("Invalid Firefox emulation ")+key);
  return value;
}
}

struct BidiFeatures::Impl {
  struct Record {
    int page=0;
    std::string directory,local_path;
    Json info;
    std::size_t bytes=0;
  };
  std::mutex mutex;
  BidiPrivileged privileged;
  bool subscribed=false,closed=false;
  std::size_t record_bytes=0;
  std::map<std::string,int> pages;
  std::map<std::string,std::string> parents;
  std::map<int,std::string> directories;
  std::map<std::string,Record> records;
  std::map<std::string,int> page_contexts;
  int owner(std::string context)const {
    for(int depth=0;depth<64;++depth) {
      if(const auto found=pages.find(context);found!=pages.end())return found->second;
      const auto parent=parents.find(context);
      if(parent==parents.end())return 0;
      context=parent->second;
    }
    throw std::runtime_error("Firefox download context nesting exceeds64");
  }
  void tree(const Json& node,const std::string& parent={}) {
    const auto name=node.at("context").as_string();
    if(!parent.empty()) {
      if(parents.size()>=4096&&!parents.contains(name))throw std::runtime_error("Firefox feature frame capacity exceeded");
      parents[name]=parent;
    }
    if(node.contains("children")&&node.at("children").is_array())
      for(const auto& child:node.at("children").as_array())tree(child,name);
  }
  void update_bytes(Record& record) {
    const auto size=record.info.dump({65536,16,32768,65536}).size()+record.local_path.size()+record.directory.size();
    if(size>max_record_bytes-(record_bytes-record.bytes))throw std::runtime_error("Firefox download record byte budget exceeded");
    record_bytes=record_bytes-record.bytes+size;record.bytes=size;
  }
  std::filesystem::path source(const Record& record,bool local)const {
    if(!local)throw std::runtime_error("Firefox remote download files cannot be read from the SDK host");
    if(record.directory.empty()||record.local_path.empty())throw std::runtime_error("Firefox download has no SDK-configured local file");
    const auto file=std::filesystem::canonical(absolute_path(record.local_path,"Firefox download source"));
    // This is the canonical directory captured when the policy was configured.
    // Resolving it again could bless a directory replaced with an outside symlink.
    const auto directory=std::filesystem::path(record.directory);
    if(!beneath(file,directory)||!std::filesystem::is_regular_file(file))
      throw std::runtime_error("Firefox download source is outside its configured directory or is not a regular file");
    return file;
  }
  Json public_record(const Record& record,bool local)const {
    Json result=record.info;
    result["path"]=Json{};
    if(text(result,"state")=="completed"&&local&&!record.directory.empty()&&!record.local_path.empty()) {
      try{result["path"]=source(record,true).string();}
      catch(const std::filesystem::filesystem_error&){/* File may have been removed by its owner. */}
    }
    return result;
  }
};
BidiFeatures::BidiFeatures():impl_(std::make_unique<Impl>()){}
BidiFeatures::~BidiFeatures()=default;

void BidiFeatures::observe(const Json& event) {
  if(!event.contains("method")||!event.contains("params"))return;
  const auto method=event.at("method").as_string();
  if(method!="browsingContext.contextCreated"&&method!="browsingContext.contextDestroyed"&&
     method!="browsingContext.downloadWillBegin"&&method!="browsingContext.downloadEnd")return;
  const auto& params=event.at("params");
  if(!params.contains("context")||!params.at("context").is_string())return;
  const auto context=params.at("context").as_string();
  std::lock_guard lock(impl_->mutex);if(impl_->closed)return;
  if(method=="browsingContext.contextCreated") {
    if(params.contains("parent")&&params.at("parent").is_string()) {
      if(impl_->parents.size()>=4096&&!impl_->parents.contains(context))throw std::runtime_error("Firefox feature frame capacity exceeded");
      impl_->parents[context]=params.at("parent").as_string();
    }
    return;
  }
  if(method=="browsingContext.contextDestroyed"){impl_->parents.erase(context);return;}
  const auto id=params.at("download").as_string();
  if(id.empty()||id.size()>1024)throw std::runtime_error("Invalid Firefox native download id");
  auto found=impl_->records.find(id);
  // A frame may disappear while its native download keeps running. A known
  // download retains its original owning page independently of that frame.
  const auto owner=impl_->owner(context);
  const auto page=found!=impl_->records.end()?found->second.page:owner;
  if(!page)return;
  if(owner&&owner!=page)throw std::runtime_error("Firefox download changed owning context");
  if(found==impl_->records.end()) {
    if(impl_->records.size()>=max_records)throw std::runtime_error("Firefox download record capacity exceeded");
    Impl::Record record;record.page=page;
    auto root=context;while(!impl_->pages.contains(root)&&impl_->parents.contains(root))root=impl_->parents.at(root);
    if(const auto owner=impl_->page_contexts.find(root);owner!=impl_->page_contexts.end())
      if(const auto directory=impl_->directories.find(owner->second);directory!=impl_->directories.end())record.directory=directory->second;
    record.info=Json{{"downloadId",id},{"guid",id},{"url",text(params,"url")},
      {"context",context},{"suggestedFilename",text(params,"suggestedFilename")},{"state","inProgress"},{"path",Json{}}};
    found=impl_->records.emplace(id,std::move(record)).first;
  }
  auto& record=found->second;if(record.page!=page)throw std::runtime_error("Firefox download id changed owning page");
  if(params.contains("suggestedFilename"))record.info["suggestedFilename"]=params.at("suggestedFilename");
  if(params.contains("timestamp"))record.info[method=="browsingContext.downloadWillBegin"?"timestamp":"endTimestamp"]=params.at("timestamp");
  if(method=="browsingContext.downloadEnd") {
    const auto status=params.at("status").as_string();
    if(status!="complete"&&status!="canceled")throw std::runtime_error("Unknown Firefox download end status");
    record.info["state"]=status=="complete"?"completed":"canceled";
    if(status=="complete"&&params.contains("filepath")&&params.at("filepath").is_string())record.local_path=params.at("filepath").as_string();
  }
  impl_->update_bytes(record);
}

void BidiFeatures::setup(BidiRuntime& runtime,BidiPage& page,Deadline deadline) {
  bool subscribe=false;
  {std::lock_guard lock(impl_->mutex);if(impl_->closed)throw std::runtime_error("Firefox features are closed");
    if(impl_->pages.size()>=4096&&!impl_->pages.contains(page.browsing_context))throw std::runtime_error("Firefox feature page capacity exceeded");
    impl_->pages[page.browsing_context]=page.id;impl_->page_contexts[page.browsing_context]=page.context;subscribe=!impl_->subscribed;}
  if(subscribe) {
    runtime.call("session.subscribe",Json{{"events",Json::array{"browsingContext.downloadWillBegin","browsingContext.downloadEnd"}}},deadline);
    std::lock_guard lock(impl_->mutex);impl_->subscribed=true;
  }
  const auto tree=runtime.call("browsingContext.getTree",Json{{"root",page.browsing_context}},deadline);
  std::lock_guard lock(impl_->mutex);for(const auto& node:tree.at("contexts").as_array())impl_->tree(node);
}

void BidiFeatures::forget(const std::string& context) {
  impl_->privileged.forget(context);
  std::lock_guard lock(impl_->mutex);
  const auto found=impl_->pages.find(context);if(found==impl_->pages.end())return;
  const auto page=found->second;
  std::set<std::string> erased{context};
  for(bool changed=true;changed;){changed=false;for(const auto&[child,parent]:impl_->parents)if(erased.contains(parent)&&!erased.contains(child)){erased.insert(child);changed=true;}}
  for(const auto& name:erased)impl_->parents.erase(name);
  for(auto record=impl_->records.begin();record!=impl_->records.end();) {
    if(record->second.page==page){impl_->record_bytes-=record->second.bytes;record=impl_->records.erase(record);}else ++record;
  }
  impl_->pages.erase(found);impl_->page_contexts.erase(context);
}

void BidiFeatures::forget_context(int context_id) {
  std::lock_guard lock(impl_->mutex);impl_->directories.erase(context_id);
}

Json BidiFeatures::command(BidiRuntime& runtime,BidiPage& page,const Json& fields,Deadline deadline) {
  bidi_check_active(deadline);
  const auto family=fields.at("command").as_string(),action=text(fields,"action",family=="emulation"?"media":"");
  if(family=="emulation") {
    Json params{{"contexts",Json::array{page.browsing_context}}};std::string method;
    if(action=="geolocation"||action=="clearGeolocation") {
      validate_fields(fields,action=="geolocation"?std::set<std::string>{"latitude","longitude","accuracy"}:std::set<std::string>{});
      method="emulation.setGeolocationOverride";params["coordinates"]=Json{};
      if(action=="geolocation") {
        if(!fields.contains("latitude")||!fields.contains("longitude"))throw std::invalid_argument("Geolocation requires latitude and longitude");
        const auto latitude=finite(fields,"latitude"),longitude=finite(fields,"longitude"),accuracy=finite(fields,"accuracy",1);
        if(latitude < -90||latitude>90||longitude < -180||longitude>180||accuracy<0)throw std::invalid_argument("Invalid Firefox geolocation coordinates");
        params["coordinates"]=Json{{"latitude",latitude},{"longitude",longitude},{"accuracy",accuracy}};
      }
    }else if(action=="timezone") {
      validate_fields(fields,{"timezoneId"});method="emulation.setTimezoneOverride";
      const auto timezone=fields.at("timezoneId").as_string();if(timezone.size()>1024)throw std::invalid_argument("Firefox timezone exceeds1024 bytes");
      params["timezone"]=timezone.empty()?Json{}:Json(timezone);
    }else if(action=="userAgent") {
      validate_fields(fields,{"userAgent","acceptLanguage","platform"});
      if(fields.contains("acceptLanguage")||fields.contains("platform"))throw std::runtime_error("Firefox native BiDi user-agent override does not support acceptLanguage or platform; use locale for language emulation");
      const auto user_agent=fields.at("userAgent").as_string();if(user_agent.size()>8192||user_agent.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid Firefox user agent");
      method="emulation.setUserAgentOverride";params["userAgent"]=user_agent.empty()?Json{}:Json(user_agent);
    }else if(action=="locale") {
      validate_fields(fields,{"locale"});const auto locale=fields.at("locale").as_string();if(locale.size()>1024)throw std::invalid_argument("Firefox locale exceeds1024 bytes");method="emulation.setLocaleOverride";params["locale"]=locale.empty()?Json{}:Json(locale);
    }else if(action=="media") {
      validate_fields(fields,{"media","colorScheme","reducedMotion","forcedColors","contrast"});
      return impl_->privileged.command(runtime,page,fields,deadline);
    }else throw std::invalid_argument("Unsupported Firefox emulation action: "+action);
    return runtime.call(method,std::move(params),deadline);
  }
  if(family!="download")throw std::invalid_argument("Unsupported Firefox feature family: "+family);
  const bool local=local_browser(runtime.options);
  if(action=="setDirectory"||action=="configure") {
    validate_fields(fields,{"path"});const auto path=absolute_path(fields.at("path").as_string(),"Download directory");
    std::string directory=path.string();
    {std::lock_guard lock(impl_->mutex);if(impl_->directories.size()>=4096&&!impl_->directories.contains(page.context))throw std::runtime_error("Firefox download directory context capacity exceeded");}
    if(local){std::filesystem::create_directories(path);directory=std::filesystem::canonical(path).string();}
    const auto user=runtime.contexts.at(page.context);
    runtime.call("browser.setDownloadBehavior",Json{{"downloadBehavior",Json{{"type","allowed"},{"destinationFolder",directory}}},{"userContexts",Json::array{user}}},deadline);
    std::lock_guard lock(impl_->mutex);impl_->directories[page.context]=directory;return Json{{"path",directory}};
  }
  validate_fields(fields,{"downloadId","path"});
  if(action=="list"||(action=="status"&&!fields.contains("downloadId"))) {
    std::vector<Impl::Record> records;
    {std::lock_guard lock(impl_->mutex);for(const auto&[id,record]:impl_->records){(void)id;if(record.page==page.id)records.push_back(record);}}
    Json::array result;for(const auto& record:records)result.push_back(impl_->public_record(record,local));return result;
  }
  const auto id=fields.at("downloadId").as_string();
  if(action!="status"&&action!="path"&&action!="saveAs"&&action!="cancel")throw std::invalid_argument("Unsupported Firefox download action: "+action);
  for(;;) {
    bidi_check_active(deadline);Impl::Record record;
    {std::lock_guard lock(impl_->mutex);const auto found=impl_->records.find(id);if(found==impl_->records.end()||found->second.page!=page.id)throw std::invalid_argument("Unknown download for this Firefox page: "+id);record=found->second;}
    if(action=="status")return impl_->public_record(record,local);
    if(action=="cancel") {
      Json request=fields;request["_suitecutDownload"]=record.info;
      if(!record.local_path.empty())request["_suitecutNativePath"]=record.local_path;
      return impl_->privileged.command(runtime,page,request,deadline);
    }
    const auto state=text(record.info,"state");if(state=="canceled")throw std::runtime_error("Firefox download was canceled");
    if(state=="completed") {
      const auto source=impl_->source(record,local);
      if(action=="path")return Json{{"path",source.string()}};
      const auto destination=absolute_path(fields.at("path").as_string(),"Download destination");
      if(std::filesystem::exists(destination)&&std::filesystem::equivalent(source,destination))return Json{{"path",destination.string()}};
      std::ifstream input(source,std::ios::binary);std::ofstream output(destination,std::ios::binary|std::ios::trunc);
      if(!input||!output)throw std::runtime_error("Cannot open Firefox download source or destination");
      char bytes[65536];while(input){bidi_check_active(deadline);input.read(bytes,sizeof bytes);output.write(bytes,input.gcount());if(!output)throw std::runtime_error("Cannot write Firefox download destination");}
      if(!input.eof())throw std::runtime_error("Cannot read Firefox download source");
      output.close();
      if(!output)throw std::runtime_error("Cannot finish Firefox download destination");
      return Json{{"path",destination.string()}};
    }
    std::this_thread::sleep_until(std::min(deadline,Clock::now()+std::chrono::milliseconds{20}));
  }
}

void BidiFeatures::close(BidiRuntime& runtime,Deadline deadline) {
  // Privileged page-native overrides need explicit restoration while their
  // browsing contexts and the BiDi session are still alive.
  impl_->privileged.close(runtime,deadline);
  std::lock_guard lock(impl_->mutex);impl_->closed=true;impl_->records.clear();impl_->record_bytes=0;
  impl_->pages.clear();impl_->parents.clear();impl_->page_contexts.clear();impl_->directories.clear();
}
}
