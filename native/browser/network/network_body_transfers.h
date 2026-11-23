// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace suitecut {
// Native delivery ownership for exact request bytes. Chunks share one ordered
// channel with the metadata that names them, so a body is complete for its
// receiver once its last chunk is emitted; no receipt round trip is awaited.
// Emission must copy bytes synchronously; false applies backpressure without
// dropping the body.
class NativeNetworkBodyTransfers {
 public:
  using Emit=std::function<bool(uint64_t,size_t,size_t,std::span<const uint8_t>)>;
  using Done=std::function<void(uint64_t,bool,const std::string&)>;
  // A body of any size reaches the host in 8 MiB chunks.
  static constexpr size_t kChunkBytes=8*1048576;
  explicit NativeNetworkBodyTransfers(Emit emit):emit_(std::move(emit)){}
  std::optional<uint64_t> Begin(std::vector<uint8_t> bytes,double deadline,Done done) {
    if(!std::isfinite(deadline) || deadline<0 || !done || serial_==UINT64_MAX)return std::nullopt;
    const auto id=++serial_;auto entry=std::make_shared<Entry>();entry->bytes=std::move(bytes);entry->deadline=deadline;entry->done=std::move(done);
    byte_count_+=entry->bytes.size();entries_.emplace(id,std::move(entry));return id;
  }
  void Tick(double now) {
    std::vector<uint64_t> expired;for(const auto& [id,entry]:entries_)if(now>=entry->deadline)expired.push_back(id);
    for(const auto id:expired)Finish(id,false,"Native request body delivery deadline exceeded");
    Flush();
  }
  // Emits up to 16 chunks round-robin and returns whether any was accepted.
  bool Flush() {
    auto entries=Snapshot();
    if(entries.empty())return false;
    auto start=std::upper_bound(entries.begin(),entries.end(),last_emitted_,[](uint64_t id,const auto& item){return id<item.first;});
    std::rotate(entries.begin(),start,entries.end());
    size_t quota=16;bool any=false;
    while(quota) {
      bool progress=false;
      for(const auto& [id,entry]:entries) {
        if(!quota)break;
        if(!entries_.contains(id))continue;
        const auto offset=entry->offset,count=std::min(kChunkBytes,entry->bytes.size()-offset);
        if(!emit_(id,offset,entry->bytes.size(),std::span<const uint8_t>(entry->bytes).subspan(offset,count)))return any;
        last_emitted_=id;--quota;progress=any=true;
        if(!entries_.contains(id))continue;
        entry->offset+=count;
        if(entry->offset==entry->bytes.size())Finish(id,true,"");
      }
      if(!progress)break;
    }
    return any;
  }
  // Receipt is informational: any identity this owner issued is valid.
  bool Issued(uint64_t id) const {return id && id<=serial_;}
  bool Cancel(uint64_t id,const std::string& reason="Native request body delivery canceled") {if(!entries_.contains(id))return false;Finish(id,false,reason);return true;}
  void Clear() {entries_.clear();byte_count_=0;}
  size_t size() const {return entries_.size();}
  size_t bytes() const {return byte_count_;}
 private:
  struct Entry {std::vector<uint8_t> bytes;size_t offset=0;double deadline=0;Done done;};
  std::vector<std::pair<uint64_t,std::shared_ptr<Entry>>> Snapshot() const {return {entries_.begin(),entries_.end()};}
  void Finish(uint64_t id,bool ok,const std::string& error) {
    const auto found=entries_.find(id);if(found==entries_.end())return;
    auto entry=std::move(found->second);entries_.erase(found);byte_count_-=entry->bytes.size();
    auto done=std::exchange(entry->done, nullptr);std::vector<uint8_t>().swap(entry->bytes);done(id,ok,error);
  }
  Emit emit_;std::map<uint64_t,std::shared_ptr<Entry>> entries_;size_t byte_count_=0;uint64_t serial_=0,last_emitted_=0;
};
}  // namespace suitecut
