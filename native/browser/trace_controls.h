// SPDX-License-Identifier: MIT
#pragma once
#include "locator_handler_async_work.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <set>
#include "include/cef_values.h"
#include "trace_journal.h"
#include "trace_live_raw.h"

namespace suitecut {
// Engine owns these context-scoped controls. Source callbacks carry immutable
// native chunk leases, so late renderer work cannot enter a replacement chunk.
class TraceControls : public std::enable_shared_from_this<TraceControls> {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  struct Hooks {
    bool action_png=false,aria=false;
    std::string raw_directory,raw_prefix="browser";
    std::function<double()> now;
    std::function<Dict(int)> context_options;
    std::function<void(int)> request_capture;
    std::function<bool(TraceJournal::Lease,bool,std::string&)> begin_session;
    std::function<void(TraceJournal::Lease)> seal_session;
    std::function<void(TraceJournal::Lease)> end_session;
    std::function<bool(int)> pending_capture;
    std::function<void(TraceJournal::Lease,const std::string&)> source;
    std::function<void()> wake;
    std::function<void(int)> settled_context;
    // Posts an actual owner-thread completion; failed posts are drained by Tick.
    std::function<bool(std::function<void()>)> post,background;
  };
  explicit TraceControls(Hooks hooks);
  ~TraceControls();
  void Handle(int owner,int context,Dict params,double deadline,Reply reply);
  void Cancel(int owner);
  void Tick(double now);
  void Closed(int context);
  void Clear();
  bool ContextReady(int context) const;
  size_t PhysicalCount() const{return physical_+(live_?live_->PhysicalCount():0);}
  bool NeedsTick() const{return physical_||!terminal_.empty()||(live_&&live_->NeedsTick());}
  void Before(int context,uint64_t call,Dict info);
  void After(int context,uint64_t call,Dict info);
  TraceJournal::Lease CaptureLease(int context) const;
  TraceJournal::Lease SessionLease(int context) const{return journal_.SessionLease(context);}
  bool ChunkCurrent(TraceJournal::Lease lease) const{return journal_.Current(lease);}
  uint64_t NextCall(){return next_group_>=9007199254740991ULL?0:++next_group_;}
  bool SessionCurrent(TraceJournal::Lease lease) const {const auto current=journal_.SessionLease(lease.context);return lease.context>0&&current.session==lease.session;}
  void Captured(TraceJournal::Lease lease,Dict event,const std::string& file,std::vector<uint8_t> bytes);
  void DOMCaptured(TraceJournal::Lease lease,const std::string& event);
  void WebSocketCaptured(TraceJournal::Lease lease,const std::string& event);
  void ConsoleCaptured(TraceJournal::Lease lease,const std::string& event){DOMCaptured(lease,event);}
  void Network(int context,Dict resource);
  void Network(TraceJournal::Lease lease,Dict resource){if(SessionCurrent(lease))Network(lease.context,resource);}
  bool Resource(TraceJournal::Lease lease,const std::string& name,std::vector<uint8_t> bytes){return SessionCurrent(lease)&&Resource(lease.context,name,std::move(bytes));}
  // Source files belong only to the exact chunk that observed the caller.
  // Network body resources deliberately use the separate inherited path.
  bool SourceResource(TraceJournal::Lease lease,const std::string& name,std::vector<uint8_t> bytes){if(!ChunkCurrent(lease))return false;std::string error;const bool result=journal_.Resource(lease.context,name,std::move(bytes),false,error);if(!result)Error(lease.context,error);else Dirty(lease.context);return result;}
  bool Resource(int context,const std::string& name,std::vector<uint8_t> bytes);
  const TraceJournal::Options* Configuration(int context) const;
  void Diagnostics(Dict output) const;
  void CaptureError(int context,const std::string& error){Error(context,error);}
  void CaptureError(TraceJournal::Lease lease,const std::string& error){if(ChunkCurrent(lease))Error(lease.context,error);}
 private:
  struct Pending;
  struct Terminal {int context;Dict config;double deadline;Reply reply;NativeWorkContinuation origin;};
  void Finish(int owner,uint64_t generation,bool ok,const std::string& error,Dict result=nullptr);
  void Save(int owner,int context,double deadline,TraceArchive archive,const std::string& path,const std::string& raw_name,Reply reply,TraceLiveRaw::State live_state={});
  void Dirty(int context){if(live_)live_->Dirty(context);}
  void Error(int context,const std::string& error);
  void RecordStack(TraceJournal::Lease lease,uint64_t call,Dict info);
  void EndGroups(int context);
  Hooks hooks_;TraceJournal journal_;std::shared_ptr<TraceLiveRaw> live_;
  std::map<int,std::string> errors_,chunk_names_;
  std::map<uint64_t,TraceJournal::Lease> calls_;
  std::map<int,std::shared_ptr<Pending>> pending_;
  std::map<int,Terminal> terminal_;
  std::set<int> stopping_;
  uint64_t next_group_=0,generation_=0;
  size_t physical_=0,archive_bytes_=0;
  std::map<std::string,int> paths_;
};
} // namespace suitecut
