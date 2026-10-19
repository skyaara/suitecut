// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iostream>
#include <deque>
#include "trace_live_raw.h"
namespace {std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);assert(file);return {std::istreambuf_iterator<char>(file),{}};}}
int main(){
  using namespace suitecut;
  const auto directory=std::filesystem::temp_directory_path()/("suitecut-live-raw-"+std::to_string(getpid()));std::filesystem::remove_all(directory);
  TraceJournal journal;TraceJournal::Options options;options.dom=true;std::string error;double now=0;std::deque<std::function<void()>> background,posted;size_t snapshots=0,failures=0,settled=0;
  TraceLiveRaw::Hooks hooks;hooks.directory=directory.string();hooks.now=[&]{return now;};hooks.background=[&](auto work){background.push_back(std::move(work));return true;};hooks.post=[&](auto work){posted.push_back(std::move(work));return false;};hooks.wake=[]{};hooks.bytes=[&](auto lease){return journal.SnapshotBytes(lease);};hooks.snapshot=[&](auto lease,TraceArchive& archive,std::string& why){++snapshots;return journal.Snapshot(lease,archive,why);};hooks.error=[&](auto,const auto&){++failures;};hooks.settled=[&](int){++settled;};auto live=std::make_shared<TraceLiveRaw>(hooks);
  auto run=[&]{assert(!background.empty());auto work=std::move(background.front());background.pop_front();work();live->Tick(now);posted.clear();};
  assert(journal.Start(1,options,"{\"type\":\"context-options\"}",error));const auto first=journal.Current(1);live->Open(first,"actual");
  // First-dirty deadline does not slide under continuous events. Until the
  // physical worker starts the real filesystem has no invented output.
  now=99;assert(journal.Event(first,"{\"type\":\"console\",\"text\":\"idle-one\"}",error));live->Dirty(1);live->Tick(now);assert(background.empty());now=100;live->Tick(now);assert(snapshots==1&&live->PhysicalCount()==1&&!std::filesystem::exists(directory/"actual.trace"));
  // A thousand events while busy own one dirty bit, not a thousand archives.
  for(int i=0;i<1000;++i){assert(journal.Event(first,"{\"type\":\"console\",\"text\":\"idle-two\"}",error));live->Dirty(1);}now=200;live->Tick(now);assert(snapshots==1&&background.size()==1);run();assert(background.size()==1&&snapshots==2&&Read(directory/"actual.trace").find("idle-one")!=std::string::npos);run();assert(live->PhysicalCount()==0&&Read(directory/"actual.trace").find("idle-two")!=std::string::npos);
  // Resource bytes precede real references, and later snapshots keep one
  // namespace while preserving literal isolated UTF16 and author tag text.
  assert(journal.Resource(1,"resources/body.dat",{0,255,1,0},true,error));assert(journal.Network(1,"{\"type\":\"resource-snapshot\",\"snapshot\":{\"response\":{\"content\":{\"_file\":\"resources/body.dat\"}}}}",error));assert(journal.Event(first,"{\"type\":\"frame-snapshot\",\"html\":[\"\\ud800\",{\"__suitecutNativeUTF16Scalar\":\"literal\"}]}",error));live->Dirty(1);now=300;live->Tick(now);run();const auto network=Read(directory/"actual.network");const auto marker=network.find("trace-resources-");assert(marker!=std::string::npos);const auto end=network.find('"',marker);const auto link=network.substr(marker,end-marker);assert(Read(directory/link)==std::string("\0\xff\1\0",4));assert(Read(directory/"actual.trace").find("\\ud800")!=std::string::npos);
  const auto inode=std::filesystem::last_write_time(directory/link);assert(journal.Event(first,"{\"type\":\"console\",\"text\":\"network-only-next\"}",error));live->Dirty(1);now=400;live->Tick(now);run();assert(Read(directory/"actual.network")==network&&std::filesystem::last_write_time(directory/link)==inode);
  // Non-mutating snapshot preserves real before/after ownership.
  assert(journal.Before(first,1,"{\"type\":\"before\",\"callId\":\"call@1\"}",error));assert(journal.Stack(first,1,{{"/actual/native-caller.cpp","actual",12,4}},error));TraceArchive view;assert(journal.Snapshot(first,view,error));assert(journal.After(first,1,"{\"type\":\"after\",\"callId\":\"call@1\"}",error));
  // Actual final writer uses the same path lease and resource namespace.
  auto state=live->Take(1);assert(state);TraceArchive final;assert(journal.StopChunk(1,final,true,error));std::atomic_bool canceled=false;{TraceRawFiles writer;assert(writer.PrepareLive(final,directory.string(),"actual",canceled,state,error));bool stack_first=false;assert(writer.Commit(error,&canceled,[&]{if(Read(directory/"actual.stacks").find("/actual/native-caller.cpp")!=std::string::npos&&Read(directory/"actual.trace").find("\"after\"")==std::string::npos)stack_first=true;return true;}));assert(stack_first);}assert(Read(directory/"actual.network")==network&&Read(directory/"actual.trace").find("\"after\"")!=std::string::npos);
  // Cancellation retains a physically queued job, rejects output, then permits
  // another actual writer only after the queued native closure really runs.
  assert(journal.StartChunk(1,"{\"type\":\"context-options\"}",error));const auto second=journal.Current(1);live->Open(second,"canceled");now=500;live->Tick(now);assert(live->Busy(1));live->Abort(1);assert(live->Busy(1)&&live->PhysicalCount()==1);run();assert(!live->Busy(1)&&!std::filesystem::exists(directory/"canceled.trace"));
  // Native job budget expires before Tick can return a late physical positive.
  live->Open(second,"expired");now=600;live->Tick(now);assert(live->PhysicalCount()==1);now=30600;live->Tick(now);assert(failures==1&&live->PhysicalCount()==1);run();assert(!std::filesystem::exists(directory/"expired.trace"));
  // An actual path error is reported once; idle retries do not allocate an
  // unbounded stream of duplicate snapshots, and true completion settles it.
  {std::ofstream file(directory/"blocker");file<<"file";}hooks.directory=(directory/"blocker/nested").string();auto bad=std::make_shared<TraceLiveRaw>(hooks);bad->Open(second,"bad");now=30700;bad->Tick(now);assert(background.size()==1);auto work=std::move(background.front());background.pop_front();work();bad->Tick(now);assert(failures==2&&bad->PhysicalCount()==0);for(int i=0;i<100;++i){bad->Dirty(1);bad->Tick(now+i*100);}assert(background.empty()&&!bad->NeedsTick());
  // Four physical snapshots total; a fifth owner retains only its dirty state.
  auto limited=std::make_shared<TraceLiveRaw>(hooks);const auto old_snapshots=snapshots;
  for(int context=10;context<15;++context){assert(journal.Start(context,options,"{\"type\":\"context-options\"}",error));limited->Open(journal.Current(context),"bounded-"+std::to_string(context));}
  now+=100;limited->Tick(now);assert(limited->PhysicalCount()==4&&background.size()==4&&snapshots==old_snapshots+4&&limited->Bytes()<=TraceLiveRaw::kBytes);limited->Clear();assert(limited->PhysicalCount()==4);while(!background.empty()){auto task=std::move(background.front());background.pop_front();task();}limited->Tick(now);assert(!limited->PhysicalCount());
  hooks.budget=[](){return size_t(0);};auto starved=std::make_shared<TraceLiveRaw>(hooks);starved->Open(second,"no-budget");now+=100;const auto before_budget=snapshots;for(int i=0;i<100;++i)starved->Tick(now+i);assert(snapshots==before_budget&&background.empty()&&!starved->PhysicalCount());starved->Clear();
  assert(settled>=7);state.reset();live->Clear();bad->Clear();for(const auto& file:std::filesystem::directory_iterator(directory))assert(!file.path().filename().string().starts_with(".suitecut-trace-"));std::filesystem::remove_all(directory);std::cout<<"Native live raw idle coalescing, exact bytes, stable namespace, cancellation, timeout and actual errors passed\n";
}
