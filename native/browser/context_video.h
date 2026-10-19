// SPDX-License-Identifier: MIT
#pragma once
#include "include/cef_values.h"
#include "media_pipeline.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace suitecut {
class ContextVideoDirectory;
class ContextVideoFile;
// Engine UI-thread owner; capture/FFmpeg remain RecordingControls-owned. This
// registry retains real terminal receipts beyond page/backend metadata lifetime.
class NativeContextVideo:public std::enable_shared_from_this<NativeContextVideo>{
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Descriptor{std::string id;};
  struct Preparation;
  using Prepared=std::shared_ptr<Preparation>;
  using PrepareReply=std::function<void(Prepared,const std::string&)>;
  using Reply=std::function<void(bool,const std::string&,const std::string&)>;
  struct Hooks{
    std::function<bool(std::function<void()>)>background,owner;
    std::function<double()>now;
    std::function<void()>wake;
    std::function<void(int)>settled;
    std::function<std::string(int,int,MediaPipelineOptions)>start;
  };
  explicit NativeContextVideo(Hooks hooks);
  ~NativeContextVideo();
  // A nonempty callback receives every rejection, including false returns.
  // The bool describes admission only; callers must not complete twice.
  bool Prepare(int owner,uint64_t generation,Dict options,double deadline,PrepareReply reply);
  bool Configure(int context,Prepared prepared,std::string&error);
  void Discard(Prepared prepared);
  bool Configured(int context)const;
  bool PageCreated(int page,int context,MediaPipelineOptions actual,std::optional<Descriptor>&video,std::string&error);
  std::optional<Descriptor>Metadata(int page)const;
  void Observe(int page,const std::string&id,const std::string&phase,const MediaPipelineResult&result);
  void Handle(int owner,uint64_t generation,int context,Dict params,double deadline,Reply reply);
  void Cancel(int owner,uint64_t generation);
  void Removed(int context);
  void Tick();void Clear();
  size_t Physical()const;
  bool ContextReady(int context)const;
  bool NeedsTick()const;
  std::string Error(int context)const;
 private:
  using Key=std::pair<int,uint64_t>;
  struct Record;
  struct Wait;
  struct Work;
  bool Submit(std::shared_ptr<Work>work,std::function<void()>body);
  void Finish(const std::shared_ptr<Work>&work);
  void Pump(const std::shared_ptr<Record>&record);
  void EndWait(Key key,bool ok,const std::string&path,const std::string&error);
  double IODeadline(double original)const;
  void RememberError(int context,const std::string&error);
  Hooks hooks_;std::map<int,Prepared>configs_;std::map<uint64_t,Prepared>prepared_;
  std::map<std::string,std::shared_ptr<Record>>records_;std::map<int,std::string>pages_;
  std::map<int,std::string>errors_;
  std::map<std::string,int>deleted_;std::deque<std::string>deleted_order_;
  std::map<Key,std::shared_ptr<Wait>>waits_;std::map<uint64_t,std::shared_ptr<Work>>work_;
  uint64_t serial_=0;bool cleared_=false,pumping_=false;
};
}
