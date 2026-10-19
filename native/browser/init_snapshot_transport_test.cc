// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "init_snapshot_transport.h"
#include <cassert>
#include <cstdio>
using Wire=suitecut::InitSnapshotWire;
using Receipts=suitecut::InitSnapshotReceipts;
using Owner=suitecut::InitSnapshotOwner;
using Pin=suitecut::InitSnapshotPin;
using Status=Receipts::Status;
Owner Actual(){return {4,8,19,"actual-native-frame"};}
int main(){int groups=0;
 {Pin pin{Actual(),7,3};const std::string text="{\"version\":7,\"raw\":\"\\ud800\",\"nested\":[1,2,3]}";
  std::vector<uint8_t>bytes(Wire::Size(pin,text.size()));assert(Wire::Encode(bytes,pin,text));
  auto view=Wire::Decode(bytes);assert(view&&view->pin==pin&&view->payload==text);++groups;}
 {Pin pin{Actual(),7,3};std::vector<uint8_t>bytes(Wire::Size(pin,2));assert(Wire::Encode(bytes,pin,"{}"));
  for(size_t n=0;n<bytes.size();++n)assert(!Wire::Decode({bytes.data(),n}));
  bytes.push_back(0);assert(!Wire::Decode(bytes));++groups;}
 {Pin pin{Actual(),7,3};std::vector<uint8_t>original(Wire::Size(pin,2));assert(Wire::Encode(original,pin,"{}"));
  for(auto offset:{size_t{0},size_t{8},size_t{12},size_t{56},size_t{60}}){auto bytes=original;bytes[offset]^=0xff;assert(!Wire::Decode(bytes));}++groups;}
 {Pin pin{Actual(),7,3};assert(!Wire::Size(pin,0)&&!Wire::Size(pin,Wire::kPayload+1));
  pin.owner.frame=std::string(Wire::kFrame+1,'f');assert(!Wire::Size(pin,2));
  pin.owner=Actual();pin.owner.frame.push_back('\0');assert(!Wire::Size(pin,2));
  pin.owner=Actual();pin.owner.renderer_epoch=0;assert(!Wire::Size(pin,2));
  pin.owner=Actual();pin.version=0;assert(!Wire::Size(pin,2));++groups;}
 {Receipts r;auto pin=r.Reserve(Actual(),1,2,10,1);assert(pin&&r.Physical()==1&&r.Bytes()>2);
  assert(!r.Acknowledge(*pin,true,2));assert(r.Submitted(pin->transfer,2)&&!r.Refused(pin->transfer));
  auto event=r.Acknowledge(*pin,true,3);assert(event&&event->public_reply&&event->status==Status::Accepted&&event->region_installed&&r.Physical()==1&&r.Bytes()>0&&!r.NeedsTick());assert(r.Released(*pin)&&r.Physical()==0&&r.Bytes()==0);++groups;}
 {Receipts r;auto pin=r.Reserve(Actual(),1,2,10,1);assert(pin&&r.Submitted(pin->transfer,2));
  for(int part=0;part<6;++part){auto wrong=*pin;if(part==0)++wrong.owner.context;if(part==1)++wrong.owner.browser;if(part==2)++wrong.owner.renderer_epoch;if(part==3)wrong.owner.frame="other";if(part==4)++wrong.version;if(part==5)++wrong.transfer;assert(!r.Acknowledge(wrong,true,3)&&r.Physical()==1);}
  assert(r.Acknowledge(*pin,true,3));++groups;}
 {Receipts r;auto pin=r.Reserve(Actual(),1,2,0,1);assert(pin&&r.Submitted(pin->transfer,2));const auto bytes=r.Bytes();
  auto canceled=r.Cancel(pin->transfer,3);assert(canceled&&canceled->public_reply&&canceled->status==Status::Canceled&&r.Physical()==1&&r.Bytes()==bytes);
  assert(!r.Cancel(pin->transfer,4));auto events=r.Tick(33);assert(events.size()==1&&events[0].status==Status::CloseRequired&&!events[0].public_reply);
  assert(r.Physical()==1&&r.Bytes()==bytes&&!r.NeedsTick()&&r.Tick(1000).empty());
  auto late=r.Acknowledge(*pin,true,1001);assert(late&&!late->public_reply&&late->status==Status::Canceled&&r.Bytes()==bytes);assert(r.Released(*pin)&&r.Bytes()==0);++groups;}
 {Receipts r;auto pin=r.Reserve(Actual(),1,2,10,1);assert(pin&&r.Submitted(pin->transfer,2));
  auto events=r.Tick(10);assert(events.size()==1&&events[0].status==Status::Expired&&events[0].public_reply&&r.Physical()==1);
  assert(r.Tick(39).empty());events=r.Tick(40);assert(events.size()==1&&events[0].status==Status::CloseRequired&&r.Physical()==1);
  auto late=r.Acknowledge(*pin,false,41);assert(late&&!late->public_reply&&late->status==Status::Expired&&r.Physical()==0);++groups;}
 {Receipts r;auto pin=r.Reserve(Actual(),1,2,10,1);assert(pin&&r.Submitted(pin->transfer,2));
  auto event=r.Acknowledge(*pin,true,10);assert(event&&event->status==Status::Expired&&event->public_reply);++groups;}
 {Receipts r;auto pin=r.Reserve(Actual(),1,2,10,1);assert(pin&&!r.Submitted(pin->transfer,10)&&r.Refused(pin->transfer)&&r.Bytes()==0);
  pin=r.Reserve(Actual(),1,2,0,2);assert(pin&&r.Cancel(pin->transfer,3)&&!r.Submitted(pin->transfer,4)&&r.Refused(pin->transfer));++groups;}
 {Receipts r;auto pin=r.Reserve(Actual(),1,2,0,1);assert(pin&&r.Submitted(pin->transfer,2));
  auto other=Actual();other.renderer_epoch++;auto fresh=r.Reserve(other,1,2,0,3);assert(fresh&&r.Submitted(fresh->transfer,3));
  assert(r.RendererGone(8,18).empty()&&r.RendererGone(9,19).empty()&&r.Physical()==2);
  const auto events=r.RendererGone(8,19);assert(events.size()==1&&events[0].status==Status::RendererGone&&events[0].public_reply&&r.Physical()==1);
  assert(!r.Acknowledge(*pin,true,4)&&r.Acknowledge(*fresh,true,4)&&r.Bytes()>0);assert(r.Released(*fresh)&&r.Bytes()==0);++groups;}
 {Receipts r;std::vector<Pin>pins;for(size_t i=0;i<Receipts::kTransfers;++i){auto pin=r.Reserve(Actual(),i+1,2,0,1);assert(pin&&r.Submitted(pin->transfer,1));pins.push_back(*pin);}
  assert(!r.Reserve(Actual(),100,2,0,1));for(const auto&pin:pins)assert(r.Cancel(pin.transfer,2));assert(!r.Reserve(Actual(),100,2,0,2)&&r.Physical()==Receipts::kTransfers);
  assert(r.Acknowledge(pins.front(),true,3)&&!r.Reserve(Actual(),100,2,0,3));assert(r.Released(pins.front())&&r.Reserve(Actual(),100,2,0,3));++groups;}
 {Receipts r;std::vector<Pin>pins;for(size_t i=0;i<7;++i){auto pin=r.Reserve(Actual(),i+1,Wire::kPayload,0,1);assert(pin&&r.Submitted(pin->transfer,1));pins.push_back(*pin);}
  assert(!r.Reserve(Actual(),8,Wire::kPayload,0,1)&&r.Bytes()<Receipts::kBytes);
  assert(r.Acknowledge(pins.front(),true,2)&&!r.Reserve(Actual(),8,Wire::kPayload,0,2));assert(r.Released(pins.front())&&r.Reserve(Actual(),8,Wire::kPayload,0,2));++groups;}
 {Receipts r;for(double bad:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){assert(!r.Reserve(Actual(),1,2,bad,1)&&!r.Reserve(Actual(),1,2,0,bad));}
  assert(!r.Reserve(Actual(),1,2,1,1)&&r.Physical()==0);++groups;}
 {Receipts r;auto pin=r.Reserve(Actual(),1,2,10,1);assert(pin&&r.Submitted(pin->transfer,1));const auto bytes=r.Bytes();
  auto wrong=*pin;wrong.owner.renderer_epoch++;assert(!r.Released(wrong));assert(r.Released(*pin)&&r.Physical()==1&&r.Bytes()==bytes);
  const auto event=r.Acknowledge(*pin,true,2);assert(event&&event->status==Status::Accepted&&r.Physical()==0&&r.Bytes()==0);++groups;}
 {Receipts r;auto pin=r.Reserve(Actual(),1,2,10,1);assert(pin&&r.Submitted(pin->transfer,1));assert(r.Acknowledge(*pin,true,2)&&!r.NeedsTick());
  assert(r.Retire(pin->transfer,3)&&r.NeedsTick());auto events=r.Tick(33);assert(events.size()==1&&events[0].status==Status::CloseRequired&&r.Physical()==1&&!r.NeedsTick());
  assert(r.RendererGone(8,19).size()==1&&r.Bytes()==0);++groups;}
 {Receipts r;auto old=r.Reserve(Actual(),1,2,0,1);auto current=r.Reserve(Actual(),2,2,0,1);auto other=Actual();++other.renderer_epoch;auto unrelated=r.Reserve(other,1,2,0,1);
  assert(old&&current&&unrelated&&r.Submitted(old->transfer,1)&&r.Submitted(current->transfer,1)&&r.Submitted(unrelated->transfer,1));
  assert(r.Acknowledge(*old,true,2)&&r.Acknowledge(*current,true,3)&&r.Acknowledge(*unrelated,true,3));const auto bytes=r.Bytes();
  assert(r.RetireSuperseded(Actual(),2,4)==1&&r.RetireSuperseded(Actual(),2,5)==0&&r.Physical()==3&&r.Bytes()==bytes);
  assert(r.Tick(33).empty());const auto events=r.Tick(34);assert(events.size()==1&&events[0].pin==*old&&events[0].status==Status::CloseRequired&&r.Physical()==3);
  assert(r.Released(*old)&&r.Physical()==2&&!r.NeedsTick());++groups;}
 {Receipts r;auto old=r.Reserve(Actual(),1,2,0,1);auto current=r.Reserve(Actual(),2,2,0,1);assert(old&&current&&r.Submitted(old->transfer,1)&&r.Submitted(current->transfer,1));
  assert(r.Acknowledge(*current,true,2)&&r.RetireSuperseded(Actual(),2,2)==0&&!r.NeedsTick());
  assert(r.Acknowledge(*old,true,3)&&r.RetireSuperseded(Actual(),2,3)==1);const auto bytes=r.Bytes();
  assert(r.RetireSuperseded(Actual(),1,4)==0&&r.RetireSuperseded(Actual(),2,-1)==0&&r.Bytes()==bytes);
  const auto events=r.Tick(33);assert(events.size()==1&&events[0].pin==*old&&r.Physical()==2);assert(r.Released(*old)&&r.Released(*current)&&r.Bytes()==0);++groups;}
 std::printf("Init snapshot immutable wire/physical receipts: %d groups PASS\n",groups);
}
