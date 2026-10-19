// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include "har_files.h"
#include "har_replay.h"
namespace suitecut {
class NativeHARReplayFiles:public std::enable_shared_from_this<NativeHARReplayFiles> {
 public:
  using Replay=std::shared_ptr<const NativeHARReplay>;
  using Done=std::function<void(Replay,const std::string&)>;
  struct Hooks {std::function<bool(std::function<void()>)>background,owner;std::function<double()>now;std::function<void()>wake;};
  explicit NativeHARReplayFiles(Hooks hooks):hooks_(std::move(hooks)){}
  ~NativeHARReplayFiles(){Clear();}
  bool Read(int owner,const std::string&path,double original_deadline,Done done,bool allow_empty_log=false);
  void Cancel(int owner);void Tick();void Clear();
  size_t Physical()const{return work_.size();}
  static size_t RetainedBytes();
 private:
  struct Work;void Finish(int owner);
  Hooks hooks_;std::map<int,std::shared_ptr<Work>>work_;bool cleared_=false;
};
}
