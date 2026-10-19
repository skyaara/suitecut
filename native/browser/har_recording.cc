// SPDX-License-Identifier: MIT
#include "har_recording.h"
#include <atomic>
#include <cmath>
#include <filesystem>
#include <vector>
namespace suitecut {
struct NativeHARRecording::Work {uint64_t serial=0,generation=0;int context=0;Dict archive;std::string path,error;NativeHARFiles::Content content;bool zipped=false;double deadline=0;std::atomic_bool canceled=false,done=false;};
bool NativeHARRecording::Configure(int context,Dict options,std::string&error){
  if(cleared_||context<1||configs_.contains(context)||configs_.size()>=16||!options){error="Invalid or excessive native HAR recording owner";return false;}
  CefDictionaryValue::KeyList names;options->GetKeys(names);for(const auto&name:names)if(name!="path"&&name!="content"&&name!="mode"&&name!="urlFilter"){error="Unsupported native HAR recording option";return false;}
  Config config;config.path=options->GetString("path");
  if(options->GetType("path")!=VTYPE_STRING||config.path.empty()||config.path.size()>4096||config.path.find('\0')!=std::string::npos||!std::filesystem::path(config.path).is_absolute()){error="Native HAR recording requires an absolute path";return false;}
  const auto content=options->HasKey("content")?options->GetString("content").ToString():"embed";
  if(content=="attach")config.content=NativeHARFiles::Content::Attach;else if(content=="omit")config.content=NativeHARFiles::Content::Omit;else if(content!="embed"){error="Invalid native HAR content option";return false;}
  if(options->HasKey("mode")){const auto mode=options->GetString("mode").ToString();if(mode!="full"&&mode!="minimal"){error="Invalid native HAR recording mode";return false;}config.minimal=mode=="minimal";}
  const auto filter=options->HasKey("urlFilter")?options->GetString("urlFilter").ToString():"**";std::vector<std::string>expanded;
  if(filter.empty()||filter.size()>8192||!NativeURLGlobExpand(filter,expanded)){error="Invalid native HAR URL filter";return false;}
  if(generation_>=9007199254740991ULL){error="Native HAR generation exhausted";return false;}config.generation=++generation_;
  config.zipped=config.path.ends_with(".zip");if(!archives_.Start(context,filter)){error="Native HAR capture owner unavailable";return false;}configs_.emplace(context,std::move(config));return true;
}
void NativeHARRecording::BeforeClose(int context,double deadline,std::function<void()> close){
  const auto found=configs_.find(context);if(found==configs_.end()){if(close)close();return;}auto&config=found->second;
  if(config.owners_closing){if(close)close();return;}
  if(close)config.close.push_back(std::move(close));
  if(deadline>0&&(!config.deadline||deadline<config.deadline))config.deadline=deadline;
  config.sealed=true;archives_.SealRecording(context);
  if(archives_.recording_pending(context)==0||(config.deadline&&hooks_.now()>=config.deadline)){config.owners_closing=true;auto callbacks=std::move(config.close);for(auto&callback:callbacks)if(callback)callback();}
  if(hooks_.wake)hooks_.wake();
}
void NativeHARRecording::BeforePageClose(int context,const std::string&owner,double deadline,std::function<void()> close){
  auto bodies=archives_.RecordingBodies(owner);if(!configs_.contains(context)||bodies.empty()){if(close)close();return;}
  if(page_closes_.size()>=64){configs_.at(context).error="Native HAR page close reservation capacity exceeded";if(close)close();return;}
  page_closes_.push_back({context,std::move(bodies),deadline,std::move(close)});if(hooks_.wake)hooks_.wake();
}
bool NativeHARRecording::ContextReady(int context){
  const auto found=configs_.find(context);if(found==configs_.end())return true;
  auto&config=found->second;if(!config.sealed)return false;
  if(!config.writing&&!config.done&&config.owners_closing&&archives_.Ready(context))BeginWrite(context);
  const auto current=configs_.find(context);return current==configs_.end()||current->second.done;
}
std::string NativeHARRecording::Error(int context)const{const auto found=configs_.find(context);return found==configs_.end()?std::string{}:found->second.error;}
void NativeHARRecording::BeginWrite(int context){
  auto&config=configs_.at(context);
  if(work_.size()>=4||(hooks_.available&&!hooks_.available()))return;
  if(config.deadline&&hooks_.now()>=config.deadline){config.done=true;if(config.error.empty())config.error="Native HAR original context close file deadline exceeded";return;}
  auto archive=archives_.Export(context);
  if(!archive||serial_>=9007199254740991ULL){config.done=true;config.error="Native HAR export or physical file capacity exceeded";return;}
  auto work=std::make_shared<Work>();work->serial=++serial_;work->generation=config.generation;work->context=context;work->archive=std::move(archive);work->path=config.path;work->content=config.content;work->zipped=config.zipped;work->deadline=config.deadline;
  if(config.minimal){auto log=work->archive->GetDictionary("log");auto entries=log->GetList("entries");for(size_t i=0;i<entries->GetSize();++i){auto entry=entries->GetDictionary(i);CefDictionaryValue::KeyList keys;entry->GetKeys(keys);for(const auto&key:keys)if(key!="request"&&key!="response")entry->Remove(key);for(const auto*name:{"request","response"}){auto section=entry->GetDictionary(name);section->GetKeys(keys);for(const auto&key:keys){const bool keep=key=="headers"||(std::string(name)=="request"?(key=="url"||key=="method"||key=="postData"||key=="_postDataUnavailable"):(key=="status"||key=="statusText"||key=="content"));if(!keep)section->Remove(key);}auto content=section->GetDictionary("content");if(content){content->Remove("size");content->Remove("compression");}}}}
  config.writing=true;work_[work->serial]=work;const auto weak=weak_from_this();const auto now=hooks_.now;const auto owner=hooks_.owner;
  bool submitted=false;try{submitted=hooks_.background([weak,work,now,owner]{
    try{NativeHARFiles::Action action{work->canceled,work->deadline,now};if(!NativeHARFiles::Write(work->path,work->archive,work->content,work->zipped,action,work->error)&&work->error.empty())work->error="Native HAR file export failed";}
    catch(...){work->error="Native HAR background file export failed";}
    work->done.store(true,std::memory_order_release);try{owner([weak,id=work->serial]{if(const auto self=weak.lock())self->Finish(id);});}catch(...){/* Tick retires actual completed work. */}
  });}catch(...){submitted=false;}
  if(!submitted){work_.erase(work->serial);config.writing=false;}
}
void NativeHARRecording::Finish(uint64_t serial){
  const auto found=work_.find(serial);if(found==work_.end()||!found->second->done.load(std::memory_order_acquire))return;
  auto work=found->second;work_.erase(found);const auto config=configs_.find(work->context);
  if(config!=configs_.end()&&config->second.generation==work->generation){config->second.done=true;config->second.writing=false;if(config->second.error.empty())config->second.error=work->error;if(config->second.error.empty()&&work->deadline&&hooks_.now()>=work->deadline)config->second.error="Native HAR original context close file deadline exceeded";archives_.Stop(work->context);}
  if(config!=configs_.end()&&config->second.generation==work->generation&&hooks_.settled)hooks_.settled(work->context);
  if(hooks_.wake)hooks_.wake();
}
void NativeHARRecording::Tick(){
  std::vector<std::function<void()>>closed;
  for(auto it=page_closes_.begin();it!=page_closes_.end();)if(!archives_.RecordingBodies(it->bodies)||(it->deadline&&hooks_.now()>=it->deadline)){closed.push_back(std::move(it->close));it=page_closes_.erase(it);}else ++it;
  for(auto&close:closed)if(close)close();
  std::vector<uint64_t>done;for(const auto&[serial,work]:work_)if(work->done.load(std::memory_order_acquire))done.push_back(serial);for(const auto serial:done)Finish(serial);
  std::vector<int>close;for(const auto&[context,config]:configs_)if(config.sealed&&!config.owners_closing&&(archives_.recording_pending(context)==0||(config.deadline&&hooks_.now()>=config.deadline)))close.push_back(context);
  for(const int context:close){const auto found=configs_.find(context);if(found==configs_.end())continue;found->second.owners_closing=true;auto callbacks=std::move(found->second.close);for(auto&callback:callbacks)if(callback)callback();}
  std::vector<int>exports;for(const auto&[context,config]:configs_)if(config.sealed&&config.owners_closing&&!config.done&&!config.writing&&archives_.Ready(context))exports.push_back(context);
  for(const int context:exports){if(!configs_.contains(context))continue;BeginWrite(context);const auto found=configs_.find(context);if(found!=configs_.end()&&found->second.done&&hooks_.settled)hooks_.settled(context);}
}
bool NativeHARRecording::NeedsTick()const{if(!work_.empty()||!page_closes_.empty())return true;for(const auto&[context,config]:configs_)if(config.sealed&&!config.done)return true;return false;}
void NativeHARRecording::Removed(int context){
  std::vector<std::function<void()>> close;
  const auto config=configs_.find(context);if(config!=configs_.end()){close=std::move(config->second.close);configs_.erase(config);}
  for(auto it=page_closes_.begin();it!=page_closes_.end();)if(it->context==context){close.push_back(std::move(it->close));it=page_closes_.erase(it);}else ++it;
  archives_.Stop(context);for(const auto&[serial,work]:work_)if(work->context==context)work->canceled.store(true,std::memory_order_release);
  // Retire the logical owner first: a physical close callback may reenter Removed.
  for(auto&callback:close)if(callback)callback();
  if(hooks_.wake)hooks_.wake();
}
void NativeHARRecording::Clear(){cleared_=true;std::vector<int>contexts;for(const auto&[context,config]:configs_)contexts.push_back(context);for(const int context:contexts)Removed(context);}
}
