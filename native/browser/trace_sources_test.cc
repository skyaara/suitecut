// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include "trace_sources.h"
int main(){
  std::vector<std::function<void()>> background,owner;int captures=0,errors=0;std::string recorded;
  const auto directory=std::filesystem::temp_directory_path()/std::string("suitecut-trace-sources-"+std::to_string(getpid()));std::filesystem::create_directory(directory);const auto file=(directory/"actual.ts").string();{std::ofstream output(file);output<<"actual native source bytes";}
  suitecut::TraceSources::Hooks hooks;hooks.background=[&](std::function<void()> task){background.push_back(std::move(task));return true;};hooks.owner=[&](std::function<void()> task){owner.push_back(std::move(task));return true;};hooks.captured=[&](suitecut::TraceJournal::Lease lease,const std::string& name,std::vector<uint8_t> bytes){assert(lease.context==1&&lease.session==2);assert(name.starts_with("src/")&&name.ends_with(".ts")&&name.size()==47);recorded.assign(bytes.begin(),bytes.end());++captures;};hooks.error=[&](suitecut::TraceJournal::Lease,const std::string&){++errors;};
  const auto sources=std::make_shared<suitecut::TraceSources>(hooks);assert(sources->Capture({1,2,3},file));assert(sources->Capture({1,2,3},file));assert(sources->PhysicalCount()==1&&sources->Pending(1));background[0]();assert(sources->PhysicalCount()==1&&captures==0);owner[0]();assert(captures==1&&recorded=="actual native source bytes"&&!sources->PhysicalCount());{std::ofstream changed(file);changed<<"actual second chunk source bytes";}assert(sources->Capture({1,2,4},file)&&background.size()==2);background[1]();owner[1]();assert(captures==2&&recorded=="actual second chunk source bytes");
  const auto missing=(directory/"missing.ts").string();assert(sources->Capture({1,2,4},missing));background[2]();owner[2]();assert(errors==1&&captures==2);
  const auto fifo=(directory/"pipe.ts").string();assert(mkfifo(fifo.c_str(),0600)==0);assert(sources->Capture({1,2,4},fifo));background[3]();owner[3]();assert(errors==2&&captures==2&&!sources->PhysicalCount());
  assert(sources->Capture({1,3,1},file));sources->Closed(1);assert(sources->PhysicalCount()==1);background[4]();owner[4]();assert(!sources->PhysicalCount()&&captures==2);
  hooks.owner=[](std::function<void()>){return false;};const auto fallback=std::make_shared<suitecut::TraceSources>(hooks);assert(fallback->Capture({1,2,4},file));background[5]();assert(fallback->PhysicalCount()==1);fallback->Tick();assert(captures==3&&!fallback->PhysicalCount());
  assert(sources->Capture({1,2,5},file)&&sources->Capture({1,2,6},file));assert(sources->PhysicalCount()==2&&background.size()==8);const auto owner_count=owner.size();background[6]();assert(owner.size()==owner_count+1);owner.back()();assert(captures==4);assert(sources->Capture({1,2,6},file)&&background.size()==8);background[7]();assert(owner.size()==owner_count+2);owner.back()();assert(captures==5&&!sources->PhysicalCount());assert(sources->Capture({1,2,6},file)&&background.size()==8);std::filesystem::remove_all(directory);
}
