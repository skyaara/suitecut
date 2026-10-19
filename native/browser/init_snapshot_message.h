// SPDX-License-Identifier: MIT
#pragma once
#include "init_snapshot_transport.h"
#include "init_snapshot_receiver.h"
#include "include/cef_shared_process_message_builder.h"
#include <memory>

namespace suitecut {
class InitSnapshotMessage {
 public:
  struct Lease {
    CefRefPtr<CefSharedMemoryRegion>region;
    InitSnapshotWire::View snapshot;
  };
  static CefRefPtr<CefProcessMessage>Build(const InitSnapshotPin&pin,std::string_view payload){
    const auto bytes=InitSnapshotWire::Size(pin,payload.size());if(!bytes)return nullptr;
    auto builder=CefSharedProcessMessageBuilder::Create(std::string(InitSnapshotWire::kMessage),bytes);
    if(!builder||!builder->IsValid()||builder->Size()!=bytes||!builder->Memory())return nullptr;
    if(!InitSnapshotWire::Encode({static_cast<uint8_t*>(builder->Memory()),bytes},pin,payload))return nullptr;
    return builder->Build();
  }
  // The caller derives expected from the actual browser/frame and admitted
  // native renderer epoch, never from the incoming packet itself.
  static std::shared_ptr<const Lease>Read(CefRefPtr<CefProcessMessage>message,const InitSnapshotPin&expected){
    if(!message||!message->IsValid()||message->GetName()!=std::string(InitSnapshotWire::kMessage))return nullptr;
    auto region=message->GetSharedMemoryRegion();
    if(!region||!region->IsValid()||region->Size()>InitSnapshotWire::kHeader+InitSnapshotWire::kFrame+InitSnapshotWire::kPayload||!region->Memory())return nullptr;
    auto view=InitSnapshotWire::Decode({static_cast<const uint8_t*>(region->Memory()),region->Size()});
    if(!view||view->pin!=expected)return nullptr;
    return std::make_shared<const Lease>(Lease{region,std::move(*view)});
  }
  static InitSnapshotReceiver::Result Install(InitSnapshotReceiver&receiver,
      CefRefPtr<CefProcessMessage>message,const InitSnapshotPin&expected,double deadline,double now){
    auto lease=Read(message,expected);
    if(!lease)return {InitSnapshotReceiver::Status::Malformed,{}};
    const auto bytes=std::span<const uint8_t>(static_cast<const uint8_t*>(lease->region->Memory()),lease->region->Size());
    return receiver.Install({std::move(lease),bytes},expected,deadline,now);
  }
};
} // namespace suitecut
