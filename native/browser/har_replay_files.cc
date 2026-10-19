// SPDX-License-Identifier: MIT
#include "har_replay_files.h"
#include <atomic>
#include <cmath>
#include <vector>
namespace suitecut {
namespace {
std::atomic_size_t retained=0;
struct Reservation {
  size_t bytes=0;
  ~Reservation(){retained.fetch_sub(bytes,std::memory_order_acq_rel);}
  bool Reserve(size_t value){auto current=retained.load(std::memory_order_acquire);do{if(value>67108864-current)return false;}while(!retained.compare_exchange_weak(current,current+value,std::memory_order_acq_rel));bytes=value;return true;}
};
struct RetainedReplay {Reservation quota;std::shared_ptr<NativeHARReplay>data;};
}
struct NativeHARReplayFiles::Work {int owner=0;std::string path,error;double deadline=0;Done reply;std::shared_ptr<NativeHARReplay>replay;std::atomic_bool done=false,canceled=false;bool allow_empty_log=false;};
size_t NativeHARReplayFiles::RetainedBytes(){return retained.load(std::memory_order_acquire);}
bool NativeHARReplayFiles::Read(int owner,const std::string&path,double deadline,Done reply,bool allow_empty_log){
  if(cleared_||owner<1||!reply||work_.contains(owner)||work_.size()>=8||path.empty()||path.size()>4096||!std::isfinite(deadline)||deadline<0){if(reply)reply(nullptr,"Invalid or excessive native HAR file operation");return false;}
  auto work=std::make_shared<Work>();work->owner=owner;work->path=path;work->deadline=deadline;work->allow_empty_log=allow_empty_log;work->reply=std::move(reply);work_[owner]=work;const auto weak=weak_from_this();const auto now=hooks_.now;const auto post=hooks_.owner;bool submitted=false;
  try{submitted=hooks_.background([weak,work,now,post]{
    try{NativeHARFiles::Dict archive;NativeHARFiles::Action action{work->canceled,work->deadline,now};if(NativeHARFiles::Read(work->path,action,archive,work->error,work->allow_empty_log)){auto replay=std::make_shared<NativeHARReplay>();if(replay->Load(archive,work->error))work->replay=std::move(replay);}if(!work->replay&&work->error.empty())work->error="Native HAR replay could not be decoded";}
    catch(...){work->error="Native HAR replay file decode failed";}
    work->done.store(true,std::memory_order_release);try{post([weak,owner=work->owner]{if(const auto self=weak.lock())self->Finish(owner);});}catch(...){/* Tick retains and drains actual completed file work. */}
  });}catch(...){submitted=false;}
  if(!submitted){auto callback=std::move(work->reply);work_.erase(owner);callback(nullptr,"Native HAR file executor capacity exceeded");return false;}if(hooks_.wake)hooks_.wake();return true;
}
void NativeHARReplayFiles::Finish(int owner){
  const auto found=work_.find(owner);if(found==work_.end()||!found->second->done.load(std::memory_order_acquire))return;auto work=found->second;work_.erase(found);auto callback=std::move(work->reply);
  if(work->canceled.load(std::memory_order_acquire)||!callback){if(hooks_.wake)hooks_.wake();return;}
  if(work->deadline&&hooks_.now()>=work->deadline){work->replay.reset();callback(nullptr,"Native HAR original file deadline exceeded");if(hooks_.wake)hooks_.wake();return;}
  Replay replay;
  if(work->replay){auto holder=std::make_shared<RetainedReplay>();if(holder->quota.Reserve(work->replay->bytes())){holder->data=std::move(work->replay);replay=Replay(holder,holder->data.get());}else work->error="Native HAR global replay retained-byte capacity exceeded";}
  callback(std::move(replay),work->error);if(hooks_.wake)hooks_.wake();
}
void NativeHARReplayFiles::Cancel(int owner){const auto found=work_.find(owner);if(found==work_.end())return;found->second->canceled.store(true,std::memory_order_release);found->second->reply={};}
void NativeHARReplayFiles::Tick(){std::vector<int>done;for(const auto&[owner,work]:work_)if(work->done.load(std::memory_order_acquire))done.push_back(owner);for(const int owner:done)Finish(owner);}
void NativeHARReplayFiles::Clear(){cleared_=true;for(const auto&[owner,work]:work_){work->canceled.store(true,std::memory_order_release);work->reply={};}}
}
