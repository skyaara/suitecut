// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "init_snapshot_message.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <cstdio>
int main(){assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
 const suitecut::InitSnapshotPin pin{{4,8,19,"actual-native-frame"},7,3};
 const std::string text="{\"version\":7,\"raw\":\"\\ud800\",\"value\":\"genuine CEF mapped bytes\"}";
 auto message=suitecut::InitSnapshotMessage::Build(pin,text);assert(message&&message->IsValid());
 assert(!message->Copy()&&!message->GetArgumentList());++groups;
 auto lease=suitecut::InitSnapshotMessage::Read(message,pin);assert(lease&&lease->snapshot.pin==pin&&lease->snapshot.payload==text);++groups;
 message=nullptr;assert(lease->region->IsValid()&&lease->snapshot.payload==text);++groups;
 auto second=suitecut::InitSnapshotMessage::Build(pin,text);auto wrong=pin;++wrong.owner.renderer_epoch;
 assert(!suitecut::InitSnapshotMessage::Read(second,wrong));wrong=pin;wrong.owner.frame="other";assert(!suitecut::InitSnapshotMessage::Read(second,wrong));++groups;
 auto regular=CefProcessMessage::Create(std::string(suitecut::InitSnapshotWire::kMessage));regular->GetArgumentList()->SetString(0,text);
 assert(!suitecut::InitSnapshotMessage::Read(regular,pin));++groups;
 auto builder=CefSharedProcessMessageBuilder::Create(std::string(suitecut::InitSnapshotWire::kMessage),text.size());assert(builder&&builder->IsValid());
 std::memcpy(builder->Memory(),text.data(),text.size());auto bad=builder->Build();assert(bad&&!builder->IsValid()&&!suitecut::InitSnapshotMessage::Read(bad,pin));++groups;
 {
  suitecut::InitSnapshotReceiver receiver;assert(receiver.Admit(pin.owner));
  auto real=suitecut::InitSnapshotMessage::Build(pin,text);
  auto installed=suitecut::InitSnapshotMessage::Install(receiver,real,pin,10,1);
  assert(installed.status==suitecut::InitSnapshotReceiver::Status::Installed);
  real=nullptr;assert(installed.lease->Payload()==text&&receiver.Physical()==1);
  assert(receiver.Retire(pin.owner)&&receiver.TakeReleased().empty());
  installed.lease.reset();assert(receiver.TakeReleased()==std::vector<suitecut::InitSnapshotPin>{pin}&&receiver.Bytes()==0);++groups;
 }
 {
  suitecut::InitSnapshotReceiver receiver;assert(receiver.Admit(pin.owner));
  auto real=suitecut::InitSnapshotMessage::Build(pin,text);
  assert(suitecut::InitSnapshotMessage::Install(receiver,real,pin,1,1).status==suitecut::InitSnapshotReceiver::Status::Expired);
  assert(receiver.Physical()==0&&receiver.TakeReleased().empty());
  assert(suitecut::InitSnapshotMessage::Install(receiver,regular,pin,0,1).status==suitecut::InitSnapshotReceiver::Status::Malformed);++groups;
 }
 lease.reset();second=nullptr;bad=nullptr;
 std::printf("Init snapshot actual CEF region mapping: %d groups PASS (same-process API test; no renderer delivery claim)\n",groups);
}
