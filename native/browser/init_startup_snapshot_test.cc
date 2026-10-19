// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "init_startup_snapshot.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

// A genuine standard-allocation failure in the production decoder/ledger,
// enabled only around the direct method call under test (not a mirror guard).
static bool fail_allocation=false;
__attribute__((noinline)) void*operator new(std::size_t bytes){if(fail_allocation)throw std::bad_alloc();if(void*memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();}
__attribute__((noinline)) void*operator new[](std::size_t bytes){return ::operator new(bytes);}
__attribute__((noinline)) void operator delete(void*memory)noexcept{std::free(memory);}
__attribute__((noinline)) void operator delete[](void*memory)noexcept{std::free(memory);}
__attribute__((noinline)) void operator delete(void*memory,std::size_t)noexcept{std::free(memory);}
__attribute__((noinline)) void operator delete[](void*memory,std::size_t)noexcept{std::free(memory);}
using Ledger=suitecut::InitStartupSelections;
using Receiver=suitecut::InitSnapshotReceiver;
using Owner=suitecut::InitSnapshotOwner;
using Pin=suitecut::InitSnapshotPin;
using Status=Ledger::Status;
Owner Actual(){return {4,8,19,"actual-main-channel"};}
std::string Body(std::string source="script()",int binding=37,int credential=0,bool installed=false){
  return "{\"contextId\":4,\"version\":1,\"scripts\":[{\"id\":1,\"source\":\""+source+"\"}],\"bindingVersion\":"+std::to_string(binding)+",\"bindings\":[{\"id\":21,\"source\":\"binding()\"}],\"credentialVersion\":"+std::to_string(credential)+",\"credentialsInstalled\":"+(installed?"true":"false")+"}";
}
struct Memory {std::vector<uint8_t>bytes;int*destroyed;~Memory(){++*destroyed;}};
struct Fixture {
  // Destroy counts outlive the actual receiver/ledger mappings.
  int destroyed=0;Receiver receiver;Ledger ledger;Owner owner=Actual();uint64_t transfer=0;
  Fixture(){assert(receiver.Admit(owner)&&ledger.Admit(owner));}
  std::shared_ptr<const Receiver::Lease>Lease(std::string_view body,uint64_t version=0){
    Pin pin{owner,version?version:transfer+1,transfer+1};++transfer;
    auto memory=std::make_shared<Memory>();memory->destroyed=&destroyed;memory->bytes.resize(suitecut::InitSnapshotWire::Size(pin,body.size()));
    assert(suitecut::InitSnapshotWire::Encode(memory->bytes,pin,body));
    auto result=receiver.Install({memory,memory->bytes},pin,0,1);assert(result.status==Receiver::Status::Installed);return result.lease;
  }
  void Finish(){ledger.Closed();receiver.Closed();receiver.TakeReleased();assert(!ledger.Readers()&&!ledger.Snapshots()&&!ledger.Bytes()&&!receiver.Physical()&&!receiver.Bytes());}
};
int main(){cef_api_hash(CEF_API_VERSION,0);int groups=0;
 {auto decoded=suitecut::DecodeInitStartupSnapshot(Body(),4);assert(decoded&&decoded->context==4&&decoded->script_version==1&&decoded->binding_version==37&&decoded->credential_version==0&&decoded->scripts[0].source=="script()"&&decoded->bindings[0].id==21);++groups;}
 {for(auto body:{"{}","[]","null","{bad", "{\"contextId\":4}"})assert(!suitecut::DecodeInitStartupSnapshot(body,4));assert(!suitecut::DecodeInitStartupSnapshot(Body(),5));++groups;}
 {auto valid=Body();for(const auto&[old,next]:std::vector<std::pair<std::string,std::string>>{{"\"version\":1","\"version\":2"},{"\"id\":1","\"id\":2"},{"\"bindingVersion\":37","\"bindingVersion\":-1"},{"\"credentialVersion\":0","\"credentialVersion\":-1"},{"\"credentialsInstalled\":false","\"credentialsInstalled\":0"},{"\"source\":\"script()\"","\"source\":\"\""},{"\"source\":\"script()\"","\"source\":\"bad\\u0000source\""}}){auto body=valid;body.replace(body.find(old),old.size(),next);assert(!suitecut::DecodeInitStartupSnapshot(body,4));}++groups;}
 {auto body=Body();body.replace(body.find("[{\"id\":21"),std::string("[{\"id\":21,\"source\":\"binding()\"}]").size(),"[{\"id\":21,\"source\":\"a\"},{\"id\":21,\"source\":\"b\"}]");assert(!suitecut::DecodeInitStartupSnapshot(body,4));assert(!suitecut::DecodeInitStartupSnapshot(Body(std::string(65537,'x')),4));++groups;}
 {Fixture f;auto first=f.Lease(Body());assert(f.ledger.Install(f.owner,first)==Status::Installed);auto selected=f.ledger.Select(f.owner,{"actual-child",1});assert(selected&&f.ledger.Current(selected));auto bad=f.Lease("{}");assert(f.ledger.Install(f.owner,bad)==Status::Malformed&&f.ledger.Latest(f.owner)->Pin()==first->Pin()&&f.ledger.Current(selected));selected.reset();first.reset();bad.reset();f.Finish();++groups;}
 {Fixture f;auto first=f.Lease(Body());assert(f.ledger.Install(f.owner,first)==Status::Installed&&f.ledger.Install(f.owner,first)==Status::Duplicate);Receiver other;assert(other.Admit(f.owner));auto memory=std::make_shared<Memory>();memory->destroyed=&f.destroyed;auto changed=Body("other()");memory->bytes.resize(suitecut::InitSnapshotWire::Size(first->Pin(),changed.size()));assert(suitecut::InitSnapshotWire::Encode(memory->bytes,first->Pin(),changed));auto conflict=other.Install({memory,memory->bytes},first->Pin(),0,1).lease;assert(f.ledger.Install(f.owner,conflict)==Status::Conflict&&f.ledger.Latest(f.owner)->Data().scripts[0].source=="script()");conflict.reset();other.Closed();other.TakeReleased();memory.reset();first.reset();f.Finish();++groups;}
 {Fixture f;auto first=f.Lease(Body());assert(f.ledger.Install(f.owner,first)==Status::Installed);auto next=f.Lease(Body("changed()",2,81,false));assert(f.ledger.Install(f.owner,next)==Status::Installed&&f.ledger.Latest(f.owner)->Data().binding_version==2&&f.ledger.Latest(f.owner)->Data().credential_version==81);assert(f.ledger.Install(f.owner,first)==Status::Stale);first.reset();next.reset();f.Finish();++groups;}
 {Fixture f;auto lease=f.Lease(Body());for(int field=0;field<4;++field){auto wrong=f.owner;if(field==0)++wrong.context;if(field==1)++wrong.browser;if(field==2)++wrong.renderer_epoch;if(field==3)wrong.frame="other-channel";assert(f.ledger.Install(wrong,lease)==Status::NotAdmitted);assert(f.ledger.Admit(wrong)&&f.ledger.Install(wrong,lease)==Status::Malformed&&!f.ledger.Select(wrong,{"actual-child",1}));}assert(f.ledger.Install(f.owner,lease)==Status::Installed);lease.reset();f.Finish();++groups;}
 {Fixture f;auto first=f.Lease(Body());assert(f.ledger.Install(f.owner,first)==Status::Installed);auto old=f.ledger.Select(f.owner,{"child",1});assert(old);auto next=f.Lease(Body("new()"));assert(f.ledger.Install(f.owner,next)==Status::Installed);auto current=f.ledger.Select(f.owner,{"child",2});assert(current&&old->Snapshot().Pin()==first->Pin()&&current->Snapshot().Pin()==next->Pin()&&f.ledger.Current(old)&&f.ledger.Current(current));assert(!f.ledger.Select(f.owner,{"child",1}));first.reset();next.reset();f.receiver.Closed();assert(f.receiver.TakeReleased().empty());assert(f.ledger.Release(f.owner,old->Identity())&&!f.ledger.Current(old)&&f.ledger.Readers()==2);old.reset();assert(f.receiver.TakeReleased().size()==1&&f.ledger.Readers()==1);current.reset();f.Finish();++groups;}
 {Fixture f;assert(!f.ledger.Select(f.owner,{"child",1}));auto lease=f.Lease(Body());assert(f.ledger.Install(f.owner,lease)==Status::Installed);assert(!f.ledger.Select(f.owner,{"child",1}));auto selected=f.ledger.Select(f.owner,{"child",2});assert(selected);selected.reset();lease.reset();f.Finish();++groups;}
 {Fixture f;auto lease=f.Lease(Body("s()",2,7,true));assert(f.ledger.Install(f.owner,lease)==Status::Installed&&!f.ledger.Select(f.owner,{"child",1}));assert(f.ledger.ResetReadiness(f.owner,10,7));assert(!f.ledger.AdmitReadiness(lease->Pin(),9,7)&&!f.ledger.AdmitReadiness(lease->Pin(),10,6));auto wrong=lease->Pin();++wrong.transfer;assert(!f.ledger.AdmitReadiness(wrong,10,7));assert(f.ledger.AdmitReadiness(lease->Pin(),10,7));auto selected=f.ledger.Select(f.owner,{"child",2});assert(selected&&f.ledger.Current(selected));assert(f.ledger.ResetReadiness(f.owner,11,8)&&!f.ledger.Current(selected)&&!f.ledger.Select(f.owner,{"child",3}));assert(!f.ledger.ResetReadiness(f.owner,10,7)&&!f.ledger.AdmitReadiness(lease->Pin(),10,7)&&!f.ledger.AdmitReadiness(lease->Pin(),11,7));selected.reset();lease.reset();f.Finish();++groups;}
 {Fixture f;auto first=f.Lease(Body("old()",1,3,true));assert(f.ledger.Install(f.owner,first)==Status::Installed&&f.ledger.ResetReadiness(f.owner,1,3)&&f.ledger.AdmitReadiness(first->Pin(),1,3));auto old=f.ledger.Select(f.owner,{"child",1});auto next=f.Lease(Body("new()",1,3,true));assert(f.ledger.Install(f.owner,next)==Status::Installed&&!f.ledger.Select(f.owner,{"child",2}));assert(!f.ledger.AdmitReadiness(first->Pin(),1,3)&&f.ledger.AdmitReadiness(next->Pin(),1,3));assert(f.ledger.Select(f.owner,{"child",3})&&f.ledger.Current(old));old.reset();first.reset();next.reset();f.Finish();++groups;}
 {Fixture f;auto lease=f.Lease(Body("clear()",1,4,false));assert(f.ledger.Install(f.owner,lease)==Status::Installed&&!f.ledger.Select(f.owner,{"child",1}));assert(f.ledger.ResetReadiness(f.owner,3,4)&&f.ledger.AdmitReadiness(lease->Pin(),3,4));auto selected=f.ledger.Select(f.owner,{"child",2});assert(selected&&f.ledger.Current(selected));assert(f.ledger.ResetReadiness(f.owner,4,5)&&!f.ledger.Current(selected));selected.reset();lease.reset();f.Finish();++groups;}
 {Fixture f;auto lease=f.Lease(Body());assert(f.ledger.Install(f.owner,lease)==Status::Installed);for(size_t i=1;i<=Ledger::kReaders;++i)assert(f.ledger.Select(f.owner,{"child",i}));assert(f.ledger.Readers()==Ledger::kReaders&&!f.ledger.Select(f.owner,{"child",Ledger::kReaders+1}));assert(!f.ledger.Release(f.owner,{"wrong-frame",1}));assert(f.ledger.Release(f.owner,{"child",1})&&f.ledger.Readers()==Ledger::kReaders-1&&f.ledger.Select(f.owner,{"child",Ledger::kReaders+2}));lease.reset();f.Finish();++groups;}
 {Fixture f;auto lease=f.Lease(Body());assert(f.ledger.Install(f.owner,lease)==Status::Installed);auto selected=f.ledger.Select(f.owner,{"child",1});lease.reset();assert(f.ledger.Retire(f.owner)&&!f.ledger.Current(selected)&&f.ledger.Readers()==1);f.receiver.Closed();assert(f.receiver.TakeReleased().empty());selected.reset();assert(f.receiver.TakeReleased().size()==1&&f.ledger.Readers()==0);f.Finish();++groups;}
 {Fixture f;auto first=f.Lease(Body());assert(f.ledger.Install(f.owner,first)==Status::Installed);auto next=f.Lease(Body("new()"));fail_allocation=true;auto status=f.ledger.Install(f.owner,next);fail_allocation=false;assert(status==Status::Allocation&&f.ledger.Latest(f.owner)->Pin()==first->Pin()&&f.ledger.Snapshots()==1);auto old=f.ledger.Select(f.owner,{"child",1});fail_allocation=true;auto none=f.ledger.Select(f.owner,{"child",2});fail_allocation=false;assert(!none&&f.ledger.Current(old)&&f.ledger.Readers()==1&&!f.ledger.Select(f.owner,{"child",2}));old.reset();first.reset();next.reset();f.Finish();++groups;}
 {Fixture f;for(size_t i=1;i<=Ledger::kSnapshots;++i){auto lease=f.Lease(Body());assert(f.ledger.Install(f.owner,lease)==Status::Installed&&f.ledger.Select(f.owner,{"child",i}));}assert(f.ledger.Snapshots()==Ledger::kSnapshots);Fixture other;other.transfer=64;auto next=other.Lease(Body());assert(f.ledger.Install(f.owner,next)==Status::Capacity&&f.ledger.Latest(f.owner)->Pin().version==64);assert(f.ledger.Release(f.owner,{"child",1}));f.receiver.TakeReleased();assert(f.ledger.Snapshots()==63&&f.ledger.Install(f.owner,next)==Status::Installed);next.reset();f.Finish();other.Finish();++groups;}
 {Ledger ledger;for(size_t i=0;i<Ledger::kOwners;++i){auto owner=Actual();owner.browser+=i;assert(ledger.Admit(owner));}auto extra=Actual();extra.browser+=64;assert(!ledger.Admit(extra)&&ledger.Owners()==64);auto invalid=Actual();invalid.context=0;assert(!ledger.Admit(invalid));ledger.Closed();assert(!ledger.Owners());++groups;}
 std::printf("Init startup snapshot: %d controlled groups PASS (data ledger only; no startup/capture activation)\n",groups);
}
