// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "har_files.h"
#include "network_archives.h"
namespace suitecut {
// A public recorder owns admission separately from physical body callbacks and
// file work. Browser/context close can await the real file commit after every
// actual response owner has detached; cancellation never retires a running job.
class NativeHARRecording:public std::enable_shared_from_this<NativeHARRecording> {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Hooks {
    std::function<bool(std::function<void()>)> background,owner;
    std::function<double()> now;
    std::function<void(int)> settled;
    std::function<void()> wake;
    std::function<bool()> available;
  };
  NativeHARRecording(NativeNetworkArchives& archives,Hooks hooks):archives_(archives),hooks_(std::move(hooks)){}
  ~NativeHARRecording(){Clear();}
  bool Configure(int context,Dict options,std::string& error);
  void BeforeClose(int context,double original_deadline,std::function<void()> close_owners);
  void BeforePageClose(int context,const std::string&owner,double original_deadline,std::function<void()> close);
  bool ContextReady(int context);
  std::string Error(int context)const;
  void Removed(int context);void Tick();void Clear();
  size_t Physical()const{return work_.size();}
  bool NeedsTick()const;
 private:
  struct Config {std::string path;NativeHARFiles::Content content=NativeHARFiles::Content::Embed;uint64_t generation=0;bool minimal=false,zipped=false,sealed=false,owners_closing=false,writing=false,done=false;double deadline=0;std::string error;std::vector<std::function<void()>> close;};
  struct PageClose {int context;std::set<uint64_t>bodies;double deadline;std::function<void()>close;};
  struct Work;
  void BeginWrite(int context);void Finish(uint64_t serial);
  NativeNetworkArchives&archives_;Hooks hooks_;std::map<int,Config> configs_;
  std::map<uint64_t,std::shared_ptr<Work>>work_;std::vector<PageClose>page_closes_;uint64_t serial_=0,generation_=0;bool cleared_=false;
};
}
