// SPDX-License-Identifier: MIT
#pragma once
#include "output_retention_policy.h"
#include "attachment_files.h"
#include "host_process.h"
#include "test_attachments.h"
#include <functional>
#include <map>
#include <memory>
namespace suitecut::runner {
struct OutputRetentionFence {
 size_t hosts=0,descendants=0,raw_callbacks=0,steps=0,file_jobs=0,file_callbacks=0,publication_callbacks=0,output_preparation=0,sources=0;
 bool Retired()const{return !hosts&&!descendants&&!raw_callbacks&&!steps&&!file_jobs&&!file_callbacks&&!publication_callbacks&&!output_preparation&&!sources;}
};
// Native post-run actor. Every source record/outcome/fence comes from native
// owners, never a Node packet. Physical IO capacity lasts through actual EOF
// and waitpid even after a logical deadline or malformed/failed helper reply.
class RunnerOutputRetention {
 public:
  static constexpr size_t kRecords=8192,kMetadata=64*1024*1024;
  static constexpr double kCleanupMaximum=kOutputRetentionCleanupMaximum;
  struct Budget {size_t records=0,metadata=0;};
  struct PhysicalBudget {size_t* count=nullptr;size_t maximum=0;};
  struct Hooks {std::function<void(const std::string&)> failure;std::function<void(int64_t)> after_spawn{};};
  RunnerOutputRetention(std::string executable,Hooks hooks,Budget& budget,PhysicalBudget physical):executable_(std::move(executable)),hooks_(std::move(hooks)),budget_(budget),physical_(physical){
   if(executable_.empty()||!physical.count||!physical.maximum||physical.maximum>64)throw std::invalid_argument("Invalid native output retention actor");
  }
  void Admit(const OutputDirectoryRecord& actual,const OutputRetentionOutcome& outcome,PreserveOutput policy,double deadline,double now){
   Time(now);ValidateRetentionSource(actual,outcome);if(now<*actual.assigned)throw std::invalid_argument("Native output retention predates assignment");const bool keep=PreserveNativeOutput(policy,outcome);
   if(entries_.contains(actual.id)||budget_.records>=kRecords||(!keep&&(!std::isfinite(deadline)||deadline<=now||deadline-now>kCleanupMaximum)))throw std::invalid_argument("Native output retention duplicate/budget/deadline rejected");
   const std::filesystem::path path(actual.path);const auto parent=path.parent_path().string(),name=path.filename().string();
   if(actual.path.find('\0')!=std::string::npos||actual.path.size()>8192||!path.is_absolute()||parent.empty()||parent.size()>8192||name.empty()||name.size()>255||!name.starts_with("suitecut-output-"))throw std::invalid_argument("Native output retention source path rejected");
   (void)AttachmentPathJSON(actual.path);
   Entry entry;auto& record=entry.record;record.output_id=actual.id;record.owner=actual.owner;record.policy=policy;record.outcome=outcome;record.path=actual.path;record.decided=now;record.deadline=keep?0:deadline;record.error.reserve(4096);
   record.status=keep?OutputRetentionStatus::Kept:OutputRetentionStatus::Pending;if(keep)record.receipt=now;
   if(!keep)entry.command="REMOVE "+std::to_string(actual.id)+" "+attachment_files::Hex(parent)+" "+attachment_files::Hex(name)+" "+std::to_string(actual.device)+" "+std::to_string(actual.inode)+" "+std::to_string(actual.parent_device)+" "+std::to_string(actual.parent_inode)+" "+std::to_string(deadline);
   const size_t charge=sizeof(Entry)+128+record.path.capacity()+record.error.capacity()+record.outcome.status.capacity()+record.outcome.expected_status.capacity()+entry.command.capacity()+5;
   if(charge>kMetadata-budget_.metadata)throw std::length_error("Native output retention metadata capacity exceeded");
   entries_.emplace(actual.id,std::move(entry));++budget_.records;budget_.metadata+=charge;
  }
  bool Start(uint64_t output,const OutputRetentionFence& actual,double now){
   Time(now);if(!actual.Retired())return false;const auto found=entries_.find(output);if(found==entries_.end())throw std::invalid_argument("Native output retention has no accepted decision");auto& entry=found->second;
   if(entry.record.status!=OutputRetentionStatus::Pending||entry.process)return false;
   if(now>=entry.record.deadline){FailUnstarted(entry,"Native output retention budget expired before admission",now);return true;}
   if(*physical_.count>=physical_.maximum)return false;
   ++*physical_.count;
   try{
    entry.process=std::make_unique<DedicatedHostProcess>(executable_,std::vector<std::string>{"--native-output-retention-worker"});entry.record.helper_pid=entry.process->Pid();
    if(hooks_.after_spawn)hooks_.after_spawn(entry.record.helper_pid);
    if(!entry.process->Send(entry.command))throw std::runtime_error("Native output retention initialization refused");
   }catch(...){
    Error(entry,"Native output retention helper admission failed");
    if(entry.process)entry.process->Kill();else{--*physical_.count;entry.record.status=OutputRetentionStatus::Failed;entry.record.receipt=now;if(hooks_.failure)hooks_.failure(entry.record.error);}
   }
   return true;
  }
  void Tick(double now){
   Time(now);
   for(auto&[id,entry]:entries_){(void)id;if(!entry.process)continue;
    if(now>=entry.record.deadline){Error(entry,"Native output retention original cleanup deadline expired");entry.process->Kill();}
    Drain(entry);const auto exit=entry.process->Exited();if(!exit)continue;
    Drain(entry);if(!entry.process->ControlClosed())continue;
    const auto received=DedicatedRealMilliseconds();entry.record.helper_signaled=exit->signaled;entry.record.helper_exit=exit->code;
    if(exit->signaled||exit->code||!entry.terminal||!entry.reply||!entry.process->Stdout().empty()||!entry.process->Stderr().empty())Error(entry,"Native output retention lacks successful actual helper receipts");
    if(received>=entry.record.deadline)Error(entry,"Native output retention deadline expired at actual helper exit");
    entry.record.physical_received=received;
    entry.record.status=entry.record.error.empty()?*entry.reply:OutputRetentionStatus::Failed;
    if(!*physical_.count)throw std::logic_error("Native output retention physical reservation underflow");
    --*physical_.count;entry.process.reset();
    if(entry.record.status==OutputRetentionStatus::Failed&&hooks_.failure)hooks_.failure(entry.record.error);
   }
  }
  void StartPending(const OutputRetentionFence& actual,double now){for(const auto&[id,entry]:entries_)if(entry.record.status==OutputRetentionStatus::Pending&&!entry.process){if(!Start(id,actual,now))break;}}
  size_t Physical()const{size_t count=0;for(const auto&[id,entry]:entries_){(void)id;if(entry.process)++count;}return count;}
  bool IdleReady()const{for(const auto&[id,entry]:entries_){(void)id;if(entry.process||entry.record.status==OutputRetentionStatus::Pending)return false;}return true;}
  template<class Add>void ForEachPollDescriptor(Add&& add)const{for(const auto&[id,entry]:entries_){(void)id;if(entry.process)for(const auto& fd:entry.process->PollDescriptors())add(fd);}}
  const OutputRetentionRecord* Find(uint64_t output)const{const auto it=entries_.find(output);return it==entries_.end()?nullptr:&it->second.record;}
  std::vector<OutputRetentionRecord> Records()const{if(!IdleReady())throw std::runtime_error("Native output retention report lacks actual physical retirement");std::vector<OutputRetentionRecord> result;result.reserve(entries_.size());for(const auto&[id,entry]:entries_){(void)id;result.push_back(entry.record);}return result;}
 private:
  struct Entry {OutputRetentionRecord record;std::string command;std::unique_ptr<DedicatedHostProcess> process;std::optional<OutputRetentionStatus> reply;bool terminal=false;};
  void Time(double now){if(!std::isfinite(now)||now<last_)throw std::invalid_argument("Native output retention clock regressed");last_=now;}
  static void Error(Entry& entry,std::string_view error){if(entry.record.error.empty())entry.record.error=attachment_files::Message(error);}
  void FailUnstarted(Entry& entry,const char* error,double now){Error(entry,error);entry.record.status=OutputRetentionStatus::Failed;entry.record.receipt=now;if(hooks_.failure)hooks_.failure(entry.record.error);}
  static void Drain(Entry& entry){try{
   for(const auto& line:entry.process->Poll(0)){
    const auto fields=attachment_files::Fields(line);if(fields.size()<2||attachment_files::UInt(fields[1])!=entry.record.output_id||entry.terminal)throw std::runtime_error("Native output retention reply has stale/duplicate identity");
    if(fields[0]=="RETIRED"&&fields.size()==6){
     const auto status=fields[2]=="removed"?OutputRetentionStatus::Removed:fields[2]=="alreadyAbsent"?OutputRetentionStatus::AlreadyAbsent:fields[2]=="foreignPreserved"?OutputRetentionStatus::ForeignPreserved:throw std::runtime_error("Invalid native output retention result");
     const auto files=attachment_files::UInt(fields[3]),directories=attachment_files::UInt(fields[4]),symlinks=attachment_files::UInt(fields[5]);
     if(files>16384||directories>16384||symlinks>16384||files+directories+symlinks>16384||(status!=OutputRetentionStatus::Removed&&(files||directories||symlinks))||(status==OutputRetentionStatus::Removed&&!directories))throw std::runtime_error("Native output retention result counts exceed actual inventory bound");
     entry.record.removed_files=files;entry.record.removed_directories=directories;entry.record.removed_symlinks=symlinks;entry.reply=status;
    }else if(fields[0]=="ERROR"&&fields.size()==5){const auto number=attachment_files::UInt(fields[2]);if(number>INT32_MAX)throw std::runtime_error("Native output retention errno exceeds bound");Error(entry,"Native output retention "+attachment_files::Unhex(fields[3],4096)+" errno="+std::to_string(number)+": "+attachment_files::Unhex(fields[4],4096));}
    else throw std::runtime_error("Invalid native output retention helper packet");
    entry.terminal=true;entry.record.receipt=DedicatedRealMilliseconds();
   }
  }catch(const std::exception& error){Error(entry,error.what());entry.process->Kill();}}
  std::string executable_;Hooks hooks_;Budget& budget_;PhysicalBudget physical_;double last_=0;std::map<uint64_t,Entry> entries_;
};
} // namespace suitecut::runner
