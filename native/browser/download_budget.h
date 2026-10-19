// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

namespace suitecut {
class DownloadQuota {
 public:
  static constexpr std::uint64_t kFileLimit=256ull*1048576;
  static constexpr std::uint64_t kTotalLimit=1024ull*1048576;
  explicit DownloadQuota(std::uint64_t limit=kTotalLimit):limit_(limit){}
  bool Reserve(std::uint64_t bytes){
    auto used=used_.load();
    do{if(bytes>limit_||used>limit_-bytes)return false;}
    while(!used_.compare_exchange_weak(used,used+bytes));
    return true;
  }
  void Release(std::uint64_t bytes){used_.fetch_sub(bytes);}
  std::uint64_t Used()const{return used_.load();}
 private:std::uint64_t limit_;std::atomic<std::uint64_t> used_{0};
};
class DownloadQuotaLease {
 public:
  explicit DownloadQuotaLease(std::shared_ptr<DownloadQuota> quota):quota_(std::move(quota)){}
  ~DownloadQuotaLease(){quota_->Release(bytes_.load());}
  bool Add(std::uint64_t bytes){
    auto current=bytes_.load();
    do{if(bytes>DownloadQuota::kFileLimit||current>DownloadQuota::kFileLimit-bytes)return false;}
    while(!bytes_.compare_exchange_weak(current,current+bytes));
    if(!quota_->Reserve(bytes)){bytes_.fetch_sub(bytes);return false;}
    return true;
  }
  bool ReduceTo(std::uint64_t bytes){
    const auto before=bytes_.load();if(bytes>before)return false;
    bytes_=bytes;if(before>bytes)quota_->Release(before-bytes);return true;
  }
  std::uint64_t Bytes()const{return bytes_.load();}
 private:std::shared_ptr<DownloadQuota> quota_;std::atomic<std::uint64_t> bytes_{0};
};
} // namespace suitecut
