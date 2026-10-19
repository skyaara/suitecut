// SPDX-License-Identifier: MIT
#pragma once
#include "worker_reuse_pool.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/syscall.h>
namespace suitecut::runner {
// Read-only kernel observation. This neither becomes a subreaper nor calls
// waitpid/kill; the single existing descendant domain remains the exit owner.
inline WorkerReusePool::Inventory ObserveWorkerReuseInventory(const WorkerReusePool::Probe& probe){
  if(syscall(SYS_gettid)!=getpid()||!probe.version||!probe.count||probe.count>WorkerReusePool::kHosts)
    throw std::invalid_argument("Native reuse inventory requires bounded main-thread host ownership");
  for(size_t i=0;i<probe.count;++i){const auto& host=probe.hosts[i];
    if(!host.lease||!host.worker||host.pid<=0||host.pid>INT32_MAX)throw std::invalid_argument("Native reuse inventory host identity rejected");
    for(size_t j=0;j<i;++j)if(probe.hosts[j].pid==host.pid||probe.hosts[j].lease==host.lease||probe.hosts[j].worker==host.worker)
      throw std::invalid_argument("Native reuse inventory duplicate host owner");
  }
  WorkerReusePool::Inventory result;result.probe=probe;result.children_clear=true;
  std::array<bool,WorkerReusePool::kHosts> seen{};size_t inspected=0;
  for(const auto& entry:std::filesystem::directory_iterator("/proc")){
    const auto name=entry.path().filename().string();
    if(name.empty()||name.find_first_not_of("0123456789")!=std::string::npos)continue;
    if(++inspected>65536)throw std::runtime_error("Native reuse process inventory capacity65536 exceeded");
    std::ifstream stream(entry.path()/"stat");
    if(!stream){if(!std::filesystem::exists(entry.path()))continue;throw std::runtime_error("Native reuse process observation unavailable");}
    std::string stat;std::getline(stream,stat);const auto close=stat.rfind(')');
    if(close==std::string::npos){if(!std::filesystem::exists(entry.path()))continue;throw std::runtime_error("Native reuse process observation malformed");}
    std::istringstream fields(stat.substr(close+1));char state;int64_t parent;
    if(!(fields>>state>>parent))throw std::runtime_error("Native reuse process parent observation malformed");
    const auto pid=std::stoll(name);bool known=false;
    for(size_t i=0;i<probe.count;++i){const auto& host=probe.hosts[i];
      if(pid==host.pid){known=true;seen[i]=parent==getpid()&&state!='Z'&&state!='X';}
      if(parent==host.pid)result.children_clear=false;
    }
    if(parent==getpid()&&!known)result.children_clear=false;
  }
  result.complete=std::all_of(seen.begin(),seen.begin()+static_cast<std::ptrdiff_t>(probe.count),[](bool found){return found;});
  result.observed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
  return result;
}
}  // namespace suitecut::runner
