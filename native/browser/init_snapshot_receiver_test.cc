// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "init_snapshot_receiver.h"
#include <cassert>
#include <cstdio>
using Receiver=suitecut::InitSnapshotReceiver;
using Owner=suitecut::InitSnapshotOwner;
using Pin=suitecut::InitSnapshotPin;
using Status=Receiver::Status;
Owner Actual(){return {4,8,19,"actual-native-frame"};}
struct Memory {std::vector<uint8_t>bytes;int*destroyed;~Memory(){++*destroyed;}};
Receiver::Region Region(const Pin&pin,std::string_view text,int&destroyed){
 auto memory=std::make_shared<Memory>();memory->destroyed=&destroyed;
 memory->bytes.resize(suitecut::InitSnapshotWire::Size(pin,text.size()));
 assert(suitecut::InitSnapshotWire::Encode(memory->bytes,pin,text));
 return {memory,memory->bytes};
}
int main(){int groups=0;
 {int destroyed=0;Receiver receiver;Pin pin{Actual(),1,1};
  const auto denied=receiver.Install(Region(pin,"{}",destroyed),pin,0,1);
  assert(denied.status==Status::NotAdmitted&&destroyed==1);
  assert(receiver.Admit(pin.owner));auto result=receiver.Install(Region(pin,"{}",destroyed),pin,0,1);
  assert(result.status==Status::Installed&&receiver.Read(pin)==result.lease&&receiver.Physical()==1);
  receiver.Retire(pin.owner);assert(!receiver.Read(pin)&&receiver.TakeReleased().empty());
  result.lease.reset();assert(destroyed==2&&receiver.Physical()==1);
  assert(receiver.TakeReleased()==std::vector<Pin>{pin}&&receiver.Physical()==0&&receiver.Bytes()==0);++groups;}
 {int destroyed=0;Receiver r;Pin pin{Actual(),3,1};assert(r.Admit(pin.owner));
  auto first=r.Install(Region(pin,"{\"raw\":\"\\ud800\"}",destroyed),pin,0,1);assert(first.status==Status::Installed);
  auto duplicate=r.Install(Region(pin,"{\"raw\":\"\\ud800\"}",destroyed),pin,0,1);
  assert(duplicate.status==Status::Duplicate&&duplicate.lease==first.lease&&r.Physical()==1&&destroyed==1);
  assert(r.Install(Region(pin,"[]",destroyed),pin,0,1).status==Status::Conflict);
  auto other=pin;++other.transfer;assert(r.Install(Region(other,"{}",destroyed),other,0,1).status==Status::Conflict);
  --other.version;assert(r.Install(Region(other,"{}",destroyed),other,0,1).status==Status::Stale);
  other=pin;++other.version;assert(r.Install(Region(other,"{}",destroyed),other,0,1).status==Status::Conflict);
  duplicate.lease.reset();first.lease.reset();r.Closed();assert(r.TakeReleased().size()==1&&r.Bytes()==0);++groups;}
 {int destroyed=0;Receiver r;Pin old{Actual(),1,1},next{Actual(),2,2};assert(r.Admit(old.owner));
  auto first=r.Install(Region(old,"old",destroyed),old,0,1);auto second=r.Install(Region(next,"new",destroyed),next,0,2);
  assert(second.status==Status::Installed&&!r.Read(old)&&r.Read(next)&&first.lease->Payload()=="old"&&r.Physical()==2);
  first.lease.reset();assert(r.TakeReleased()==std::vector<Pin>{old}&&r.Physical()==1);
  second.lease.reset();r.Closed();assert(r.TakeReleased()==std::vector<Pin>{next});++groups;}
 {int destroyed=0;Receiver r;Pin pin{Actual(),1,1};assert(r.Admit(pin.owner));
  auto wrong=pin;++wrong.owner.renderer_epoch;assert(r.Install(Region(wrong,"{}",destroyed),pin,0,1).status==Status::Malformed);
  assert(r.Install(Region(pin,"{}",destroyed),pin,1,1).status==Status::Expired&&r.Physical()==0);
  assert(r.Install(Region(pin,"{}",destroyed),pin,0,std::numeric_limits<double>::quiet_NaN()).status==Status::Expired);++groups;}
 {int destroyed=0;Receiver r;std::vector<std::shared_ptr<const Receiver::Lease>>readers;auto owner=Actual();assert(r.Admit(owner));
  for(size_t i=0;i<suitecut::InitSnapshotReceipts::kTransfers;++i){Pin pin{owner,i+1,i+1};auto result=r.Install(Region(pin,"{}",destroyed),pin,0,1);assert(result.status==Status::Installed);readers.push_back(result.lease);}
  Pin next{owner,65,65};assert(r.Install(Region(next,"{}",destroyed),next,0,1).status==Status::Capacity);
  readers.front().reset();assert(r.Physical()==64&&r.Install(Region(next,"{}",destroyed),next,0,1).status==Status::Capacity);
  assert(r.TakeReleased().size()==1&&r.Install(Region(next,"{}",destroyed),next,0,1).status==Status::Installed);
  readers.clear();r.Closed();assert(r.TakeReleased().size()==64&&r.Bytes()==0);++groups;}
 {int destroyed=0;Receiver r;auto owner=Actual();assert(r.Admit(owner));std::vector<std::shared_ptr<const Receiver::Lease>>readers;
  const std::string text(suitecut::InitSnapshotWire::kPayload,'x');
  for(size_t i=0;i<7;++i){Pin pin{owner,i+1,i+1};auto result=r.Install(Region(pin,text,destroyed),pin,0,1);assert(result.status==Status::Installed);readers.push_back(result.lease);}
  Pin next{owner,8,8};assert(r.Install(Region(next,text,destroyed),next,0,1).status==Status::Capacity&&r.Bytes()<suitecut::InitSnapshotReceipts::kBytes);
  readers.clear();r.Closed();assert(r.TakeReleased().size()==7&&r.Bytes()==0);++groups;}
 {Receiver r;for(size_t i=0;i<64;++i){auto owner=Actual();owner.frame+=std::to_string(i);assert(r.Admit(owner));}
  auto owner=Actual();assert(!r.Admit(owner)&&r.Owners()==64);owner.context=0;assert(!r.Admit(owner));r.Closed();assert(r.Owners()==0);++groups;}
 {int destroyed=0;Receiver r;Pin pin{Actual(),1,1};assert(r.Admit(pin.owner));auto result=r.Install(Region(pin,"{}",destroyed),pin,0,1);
  auto wrong=pin;++wrong.transfer;assert(!r.Read(wrong));assert(r.Retire(pin.owner)&&!r.Retire(pin.owner));
  assert(r.Install(Region(pin,"{}",destroyed),pin,0,1).status==Status::NotAdmitted);
  r.Closed();assert(result.lease->Payload()=="{}"&&r.TakeReleased().empty());result.lease.reset();assert(r.TakeReleased().size()==1);++groups;}
 {int destroyed=0;std::shared_ptr<const Receiver::Lease>reader;{Receiver r;Pin pin{Actual(),1,1};assert(r.Admit(pin.owner));reader=r.Install(Region(pin,"{}",destroyed),pin,0,1).lease;}
  assert(destroyed==0&&reader->Payload()=="{}");reader.reset();assert(destroyed==1);++groups;}
 std::printf("Init snapshot receiver: %d controlled groups PASS (no IPC/startup claim)\n",groups);
}
