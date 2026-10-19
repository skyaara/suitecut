// SPDX-License-Identifier: MIT
#pragma once
#include "output_directory_files.h"
#include "output_directory_record.h"
#include "host_process.h"
#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <type_traits>
namespace suitecut::runner {
// Per-actor directory IO remains an exact foreign-child lease until BOTH the
// actual writer and any inode-checked rollback have waitpid-confirmed exits.
// The shared artifact physical counter is also used by RunnerAttachments.
class RunnerOutputDirectories {
 public:
  static constexpr size_t kRecords=8192,kMetadata=64*1024*1024;
  struct Budget {uint64_t sequence=0;size_t records=0,reserved=0;};
  struct PhysicalBudget {size_t* count=nullptr;size_t maximum=0;};
  struct Hooks {std::function<void(const std::string&)> failure;std::function<void(int64_t)> after_spawn{};};
  RunnerOutputDirectories(std::string executable,Hooks hooks,Budget& budget,PhysicalBudget physical)
      :executable_(std::move(executable)),hooks_(std::move(hooks)),budget_(budget),physical_(physical){
    if(executable_.empty()||!physical_.count||!physical_.maximum||physical_.maximum>64)throw std::invalid_argument("Invalid native output directory actor composition");
  }
  bool Begin(const OutputDirectoryOwner& owner,const std::filesystem::path& parent,double deadline,double now){
    Time(now);if(!owner.test_id||!owner.generation||!owner.worker_index||owner.pid<=0||!std::isfinite(deadline)||deadline<=now||records_.contains(owner.Key())||jobs_.contains(owner.Key()))return false;
    const auto normalized=parent.lexically_normal();const auto bytes=normalized.string();
    if(!normalized.is_absolute()||bytes.empty()||bytes.size()>7900||bytes.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid native output directory parent");
    if(*physical_.count>=physical_.maximum)return false;
    if(budget_.sequence==UINT64_MAX||budget_.records>=kRecords)throw std::length_error("Native output directory identity/history capacity exhausted");
    const auto id=budget_.sequence+1;const auto name="suitecut-output-"+std::to_string(getpid())+"-"+std::to_string(id)+"-"+std::to_string(owner.test_id)+"-"+std::to_string(owner.generation);
    const auto path=(normalized/name).string();if(path.size()>8192)return false;
    // Pin known path storage plus two bounded raw native UTF8 error buffers
    // before IO. Errors are at most4096 stored bytes each, not UTF16 units.
    // Physical retirement never depends on later metadata admission.
    const auto reservation=bytes.size()+name.size()+path.size()*6+2*4096+768;
    if(reservation>kMetadata-budget_.reserved)throw std::length_error("Native output directory metadata capacity exhausted");
    Job job;job.record.id=id;job.record.owner=owner;job.record.path=path;job.record.started=now;job.record.deadline=deadline;job.parent=bytes;job.name=name;
    job.record.error.reserve(4096);job.record.cleanup_error.reserve(4096);
    const auto initialize="CREATE "+std::to_string(id)+" "+attachment_files::Hex(bytes)+" "+attachment_files::Hex(name)+" "+std::to_string(deadline);
    // Allocate BOTH owning nodes before any actual fork. Final retirement only
    // move-assigns into this reserved node, and cannot lose a waitpid receipt to
    // a later map allocation failure.
    records_.try_emplace(owner.Key());
    std::map<Key,Job>::iterator admitted;
    try{admitted=jobs_.emplace(owner.Key(),std::move(job)).first;}
    catch(...){records_.erase(owner.Key());throw;}
    ++budget_.sequence;++budget_.records;budget_.reserved+=reservation;++*physical_.count;
    auto& actual=admitted->second;
    try{
      actual.process=std::make_unique<DedicatedHostProcess>(executable_,std::vector<std::string>{"--native-output-directory-worker"});
      actual.record.writer_pid=actual.process->Pid();
      if(hooks_.after_spawn)hooks_.after_spawn(actual.record.writer_pid);
      if(!actual.process->Send(initialize))throw std::runtime_error("Native output directory writer initialization rejected");
    }catch(...){
      if(!actual.process){--*physical_.count;jobs_.erase(admitted);records_.erase(owner.Key());throw;}
      // In particular, a Send queue allocation exception after the actual fork
      // retains its exact actor/counter lease until real exit and final drain.
      actual.record.status=OutputDirectoryStatus::Failed;actual.record.error="Native output directory writer initialization failed";actual.process->Kill();
    }
    return true;
  }
  void Cancel(const OutputDirectoryOwner& owner,const std::string& reason,double now){Time(now);const auto found=jobs_.find(owner.Key());if(found!=jobs_.end())Stop(found->second,OutputDirectoryStatus::Interrupted,reason,now);}
  void CancelAll(const std::string& reason,double now){Time(now);for(auto&[key,job]:jobs_){(void)key;Stop(job,OutputDirectoryStatus::Interrupted,reason,now);}}
  void Tick(double now){
    Time(now);
    for(auto it=jobs_.begin();it!=jobs_.end();){auto& job=it->second;
      if(!job.cleaning&&now>=job.record.deadline)Stop(job,OutputDirectoryStatus::TimedOut,"Native output directory original deadline expired",now);
      if(job.cleaning&&now>=job.cleanup_deadline){job.process->Kill();if(job.record.cleanup_error.empty())job.record.cleanup_error="Native output directory rollback original deadline expired";}
      Drain(job,DedicatedRealMilliseconds());const auto exit=job.process->Exited();if(!exit){++it;continue;}
      // waitpid and control EOF are independent; drain the final real native
      // messages once again after the confirmed exit, just as file writers do.
      const auto received=DedicatedRealMilliseconds();Drain(job,received);
      if(!job.cleaning){
        job.record.writer_received=received;job.record.writer_signaled=exit->signaled;job.record.writer_exit=exit->code;
        if(!job.process->Stdout().empty()||!job.process->Stderr().empty()){Stop(job,OutputDirectoryStatus::Failed,"Native output directory writer emitted unexpected native output",received);if(hooks_.failure)hooks_.failure(job.record.error);}
        if(job.record.status==OutputDirectoryStatus::Preparing&&(exit->signaled||exit->code!=0||!job.record.ready_receipt||!job.record.inode))Stop(job,OutputDirectoryStatus::Failed,"Native output directory writer exited without complete actual readiness",now);
        if(job.record.status==OutputDirectoryStatus::Preparing&&received>=job.record.deadline)Stop(job,OutputDirectoryStatus::TimedOut,"Native output directory deadline expired at actual writer exit",received);
        if(job.record.status==OutputDirectoryStatus::Preparing){job.record.status=OutputDirectoryStatus::Ready;Finish(it,received);continue;}
        if(job.record.inode){if(!StartCleanup(job,received)){if(hooks_.failure)hooks_.failure(job.record.cleanup_error);Finish(it,received);continue;}++it;continue;}
        // No user callback was admitted. Before the actual CREATED inode receipt
        // there can be an unclaimed empty directory; never path-delete it.
        Finish(it,received);continue;
      }
      job.record.rollback_signaled=exit->signaled;job.record.rollback_exit=exit->code;
      if(exit->signaled||exit->code!=0||!job.record.rollback_receipt||!job.process->Stdout().empty()||!job.process->Stderr().empty()){if(job.record.cleanup_error.empty())job.record.cleanup_error="Native output directory rollback exited without complete actual receipt";}
      if(!job.record.cleanup_error.empty()&&hooks_.failure)hooks_.failure(job.record.cleanup_error);
      Finish(it,received);
    }
  }
  bool Assign(const OutputDirectoryOwner& owner,double now){Time(now);const auto found=records_.find(owner.Key());if(found==records_.end()||found->second.status!=OutputDirectoryStatus::Ready||found->second.assigned||now>=found->second.deadline)return false;found->second.assigned=now;return true;}
  size_t Physical()const{return jobs_.size();}
  const OutputDirectoryRecord* Observe(const OutputDirectoryOwner& owner)const{const auto found=jobs_.find(owner.Key());return found==jobs_.end()?Find(owner):&found->second.record;}
  template<class Add>void ForEachPollDescriptor(Add&& add)const{for(const auto&[key,job]:jobs_){(void)key;for(const auto& fd:job.process->PollDescriptors())add(fd);}}
  std::vector<pollfd> PollDescriptors()const{std::vector<pollfd> all;for(const auto&[key,job]:jobs_){(void)key;const auto fds=job.process->PollDescriptors();all.insert(all.end(),fds.begin(),fds.end());}return all;}
  const OutputDirectoryRecord* Find(const OutputDirectoryOwner& owner)const{const auto found=records_.find(owner.Key());return found==records_.end()||!found->second.received?nullptr:&found->second;}
  std::vector<OutputDirectoryRecord> Records()const{std::vector<OutputDirectoryRecord> result;for(const auto&[key,row]:records_){(void)key;if(row.received)result.push_back(row);}return result;}
 private:
  using Key=decltype(OutputDirectoryOwner{}.Key());
  struct Job {OutputDirectoryRecord record;std::string parent,name;std::unique_ptr<DedicatedHostProcess> process;bool cleaning=false;double cleanup_deadline=0;};
  void Time(double now){if(!std::isfinite(now)||now<last_now_)throw std::invalid_argument("Invalid native output directory clock ingress");last_now_=now;}
  void Stop(Job& job,OutputDirectoryStatus status,const std::string& reason,double now){(void)now;if(job.record.status==OutputDirectoryStatus::Preparing){job.record.status=status;job.record.error=attachment_files::Message(reason);}if(!job.cleaning)job.process->Kill();}
  void Drain(Job& job,double now){
    try{for(const auto& line:job.process->Poll(0)){
      const auto fields=attachment_files::Fields(line);if(fields.size()<2||attachment_files::UInt(fields[1])!=job.record.id)throw std::runtime_error("Native output directory receipt has wrong actual identity");
      if(!job.cleaning&&fields[0]=="CREATED"&&fields.size()==6&&!job.record.created){job.record.device=attachment_files::UInt(fields[2]);job.record.inode=attachment_files::UInt(fields[3]);if(!job.record.inode)throw std::runtime_error("Native output directory actual inode is zero");job.record.parent_device=attachment_files::UInt(fields[4]);job.record.parent_inode=attachment_files::UInt(fields[5]);if(!job.record.parent_inode)throw std::runtime_error("Native output directory actual parent inode is zero");job.record.created=now;}
      else if(!job.cleaning&&fields[0]=="READY"&&fields.size()==2&&job.record.created&&!job.record.ready_receipt)job.record.ready_receipt=true;
      else if(job.cleaning&&fields[0]=="CLEANED"&&fields.size()==3&&!job.record.rollback_receipt&&(fields[2]=="removed"||fields[2]=="alreadyAbsent"||fields[2]=="foreignPreserved")){job.record.rollback_receipt=true;job.record.foreign_preserved=fields[2]=="foreignPreserved";}
      else if(fields[0]=="ERROR"&&fields.size()==5){const auto error="Native output directory "+attachment_files::Unhex(fields[3],4096)+" errno="+fields[2]+": "+attachment_files::Unhex(fields[4],4096);if(job.cleaning){if(job.record.cleanup_error.empty())job.record.cleanup_error=attachment_files::Message(error);}else Stop(job,OutputDirectoryStatus::Failed,error,now);}
      else throw std::runtime_error("Invalid native output directory physical receipt");
    }}catch(const std::exception& error){if(job.cleaning){if(job.record.cleanup_error.empty())job.record.cleanup_error=attachment_files::Message(error.what());job.process->Kill();}else Stop(job,OutputDirectoryStatus::Failed,error.what(),now);}
  }
  bool StartCleanup(Job& job,double now){
    job.process.reset();job.cleaning=true;job.cleanup_deadline=now+1000;
    try{job.process=std::make_unique<DedicatedHostProcess>(executable_,std::vector<std::string>{"--native-output-directory-cleanup"});}
    catch(const std::exception& error){job.record.cleanup_error=attachment_files::Message(error.what());return false;}
    job.record.rollback_pid=job.process->Pid();
    if(!job.process->Send("ROLLBACK "+std::to_string(job.record.id)+" "+attachment_files::Hex(job.parent)+" "+attachment_files::Hex(job.name)+" "+std::to_string(job.record.device)+" "+std::to_string(job.record.inode)+" "+std::to_string(job.record.parent_device)+" "+std::to_string(job.record.parent_inode)+" "+std::to_string(job.cleanup_deadline))){job.record.cleanup_error="Native output directory rollback initialization failed";job.process->Kill();}
    return true;
  }
  void Finish(std::map<Key,Job>::iterator& it,double now){static_assert(std::is_nothrow_move_assignable_v<OutputDirectoryRecord>);auto& job=it->second;job.record.received=now;if(!*physical_.count)throw std::logic_error("Native output directory shared physical reservation underflow");records_.at(it->first)=std::move(job.record);--*physical_.count;it=jobs_.erase(it);}
  std::string executable_;Hooks hooks_;Budget& budget_;PhysicalBudget physical_;double last_now_=0;std::map<Key,Job> jobs_;std::map<Key,OutputDirectoryRecord> records_;
};
} // namespace suitecut::runner
