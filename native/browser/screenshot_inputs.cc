// SPDX-License-Identifier: MIT
#include "screenshot_inputs.h"
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
namespace suitecut {
struct ScreenshotInputs::Retentions{std::mutex mutex;size_t count=0;std::map<int,size_t>contexts;};
struct ScreenshotInputs::Admission{
 std::shared_ptr<Retentions>state;int context=0;bool active=false;std::function<void(int)>settled;std::function<void()>wake;
 ~Admission(){if(!active)return; {std::lock_guard lock(state->mutex);--state->count;auto found=state->contexts.find(context);if(!--found->second)state->contexts.erase(found);}if(settled)settled(context);if(wake)wake();}
};
struct ScreenshotInputs::Record{
 Purpose purpose=Purpose::Comparison;
 Owner owner;std::string token,client,path;double deadline=0;bool file=false,claimed=false;int command=0;size_t png_size=0,config_size=0,png_received=0,config_received=0;
 std::shared_ptr<Data>data;Reply reply;uint64_t physical=0;int watch_owner=0;std::function<void(bool,const std::string&)>watch;
};
struct ScreenshotInputs::Pending{
 uint64_t id=0;Owner owner;std::string token,path,error;std::shared_ptr<Data>data;
 std::shared_ptr<ScreenshotInputs>physical_owner;std::atomic_bool canceled=false,done=false;
};
namespace {
constexpr size_t kConfigLimit=1024*1024,kChunkLimit=24*1024;
// Count native protocol structure before CEF allocates dictionary/list nodes.
// CEF remains the authoritative JSON/UTF8 parser; this scan only bounds work.
bool ConfigurationBudget(const std::string&text,double deadline,const std::function<double()>&now,std::string&error){
 bool quoted=false,escaped=false,atom=false;size_t depth=0,work=0,nodes=0,entries=0,processed=0;
 const auto expired=[&]{if(deadline>0&&now()>=deadline){error="Native artifact original deadline expired during configuration input";return true;}return false;};
 const auto node=[&]{if(++nodes>100000){error="Native artifact configuration node/entry work budget exceeded";return false;}return true;};
 for(unsigned char unit:text){
  // Long HTML/CSS string bodies still consume the original preparation clock.
  if((processed++&4095)==0&&expired())return false;
  if(quoted){if(escaped)escaped=false;else if(unit=='\\')escaped=true;else if(unit=='"')quoted=false;continue;}
  if(unit==' '||unit=='\t'||unit=='\r'||unit=='\n'){atom=false;continue;}
  if(++work>1000000){error="Native artifact configuration structure exceeds its work budget";return false;}
  if(unit=='"'){if(!node())return false;quoted=true;atom=false;}
  else if(unit=='{'||unit=='['){if(!node())return false;atom=false;if(++depth>256){error="Native artifact configuration exceeds256 nesting levels";return false;}}
  else if(unit=='}'||unit==']'){atom=false;if(!depth){error="Invalid native artifact configuration structure";return false;}--depth;}
  else if(unit==','||unit==':'){atom=false;if(++entries>100000){error="Native artifact configuration node/entry work budget exceeded";return false;}}
  else if(!atom){if(!node())return false;atom=true;}
 }
 if(quoted||escaped||depth){error="Invalid native artifact configuration structure";return false;}
 return !expired();
}
const char* Expired(ScreenshotInputs::Purpose purpose=ScreenshotInputs::Purpose::Comparison){return purpose==ScreenshotInputs::Purpose::Comparison?"Native screenshot assertion original deadline expired during baseline input":"Native artifact original deadline expired during configuration input";}
std::string IO(const char*step,int code){return std::string("Native screenshot baseline ")+step+": "+std::strerror(code);}
bool SameStat(const struct stat&a,const struct stat&b){
#if defined(__APPLE__)
 return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec&&a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec&&a.st_ctimespec.tv_sec==b.st_ctimespec.tv_sec&&a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;
#else
 return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
#endif
}
void Read(const std::string&path,std::shared_ptr<ScreenshotInputs::Data>data,const std::atomic_bool&canceled,std::string&error){
 if(canceled.load(std::memory_order_acquire))return;
 const int descriptor=open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
 if(descriptor<0){error=IO("open",errno);return;}
 struct FD{int value;~FD(){close(value);}}fd{descriptor};struct stat before{},after{};
 if(fstat(descriptor,&before)!=0){error=IO("stat",errno);return;}
 if(!S_ISREG(before.st_mode)||before.st_size<33||uint64_t(before.st_size)>kScreenshotComparisonPNGBytes){error="Native screenshot baseline must be a regular PNG file of at most2MiB";return;}
 data->png.resize(size_t(before.st_size)+1);size_t count=0;
 while(count<data->png.size()){
  if(canceled.load(std::memory_order_acquire))return;
  const auto bytes=read(descriptor,data->png.data()+count,std::min(kChunkLimit,data->png.size()-count));
  if(bytes<0){if(errno==EINTR)continue;error=IO("read",errno);return;}
  if(!bytes)break;
  count+=size_t(bytes);
 }
 if(fstat(descriptor,&after)!=0){error=IO("restat",errno);return;}
 if(count!=size_t(before.st_size)||!SameStat(before,after)){error="Native screenshot baseline changed while reading";return;}
 data->png.resize(count);
}
}
ScreenshotInputs::ScreenshotInputs(std::shared_ptr<ScreenshotByteBudget>budget,Hooks hooks):hooks_(std::move(hooks)),budget_(std::move(budget)),retentions_(std::make_shared<Retentions>()){}
ScreenshotInputs::~ScreenshotInputs(){for(auto&[token,record]:records_)record->reply={};}
ScreenshotInputs::Ticket ScreenshotInputs::Prepare(Owner owner,const std::string&client,bool file,size_t png_bytes,size_t config_bytes,double deadline,std::string&error,Purpose purpose){
 const bool configuration=purpose==Purpose::Screenshot||purpose==Purpose::PDF;
 if((purpose!=Purpose::Comparison&&!configuration)||(configuration&&(file||png_bytes))||client.empty()||client.size()>128||client.find('\0')!=std::string::npos||owner.context<=0||owner.page<=0||!owner.source||AdmissionCount()>=16||(hooks_.available&&!hooks_.available())||!config_bytes||config_bytes>(configuration?16*kConfigLimit:kConfigLimit)||(!configuration&&!file&&(!png_bytes||png_bytes>kScreenshotComparisonPNGBytes))||(file&&png_bytes)||!std::isfinite(deadline)||deadline<0||generation_==std::numeric_limits<uint64_t>::max()){
  error="Invalid native screenshot input owner, size, deadline or bounded capacity";return {};
 }
 for(const auto&[token,record]:records_)if(record->owner.page==owner.page&&record->client==client){error="Duplicate native screenshot preparation client token";return {};}
 if(deadline>0&&hooks_.now()>=deadline){error=Expired(purpose);return {};}
 auto lease=budget_->Reserve((file?kScreenshotComparisonPNGBytes+1:png_bytes)+config_bytes*(configuration?4:2));if(!lease){error="Native screenshot input64MiB shared byte budget exceeded";return {};}
 auto record=std::make_shared<Record>();record->owner=owner;record->purpose=purpose;record->client=client;record->file=file;record->png_size=png_bytes;record->config_size=config_bytes;record->deadline=deadline;record->token="nativeScreenshotInput:"+std::to_string(++generation_);record->data=std::make_shared<Data>();record->data->reservation=std::move(lease);record->data->config.reserve(config_bytes);if(!file)record->data->png.resize(png_bytes);records_[record->token]=record;
 if(hooks_.wake)hooks_.wake();
 return {record->token,deadline};
}
std::shared_ptr<ScreenshotInputs::Record> ScreenshotInputs::Find(Owner owner,const std::string&token,std::string&error){
 const auto found=records_.find(token);if(found==records_.end()||found->second->owner!=owner){error="Native screenshot input token is stale, expired or belongs to another source";return {};}
 const auto record=found->second;if(record->deadline>0&&hooks_.now()>=record->deadline){Finish(record,Expired(record->purpose));error=Expired(record->purpose);return {};}
 return record;
}
bool ScreenshotInputs::Append(Owner owner,const std::string&token,bool config,size_t offset,const std::vector<uint8_t>&bytes,std::string&error){
 const auto record=Find(owner,token,error);if(!record)return false;
 if(record->claimed||bytes.empty()||bytes.size()>kChunkLimit||(!config&&record->file)){error="Invalid native screenshot input chunk or already-claimed token";return false;}
 auto& received=config?record->config_received:record->png_received;const size_t total=config?record->config_size:record->png_size;
 if(offset!=received||bytes.size()>total-received){error="Native screenshot input chunk offset/length is duplicate, out of order or oversized";return false;}
 if(config)record->data->config.append(reinterpret_cast<const char*>(bytes.data()),bytes.size());else std::memcpy(record->data->png.data()+received,bytes.data(),bytes.size());received+=bytes.size();return true;
}
bool ScreenshotInputs::Inspect(Owner owner,const std::string&token,std::string&json,std::string&error,Purpose purpose,std::shared_ptr<Data>* retention){
 const auto record=Find(owner,token,error);if(!record)return false;
 if(record->purpose!=purpose){error="Native artifact input purpose mismatch";return false;}
 if(record->claimed||record->config_received!=record->config_size||(!record->file&&record->png_received!=record->png_size)){error="Native screenshot input is incomplete or already claimed";return false;}
 if(purpose!=Purpose::Comparison&&!ConfigurationBudget(record->data->config,record->deadline,hooks_.now,error)){
  if(record->deadline>0&&hooks_.now()>=record->deadline){error=Expired(record->purpose);Finish(record,error);}return false;
 }
 if(retention)*retention=record->data;
 json=record->data->config;return true;
}
bool ScreenshotInputs::Claim(Owner owner,const std::string&token,const std::string&path,int command,Reply reply,std::string&error,Purpose purpose){
 const auto record=Find(owner,token,error);if(!record)return false;
 if(record->purpose!=purpose){error="Native artifact input purpose mismatch";return false;}
 if(record->claimed||!reply||command<=0||record->config_received!=record->config_size||(!record->file&&record->png_received!=record->png_size)||(record->file&&(path.empty()||path.find('\0')!=std::string::npos))){error="Invalid native screenshot input claim";return false;}
 if(record->purpose!=Purpose::Comparison){
  auto admission=std::make_shared<Admission>();admission->state=retentions_;admission->context=owner.context;admission->settled=hooks_.settled;admission->wake=hooks_.wake;{std::lock_guard lock(retentions_->mutex);++retentions_->contexts[owner.context];++retentions_->count;admission->active=true;}record->data->admission=std::move(admission);
 }
 if(!record->file){record->claimed=true;record->command=command;record->reply=std::move(reply);Finish(record,"");return true;}
 record->claimed=true;record->command=command;record->reply=std::move(reply);record->path=path;
 StartFile(record);if(hooks_.wake)hooks_.wake();return true;
}
void ScreenshotInputs::StartFile(const std::shared_ptr<Record>&record){
 if(!record->claimed||!record->file||record->physical||pending_.size()>=2||(hooks_.physical_available&&!hooks_.physical_available()))return;
 if(generation_==std::numeric_limits<uint64_t>::max()){Finish(record,"Native screenshot input generation exhausted");return;}
 if(record->deadline>0&&hooks_.now()>=record->deadline){Finish(record,Expired(record->purpose));return;}
 const auto owner=record->owner;const auto token=record->token;const auto path=record->path;
 auto work=std::make_shared<Pending>();work->owner=owner;work->token=token;work->path=path;work->data=record->data;work->id=++generation_;work->physical_owner=shared_from_this();pending_[work->id]=work;record->physical=work->id;
 const auto weak=weak_from_this();const auto post=hooks_.post;bool accepted=false;
 try{accepted=hooks_.background([work,weak,post]{try{Read(work->path,work->data,work->canceled,work->error);}catch(...){work->error="Native screenshot baseline file job failed";}work->done.store(true,std::memory_order_release);try{post([weak,id=work->id]{if(const auto self=weak.lock())self->FinishPhysical(id);});}catch(...){}});}catch(...){accepted=false;}
 if(!accepted){pending_.erase(work->id);work->physical_owner.reset();record->physical=0;Finish(record,"Native screenshot file background admission rejected");}
 if(hooks_.wake)hooks_.wake();
}
void ScreenshotInputs::Finish(std::shared_ptr<Record>record,const std::string&error){
 const auto found=records_.find(record->token);if(found==records_.end()||found->second!=record)return;
 auto reply=std::move(record->reply);auto watch=std::move(record->watch);auto data=error.empty()?record->data:nullptr;const double deadline=record->deadline;
 records_.erase(found);record->data.reset();if(error.size()&&record->physical){const auto pending=pending_.find(record->physical);if(pending!=pending_.end())pending->second->canceled.store(true,std::memory_order_release);}
 if(watch)watch(error.empty(),error);
 if(reply)reply(std::move(data),deadline,error);
 if(hooks_.settled)hooks_.settled(record->owner.context);
 if(hooks_.wake)hooks_.wake();
}
bool ScreenshotInputs::Watch(Owner owner,const std::string&token,int command,std::function<void(bool,const std::string&)> reply,std::string&error){
 const auto record=Find(owner,token,error);if(!record)return false;
 if(command<=0||record->watch||!reply){error="Native screenshot input already has a deadline watch";return false;}
 record->watch_owner=command;record->watch=std::move(reply);return true;
}
void ScreenshotInputs::CancelWait(int command){for(auto&[token,record]:records_)if(record->watch_owner==command){record->watch={};record->watch_owner=0;}}
void ScreenshotInputs::DiscardClient(int page,const std::string&client){std::vector<std::shared_ptr<Record>>items;for(const auto&[token,record]:records_)if(record->owner.page==page&&record->client==client)items.push_back(record);for(const auto&record:items)Cancel(record);}
void ScreenshotInputs::Discard(int context,int page,const std::string&token){const auto found=records_.find(token);if(found!=records_.end()&&found->second->owner.context==context&&found->second->owner.page==page)Cancel(found->second);}
void ScreenshotInputs::Clear(){std::vector<std::shared_ptr<Record>>items;for(const auto&[token,record]:records_)items.push_back(record);for(const auto&record:items)Cancel(record);}
void ScreenshotInputs::Cancel(std::shared_ptr<Record>record){record->reply={};Finish(std::move(record),"Native screenshot input canceled");}
void ScreenshotInputs::Cancel(Owner owner,const std::string&token){const auto found=records_.find(token);if(found!=records_.end()&&found->second->owner==owner)Cancel(found->second);}
void ScreenshotInputs::CancelCommand(int page,int command){std::vector<std::shared_ptr<Record>>items;for(const auto&[token,record]:records_)if(record->owner.page==page&&record->command==command)items.push_back(record);for(const auto&record:items)Cancel(record);}
void ScreenshotInputs::PageClosed(int page){std::vector<std::shared_ptr<Record>>items;for(const auto&[token,record]:records_)if(record->owner.page==page)items.push_back(record);for(const auto&record:items)Cancel(record);}
void ScreenshotInputs::FinishPhysical(uint64_t id){
 const auto found=pending_.find(id);if(found==pending_.end()||!found->second->done.load(std::memory_order_acquire))return;
 const auto work=found->second;const auto logical=records_.find(work->token);
 if(logical!=records_.end()&&logical->second->physical==id){const auto record=logical->second;record->physical=0;Finish(record,record->deadline>0&&hooks_.now()>=record->deadline?Expired(record->purpose):work->error);}
 work->data.reset();work->path.clear();pending_.erase(id);if(hooks_.settled)hooks_.settled(work->owner.context);if(hooks_.wake)hooks_.wake();work->physical_owner.reset();
}
void ScreenshotInputs::Tick(){
 std::vector<uint64_t>completed;for(const auto&[id,work]:pending_)if(work->done.load(std::memory_order_acquire))completed.push_back(id);for(auto id:completed)FinishPhysical(id);
 std::vector<std::shared_ptr<Record>>expired;for(const auto&[token,record]:records_)if(record->deadline>0&&hooks_.now()>=record->deadline)expired.push_back(record);for(const auto&record:expired)Finish(record,Expired(record->purpose));
 std::vector<std::shared_ptr<Record>>queued;for(const auto&[token,record]:records_)if(record->claimed&&record->file&&!record->physical)queued.push_back(record);for(const auto&record:queued)StartFile(record);
}
size_t ScreenshotInputs::RetainedCount()const{std::lock_guard lock(retentions_->mutex);return retentions_->count;}
bool ScreenshotInputs::ContextReady(int context)const{
 {std::lock_guard lock(retentions_->mutex);if(retentions_->contexts.contains(context))return false;}for(const auto&[token,record]:records_)if(record->owner.context==context)return false;for(const auto&[id,work]:pending_)if(work->owner.context==context)return false;return true;}
} // namespace suitecut
