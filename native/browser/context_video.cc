// SPDX-License-Identifier: MIT
#include "context_video.h"
#include "locator_handler_async_work.h"
#include "context_video_files.h"
#include "context_video_policy.h"
#include "devtools_utf16.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
namespace suitecut {
namespace {
bool PathText(CefString text,std::string&out){
  const auto units=text.ToString16();if(units.empty()||units.size()>4096||DevToolsHasIsolated(units)||units.find(char16_t(0))!=std::u16string::npos)return false;
  out=text.ToString();return out.size()<=4096&&std::filesystem::path(out).is_absolute();
}
bool Deadline(double value){return std::isfinite(value)&&value>=0;}
}
struct NativeContextVideo::Preparation{
  NativeContextVideo*issuer=nullptr;uint64_t token=0;bool consumed=false;
  std::shared_ptr<ContextVideoDirectory>directory;std::optional<NativeContextVideoSize>size;std::string ffmpeg;
};
struct NativeContextVideo::Record{
  int page=0,context=0;std::string id,name,path,error;Prepared config;
  bool terminal=false,sealed=false,deleted=false;uint64_t active_work=0;
  MediaPipelineResult result;std::shared_ptr<ContextVideoFile>file;std::deque<Key>waits;
};
struct NativeContextVideo::Wait{Key key;int context=0;std::shared_ptr<Record>record;std::string action,path;double deadline=0;Reply reply;NativeWorkContinuation origin;};
struct NativeContextVideo::Work{
  NativeLocatorHandlerAsyncWork handler_receipt;
  enum class Kind{Prepare,Seal,Operation};Kind kind=Kind::Operation;
  uint64_t serial=0;Key key{};int context=0;double deadline=0;
  std::atomic<bool>canceled{false},returned{false};bool ok=false,deleted=false;
  std::string error,path;PrepareReply prepare_reply;Prepared prepared;
  std::shared_ptr<Record>record;std::shared_ptr<ContextVideoFile>file;
};
NativeContextVideo::NativeContextVideo(Hooks hooks):hooks_(std::move(hooks)){
  if(!hooks_.background||!hooks_.owner||!hooks_.now||!hooks_.start)throw std::runtime_error("Native context video requires actual background/owner/clock/recording hooks");
}
NativeContextVideo::~NativeContextVideo(){Clear();}
double NativeContextVideo::IODeadline(double original)const{const double phase=hooks_.now()+60000.;return original?std::min(original,phase):phase;}
bool NativeContextVideo::Submit(std::shared_ptr<Work>work,std::function<void()>body){
  if(work_.size()>=8||serial_==UINT64_MAX)return false;
  work->serial=++serial_;work_.emplace(work->serial,work);
  // Destruction of the actual queued job releases its body/captures before
  // publishing return. No logical cancellation can manufacture this receipt.
  struct Job{
    std::shared_ptr<Work>work;std::weak_ptr<NativeContextVideo>owner;
    std::function<bool(std::function<void()>)>post;
    std::function<void()>body;bool admitted=false;
    ~Job(){
      body={};
      if(!admitted)return;
      work->returned.store(true,std::memory_order_release);
      const auto weak=owner;
      try{(void)post([weak]{if(const auto self=weak.lock())self->Tick();});}catch(...){/* Existing owner Tick also observes the retained return flag. */}
    }
    void Run(){
      try{body();}catch(const std::exception&error){work->error=std::string(error.what()).substr(0,4096);}
      catch(...){work->error="Native video file operation failed with a native exception";}
    }
  };
  auto job=std::make_shared<Job>();job->work=work;job->owner=weak_from_this();job->post=hooks_.owner;job->body=std::move(body);
  bool accepted=false;
  try{accepted=hooks_.background([job]{job->Run();});}catch(...){work_.erase(work->serial);throw;}
  job->admitted=accepted; // Local ref prevents destruction until admission is known.
  job.reset();
  if(!accepted){work_.erase(work->serial);return false;}
  if(hooks_.wake)hooks_.wake();
  return true;
}
bool NativeContextVideo::Prepare(int owner,uint64_t generation,Dict options,double deadline,PrepareReply reply){
  const auto reject=[&](const std::string&error){if(reply)reply({},error);return false;};
  if(!reply)return false;
  if(cleared_||owner<1||!generation||!Deadline(deadline)||prepared_.size()+configs_.size()>=256)return reject("Native video preparation lacks capacity or actual operation owner");
  for(const auto&[serial,work]:work_)if(work->key==Key{owner,generation})return reject("Native video preparation already owns this operation");
  std::string directory;if(!options||options->GetType("dir")!=VTYPE_STRING||!PathText(options->GetString("dir"),directory))return reject("Native recordVideo.dir requires a bounded absolute scalar path");
  CefDictionaryValue::KeyList keys;options->GetKeys(keys);for(const auto&key:keys)if(key!="dir"&&key!="size")return reject("Unexpected native recordVideo option");
  std::optional<NativeContextVideoSize>size;
  if(options->HasKey("size")){
    const auto value=options->GetDictionary("size");
    if(!value||value->GetSize()!=2||value->GetType("width")!=VTYPE_INT||value->GetType("height")!=VTYPE_INT)return reject("Native recordVideo.size requires exactly integer width and height");
    size=NativeContextVideoSize{value->GetInt("width"),value->GetInt("height")};
    try{(void)ContextVideoCanvas(1,1,size);}catch(const std::exception&error){return reject(error.what());}
  }
  auto work=std::make_shared<Work>();work->kind=Work::Kind::Prepare;work->key={owner,generation};work->deadline=IODeadline(deadline);work->prepare_reply=std::move(reply);
  const auto now=hooks_.now;
  if(!Submit(work,[this,work,directory=std::move(directory),size,now]{
    auto prepared=std::make_shared<Preparation>();prepared->issuer=this;prepared->size=size;
    prepared->directory=ContextVideoDirectory::Create(directory,work->canceled,work->deadline,now);
    prepared->ffmpeg=ResolveMediaExecutable("ffmpeg");ContextVideoActive(work->canceled,work->deadline,now);
    work->prepared=std::move(prepared);work->ok=true;
  })){auto done=std::move(work->prepare_reply);if(done)done({},"Native video preparation background capacity exceeded");return false;}
  return true;
}
bool NativeContextVideo::Configure(int context,Prepared prepared,std::string&error){
  if(context<1||cleared_||configs_.contains(context)||configs_.size()>=256||!prepared||prepared->issuer!=this||prepared->consumed){error="Native video configuration lacks its exact one-shot preparation";return false;}
  const auto found=prepared_.find(prepared->token);if(found==prepared_.end()||found->second!=prepared){error="Native video preparation is unavailable or foreign";return false;}
  prepared->consumed=true;configs_.emplace(context,prepared);prepared_.erase(found);return true;
}
void NativeContextVideo::Discard(Prepared prepared){if(!prepared||prepared->issuer!=this||prepared->consumed)return;const auto found=prepared_.find(prepared->token);if(found!=prepared_.end()&&found->second==prepared)prepared_.erase(found);}
bool NativeContextVideo::Configured(int context)const{return configs_.contains(context)&&!cleared_;}
bool NativeContextVideo::PageCreated(int page,int context,MediaPipelineOptions actual,std::optional<Descriptor>&video,std::string&error){
  video.reset();const auto config=configs_.find(context);if(config==configs_.end())return true;
  if(records_.size()>=256){
    for(auto it=records_.begin();it!=records_.end();){
      const auto&record=*it->second;
      if(!record.deleted||record.active_work||!record.waits.empty()){++it;continue;}
      // Only explicitly deleted identities may be reclaimed. Recent deletes
      // remain idempotent in a separately bounded tombstone cache.
      deleted_.emplace(record.id,record.context);deleted_order_.push_back(record.id);
      while(deleted_order_.size()>256){deleted_.erase(deleted_order_.front());deleted_order_.pop_front();}
      pages_.erase(record.page);it=records_.erase(it);
    }
  }
  if(cleared_||page<1||pages_.contains(page)||records_.size()>=256){error="Native automatic video identity capacity or page ownership exceeded";RememberError(context,error);return false;}
  try{
    if(actual.first_frame_deadline!=std::chrono::steady_clock::time_point{}&&actual.first_frame_deadline<=std::chrono::steady_clock::now())throw std::runtime_error("Native video page creation original deadline expired before recording admission");
    const int css_width=actual.output_width,css_height=actual.output_height;
    if(css_width<1||css_height<1)throw std::runtime_error("Native video requires positive CSS viewport dimensions");
    const auto canvas=ContextVideoCanvas(css_width,css_height,config->second->size);
    actual.max_content_width=0;actual.max_content_height=0;
    actual.dynamic_viewport=true;actual.source_width=canvas.width;actual.source_height=canvas.height;actual.i420=false;
    actual.output_width=canvas.width;actual.output_height=canvas.height;actual.audio=false;actual.top_left_padding=true;
    actual.ffmpeg_path=config->second->ffmpeg;actual.output_path=(std::filesystem::path(config->second->directory->Path())/("page-"+std::to_string(page)+".webm")).string();actual.max_file_bytes=ContextVideoFile::kMaxBytes;
    config->second->directory->Check();ValidateMediaPipelineOptions(actual);
    auto record=std::make_shared<Record>();record->page=page;record->context=context;record->path=actual.output_path;record->name=std::filesystem::path(actual.output_path).filename().string();record->config=config->second;
    // Allocate every public registry node/string before starting a producer.
    // Transferring these nodes after the genuine native ID returns allocates no
    // storage and cannot strand a recording behind a metadata allocation error.
    record->id.reserve(128);Descriptor descriptor;descriptor.id.reserve(128);
    decltype(records_)record_node;record_node.emplace("",record);auto rn=record_node.extract(record_node.begin());rn.key().reserve(128);
    decltype(pages_)page_node;page_node.emplace(page,"");auto pn=page_node.extract(page_node.begin());pn.mapped().reserve(128);
    const auto id=hooks_.start(page,context,std::move(actual));
    if(id.empty()||id.size()>128||records_.contains(id))throw std::runtime_error("Native automatic recording returned an invalid actual identity");
    record->id=id;rn.key()=id;pn.mapped()=id;descriptor.id=id;
    records_.insert(std::move(rn));pages_.insert(std::move(pn));video.emplace(std::move(descriptor));return true;
  }catch(const std::exception&failure){error=std::string(failure.what()).substr(0,4096);RememberError(context,error);return false;}
}
std::optional<NativeContextVideo::Descriptor>NativeContextVideo::Metadata(int page)const{const auto found=pages_.find(page);return found==pages_.end()?std::nullopt:std::optional<Descriptor>(Descriptor{found->second});}
void NativeContextVideo::Observe(int page,const std::string&id,const std::string&phase,const MediaPipelineResult&result){
  const auto found=records_.find(id);if(found==records_.end()||found->second->page!=page||found->second->terminal||(phase!="complete"&&phase!="failed"))return;
  auto&record=*found->second;record.terminal=true;record.result=result;
  if(result.output_path!=record.path||result.physical_encoder_pid!=0||(phase=="complete"&&!result.failure.empty())||(phase=="failed"&&result.failure.empty()))record.error="Native video terminal recording receipt is inconsistent";
  else if(!result.failure.empty())record.error=result.failure.substr(0,4096);
  if(!record.error.empty())RememberError(record.context,record.error);
  Tick();
}
void NativeContextVideo::Handle(int owner,uint64_t generation,int context,Dict params,double deadline,Reply reply){
  const auto reject=[&](const std::string&error){if(reply)reply(false,"",error);};
  if(!reply)return;
  if(cleared_||owner<1||!generation||!Deadline(deadline)||waits_.size()>=64||waits_.contains({owner,generation})||!params||params->GetType("videoId")!=VTYPE_STRING||params->GetType("action")!=VTYPE_STRING){reject("Native video request lacks actual ownership or capacity");return;}
  if(deadline&&hooks_.now()>=deadline){reject("Native video original request deadline expired");return;}
  const auto id=params->GetString("videoId").ToString(),action=params->GetString("action").ToString();const auto record=records_.find(id);
  if(id.empty()||id.size()>128||(action!="path"&&action!="saveAs"&&action!="delete")){reject("Native video identity or action is unavailable for this context");return;}
  if(record==records_.end()){
    const auto deleted=deleted_.find(id);
    if(action=="delete"&&deleted!=deleted_.end()&&deleted->second==context)reply(true,"","");
    else reject("Native video identity is deleted or unavailable for this context");
    return;
  }
  if(record->second->context!=context){reject("Native video identity or action is unavailable for this context");return;}
  std::string destination;if(action=="saveAs"&&(params->GetType("path")!=VTYPE_STRING||!PathText(params->GetString("path"),destination))){reject("Native video saveAs requires a bounded absolute scalar path");return;}
  auto wait=std::make_shared<Wait>();wait->key={owner,generation};wait->context=context;wait->record=record->second;wait->action=action;wait->path=std::move(destination);wait->deadline=deadline;wait->reply=std::move(reply);
  waits_.emplace(wait->key,wait);record->second->waits.push_back(wait->key);Tick();
}
void NativeContextVideo::EndWait(Key key,bool ok,const std::string&path,const std::string&error){
  const auto found=waits_.find(key);if(found==waits_.end())return;auto wait=std::move(found->second);waits_.erase(found);std::erase(wait->record->waits,key);auto reply=std::move(wait->reply);if(reply)reply(ok,path,error);
}
void NativeContextVideo::Pump(const std::shared_ptr<Record>&record){
  if(record->active_work||!record->terminal)return;
  if(!record->error.empty()){
    const auto queued=record->waits;for(const auto&key:queued)EndWait(key,false,"",record->error);return;
  }
  if(!record->sealed){
    auto work=std::make_shared<Work>();work->kind=Work::Kind::Seal;work->record=record;work->context=record->context;work->deadline=IODeadline(0);const auto now=hooks_.now;
    if(Submit(work,[work,now]{ContextVideoActive(work->canceled,work->deadline,now);work->file=ContextVideoFile::Seal(work->record->config->directory,work->record->name,work->record->result.output_bytes);ContextVideoActive(work->canceled,work->deadline,now);work->ok=true;}))record->active_work=work->serial;
    return;
  }
  while(!record->waits.empty()){
    const auto key=record->waits.front();const auto found=waits_.find(key);if(found==waits_.end()){record->waits.pop_front();continue;}
    const auto wait=found->second;
    if(wait->deadline&&hooks_.now()>=wait->deadline){EndWait(key,false,"","Native video original request deadline expired");continue;}
    if(record->deleted){EndWait(key,wait->action=="delete","",wait->action=="delete"?"":"Native video artifact was deleted");continue;}
    const NativeWorkOriginFrame handler_origin(wait->origin.Origin());
    auto work=std::make_shared<Work>();work->record=record;work->context=record->context;work->key=key;work->deadline=IODeadline(wait->deadline);work->file=record->file;const auto now=hooks_.now;
    if(Submit(work,[work,wait,now]{
      ContextVideoActive(work->canceled,work->deadline,now);
      if(wait->action=="path")work->path=work->file->Path();
      else if(wait->action=="saveAs")work->file->Copy(wait->path,work->canceled,work->deadline,now);
      else work->file->Delete(work->canceled,work->deadline,now,work->deleted);
      ContextVideoActive(work->canceled,work->deadline,now);work->ok=true;
    }))record->active_work=work->serial;
    return;
  }
}
void NativeContextVideo::Finish(const std::shared_ptr<Work>&work){
  if(work->ok&&work->deadline&&hooks_.now()>=work->deadline){work->ok=false;work->error="Native video original file operation deadline expired before owner publication";}
  if(work->kind==Work::Kind::Prepare){
    auto reply=std::move(work->prepare_reply);
    if(!work->canceled.load()&&!cleared_&&work->ok&&work->prepared){work->prepared->token=work->serial;prepared_.emplace(work->serial,work->prepared);if(reply)reply(work->prepared,"");}
    else if(reply&&!work->canceled.load())reply({},work->error.empty()?"Native video preparation failed":work->error);
    return;
  }
  const auto record=work->record;record->active_work=0;
  if(work->kind==Work::Kind::Seal){
    record->sealed=true;if(work->ok&&work->file)record->file=work->file;else record->error=work->error.empty()?"Native video final artifact could not be sealed":work->error;
  }else{
    if(work->deleted){record->deleted=true;record->file.reset();}
    if(work->canceled.load())EndWait(work->key,false,"","Native video file operation canceled");
    else EndWait(work->key,work->ok,work->path,work->ok?"":work->error.empty()?"Native video file operation failed":work->error);
  }
  if(!record->error.empty())RememberError(record->context,record->error);
  if(hooks_.settled)hooks_.settled(record->context);
}
void NativeContextVideo::Tick(){
  if(pumping_)return;
  pumping_=true;
  struct Restore{bool&flag;~Restore(){flag=false;}}restore{pumping_};
  std::vector<uint64_t>completed;for(const auto&[serial,work]:work_)if(work->returned.load(std::memory_order_acquire))completed.push_back(serial);
  for(const auto serial:completed){const auto found=work_.find(serial);if(found==work_.end())continue;auto work=std::move(found->second);work_.erase(found);Finish(work);}
  std::vector<Key>expired;for(const auto&[key,wait]:waits_)if(wait->deadline&&hooks_.now()>=wait->deadline)expired.push_back(key);
  for(const auto&key:expired){EndWait(key,false,"","Native video original request deadline expired");Cancel(key.first,key.second);}
  for(const auto&[id,record]:records_)Pump(record);
  if(NeedsTick()&&hooks_.wake)hooks_.wake();
}
void NativeContextVideo::Cancel(int owner,uint64_t generation){
  const Key key{owner,generation};for(const auto&[serial,work]:work_)if(work->key==key){work->canceled=true;work->prepare_reply={};}
  const auto wait=waits_.find(key);if(wait!=waits_.end()){std::erase(wait->second->record->waits,key);waits_.erase(wait);}
}
void NativeContextVideo::Removed(int context){configs_.erase(context);errors_.erase(context);}
void NativeContextVideo::Clear(){
  cleared_=true;for(const auto&[serial,work]:work_)if(work->kind!=Work::Kind::Seal){work->canceled=true;work->prepare_reply={};}
  std::vector<Key>keys;for(const auto&[key,wait]:waits_)keys.push_back(key);for(const auto&key:keys)EndWait(key,false,"","Native video browser is closing");prepared_.clear();
}
size_t NativeContextVideo::Physical()const{return work_.size();}
bool NativeContextVideo::ContextReady(int context)const{for(const auto&[serial,work]:work_)if(work->context==context)return false;for(const auto&[id,record]:records_)if(record->context==context&&record->terminal&&!record->sealed&&record->error.empty())return false;return true;}
bool NativeContextVideo::NeedsTick()const{if(!work_.empty())return true;for(const auto&[id,record]:records_)if(record->terminal&&((!record->sealed&&record->error.empty())||!record->waits.empty()))return true;return false;}
void NativeContextVideo::RememberError(int context,const std::string&error){if(!error.empty())errors_.try_emplace(context,error.substr(0,4096));}
std::string NativeContextVideo::Error(int context)const{const auto found=errors_.find(context);return found==errors_.end()?"":found->second;}
}
