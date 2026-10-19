// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <set>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>
namespace suitecut::runner {
// All observations use the native coordinator's single monotonic clock. These
// are actual admission/packet/waitpid observations, not author-supplied times.
struct DirectoryHostTiming {
  unsigned worker_index=0;int64_t pid=0;double admitted=0;
  std::optional<double> metadata,close_started,captured;
};
struct DirectoryActorTiming {
  double admitted=0;std::optional<double> completed;
  std::vector<DirectoryHostTiming> hosts;
  explicit DirectoryActorTiming(double now):admitted(now){Time(now);}
  void HostStarted(unsigned worker,int64_t pid,double now){Time(now);if(completed||!worker||pid<=0||now<admitted||hosts.size()>=8192||Find(worker,pid))throw std::invalid_argument("Native directory timing host admission rejected");hosts.push_back({worker,pid,now,std::nullopt,std::nullopt,std::nullopt});}
  void Metadata(unsigned worker,int64_t pid,double now){auto& h=Host(worker,pid,now);if(h.captured)throw std::invalid_argument("Native directory metadata observed after host capture");if(!h.metadata)h.metadata=now;}
  void Closing(unsigned worker,int64_t pid,double now){auto& h=Host(worker,pid,now);if(h.captured)return;if(!h.close_started)h.close_started=now;}
  void Captured(unsigned worker,int64_t pid,double now){auto& h=Host(worker,pid,now);if(h.captured)throw std::invalid_argument("Native directory timing duplicate waitpid capture");h.captured=now;}
  void Complete(double now){Time(now);if(completed||now<admitted)throw std::invalid_argument("Native directory timing completion rejected");for(const auto& h:hosts)if(!h.captured||now<*h.captured)throw std::invalid_argument("Native directory timing host lacks actual capture");completed=now;}
  void Validate()const{
    Time(admitted);if(!completed||!std::isfinite(*completed)||*completed<admitted||hosts.size()>8192)throw std::invalid_argument("Native directory incomplete timing observation");
    std::set<std::pair<unsigned,int64_t>> seen;
    for(const auto& h:hosts){if(!h.worker_index||h.pid<=0||!seen.emplace(h.worker_index,h.pid).second||!std::isfinite(h.admitted)||h.admitted<admitted||!h.captured||!std::isfinite(*h.captured)||*h.captured<h.admitted||*h.captured>*completed||(h.metadata&&(!std::isfinite(*h.metadata)||*h.metadata<h.admitted||*h.metadata>*h.captured))||(h.close_started&&(!std::isfinite(*h.close_started)||*h.close_started<h.admitted||*h.close_started>*h.captured)))throw std::invalid_argument("Native directory timing receipt order/identity rejected");}
  }

 private:
  static void Time(double now){if(!std::isfinite(now)||now<=0)throw std::invalid_argument("Native directory timing clock rejected");}
  DirectoryHostTiming* Find(unsigned worker,int64_t pid){for(auto& h:hosts)if(h.worker_index==worker&&h.pid==pid)return &h;return nullptr;}
  DirectoryHostTiming& Host(unsigned worker,int64_t pid,double now){Time(now);auto* h=Find(worker,pid);if(completed||!h||now<h->admitted||(h->metadata&&now<*h->metadata)||(h->close_started&&now<*h->close_started))throw std::invalid_argument("Native directory timing stale/foreign host observation");return *h;}
};
}
