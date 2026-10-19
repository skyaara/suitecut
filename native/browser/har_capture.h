// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <openssl/evp.h>
#include "include/cef_parser.h"
#include "network_glob.h"
#include "network_post_data.h"
#include "har_pages.h"
#include "har_cookie_expiry.h"
#include "har_response_bytes.h"
namespace suitecut {
// Genuine metadata/body assembly shared by HAR recording and browser tracing.
// Physical owner identities are opaque and may name a page-less worker broker.
class NativeHARCapture {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;using Bytes=std::vector<uint8_t>;
  using BodyReply=std::function<void(bool,Bytes,const std::string&)>;
  using BodyFetch=std::function<void(const std::string&,const std::string&,double,BodyReply)>;
  using Snapshot=std::function<void(int,Dict)>;using Resource=std::function<void(int,const std::string&,const Bytes&)>;
  // Reservation occurs when the native request is observed. Trace adapters
  // capture an immutable SessionLease here; delayed bodies never retarget it.
  struct Publication {std::function<void(Dict)> snapshot;std::function<void(const std::string&,const Bytes&)> resource;std::function<bool(Dict,Dict)> capture_body={};};
  // Bound to the record admitted by the real first request observation. These
  // callbacks never resolve a replacement trace session or context later.
  struct PostPublication {std::function<void(bool,const Bytes&,bool)> data;std::function<void()> receipt;};
  enum class OwnerKind {Page,WorkerBroker,APIRequestContext};
  using Reserve=std::function<Publication(int)>;
  void SetReservation(Reserve reserve){reserve_=std::move(reserve);}
  NativeHARCapture(Snapshot snapshot,Resource resource,std::function<double()> now,std::function<void(const std::string&)>failure,std::function<double()> wall_now={})
      :snapshot_(std::move(snapshot)),resource_(std::move(resource)),now_(std::move(now)),wall_now_(std::move(wall_now)),failure_(std::move(failure)){
    if(!wall_now_)wall_now_=[] {return std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();};
  }
  ~NativeHARCapture(){Clear();}
  bool Start(int context,const std::string&filter="**"){
    if(disposed_||context<1||filter.empty()||filter.size()>8192||captures_.contains(context)||captures_.size()>=16)return false;
    if(!pages_.Start(context))return false;
    captures_.emplace(context,Capture{filter});return true;
  }
  bool Active(int context)const{return !disposed_&&captures_.contains(context);}
  bool Attach(int context,const std::string&owner,BodyFetch fetch,OwnerKind kind=OwnerKind::Page){
    if(disposed_||context<1||owner.empty()||owner.size()>128||!fetch||owners_.contains(owner)||owners_.size()>=192)return false;
    owners_.emplace(owner,Owner{context,std::move(fetch),kind});return true;
  }
  bool PageCreated(int context,const std::string&owner,const std::string&id,double wall){const auto actor=owners_.find(owner);return actor!=owners_.end()&&actor->second.context==context&&actor->second.kind==OwnerKind::Page&&pages_.Created(context,owner,id,wall);}
  bool PageTitle(int context,const std::string&owner,const std::string&title){return pages_.Title(context,owner,title);}
  bool PageLifecycle(int context,const std::string&owner,const std::string&phase,double timestamp,double wall,const std::string&loader){return pages_.Lifecycle(context,owner,phase,timestamp,wall,loader);}
  // Real root Network.enable ACK only; a new enable/disable or renderer loss
  // invalidates existing sums without changing any network callbacks/cursors.
  void PrimaryObservation(int context,const std::string&owner,bool ready){
    const auto actor=owners_.find(owner);if(actor==owners_.end()||actor->second.context!=context)return;
    actor->second.primary_ready=ready;
    if(!ready){for(auto&[key,record]:records_)if(record->owner==owner)record->wire.CoverageLost();for(auto&[key,original]:originals_)if(original.context==context&&key.starts_with(owner+"\n"))original.wire.Invalidate();}
  }
  // Dedicated raw observation, not NetworkControls::Event: no session guessing,
  // frame migration, event publication, waiters or cursors. First slice admits
  // page-owned primary-channel records only; workers/private handoffs stay unknown.
  void ResponseBytes(int context,const std::string&owner,const std::string&method,Dict data,const std::string&session){
    if(disposed_||!session.empty()||!data)return;
    const auto actor=owners_.find(owner);if(actor==owners_.end()||actor->second.context!=context||actor->second.kind!=OwnerKind::Page)return;
    const auto id=data->GetString("requestId").ToString();if(id.empty()||id.size()>256)return;
    std::shared_ptr<Record> selected;
    for(auto&[key,record]:records_)if(record->owner==owner&&record->context==context&&record->wire_id==id){if(selected){selected->wire.Invalidate();record->wire.Invalidate();return;}selected=record;}
    if(!selected)return;
    auto&record=*selected;if(!actor->second.primary_ready){record.wire.Invalidate();return;}
    if(method=="Network.requestServedFromCache"){record.wire.Invalidate();return;}
    if(method=="Network.responseReceived"){
      const auto response=data->GetDictionary("response");bool uncached=response!=nullptr;
      for(const char*flag:{"fromDiskCache","fromServiceWorker","fromPrefetchCache"})if(!response||response->GetType(flag)!=VTYPE_BOOL||response->GetBool(flag))uncached=false;
      if(data->GetString("frameId")!=record.wire_frame||data->GetString("loaderId")!=record.wire_loader||!response||response->GetString("url")!=record.request->GetString("url"))uncached=false;
      record.wire.Response(uncached);
    }else if(method=="Network.dataReceived"){
      double decoded=0,encoded=0;if(!RawNumber(data,"dataLength",decoded)||!RawNumber(data,"encodedDataLength",encoded))record.wire.Invalidate();else record.wire.Data(decoded,encoded);
    }else if(method=="Network.loadingFinished"||method=="Network.loadingFailed")record.wire.Terminal(method=="Network.loadingFinished");
  }
  // Called before the existing request-body publication mutates raw metadata.
  void Seal(int context){pages_.Seal(context);const auto found=captures_.find(context);if(found!=captures_.end())found->second.sealed=true;}
  bool OriginalRequest(int context,const std::string&owner,const std::string&session,Dict params){
    if(disposed_||!captures_.contains(context)||captures_.at(context).sealed)return true;
    auto request=params?params->GetDictionary("request"):nullptr;const auto actor=owners_.find(owner);
    if(!request||actor==owners_.end()||actor->second.context!=context)return false;
    if(!NativeURLGlobMatches(captures_.at(context).filter,request->GetString("url")))return true;
    const auto key=owner+"\n"+session+"\n"+params->GetString("requestId").ToString();
    for(auto&[stored,record]:records_)if(record->owner==owner&&record->wire_id==params->GetString("requestId").ToString())record->wire.Invalidate();
    if(originals_.contains(key)){originals_.at(key).wire.Invalidate();return true;}
    if(originals_.size()>=512)return false;
    auto data=CaptureNativePostData(request);Original original;original.context=context;original.observed_ms=now_();original.wall_ms=Number(params,"wallTime")*1000;original.deadline=now_()+30000;
    original.wire.Begin(actor->second.kind==OwnerKind::Page&&actor->second.primary_ready&&session.empty()&&!params->HasKey("redirectResponse")&&params->GetType("frameId")==VTYPE_STRING&&!params->GetString("frameId").empty()&&params->GetString("frameId").length()<=256&&params->GetType("loaderId")==VTYPE_STRING&&params->GetString("loaderId").length()<=256);
    if(params->GetString("frameId").length()<=256)original.wire_frame=params->GetString("frameId").ToString();
    if(params->GetString("loaderId").length()<=256)original.wire_loader=params->GetString("loaderId").ToString();
    original.unavailable=data.kind==NativePostData::Kind::Unavailable;original.absent=data.kind==NativePostData::Kind::Absent;
    if(data.kind==NativePostData::Kind::Text){auto rendered=request->GetString("postData").ToString();original.bytes.assign(rendered.begin(),rendered.end());original.present=true;original.text=true;}
    else if(data.kind==NativePostData::Kind::Binary){original.bytes=std::move(data.bytes);original.present=true;}
    if(original.bytes.size()>16777216-original_bytes_-post_bytes_){original.bytes.clear();original.unavailable=true;original.present=false;}
    // Charge additional retained channel-pin string capacities separately from
    // real POST bytes; fixed Original/core objects retain the existing 512 cap.
    original.wire_cost=original.wire_frame.capacity()+original.wire_loader.capacity();
    if(original.wire_cost>1048576-original_wire_bytes_){original.wire.Invalidate();std::string().swap(original.wire_frame);std::string().swap(original.wire_loader);original.wire_cost=original.wire_frame.capacity()+original.wire_loader.capacity();}
    if(original.wire_cost>1048576-original_wire_bytes_)return false;
    original_wire_bytes_+=original.wire_cost;original_bytes_+=original.bytes.size();originals_.emplace(key,std::move(original));return true;
  }
  // wall_ms is the actual observed request wall clock, not the monotonic CDP clock.
  bool Observe(int context,const std::string&owner,Dict event,double wall_ms){
    if(disposed_||!captures_.contains(context))return true;
    const auto actor=owners_.find(owner);auto request=event?event->GetDictionary("request"):nullptr;
    if(actor==owners_.end()||actor->second.context!=context||!request||!std::isfinite(wall_ms)||wall_ms<0)return false;
    const auto id=request->GetString("id").ToString();if(id.empty()||id.size()>256)return false;
    const auto kind=event->GetString("event").ToString();const auto key=owner+"\n"+id;
    auto found=records_.find(key);
    if(found==records_.end()){
      if(captures_.at(context).sealed)return true;
      if(kind!="request"||!NativeURLGlobMatches(captures_.at(context).filter,request->GetString("url")))return true;
      if(records_.size()>=2048){Fail("Native HAR entry capacity exceeded");return false;}
      auto record=std::make_shared<Record>();record->context=context;record->observed_ms=now_();record->owner=owner;record->pageref=pages_.Reference(context,owner);record->frame_allowed=actor->second.kind==OwnerKind::Page;record->request=request->Copy(false);record->wall_ms=wall_ms;record->generation=++serial_;if(reserve_)record->publication=reserve_(context);
      auto original=originals_.find(owner+"\n"+request->GetString("session").ToString()+"\n"+request->GetString("requestId").ToString());
      if(original!=originals_.end()){
        record->observed_ms=original->second.observed_ms;if(original->second.wall_ms>=0)record->wall_ms=original->second.wall_ms;
        record->post=std::move(original->second.bytes);record->post_available=original->second.present;record->post_text=original->second.text;record->post_unavailable=original->second.unavailable;record->post_absent=original->second.absent;
        record->wire=original->second.wire;record->wire_frame=std::move(original->second.wire_frame);record->wire_loader=std::move(original->second.wire_loader);record->wire_id=request->GetString("requestId").ToString();
        original_wire_bytes_-=original->second.wire_cost;original_bytes_-=record->post.size();post_bytes_+=record->post.size();originals_.erase(original);
      }
      records_.emplace(key,record);captures_.at(context).order.push_back(key);found=records_.find(key);
    }
    auto record=found->second;
    record->request=request->Copy(false);if(auto response=event->GetDictionary("response"))record->response=response->Copy(false);
    if(request->GetBool("serviceWorker")||request->GetBool("contextWorker")||request->GetDictionary("workerInfo")||request->GetString("session")!=""||request->GetString("requestId")!=record->wire_id||request->GetString("frameId")!=record->wire_frame)record->wire.Invalidate();
    if(HeadersObservedReady(record->response)){
      const auto coding=ContentCoding(record->response);
      if(!coding){record->wire_coding_conflict=true;record->wire.Invalidate();}else if(record->wire_coding&&*record->wire_coding!=*coding){record->wire_coding_conflict=true;record->wire.Invalidate();}else record->wire_coding=coding;
    }
    if(!record->header_receipt_taken&&HeadersObservedReady(record->response)){
      // Pin once at the actual matched authoritative header observation, never
      // at request start, provisional headers, duplicate updates or export.
      // The native clock is nonthrowing; nonfinite/unavailable time consumes
      // this receipt too, so a later callback cannot renew it.
      record->header_receipt_taken=true;
      const auto wall=wall_now_();if(NativeHARCookieExpiry::ValidTime(wall))record->header_wall_ms=wall;
    }
    if(!Budget(*record)){Fail("Native HAR metadata byte budget exceeded");return false;}
    // A body reply can precede the real redirect-hop ExtraInfo. Keep the
    // immutable snapshot open until those existing header receipts settle.
    if((record->terminal||record->response)&&HeadersPending(*record)&&!record->header_deadline)record->header_deadline=now_()+30000;
    if(record->awaiting_headers&&!HeadersPending(*record)){record->awaiting_headers=false;--header_pending_;}
    if(record->terminal){if(!record->fetching)Publish(record);return true;}
    if(kind=="requestfailed"){
      record->terminal=true;record->completed_ms=now_();record->error=event->GetString("errorText").ToString();Publish(record);return true;
    }
    if(kind!="requestfinished")return true;
    record->terminal=true;record->completed_ms=now_();
    if(!record->response){record->error="Actual native request completed without response metadata";Publish(record);return true;}
    if(record->publication.capture_body&&!record->publication.capture_body(record->request->Copy(false),record->response->Copy(false))){record->body_skipped=true;Publish(record);return true;}
    if(physical_->count>=64){record->body_error="Native HAR physical body reader capacity exceeded";Publish(record);return true;}
    record->fetching=true;record->deadline=now_()+30000;++pending_;++physical_->count;++physical_->contexts[context];
    const auto generation=record->generation;auto fetch=actor->second.fetch;
    fetch(record->response->GetString("session"),record->response->GetString("requestId"),record->deadline,
      [this,alive=alive_,physical=physical_,settled=std::make_shared<bool>(false),context,key,generation](bool ok,Bytes bytes,const std::string&error){
        if(*settled)return;
        *settled=true;--physical->count;if(!--physical->contexts.at(context))physical->contexts.erase(context);
        if(!*alive)return;
        const auto found=records_.find(key);if(found==records_.end()||found->second->generation!=generation||!found->second->fetching)return;
        auto record=found->second;record->fetching=false;--pending_;
        if(!ok){record->body_error=error;Publish(record);return;}
        if(bytes.size()>2097152||bytes.size()>16777216-body_bytes_){record->body_error="Actual response body exceeds native HAR retained-byte limit";Publish(record);return;}
        record->body=std::move(bytes);record->body_available=true;body_bytes_+=record->body.size();Publish(record);
      });return true;
  }
  PostPublication ReservePost(int context,const std::string&owner,const std::string&id){
    const auto key=owner+"\n"+id;const auto found=records_.find(key);
    if(disposed_||found==records_.end()||found->second->context!=context||found->second->published||found->second->post_available||found->second->post_pending||physical_->count>=64)return {};
    auto record=found->second;record->post_pending=true;++post_pending_;++physical_->count;++physical_->contexts[context];
    const auto generation=record->generation;const std::weak_ptr<Record> weak=record;const auto stored=std::make_shared<bool>(false),settled=std::make_shared<bool>(false);
    PostPublication publication;
    publication.data=[this,alive=alive_,weak,key,generation,stored,settled](bool available,const Bytes&bytes,bool text){
      if(!*alive||*stored||*settled)return;
      *stored=true;const auto record=weak.lock();const auto found=records_.find(key);
      if(!record||found==records_.end()||found->second!=record||record->generation!=generation)return;
      post_bytes_-=record->post.size();Bytes().swap(record->post);record->post_available=false;record->post_absent=false;record->post_unavailable=true;
      if(!available||bytes.size()>2097152||bytes.size()>16777216-post_bytes_-original_bytes_)return;
      record->post=bytes;record->post_available=true;record->post_unavailable=false;record->post_text=text;post_bytes_+=record->post.size();
    };
    publication.receipt=[this,alive=alive_,physical=physical_,weak,key,generation,context,settled]{
      if(*settled)return;
      *settled=true;--physical->count;if(!--physical->contexts.at(context))physical->contexts.erase(context);
      if(!*alive)return;
      const auto record=weak.lock();const auto found=records_.find(key);
      if(!record||found==records_.end()||found->second!=record||record->generation!=generation||!record->post_pending)return;
      record->post_pending=false;--post_pending_;if(record->terminal&&!record->fetching)Publish(record);
    };
    return publication;
  }
  void Detach(const std::string&owner){
    pages_.Detach(owner);owners_.erase(owner);for(auto it=originals_.begin();it!=originals_.end();)if(it->first.starts_with(owner+"\n")){original_wire_bytes_-=it->second.wire_cost;original_bytes_-=it->second.bytes.size();it=originals_.erase(it);}else++it;std::vector<std::shared_ptr<Record>> affected;for(const auto&[key,record]:records_)if(record->owner==owner&&!record->published)affected.push_back(record);
    for(const auto&record:affected){if(record->fetching){record->fetching=false;--pending_;record->body_error="Actual native response owner closed before body capture";}else if(!record->terminal)record->error="Actual native request owner closed before completion";if(!record->terminal)record->completed_ms=now_();record->terminal=true;if(record->awaiting_headers||HeadersPending(*record)){record->header_error="Actual native header owner closed before receipt";record->header_deadline=now_();}Publish(record);}
  }
  void Tick(double now){
    for(auto it=originals_.begin();it!=originals_.end();)if(now>=it->second.deadline){original_wire_bytes_-=it->second.wire_cost;original_bytes_-=it->second.bytes.size();it=originals_.erase(it);}else++it;
    std::vector<std::shared_ptr<Record>>expired;
    for(const auto&[key,record]:records_)if((record->fetching&&now>=record->deadline)||(record->awaiting_headers&&now>=record->header_deadline))expired.push_back(record);
    for(const auto&record:expired){
      if(record->fetching&&now>=record->deadline){record->fetching=false;--pending_;record->body_error="Native HAR original body deadline exceeded";}
      if(!record->fetching)Publish(record);
    }
  }
  std::set<uint64_t> PendingBodies(const std::string&owner)const{std::set<uint64_t>scope;for(const auto&[key,record]:records_)if(record->owner==owner&&(record->fetching||record->post_pending||record->awaiting_headers))scope.insert(record->generation);return scope;}
  bool PendingBodies(const std::set<uint64_t>&scope)const{for(const auto&[key,record]:records_)if((record->fetching||record->post_pending||record->awaiting_headers)&&scope.contains(record->generation))return true;return false;}
  bool Ready(int context)const{for(const auto&[key,record]:records_)if(record->context==context&&(!record->terminal||record->fetching||record->post_pending||record->awaiting_headers))return false;return true;}
  Dict Export(int context)const{
    const auto capture=captures_.find(context);if(capture==captures_.end()||!Ready(context))return nullptr;
    auto result=CefDictionaryValue::Create();auto log=CefDictionaryValue::Create();log->SetString("version","1.2");auto creator=CefDictionaryValue::Create();creator->SetString("name","SuiteCut native");creator->SetString("version","2.0.0");log->SetDictionary("creator",creator);auto entries=CefListValue::Create();size_t index=0;for(const auto&key:capture->second.order){const auto record=records_.find(key);if(record!=records_.end())entries->SetDictionary(index++,Entry(*record->second));}log->SetList("entries",entries);log->SetList("pages",pages_.Export(context,Date));result->SetDictionary("log",log);return result;
  }
  void Stop(int context){pages_.Stop(context);captures_.erase(context);for(auto it=originals_.begin();it!=originals_.end();)if(it->second.context==context){original_wire_bytes_-=it->second.wire_cost;original_bytes_-=it->second.bytes.size();it=originals_.erase(it);}else++it;for(auto it=records_.begin();it!=records_.end();)if(it->second->context==context){if(it->second->fetching)--pending_;if(it->second->post_pending)--post_pending_;if(it->second->awaiting_headers)--header_pending_;metadata_bytes_-=it->second->cost;body_bytes_-=it->second->body.size();post_bytes_-=it->second->post.size();it=records_.erase(it);}else++it;}
  void Clear(){if(disposed_)return;disposed_=true;*alive_=false;pages_.Clear();captures_.clear();owners_.clear();records_.clear();originals_.clear();pending_=post_pending_=header_pending_=metadata_bytes_=body_bytes_=original_bytes_=original_wire_bytes_=post_bytes_=0;}
  size_t page_metadata_bytes()const{return pages_.bytes();}
  bool NeedsTick()const{return pending_||post_pending_||header_pending_||!originals_.empty();}
  size_t pending()const{return pending_+post_pending_+header_pending_;}size_t physical()const{return physical_->count;}size_t physical(int context)const{const auto found=physical_->contexts.find(context);return found==physical_->contexts.end()?0:found->second;}size_t pending(int context)const{size_t count=0;for(const auto&[key,record]:records_)if(record->context==context){if(record->fetching)++count;if(record->post_pending)++count;if(record->awaiting_headers)++count;}return count;}size_t retained_bytes()const{return pages_.bytes()+metadata_bytes_+body_bytes_+original_bytes_+original_wire_bytes_+post_bytes_;}
 private:
  struct Physical{size_t count=0;std::map<int,size_t>contexts;};struct Owner{int context;BodyFetch fetch;OwnerKind kind;bool primary_ready=false;};struct Capture{std::string filter;std::vector<std::string>order;bool sealed=false;};
  struct Original{int context=0;Bytes bytes;NativeHARResponseBytes wire;std::string wire_frame,wire_loader;size_t wire_cost=0;double wall_ms=-1,deadline=0,observed_ms=0;bool present=false,unavailable=false,text=false,absent=false;};
  struct Record{int context=0;std::string owner,pageref,error,body_error,header_error;Dict request,response;Publication publication;Bytes body,post;bool post_pending=false,post_available=false,post_unavailable=false,post_text=false,post_published=false,post_absent=false;double wall_ms=0,deadline=0,header_deadline=0,observed_ms=0,completed_ms=0;uint64_t generation=0;size_t cost=0;bool terminal=false,fetching=false,awaiting_headers=false,published=false,body_available=false,body_skipped=false,frame_allowed=false,header_receipt_taken=false,wire_coding_conflict=false;std::optional<double>header_wall_ms;NativeHARResponseBytes wire;std::string wire_id,wire_frame,wire_loader;std::optional<std::string>wire_coding;};
  static std::string Json(Dict dict){auto value=CefValue::Create();value->SetDictionary(dict->Copy(false));return CefWriteJSON(value,JSON_WRITER_DEFAULT);}
  bool Budget(Record&record){const auto cost=Json(record.request).size()+(record.response?Json(record.response).size():0)+640+record.wire_id.capacity()+record.wire_frame.capacity()+record.wire_loader.capacity()+(record.wire_coding?record.wire_coding->capacity():0);if(cost>16777216-(metadata_bytes_-record.cost))return false;metadata_bytes_=metadata_bytes_-record.cost+cost;record.cost=cost;return true;}
  static std::string Date(double ms){const auto whole=static_cast<time_t>(ms/1000);std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc,&whole);
#else
    gmtime_r(&whole,&utc);
#endif
    std::ostringstream output;output<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%S")<<'.'<<std::setw(3)<<std::setfill('0')<<static_cast<unsigned>(std::fmod(ms,1000))<<'Z';return output.str();}
  static std::string Lower(std::string text){std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});return text;}
  static std::string Trim(std::string text){const auto first=text.find_first_not_of(" \t\r\n");if(first==std::string::npos)return "";const auto last=text.find_last_not_of(" \t\r\n");return text.substr(first,last-first+1);}
  static std::vector<std::pair<std::string,std::string>> HeaderPairs(Dict object){
    std::vector<std::pair<std::string,std::string>> pairs;auto headers=object?object->GetDictionary("headers"):nullptr;if(!headers)return pairs;
    CefDictionaryValue::KeyList names;headers->GetKeys(names);for(const auto&name:names){std::vector<std::string>values;if(auto list=headers->GetList(name)){for(size_t i=0;i<list->GetSize();++i)values.push_back(list->GetString(i));}else values.push_back(headers->GetString(name));
      for(const auto&value:values){size_t begin=0;while(true){const auto end=value.find('\n',begin);pairs.emplace_back(name.ToString(),value.substr(begin,end==std::string::npos?std::string::npos:end-begin));if(end==std::string::npos)break;begin=end+1;}}
    }return pairs;
  }
  static CefRefPtr<CefListValue> Headers(Dict headers){auto object=CefDictionaryValue::Create();if(headers)object->SetDictionary("headers",headers->Copy(false));auto result=CefListValue::Create();size_t index=0;for(const auto&[name,value]:HeaderPairs(object)){auto pair=CefDictionaryValue::Create();pair->SetString("name",name);pair->SetString("value",value);result->SetDictionary(index++,pair);}return result;}
  static std::string MIME(Dict response){
    if(!response){return "";}auto headers=response->GetDictionary("headers");if(headers){CefDictionaryValue::KeyList names;headers->GetKeys(names);for(const auto&name:names){auto key=name.ToString();std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});if(key=="content-type")return headers->GetString(name);}}
    return response->GetType("mimeType")==VTYPE_STRING?response->GetString("mimeType").ToString():std::string();
  }
  // HAR entity MIME default; this never inserts a received/wire header.
  static std::string PostMIME(Dict request){const auto type=MIME(request);return type.empty()?"application/octet-stream":type;}
  static std::string Hash(const Bytes&bytes){unsigned char output[EVP_MAX_MD_SIZE];unsigned count=0;if(!EVP_Digest(bytes.data(),bytes.size(),output,&count,EVP_sha1(),nullptr))return "";std::ostringstream text;text<<std::hex<<std::setfill('0');for(unsigned i=0;i<count;++i)text<<std::setw(2)<<unsigned(output[i]);return text.str();}
  static std::string Header(Dict object,const std::string&name){auto headers=object?object->GetDictionary("headers"):nullptr;if(!headers)return "";CefDictionaryValue::KeyList names;headers->GetKeys(names);for(const auto&key:names){auto lower=key.ToString();std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});if(lower==name){if(auto values=headers->GetList(key))return values->GetSize()?values->GetString(0).ToString():"";return headers->GetString(key);}}return "";}
  static std::string Decode(const std::string&value){std::string result;for(size_t i=0;i<value.size();++i){if(value[i]=='+'){result+=' ';continue;}if(value[i]=='%'&&i+2<value.size()){auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};int high=hex(value[i+1]),low=hex(value[i+2]);if(high>=0&&low>=0){result+=char(high*16+low);i+=2;continue;}}result+=value[i];}return result;}
  static CefRefPtr<CefListValue> Query(const std::string&url){auto result=CefListValue::Create();const auto question=url.find('?');if(question==std::string::npos)return result;const auto end=url.find('#',question);const auto query=url.substr(question+1,end==std::string::npos?std::string::npos:end-question-1);size_t begin=0,index=0;while(begin<query.size()){auto next=query.find('&',begin);auto item=query.substr(begin,next==std::string::npos?std::string::npos:next-begin);auto equal=item.find('=');auto pair=CefDictionaryValue::Create();pair->SetString("name",Decode(item.substr(0,equal)));pair->SetString("value",equal==std::string::npos?"":Decode(item.substr(equal+1)));result->SetDictionary(index++,pair);if(next==std::string::npos)break;begin=next+1;}return result;}
  static void PostParameters(Dict post,const std::string&mime,const std::string&text){
    const auto end=mime.find(';');const bool form=Lower(Trim(mime.substr(0,end)))=="application/x-www-form-urlencoded";
    auto params=CefListValue::Create();
    if(form){
      // Bound transient CEF pairs before allocating them. Excess metadata is
      // explicitly unavailable; the genuine body remains intact, never
      // relabeled as an empty form or silently truncated.
      constexpr size_t limit=256;size_t count=0,begin=0;
      while(begin<text.size()){const auto next=text.find('&',begin);if(next!=begin&&++count>limit){post->SetString("_paramsError","Native HAR form parameter capacity exceeded");return;}if(next==std::string::npos)break;begin=next+1;}
      begin=0;while(begin<text.size()){
        const auto next=text.find('&',begin);const auto item=text.substr(begin,next==std::string::npos?std::string::npos:next-begin);
        if(!item.empty()){const auto equal=item.find('=');auto pair=CefDictionaryValue::Create();pair->SetString("name",Decode(item.substr(0,equal)));pair->SetString("value",equal==std::string::npos?"":Decode(item.substr(equal+1)));params->SetDictionary(params->GetSize(),pair);}
        if(next==std::string::npos)break;
        begin=next+1;
      }
    }
    post->SetList("params",params);
  }
  static bool HeadersPending(const Record&record){
    const auto pending=[](Dict value){return value&&value->GetType("headersReady")==VTYPE_BOOL&&!value->GetBool("headersReady")&&value->GetType("_nativeHeadersError")!=VTYPE_STRING;};
    return pending(record.request)||pending(record.response);
  }
  static bool HeadersObservedReady(Dict response){return response&&response->GetType("_nativeHeadersObserved")==VTYPE_BOOL&&response->GetBool("_nativeHeadersObserved")&&response->GetType("headersReady")==VTYPE_BOOL&&response->GetBool("headersReady")&&response->GetDictionary("headers");}
  static bool RawNumber(Dict object,const char*key,double&value){if(object->GetType(key)==VTYPE_INT)value=object->GetInt(key);else if(object->GetType(key)==VTYPE_DOUBLE)value=object->GetDouble(key);else return false;return std::isfinite(value);}
  static std::optional<std::string> ContentCoding(Dict response){
    std::string result;for(const auto&[name,value]:HeaderPairs(response))if(Lower(name)=="content-encoding"){const auto coding=Lower(Trim(value));const size_t comma=result.empty()?0:1;if(coding.size()+comma>4096-result.size())return std::nullopt;if(comma)result+=",";result+=coding;}
    return result;
  }
  static std::optional<uint64_t> EncodedBody(const Record&record){
    if(!record.body_available||!record.error.empty()||!HeadersObservedReady(record.response)||!record.wire_coding)return std::nullopt;
    const auto coding=ContentCoding(record.response);if(!coding||*coding!=*record.wire_coding||(*coding!="gzip"&&*coding!="identity"&&!coding->empty()))return std::nullopt;
    return record.wire.Encoded(record.body.size());
  }
  static bool IdentityContent(const Record&record){
    // A projected empty headers dictionary is not evidence that coding is
    // absent. Both flags originate in the native matched-header observation;
    // SDK/user input cannot opt a response into this internal projection.
    if(!record.body_available||record.wire_coding_conflict||!HeadersObservedReady(record.response))return false;
    for(const auto&[name,value]:HeaderPairs(record.response))if(Lower(name)=="content-encoding"){const auto coding=Lower(Trim(value));if(!coding.empty()&&coding!="identity")return false;}
    return true;
  }
  static CefRefPtr<CefListValue> Cookies(Dict object,bool response,bool header_receipt=false,const std::optional<double>&wall_ms={}){
    auto result=CefListValue::Create();size_t index=0;
    for(const auto&[name,raw]:HeaderPairs(object)){if(Lower(name)!=(response?"set-cookie":"cookie"))continue;size_t begin=0;
      while(begin<raw.size()){const auto end=raw.find(';',begin);auto item=Trim(raw.substr(begin,end==std::string::npos?std::string::npos:end-begin));const auto equals=item.find('=');
        if(equals!=std::string::npos){auto cookie=CefDictionaryValue::Create();cookie->SetString("name",Trim(item.substr(0,equals)));cookie->SetString("value",item.substr(equals+1));
          if(response){NativeHARCookieExpiry expiry;size_t attribute=end==std::string::npos?raw.size():end+1;
            while(attribute<raw.size()){const auto next=raw.find(';',attribute);const auto actual=Trim(raw.substr(attribute,next==std::string::npos?std::string::npos:next-attribute));const auto equal=actual.find('=');const auto key=Lower(Trim(actual.substr(0,equal)));const auto value=equal==std::string::npos?"":Trim(actual.substr(equal+1));
              if(key=="path"||key=="domain")cookie->SetString(key,value);else if(key=="httponly")cookie->SetBool("httpOnly",true);else if(key=="secure")cookie->SetBool("secure",true);else if(key=="samesite"){const auto site=Lower(value);if(site=="lax")cookie->SetString("sameSite","Lax");else if(site=="strict")cookie->SetString("sameSite","Strict");else if(site=="none")cookie->SetString("sameSite","None");else cookie->Remove("sameSite");}
              expiry.Attribute(key,value);
              if(next==std::string::npos)break;
              attribute=next+1;
            }
            if(header_receipt)if(const auto expiration=expiry.Resolve(wall_ms))cookie->SetString("expires",*expiration);
          }result->SetDictionary(index++,cookie);
        }
        if(response||end==std::string::npos)break;
        begin=end+1;
      }
    }return result;
  }
  static bool TextBody(const Record&record){if(record.body.empty())return true;const auto mime=Lower(MIME(record.response));if(!(mime.starts_with("text/")||mime.find("json")!=std::string::npos||mime.find("javascript")!=std::string::npos||mime.find("xml")!=std::string::npos))return false;const std::string actual(record.body.begin(),record.body.end());return CefString(actual).ToString()==actual;}
  static std::string ResourceName(const Record&record){const auto mime=Lower(MIME(record.response));const auto suffix=mime.starts_with("text/html")?".html":mime.starts_with("text/css")?".css":mime.find("javascript")!=std::string::npos?".js":".dat";return Hash(record.body)+suffix;}
  static double Number(Dict object,const char*key){if(!object)return -1;const auto kind=object->GetType(key);return kind==VTYPE_INT?object->GetInt(key):kind==VTYPE_DOUBLE?object->GetDouble(key):-1;}
  static double Duration(Dict timing,const char*start,const char*end){const auto first=Number(timing,start),last=Number(timing,end);return first>=0&&last>=first?last-first:-1;}
  static std::string Version(Dict response){if(!response)return "";auto protocol=response->GetString("protocol").ToString();if(protocol=="h2")return "HTTP/2.0";if(protocol=="h3")return "HTTP/3.0";if(protocol.starts_with("http/")){std::transform(protocol.begin(),protocol.end(),protocol.begin(),[](unsigned char c){return char(std::toupper(c));});}return protocol;}
  static Dict Entry(const Record&record,bool links=false){
    auto entry=CefDictionaryValue::Create();entry->SetString("startedDateTime",Date(record.wall_ms));entry->SetString("_owner",record.owner);if(!record.pageref.empty())entry->SetString("pageref",record.pageref);entry->SetString("_resourceType",record.request->GetString("resourceType"));if(record.frame_allowed&&!record.request->GetString("frameId").empty()&&!record.request->GetBool("serviceWorker"))entry->SetString("_frameref",record.request->GetString("frameId"));
    if(record.response){
      if(auto address=record.response->GetDictionary("serverAddr")){entry->SetString("serverIPAddress",address->GetString("ipAddress"));entry->SetInt("_serverPort",address->GetInt("port"));}
      auto security=record.response->GetDictionary("securityDetails");entry->SetDictionary("_securityDetails",security?security->Copy(false):CefDictionaryValue::Create());
    }
    auto timing=record.request->GetDictionary("timing");double elapsed=-1;if(timing){auto value=timing->GetValue("responseEnd");if(value)elapsed=value->GetType()==VTYPE_INT?value->GetInt():value->GetDouble();}entry->SetDouble("_monotonicTime",record.observed_ms);entry->SetDouble("time",elapsed>=0?elapsed:std::max(0.0,record.completed_ms-record.observed_ms));
    auto request=CefDictionaryValue::Create();request->SetString("method",record.request->GetString("method"));request->SetString("url",record.request->GetString("url"));request->SetString("httpVersion",Version(record.response));request->SetList("headers",Headers(record.request->GetDictionary("headers")));request->SetList("cookies",Cookies(record.request,false));request->SetList("queryString",Query(record.request->GetString("url")));request->SetInt("headersSize",-1);request->SetInt("bodySize",record.post_available?static_cast<int>(record.post.size()):record.post_absent?0:-1);
    if(record.post_available){auto post=CefDictionaryValue::Create();post->SetString("mimeType",PostMIME(record.request));if(links&&record.post_published)post->SetString("_file","resources/"+Hash(record.post)+".dat");if(record.post_text)post->SetString("text",std::string(record.post.begin(),record.post.end()));else{post->SetString("text",CefBase64Encode(record.post.data(),record.post.size()));post->SetString("_encoding","base64");}PostParameters(post,MIME(record.request),std::string(record.post.begin(),record.post.end()));request->SetDictionary("postData",post);}
    else if(record.post_unavailable||record.request->GetBool("postDataUnavailable")||record.request->HasKey("postDataBodyId"))request->SetBool("_postDataUnavailable",true);else if(record.request->HasKey("postData")){auto post=CefDictionaryValue::Create();post->SetString("mimeType",PostMIME(record.request));post->SetString("text",record.request->GetString("postData"));PostParameters(post,MIME(record.request),record.request->GetString("postData"));request->SetDictionary("postData",post);}
    if(record.request->GetType("_nativeHeadersError")==VTYPE_STRING)request->SetString("_headersError",record.request->GetString("_nativeHeadersError"));
    entry->SetDictionary("request",request);if(!record.header_error.empty())entry->SetString("_headersError",record.header_error);
    auto response=CefDictionaryValue::Create();response->SetInt("status",record.response?record.response->GetInt("status"):0);response->SetString("statusText",record.response?record.response->GetString("statusText"):CefString{});response->SetString("httpVersion",Version(record.response));response->SetList("headers",Headers(record.response?record.response->GetDictionary("headers"):nullptr));response->SetList("cookies",Cookies(record.response,true,record.header_receipt_taken,record.header_wall_ms));const auto location=Header(record.response,"location");CefString redirect;if(!location.empty()&&CefResolveURL(record.request->GetString("url"),location,redirect))response->SetString("redirectURL",redirect);else response->SetString("redirectURL",location);response->SetInt("headersSize",-1);const auto encoded_body=EncodedBody(record);response->SetInt("bodySize",encoded_body?static_cast<int>(*encoded_body):record.request->GetString("method")=="HEAD"?0:-1);
    if(record.response&&record.response->GetType("_nativeHeadersError")==VTYPE_STRING)response->SetString("_headersError",record.response->GetString("_nativeHeadersError"));
    auto content=CefDictionaryValue::Create();content->SetString("mimeType",MIME(record.response));content->SetInt("size",record.body_available?static_cast<int>(record.body.size()):-1);if(encoded_body)content->SetInt("compression",static_cast<int>(record.body.size())-static_cast<int>(*encoded_body));else if(IdentityContent(record))content->SetInt("compression",0);
    if(record.body_available){if(TextBody(record))content->SetString("text",std::string(record.body.begin(),record.body.end()));else{content->SetString("text",CefBase64Encode(record.body.data(),record.body.size()));content->SetString("encoding","base64");}content->SetString("_sha1",ResourceName(record));}else if(!record.body_error.empty())content->SetString("_error",record.body_error);else if(record.body_skipped)content->SetBool("_bodySkipped",true);
    if(!record.error.empty()){response->SetString("_failureText",record.error);}response->SetDictionary("content",content);entry->SetDictionary("response",response);auto timings=CefDictionaryValue::Create();for(const auto*field:{"blocked","dns","connect","send","wait","receive","ssl"})timings->SetInt(field,-1);timings->SetDouble("dns",Duration(timing,"domainLookupStart","domainLookupEnd"));timings->SetDouble("connect",Duration(timing,"connectStart","connectEnd"));timings->SetDouble("send",Duration(record.response?record.response->GetDictionary("timing"):nullptr,"sendStart","sendEnd"));timings->SetDouble("wait",Duration(record.response?record.response->GetDictionary("timing"):nullptr,"sendEnd","receiveHeadersEnd"));timings->SetDouble("receive",Duration(timing,"responseStart","responseEnd"));timings->SetDouble("ssl",Duration(timing,"secureConnectionStart","connectEnd"));entry->SetDictionary("timings",timings);entry->SetDictionary("cache",CefDictionaryValue::Create());if(!record.error.empty())entry->SetString("_failureText",record.error);return entry;
  }
  void Publish(const std::shared_ptr<Record>&keep){auto&record=*keep;if(record.published||record.post_pending||record.fetching)return;
    if(HeadersPending(record)&&record.header_error.empty()){
      if(!record.header_deadline)record.header_deadline=now_()+30000;
      if(now_()<record.header_deadline){if(!record.awaiting_headers){record.awaiting_headers=true;++header_pending_;}return;}
      record.header_error="Native HAR original header receipt deadline exceeded";
    }
    if(record.awaiting_headers){record.awaiting_headers=false;--header_pending_;}
    record.published=true;if(record.post_available){const auto hash=Hash(record.post);if(hash.empty()){Fail("Native HAR request digest failed");return;}if(record.publication.resource){record.publication.resource("resources/"+hash+".dat",record.post);record.post_published=true;}else if(!reserve_&&resource_){resource_(record.context,"resources/"+hash+".dat",record.post);record.post_published=true;}}if(record.body_available){const auto hash=Hash(record.body);if(hash.empty()){Fail("Native HAR digest failed");return;}if(record.publication.resource)record.publication.resource("resources/"+ResourceName(record),record.body);else if(!reserve_)resource_(record.context,"resources/"+ResourceName(record),record.body);}auto snapshot=CefDictionaryValue::Create();snapshot->SetString("type","resource-snapshot");snapshot->SetDictionary("snapshot",Entry(record,true));if(record.publication.snapshot)record.publication.snapshot(snapshot);else if(!reserve_)snapshot_(record.context,snapshot);}
  void Fail(const std::string&error){Clear();failure_(error);}
  NativeHARPages pages_;
  Snapshot snapshot_;Resource resource_;Reserve reserve_;std::function<double()>now_,wall_now_;std::function<void(const std::string&)>failure_;bool disposed_=false;uint64_t serial_=0;size_t pending_=0,post_pending_=0,header_pending_=0,metadata_bytes_=0,body_bytes_=0,original_bytes_=0,original_wire_bytes_=0,post_bytes_=0;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);std::shared_ptr<Physical>physical_=std::make_shared<Physical>();
  std::map<std::string,Original>originals_;std::map<int,Capture>captures_;std::map<std::string,Owner>owners_;std::map<std::string,std::shared_ptr<Record>>records_;
};
}
