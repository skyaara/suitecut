// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "include/cef_values.h"
#include "trace_journal.h"

namespace suitecut {
// Native CPU paints are sampled and encoded independently of recording cadence.
// Queued work owns its copied pixels and original context/chunk identity.
class TracePreview : public std::enable_shared_from_this<TracePreview> {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Hooks {
    std::function<bool(std::function<void()>)> background;
    std::function<bool(std::function<void()>)> owner;
    std::function<void(TraceJournal::Lease,Dict,const std::string&,std::vector<uint8_t>)> captured;
    std::function<void(TraceJournal::Lease,const std::string&)> error;
    std::function<void()> wake;
  };
  explicit TracePreview(Hooks hooks):hooks_(std::move(hooks)){}
  bool Paint(TraceJournal::Lease lease,int page,int css_width,int css_height,double now,
             const void* bgra,int pixel_width,int pixel_height);
  void Tick();
  void PageClosed(int page);
  void Clear();
  size_t PhysicalCount() const{return pending_.size();}
  bool HasPendingForContext(int context) const;
 private:
  struct Pending;
  void Finish(uint64_t id);
  Hooks hooks_;
  std::map<uint64_t,std::shared_ptr<Pending>> pending_;
  struct Last {uint64_t session,chunk;double time;};
  std::map<int,Last> last_;
  uint64_t generation_=0;
};
} // namespace suitecut
