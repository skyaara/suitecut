// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include "include/cef_values.h"
#include "screenshot_pixels.h"
#include "screenshot_inputs.h"
#include "screenshot_artifact_files.h"
namespace suitecut {
// Engine-owned logical assertions and real CPU jobs. Actual screenshot work
// stays in Source's physical capture latch, including after logical cancel.
class ScreenshotComparisons:public std::enable_shared_from_this<ScreenshotComparisons>{
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  using Capture=std::function<void(uint64_t,Dict)>;
  // Source-issued native callback capability. Default raw comparison callers
  // retain the existing path; public screenshot assertions supply this owner.
  enum class AssertionWork {Pixels,Validation,Files};
  struct AssertionObserver {
    std::function<bool(uint64_t,uint64_t,AssertionWork)> admit;
    std::function<bool(uint64_t)> submitting;
    std::function<void(uint64_t,bool,bool,const std::string&)> settled;
    std::function<bool(uint64_t)> observed;
    std::function<void(uint64_t,Dict)> unobserved;
  };
  using ArtifactReply=std::function<void(std::shared_ptr<const ScreenshotArtifactPublication>)>;
  struct Hooks{
    std::function<double()> now;
    std::function<bool(std::function<void()>)> background,post;
    std::function<void()> wake;
    std::function<void(int)> settled_context;
    std::function<bool()> available,physical_available;
  };
  explicit ScreenshotComparisons(Hooks hooks);
  ~ScreenshotComparisons();
  void Handle(int context,int page,int owner,Dict config,double deadline,Capture capture,Reply reply,std::shared_ptr<AssertionObserver> assertion={});
  void HandlePrepared(int context,int page,int owner,Dict config,std::shared_ptr<ScreenshotInputs::Data> input,double deadline,Capture capture,Reply reply,std::shared_ptr<AssertionObserver> assertion={});
  std::shared_ptr<ScreenshotByteBudget> Budget()const{return budget_;}
  // Cleanup-only validation is linked by Source to an exact admitted capture.
  // It never compares pixels or publishes a Received observation.
  bool ValidateCaptured(int context,int page,int owner,uint64_t sample,Dict result,double cleanup_deadline,std::shared_ptr<AssertionObserver> assertion,std::string&error);
  bool WriteArtifacts(int context,int page,int owner,const std::string&root,const std::string&actual,const std::string&diff,double cleanup_deadline,std::shared_ptr<AssertionObserver> assertion,ArtifactReply reply,std::string&error);
  bool Captured(int page,int owner,uint64_t sample,bool ok,const std::string& error,Dict result);
  bool Contains(int page,int owner)const;
  void Cancel(int page,int owner);
  void PageClosed(int page);
  void Deadline(int page,int owner);
  void Tick();
  size_t PhysicalCount()const{return pending_.size();}
  size_t LogicalCount()const{return records_.size()+finalizations_.size();}
  bool ContextReady(int context)const;
  size_t Bytes()const{return budget_->Used();}
  bool NeedsTick()const{return !records_.empty()||!pending_.empty()||!finalizations_.empty();}
 private:
  using Key=std::pair<int,int>;
  struct Encoded;
  struct Record;
  struct Pending;
  struct Finalization;
  bool QueueFinalization(const std::shared_ptr<Finalization>&work,std::string&error);
  void FinalizeNext(const std::shared_ptr<Finalization>&work);
  void RetireFinalization(const std::shared_ptr<Finalization>&work,const std::string&error);
  void Begin(int context,int page,int owner,Dict config,std::shared_ptr<ScreenshotInputs::Data> input,double deadline,Capture capture,Reply reply,std::shared_ptr<AssertionObserver> assertion={});
  void CaptureNext(const std::shared_ptr<Record>& record);
  void Compute(const std::shared_ptr<Record>& record);
  void FinishPhysical(uint64_t generation);
  void Finish(std::shared_ptr<Record> record,bool passed,bool timed_out,const std::string& error={});
  void Fail(const std::shared_ptr<Record>& record,const std::string& error);
  bool Current(const std::shared_ptr<Record>& record)const;
  std::shared_ptr<Encoded> Input(const std::string& value,std::string& error);
  Hooks hooks_;
  std::shared_ptr<ScreenshotByteBudget> budget_;
  std::map<Key,std::shared_ptr<Record>> records_;
  std::map<uint64_t,std::shared_ptr<Pending>> pending_;
  std::map<uint64_t,std::shared_ptr<Finalization>> finalizations_;
  uint64_t generation_=0,job_generation_=0,sample_generation_=0;
};
} // namespace suitecut
