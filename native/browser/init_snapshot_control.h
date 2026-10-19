// SPDX-License-Identifier: MIT
#pragma once
#include "init_snapshot_transport.h"
#include "include/cef_process_message.h"
#include <charconv>
namespace suitecut {
struct InitSnapshotControl {
  static constexpr const char* kRequest="suitecutSnapshotRequestV1";
  static constexpr const char* kAdmission="suitecutSnapshotAdmissionV1";
  static constexpr const char* kReady="suitecutSnapshotReadyV1";
  static constexpr const char* kAck="suitecutSnapshotInstalledV1";
  static constexpr const char* kReleased="suitecutSnapshotReleasedV1";
  static constexpr const char* kClosed="suitecutSnapshotClosedV1";
  static bool UInt(const std::string&text,uint64_t&value){
    if(text.empty()||text.size()>20||text.front()=='0')return false;
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    return result.ec==std::errc{}&&result.ptr==text.data()+text.size()&&value!=0;
  }
  static CefRefPtr<CefProcessMessage>Admission(const char*kind,int context,uint64_t epoch,uint64_t request,const std::string&frame){
    auto message=CefProcessMessage::Create(kind);auto args=message->GetArgumentList();
    args->SetInt(0,context);args->SetString(1,std::to_string(epoch));args->SetString(2,std::to_string(request));args->SetString(3,frame);return message;
  }
  static bool Admission(CefRefPtr<CefProcessMessage>message,int&context,uint64_t&epoch,uint64_t&request,std::string&frame){
    auto args=message?message->GetArgumentList():nullptr;
    if(!args||args->GetSize()!=4||args->GetType(0)!=VTYPE_INT||args->GetType(1)!=VTYPE_STRING||args->GetType(2)!=VTYPE_STRING||args->GetType(3)!=VTYPE_STRING)return false;
    context=args->GetInt(0);frame=args->GetString(3);return context>0&&!frame.empty()&&frame.size()<=InitSnapshotWire::kFrame&&frame.find('\0')==std::string::npos&&UInt(args->GetString(1),epoch)&&UInt(args->GetString(2),request);
  }
  static CefRefPtr<CefProcessMessage>Receipt(const char*kind,const InitSnapshotPin&pin,bool installed=false){
    auto message=CefProcessMessage::Create(kind);auto args=message->GetArgumentList();
    args->SetInt(0,static_cast<int>(pin.owner.context));args->SetInt(1,static_cast<int>(pin.owner.browser));
    args->SetString(2,std::to_string(pin.owner.renderer_epoch));args->SetString(3,pin.owner.frame);
    args->SetString(4,std::to_string(pin.version));args->SetString(5,std::to_string(pin.transfer));args->SetBool(6,installed);return message;
  }
  static bool Receipt(CefRefPtr<CefProcessMessage>message,InitSnapshotPin&pin,bool&installed){
    auto args=message?message->GetArgumentList():nullptr;
    if(!args||args->GetSize()!=7||args->GetType(0)!=VTYPE_INT||args->GetType(1)!=VTYPE_INT||args->GetType(2)!=VTYPE_STRING||args->GetType(3)!=VTYPE_STRING||args->GetType(4)!=VTYPE_STRING||args->GetType(5)!=VTYPE_STRING||args->GetType(6)!=VTYPE_BOOL)return false;
    const int context=args->GetInt(0),browser=args->GetInt(1);if(context<=0||browser<=0)return false;
    pin.owner.context=static_cast<uint64_t>(context);pin.owner.browser=static_cast<uint64_t>(browser);pin.owner.frame=args->GetString(3);
    installed=args->GetBool(6);return UInt(args->GetString(2),pin.owner.renderer_epoch)&&UInt(args->GetString(4),pin.version)&&UInt(args->GetString(5),pin.transfer)&&InitSnapshotWire::ValidOwner(pin.owner);
  }
};
} // namespace suitecut
