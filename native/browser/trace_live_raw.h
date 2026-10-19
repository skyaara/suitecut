// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include "trace_journal.h"
#include "trace_raw_files.h"

namespace suitecut {
// Owner-thread coalescing: idle events flush after 100ms from first dirty event.
// At most one immutable snapshot per chunk is physically owned at a time. Busy
// chunks retain only a dirty bit, never a queue of per-action snapshot copies.
class TraceLiveRaw : public std::enable_shared_from_this<TraceLiveRaw> {
 public:
  using Lease=TraceJournal::Lease;
  using State=std::shared_ptr<TraceRawFiles::Live>;
  static constexpr size_t kPhysical=4,kBytes=128*1024*1024;
  static constexpr double kWindow=100;
  struct Hooks {
    std::string directory;
    std::function<double()> now;
    std::function<bool(std::function<void()>)> background,post;
    std::function<size_t(Lease)> bytes;
    std::function<size_t()> capacity,budget;
    std::function<bool(Lease,TraceArchive&,std::string&)> snapshot;
    std::function<void(Lease,const std::string&)> error;
    std::function<void(int)> settled;
    std::function<void()> wake;
  };
  explicit TraceLiveRaw(Hooks hooks):hooks_(std::move(hooks)){}
  ~TraceLiveRaw(){Clear();}
  void Open(Lease lease,std::string name){if(hooks_.directory.empty())return;Abort(lease.context);auto record=std::make_shared<Record>();record->lease=lease;record->name=std::move(name);records_[lease.context]=std::move(record);Dirty(lease.context);}
  void Pause(int context){if(const auto found=records_.find(context);found!=records_.end())found->second->paused=true;}
  bool Has(int context)const{return records_.contains(context);}
  void Dirty(int context){const auto found=records_.find(context);if(found==records_.end())return;auto& record=*found->second;if(record.failed)return;if(!record.dirty){record.dirty=true;record.due=hooks_.now()+kWindow;}if(hooks_.wake)hooks_.wake();}
  bool Busy(int context)const{for(const auto& [id,job]:jobs_)if(job->record->lease.context==context)return true;return false;}
  State Take(int context){if(Busy(context))return {};const auto found=records_.find(context);if(found==records_.end())return {};auto state=found->second->state;records_.erase(found);return state;}
  void Abort(int context){records_.erase(context);for(const auto& [id,job]:jobs_)if(job->record->lease.context==context){job->canceled=true;}}
  void Clear(){records_.clear();for(const auto& [id,job]:jobs_){job->canceled=true;}}
  size_t PhysicalCount()const{return jobs_.size();}
  size_t Bytes()const{return bytes_;}
  bool NeedsTick()const{if(!jobs_.empty())return true;for(const auto& [context,record]:records_)if(record->dirty&&!record->paused&&!record->failed)return true;return false;}
  void Tick(double now){
    if(ticking_){return;}
    struct Guard{bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(ticking_);
    for(const auto& [id,job]:jobs_)if(!job->completed.load(std::memory_order_acquire)&&!job->canceled&&now>=job->deadline){job->canceled=true;const auto found=records_.find(job->record->lease.context);if(found!=records_.end()&&found->second==job->record){found->second->failed=true;found->second->dirty=false;hooks_.error(job->record->lease,"Native live trace writer timed out; awaiting actual physical settlement");}}
    std::vector<uint64_t> complete;for(const auto& [id,job]:jobs_)if(job->completed.load(std::memory_order_acquire))complete.push_back(id);
    for(const auto id:complete){const auto found=jobs_.find(id);if(found==jobs_.end())continue;auto job=found->second;bytes_-=job->bytes;jobs_.erase(found);const auto current=records_.find(job->record->lease.context);if(current!=records_.end()&&current->second==job->record&&!job->canceled&&!job->ok){current->second->dirty=false;current->second->failed=true;if(hooks_.error)hooks_.error(job->record->lease,job->error);}if(hooks_.settled)hooks_.settled(job->record->lease.context);}
    for(const auto& [context,record]:records_){
      if(jobs_.size()>=(hooks_.capacity?std::min(kPhysical,hooks_.capacity()):kPhysical))break;
      if(record->failed||record->paused||!record->dirty||record->due>now||Busy(context))continue;
      const auto estimate=hooks_.bytes(record->lease);if(estimate>kBytes){record->failed=true;record->dirty=false;hooks_.error(record->lease,"Native live trace snapshot capacity exceeded");continue;}const auto budget=hooks_.budget?std::min(kBytes,hooks_.budget()):kBytes;if(bytes_>budget||estimate>budget-bytes_)continue;
      TraceArchive archive;std::string error;if(!hooks_.snapshot(record->lease,archive,error)){record->failed=true;record->dirty=false;hooks_.error(record->lease,error);continue;}
      if(archive.Bytes()>budget-bytes_){record->due=now+kWindow;continue;}
      auto job=std::make_shared<Job>();job->record=record;job->bytes=archive.Bytes();job->deadline=now+30000;if(generation_>=9007199254740991ULL){record->failed=true;hooks_.error(record->lease,"Native live trace writer identity exceeded");continue;}job->expires=std::chrono::steady_clock::now()+std::chrono::seconds(30);const auto id=++generation_;jobs_[id]=job;bytes_+=job->bytes;record->dirty=false;
      const auto weak=weak_from_this();const auto post=hooks_.post;const auto directory=hooks_.directory;
      auto work=[weak,post,directory,job,archive=std::move(archive)]()mutable{
        try{TraceRawFiles writer;job->ok=writer.PrepareLive(archive,directory,job->record->name,job->canceled,job->record->state,job->error);{if(job->canceled){job->ok=false;job->error="Native live trace operation canceled";}else if(job->ok)job->ok=writer.Commit(job->error,&job->canceled,[job]{return std::chrono::steady_clock::now()<job->expires;});}}
        catch(const std::exception& exception){job->ok=false;job->error="Native live trace writer failed: "+std::string(exception.what());}
        job->completed.store(true,std::memory_order_release);
        try{if(post)post([weak]{if(const auto self=weak.lock()){self->Tick(self->hooks_.now());if(self->hooks_.wake)self->hooks_.wake();}});}catch(...){/* Tick retains actual completion on rejected owner posts. */}
      };
      bool accepted=false;try{accepted=hooks_.background&&hooks_.background(std::move(work));}catch(...){accepted=false;}
      if(!accepted){bytes_-=job->bytes;jobs_.erase(id);record->dirty=true;record->due=now+kWindow;}
    }
  }
 private:
  struct Record {Lease lease;std::string name;State state=std::make_shared<TraceRawFiles::Live>();double due=0;bool dirty=false,failed=false,paused=false;};
  struct Job {std::shared_ptr<Record> record;size_t bytes=0;double deadline=0;std::chrono::steady_clock::time_point expires;std::atomic_bool canceled=false,completed=false;bool ok=false;std::string error;};
  Hooks hooks_;std::map<int,std::shared_ptr<Record>> records_;std::map<uint64_t,std::shared_ptr<Job>> jobs_;uint64_t generation_=0;size_t bytes_=0;bool ticking_=false;
};
} // namespace suitecut
