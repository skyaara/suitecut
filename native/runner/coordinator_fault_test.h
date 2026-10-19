// SPDX-License-Identifier: MIT
#pragma once
// Compiled only into the dedicated authored fault binary, never the production
// executable. Injects at a genuine coordinator operation with real owners live.
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <time.h>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <unistd.h>
namespace suitecut::runner::coordinator_fault_test {
inline void Hook(const char* phase,int64_t host,size_t workers,size_t files){
  static bool fired=false;const auto* target=std::getenv("SUITECUT_COORDINATOR_FAULT_PHASE");if(fired||!target||std::strcmp(phase,target))return;
  const auto* ready=std::getenv("SUITECUT_COORDINATOR_FAULT_READY");if(ready&&::access(ready,F_OK))return;
  const auto* trace=std::getenv("SUITECUT_COORDINATOR_FAULT_TRACE");if(!trace)throw std::runtime_error("Missing authored coordinator trace path");
  timespec now{};if(clock_gettime(CLOCK_MONOTONIC,&now))throw std::runtime_error("Authored coordinator monotonic read failed");
  const auto line=std::string(phase)+" "+std::to_string(getpid())+" "+std::to_string(host)+" "+std::to_string(workers)+" "+std::to_string(files)+" "+std::to_string(static_cast<uint64_t>(now.tv_sec)*1000000000ULL+static_cast<uint64_t>(now.tv_nsec))+"\n";
  const int fd=::open(trace,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(fd<0)throw std::runtime_error("Authored coordinator trace creation failed");
  const auto bytes=::write(fd,line.data(),line.size());::close(fd);if(bytes!=static_cast<ssize_t>(line.size()))throw std::runtime_error("Authored coordinator trace write failed");fired=true;
  if(!std::strcmp(phase,"actor-staging")||!std::strcmp(phase,"descriptor-allocation"))throw std::bad_alloc();
  throw std::runtime_error(std::string("Injected native coordinator ")+phase+" failure");
}
}
#define SUITECUT_NATIVE_COORDINATOR_TEST_HOOK(phase,pid,workers,files) ::suitecut::runner::coordinator_fault_test::Hook(phase,pid,workers,files)
