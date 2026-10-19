// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "trace_journal.h"
namespace suitecut {
class TraceSources : public std::enable_shared_from_this<TraceSources> {
 public:
  struct Hooks {
    std::function<bool(std::function<void()>)> background,owner;
    std::function<void(TraceJournal::Lease,const std::string&,std::vector<uint8_t>)> captured;
    std::function<void(TraceJournal::Lease,const std::string&)> error;
    std::function<void()> wake;
  };
  explicit TraceSources(Hooks hooks):hooks_(std::move(hooks)){}
  bool Capture(TraceJournal::Lease lease,const std::string& path);
  bool Pending(int context) const;
  size_t PhysicalCount() const{return pending_.size();}
  void Tick();void Closed(int context);void Clear();
 private:
  struct Work;struct Cache {uint64_t session=0,chunk=0;std::set<std::string> files;};
  void Finish(uint64_t id);
  Hooks hooks_;std::map<uint64_t,std::shared_ptr<Work>> pending_;std::map<int,Cache> cache_;uint64_t next_=0;
};
} // namespace suitecut
