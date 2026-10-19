// SPDX-License-Identifier: MIT
#include "runner_callback_retries.h"
#include "runner_steps.h"
#include <cassert>
#include <iostream>
namespace {
using namespace suitecut::runner;
using Controller=RunnerCallbackRetries;
using Schedule=CallbackRetrySchedule;
const StepOwner owner{1,4,0,1,4,2,501,"body"};
template<class F>void Reject(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
struct Fixture {
  double now=10;TestSteps::Budget budget;std::vector<std::string> sent,hard,terminated;
  bool deny_begin=false,throw_begin=false,throw_after_admit=false,hold_files=false,infrastructure=false;
  Controller::SourceFence source=Controller::SourceFence::Ready;
  unsigned source_calls=0;std::optional<std::string> file_error;
  std::function<void(const std::optional<std::string>&)> held;
  std::unique_ptr<RunnerSteps> steps;std::unique_ptr<Controller> retries;uint64_t request=0;
  Fixture(){
    RunnerSteps::Hooks sh;
    sh.send=[this](const auto& packet){sent.push_back(packet);};sh.now=[this]{return now;};sh.terminate=[this](const auto& e){terminated.push_back(e);};
    sh.fileFence=[this](const auto& o,uint64_t id,auto forward,double t){
      if(retries->OwnsIteration(o,id))retries->FenceIteration(o,id,std::move(forward),t);
      else retries->FenceStep(o,id,std::move(forward),t);
    };
    steps=std::make_unique<RunnerSteps>(std::move(sh),&budget);
    Controller::Hooks h;h.send=[this](const auto& packet){sent.push_back(packet);};h.next_step=[this]{return budget.sequence+1;};
    h.begin=[this](const auto& o,uint64_t p,uint64_t a,uint64_t n,std::optional<double> cap,double t){if(throw_begin)throw std::runtime_error("actual preadmission rejection");const auto id=deny_begin?std::optional<uint64_t>{}:steps->BeginCallbackIteration(o,p,a,n,cap,t);if(throw_after_admit)throw std::runtime_error("original actual postadmission failure");return id;};
    h.settled=[this](const auto& o,uint64_t id,uint64_t n,bool ok,const auto& e,uint64_t from,auto forward,double t){steps->SettleCallbackIteration(o,id,n,ok,e,from,std::move(forward),t);};
    h.cancel=[this](const auto& o,uint64_t id,const auto& e,double t){steps->CancelCallbackIteration(o,id,e,t);};
    h.files=[this](const auto&,const auto&,auto forward,double){if(hold_files)held=std::move(forward);else forward(file_error);};
    h.source=[this](const auto& o,uint64_t req,uint64_t a,uint64_t n,uint64_t id,uint64_t ticket,double,double){
      assert(ticket==7&&retries->HasSourceFenceOwner(o,req,a,n,id,steps->Find(id)->parent));++source_calls;return source;
    };
    h.step=[this](uint64_t id){return steps->Find(id);};h.hard=[this](const auto&,const auto& e){hard.push_back(e);};
    h.terminate=[this](const auto& e){terminated.push_back(e);};h.now=[this]{return now;};h.infrastructure=[this](const auto&){return infrastructure;};
    retries=std::make_unique<Controller>(std::move(h),budget);
    steps->BeginCallback(owner,10000,now);retries->Open(owner,10000,now);
  }
  uint64_t Begin(std::string cap="-",std::string intervals="-",uint64_t parent=0){
    const auto id=budget.sequence+1;
    assert(retries->Packet({"TOPASS_BEGIN",std::to_string(++request),"0","1","4",std::to_string(parent),cap,intervals,"-","0","0","0"},now,false));return id;
  }
  const StepRecord& Current(uint64_t assertion)const{
    const StepRecord* found=nullptr;for(const auto& row:steps->Records())if(row.callback_iteration&&row.callback_iteration->assertion==assertion)found=&row;
    assert(found);return *found;
  }
  std::vector<std::string> Prefix(const std::string& kind,uint64_t assertion)const{
    const auto& step=Current(assertion);
    // Each BEGIN request is monotonic and its genuine first step is retained.
    size_t req=0;for(const auto& packet:sent)if(packet.starts_with("TOPASS_ADMITTED ")){std::istringstream in(packet);std::string tag,token,scope,generation,id;in>>tag>>req>>token>>scope>>generation>>id;if(std::stoull(id)==assertion)break;}
    return{kind,std::to_string(req),"0","1","4",std::to_string(assertion),std::to_string(step.callback_iteration->invocation),std::to_string(step.id)};
  }
  void Raw(uint64_t assertion,bool ok,uint64_t pending=0,uint64_t ticket=0){auto f=Prefix("TOPASS_RAW",assertion);f.insert(f.end(),{ok?"P":"E",ok?"-":StepWire("\"actual author failure\""),"0",std::to_string(ticket),std::to_string(pending)});assert(retries->Packet(f,now,false));}
  void Tick(double t){now=t;steps->Tick(now);retries->Tick(now);}
  void ReleaseFiles(){assert(held);auto f=std::move(held);f(file_error);}
  void PumpRaw(uint64_t assertion,bool ok,uint64_t pending=0,uint64_t ticket=0){Raw(assertion,ok,pending,ticket);Tick(now);}
  void PumpReleaseFiles(){ReleaseFiles();Tick(now);}
  void Exit(double actual_step_exit,double summary_receipt){now=actual_step_exit;steps->WorkerExited(owner.worker_index,owner.pid,now);now=summary_receipt;retries->WorkerExited(owner.worker_index,owner.pid,now);}
  void Finish(){bool retired=false;retries->Fence(owner,[&](const auto& e){assert(!e);retired=true;},now);assert(retired);}
};
}
int main(int argc,char** argv){
  const bool followup=argc==2&&std::string(argv[1])=="--followup-only";assert(argc==1||followup);
  unsigned controls=0;
  if(!followup){
  {
    Fixture f;const auto id=f.Begin();assert(f.retries->Physical()==1&&f.steps->Physical()==1&&!f.retries->CanSkip(owner));
    f.now=11;f.PumpRaw(id,false);assert(f.retries->Records().empty()&&f.steps->Find(id)->receipt==StepReceipt::Settled&&f.steps->Find(id)->status==StepStatus::Failed);
    assert(f.retries->NextWake()==111);f.Tick(110);assert(f.Current(id).id==id);f.Tick(111);assert(f.Current(id).id!=id);
    f.now=112;f.PumpRaw(id,true);assert(f.retries->Records().size()==1&&f.retries->Records()[0].status==CallbackAssertionStatus::Passed&&f.retries->Records()[0].attempts==2);
    assert(f.steps->Find(id)->error_json==RuntimeSkipReason(StepWire("\"actual author failure\""))&&f.retries->CanSkip(owner));f.Finish();++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","5,10");
    for(const auto due:{16,27,38}){f.now=due==16?11:due-10;f.PumpRaw(id,false);assert(f.retries->NextWake()==due);f.Tick(due);}
    f.now=39;f.PumpRaw(id,true);assert(f.retries->Records()[0].attempts==4);++controls;
  }
  {
    Fixture f;const auto first=f.Begin("-","0"),second=f.Begin("-","0");f.now=11;f.Raw(first,false);f.Raw(second,false);
    assert(f.steps->Physical()==2&&f.retries->Records().empty());
    const auto prior=f.budget.sequence;f.Tick(11);assert(f.budget.sequence==prior+1&&f.Current(first).callback_iteration->invocation==2&&f.Current(second).callback_iteration->invocation==1);
    f.Raw(first,false);f.Tick(11);assert(f.Current(second).callback_iteration->invocation==2&&f.Current(first).callback_iteration->invocation==2);++controls;
  }
  {
    Fixture f;const auto id=f.Begin("10","0");f.Tick(20);assert(f.retries->Records().empty()&&f.steps->Physical()==1&&!f.Current(id).callback_received);
    bool outer=false;f.retries->Fence(owner,[&](const auto& error){assert(!error);outer=true;},f.now);assert(!outer);
    f.now=21;f.PumpRaw(id,true);assert(outer&&f.retries->Records()[0].status==CallbackAssertionStatus::TimedOut&&f.retries->Records()[0].receipt==CallbackAssertionReceipt::Settled);
    assert(f.Current(id).callback_received==21&&f.Current(id).status==StepStatus::TimedOut&&f.hard.empty());++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");bool outer=false;
    f.retries->Fence(owner,[&](const auto& error){assert(error);outer=true;},f.now);assert(!outer&&f.retries->Physical()==1);
    f.now=11;f.PumpRaw(id,true);assert(outer&&f.retries->Records()[0].status==CallbackAssertionStatus::Canceled);++controls;
  }
  {
    Fixture f;const auto id=f.Begin("10","0");f.Tick(20);f.Tick(1019);assert(f.terminated.empty()&&f.retries->Physical()==1);f.Tick(1020);f.Tick(1030);assert(f.terminated.size()==1);
    bool forwarded=false;f.retries->Fence(owner,[&](const auto&){forwarded=true;},f.now);
    f.Exit(1031,1032);
    assert(!forwarded&&f.retries->IdleReady()&&f.retries->Records()[0].receipt==CallbackAssertionReceipt::WorkerExited&&!f.Current(id).callback_received);++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");f.hold_files=true;f.now=11;f.PumpRaw(id,true);assert(f.held&&f.steps->Physical()==1&&f.retries->Records().empty());
    f.Tick(12);assert(f.source_calls==0);f.now=13;f.PumpReleaseFiles();assert(f.Current(id).callback_received==11&&f.Current(id).received==13&&f.retries->Records()[0].status==CallbackAssertionStatus::Passed);++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");f.source=Controller::SourceFence::Pending;f.now=11;f.PumpRaw(id,true,1,7);
    assert(f.source_calls==0&&f.steps->Physical()==1);
    auto retired=f.Prefix("TOPASS_OPERATIONS_RETIRED",id);retired.push_back("7");f.now=12;f.retries->Packet(retired,f.now,false);
    assert(f.source_calls==0&&f.steps->Physical()==1&&!f.Current(id).received);
    f.Tick(12);assert(f.source_calls==1&&f.steps->Physical()==1&&!f.Current(id).received);
    f.Tick(13);assert(f.source_calls==2&&f.retries->Records().empty());f.source=Controller::SourceFence::Ready;f.Tick(14);
    assert(f.Current(id).received==14&&f.retries->Records()[0].status==CallbackAssertionStatus::Passed);Reject([&]{f.retries->Packet(retired,f.now,false);});++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");f.source=Controller::SourceFence::Failed;f.now=11;f.PumpRaw(id,true,0,7);
    f.Tick(12);f.Tick(13);assert(f.hard.size()==1&&f.source_calls==1&&f.retries->Physical()==1&&f.retries->Records().empty());++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");f.hold_files=true;f.now=11;f.PumpRaw(id,true);
    auto failed=f.Prefix("TOPASS_PRODUCER_FAILURE",id);failed.insert(failed.end(),{"0",StepWire("\"actual late native producer failure\"")});
    f.now=12;f.retries->Packet(failed,f.now,false);assert(f.hard.size()==1&&f.retries->Records().empty());f.now=13;f.PumpReleaseFiles();
    assert(f.retries->Records()[0].status==CallbackAssertionStatus::Infrastructure&&f.retries->Records()[0].error_json==RuntimeSkipReason(StepWire("\"actual late native producer failure\"")));++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");f.hold_files=true;f.file_error="\"actual ordinary file input failure\"";f.now=11;f.PumpRaw(id,true);f.now=12;f.PumpReleaseFiles();
    assert(f.steps->Find(id)->status==StepStatus::Failed&&f.steps->Find(id)->attachment_error_json==f.file_error&&f.hard.empty());
    f.hold_files=false;f.file_error.reset();f.Tick(12);f.now=13;f.PumpRaw(id,true);assert(f.retries->Records()[0].status==CallbackAssertionStatus::Passed&&f.retries->Records()[0].attempts==2);++controls;
  }
  {
    Fixture f;const auto id=f.Begin("0","0");assert(!f.Current(id).callback_deadline);f.retries->CancelAll(f.now);assert(f.retries->Physical()==1);f.now=11;f.PumpRaw(id,true);assert(f.retries->Records()[0].status==CallbackAssertionStatus::Canceled);++controls;
  }
  {
    Fixture f;const auto id=f.Begin();f.infrastructure=true;f.Tick(11);f.PumpRaw(id,true);assert(f.retries->Records()[0].status==CallbackAssertionStatus::Infrastructure);++controls;
  }
  {
    Fixture f;f.deny_begin=true;const auto reserved=f.budget.reserved;f.Begin();assert(f.retries->Records().empty()&&!f.retries->Physical()&&f.budget.reserved==reserved&&!f.steps->Physical());++controls;
  }
  {
    Fixture f;const auto id=f.Begin();auto raw=f.Prefix("TOPASS_RAW",id);raw.insert(raw.end(),{"P","-","0","0","0"});raw[3]="9";Reject([&]{f.retries->Packet(raw,f.now,false);});
    raw[3]="1";raw[6]="2";Reject([&]{f.retries->Packet(raw,f.now,false);});
    Reject([&]{f.steps->Packet({"STEP_SETTLED",std::to_string(id),"0","1","4","1","-"},f.now,false);});
    assert(f.steps->Physical()==1&&!f.Current(id).callback_received);++controls;
  }
  {
    Fixture f;assert(f.steps->Packet({"STEP_BEGIN","1","0","1","4","0","22706172656e7422","0"},f.now,false));
    const auto id=f.Begin("10","0",1);assert(!f.retries->CanSkip(owner,1));f.Tick(20);
    assert(f.steps->Packet({"STEP_SETTLED","1","0","1","4","1","-"},f.now,false));assert(f.steps->Find(1)->receipt==StepReceipt::Pending);
    f.now=21;f.PumpRaw(id,true);assert(f.steps->Find(1)->receipt==StepReceipt::Settled&&f.steps->Find(1)->status==StepStatus::Passed&&f.retries->Records()[0].status==CallbackAssertionStatus::TimedOut);++controls;
  }
  {
    Fixture f;assert(f.steps->Packet({"STEP_BEGIN","1","0","1","4","0","22706172656e7422","0"},f.now,false));
    const auto id=f.Begin("-","0",1);f.now=11;assert(f.steps->Packet({"STEP_SETTLED","1","0","1","4","1","-"},f.now,false));
    f.now=12;f.PumpRaw(id,true);assert(f.steps->Find(1)->status==StepStatus::Failed&&f.steps->Find(1)->attachment_error_json&&f.retries->Records()[0].status==CallbackAssertionStatus::Canceled);++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");f.now=11;
    assert(f.steps->Packet({"STEP_BEGIN","1","0","1","4",std::to_string(id),"226368696c6422","0"},f.now,false));
    f.now=12;f.PumpRaw(id,false);assert(f.steps->Physical()==2&&!f.Current(id).received&&f.retries->Records().empty());
    f.now=13;assert(f.steps->Packet({"STEP_SETTLED","2","0","1","4","1","-"},f.now,false));
    assert(f.Current(id).callback_received==12&&!f.Current(id).received&&f.steps->Find(2)->receipt==StepReceipt::Settled&&f.steps->Physical()==1);
    f.Tick(13);assert(f.steps->Find(id)->received==13&&f.Current(id).callback_iteration->invocation==2);
    f.now=14;f.PumpRaw(id,true);assert(f.retries->Records()[0].attempts==2);++controls;
  }
  {
    Fixture f;const auto id=f.Begin("0","0");f.now=11;f.steps->Amend(0,1,4,20,f.now);f.retries->Amend(owner,20,f.now);
    f.Tick(20);assert(f.retries->Physical()==1&&!f.Current(id).callback_deadline);f.now=21;f.PumpRaw(id,true);
    assert(f.retries->Records()[0].status==CallbackAssertionStatus::TimedOut&&f.retries->Records()[0].deadline==20&&!f.retries->Records()[0].own_deadline);++controls;
  }
  {
    Fixture f;const auto count=f.budget.records,reserved=f.budget.reserved;
    Reject([&]{f.Begin("9007199254740992");});assert(!f.retries->Physical()&&f.budget.records==count&&f.budget.reserved==reserved);
    Reject([&]{f.retries->Packet({"TOPASS_BEGIN","2","0","1","4","0","-","0","-","0","0","0"},f.now,true);});
    assert(!f.retries->Physical()&&f.retries->Records().empty()&&f.steps->Records().empty());++controls;
  }
  {
    Fixture f;f.throw_begin=true;const auto reserved=f.budget.reserved;Reject([&]{f.Begin();});
    assert(!f.retries->Physical()&&f.retries->Records().empty()&&f.budget.reserved==reserved&&f.steps->Records().empty());++controls;
  }
  {
    Fixture f;bool complete=false;f.retries->Fence(owner,[&](const auto& error){assert(!error);f.retries->WorkerExited(owner.worker_index,owner.pid,f.now);complete=true;},f.now);
    assert(complete&&f.retries->IdleReady());++controls;
  }
  {
    Fixture f;f.throw_after_admit=true;Reject([&]{f.Begin();});assert(f.steps->Physical()==1&&f.retries->Physical()==1&&!f.steps->Find(1)->callback_received);
    for(unsigned stage=0;stage<2;++stage){bool original=false;try{f.retries->ThrowIfUnpublishable();}catch(const std::runtime_error& error){original=std::string(error.what())=="original actual postadmission failure";}assert(original);
      if(!stage){f.now=11;f.retries->CancelAll(f.now);f.Exit(12,13);assert(f.retries->IdleReady()&&f.steps->IdleReady()&&f.steps->Find(1)->receipt==StepReceipt::WorkerExited&&!f.steps->Find(1)->callback_received);}
    }++controls;
  }
  }
  {
    Fixture f;assert(f.retries->Records().capacity()==0);f.budget.reserved=TestSteps::kMetadata;const auto sequence=f.budget.sequence;
    f.Begin();assert(f.sent.back().starts_with("TOPASS_ADMITTED 1 0 1 4 0 0 ")&&f.budget.sequence==sequence&&f.steps->Records().empty()&&!f.retries->Physical()&&f.retries->Records().capacity()==0);
    f.budget.reserved=0;const auto id=f.Begin();assert(id==1&&f.retries->Records().capacity()>=TestSteps::kRecords);f.now=11;f.PumpRaw(id,true);assert(f.retries->Records()[0].attempts==1);++controls;
  }
  {
    Fixture f;f.deny_begin=true;const auto reserved=f.budget.reserved;f.Begin();
    assert(f.sent.back().starts_with("TOPASS_ADMITTED 1 0 1 4 0 0 ")&&f.budget.reserved==reserved&&f.budget.sequence==0&&f.steps->Records().empty()&&f.retries->Records().empty());
    auto stale=std::vector<std::string>{"TOPASS_RAW","1","0","1","4","0","1","0","P","-","0","0","0"};Reject([&]{f.retries->Packet(stale,f.now,false);});++controls;
  }
  {
    Fixture f;for(size_t i=0;i<TestSteps::kPhysical;++i)f.Begin("-","0");const auto sequence=f.budget.sequence,reserved=f.budget.reserved;
    f.Begin();assert(f.sent.back().starts_with("TOPASS_ADMITTED 65 0 1 4 0 0 ")&&f.budget.sequence==sequence&&f.budget.reserved==reserved&&f.retries->Physical()==TestSteps::kPhysical&&f.steps->Physical()==TestSteps::kPhysical);
    Reject([&]{f.retries->Packet({"TOPASS_BEGIN","66","0","9","4","0","-","0","-","0","0","0"},f.now,false);});++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");f.hold_files=true;const auto first=StepWire("\"original actual producer failure\"");
    auto failed=f.Prefix("TOPASS_PRODUCER_FAILURE",id);failed.insert(failed.end(),{"0",first});f.now=11;f.retries->Packet(failed,f.now,false);
    failed.back()=StepWire("\"secondary actual producer failure\"");f.retries->Packet(failed,f.now,false);
    f.now=12;f.PumpRaw(id,false);f.file_error="\"later ordinary file failure\"";f.now=13;f.PumpReleaseFiles();
    assert(f.hard.size()==1&&f.retries->Records()[0].status==CallbackAssertionStatus::Infrastructure&&f.retries->Records()[0].error_json==RuntimeSkipReason(first));
    assert(f.Current(id).receipt_error_json==RuntimeSkipReason(StepWire("\"actual author failure\"")));++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");f.retries->FileFailure(owner,AttachmentRetryProvenance{id,1,id},"\"original actual native writer failure\"",true,f.now);
    f.now=11;f.PumpRaw(id,false);assert(f.retries->Records()[0].error_json=="\"original actual native writer failure\""&&f.hard.size()==1);++controls;
  }
  {
    Fixture f;assert(f.steps->Packet({"STEP_BEGIN","1","0","1","4","0","22706172656e7422","0"},f.now,false));
    f.now=20;const auto id=f.Begin("-","0",1);f.now=21;f.steps->Amend(0,1,4,15,f.now);assert(f.steps->Find(1)->status==StepStatus::TimedOut);
    f.retries->Amend(owner,15,f.now);f.Tick(21);f.now=22;f.PumpRaw(id,true);
    assert(f.retries->Records()[0].status==CallbackAssertionStatus::TimedOut&&f.retries->Records()[0].deadline==15&&f.retries->Records()[0].started==20);++controls;
  }
  {
    Fixture f;const auto id=f.Begin();f.now=11;f.PumpRaw(id,false);const auto raw=f.Current(id).callback_received,physical=f.Current(id).received;
    f.now=12;f.retries->CancelAll(f.now);f.retries->WorkerExited(owner.worker_index,owner.pid+1,f.now);assert(f.retries->Physical()==1&&f.retries->Records().empty());
    f.Exit(13,14);
    assert(f.retries->IdleReady()&&f.retries->Records()[0].status==CallbackAssertionStatus::Infrastructure&&f.retries->Records()[0].receipt==CallbackAssertionReceipt::WorkerExited&&f.retries->Records()[0].attempts==1);
    assert(f.Current(id).receipt==StepReceipt::Settled&&f.Current(id).callback_received==raw&&f.Current(id).received==physical&&f.Current(id).status==StepStatus::Failed);++controls;
  }
  {
    Fixture f;f.throw_after_admit=true;Reject([&]{f.Begin();});f.now=11;f.retries->CancelAll(f.now);assert(f.retries->Physical()==1&&f.steps->Physical()==1);
    f.Exit(12,13);
    assert(f.retries->IdleReady()&&f.steps->IdleReady()&&f.steps->Find(1)->receipt==StepReceipt::WorkerExited&&!f.steps->Find(1)->callback_received);
    bool original=false;try{f.retries->Records();}catch(const std::runtime_error& error){original=std::string(error.what())=="original actual postadmission failure";}assert(original);++controls;
  }
  {
    Fixture f;const auto id=f.Begin();f.hold_files=true;f.now=11;f.Raw(id,false);
    assert(f.Current(id).callback_received==11&&!f.Current(id).received&&!f.held&&f.steps->Physical()==1);
    f.Tick(12);assert(f.held&&!f.Current(id).received);
    // The actual file adapter can observe a later terminal receipt while its
    // caller still exposes an older ingress event. Only fresh Tick may stamp
    // this step or start its backoff. These times are controlled seam inputs.
    const double file_terminal=25;f.now=11;f.ReleaseFiles();
    assert(!f.Current(id).received&&f.steps->Physical()==1&&f.retries->Records().empty());
    f.Tick(26);assert(f.Current(id).received==26&&*f.Current(id).received>=file_terminal&&f.retries->NextWake()==126);
    f.hold_files=false;f.Tick(125);assert(f.Current(id).id==id);f.Tick(126);assert(f.Current(id).id!=id);f.now=127;f.PumpRaw(id,true);assert(f.retries->Records().size()==1&&f.retries->Records()[0].status==CallbackAssertionStatus::Passed);++controls;
  }
  {
    Fixture f;const auto id=f.Begin("10","0");f.hold_files=true;f.now=11;f.Raw(id,true);f.Tick(12);
    const double file_terminal=25;f.now=11;f.ReleaseFiles();assert(!f.Current(id).received&&f.retries->Records().empty());
    f.Tick(26);assert(f.Current(id).callback_received==11&&f.Current(id).received==26&&*f.Current(id).received>=file_terminal);
    assert(f.Current(id).status==StepStatus::TimedOut&&f.Current(id).callback_ok&&f.retries->Records().size()==1&&f.retries->Records()[0].status==CallbackAssertionStatus::TimedOut&&f.retries->Records()[0].attempts==1&&f.retries->Records()[0].received==26);++controls;
  }
  {
    Fixture f;const auto id=f.Begin("-","0");f.now=11;f.Raw(id,true,1,7);
    auto retired=f.Prefix("TOPASS_OPERATIONS_RETIRED",id);retired.push_back("7");f.now=12;f.retries->Packet(retired,f.now,false);
    assert(f.source_calls==0&&!f.Current(id).received&&f.retries->Records().empty());
    f.Tick(30);assert(f.source_calls==1&&f.Current(id).received==30&&f.retries->Records().size()==1&&f.retries->Records()[0].received==30&&f.retries->Records()[0].status==CallbackAssertionStatus::Passed);++controls;
  }
  {
    Fixture f;const auto id=f.Begin();f.now=11;f.Raw(id,true);f.steps->WorkerExited(owner.worker_index,owner.pid,20);
    assert(f.Current(id).receipt==StepReceipt::WorkerExited&&f.Current(id).received==20&&f.retries->Physical()==1&&f.retries->Records().empty());
    f.now=21;f.retries->WorkerExited(owner.worker_index,owner.pid,f.now);
    assert(f.retries->IdleReady()&&f.retries->Records().size()==1&&f.retries->Records()[0].receipt==CallbackAssertionReceipt::WorkerExited&&f.retries->Records()[0].received==21&&f.retries->Records()[0].received>=*f.Current(id).received);++controls;
  }
  {
    Fixture f;assert(f.steps->Packet({"STEP_BEGIN","1","0","1","4","0","22706172656e7422","0"},f.now,false));
    const auto id=f.Begin("-","0",1);assert(id==2);
    // Production observes one actual waitpid/file-drained boundary, then gives
    // both ledgers that same observation. This component controls the event;
    // it does not supply a fabricated raw callback or claim an OS receipt.
    f.now=20;f.steps->WorkerExited(owner.worker_index,owner.pid,f.now);f.retries->WorkerExited(owner.worker_index,owner.pid,f.now);
    const auto* parent=f.steps->Find(1);const auto& child=f.Current(id);const auto& rows=f.retries->Records();
    assert(parent&&rows.size()==1&&rows[0].parent==parent->id&&child.parent==parent->id);
    assert(parent->receipt==StepReceipt::WorkerExited&&child.receipt==StepReceipt::WorkerExited&&rows[0].receipt==CallbackAssertionReceipt::WorkerExited&&rows[0].status==CallbackAssertionStatus::Infrastructure);
    assert(parent->received==20&&child.received==parent->received&&rows[0].received==*parent->received);
    assert(!parent->callback_received&&!child.callback_received&&rows[0].attempts==1&&f.steps->IdleReady()&&f.retries->IdleReady());++controls;
  }
  std::cout<<"native callback retry controller controls "<<controls<<" passed (real RunnerSteps; controlled file/Source/exit seams)\n";
}
