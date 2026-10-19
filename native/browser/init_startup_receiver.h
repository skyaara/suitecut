// SPDX-License-Identifier: MIT
#pragma once
#include "init_snapshot_message.h"
#include "init_snapshot_control.h"
#include "init_startup_peer.h"
#include "init_startup_snapshot.h"

namespace suitecut {
// Bounded cross-process DATA receipt only. Delivery is never inferred here.
struct InitStartupSemanticMessage {
  static constexpr const char*kName="suitecutStartupSemanticReceiptV1";
  static CefRefPtr<CefProcessMessage>Build(const InitSnapshotPin&pin,int credential_version,bool credentials_installed){
    auto message=InitSnapshotControl::Receipt(kName,pin,true);auto args=message->GetArgumentList();
    args->SetInt(7,credential_version);args->SetBool(8,credentials_installed);return message;
  }
  static bool Read(CefRefPtr<CefProcessMessage>message,InitSnapshotPin&pin,int&credential_version,bool&credentials_installed){
    if(!message||!message->IsValid()||message->GetName()!=kName)return false;
    auto args=message->GetArgumentList();
    if(!args||args->GetSize()!=9||args->GetType(0)!=VTYPE_INT||args->GetType(1)!=VTYPE_INT||
       args->GetType(2)!=VTYPE_STRING||args->GetType(3)!=VTYPE_STRING||args->GetType(4)!=VTYPE_STRING||args->GetType(5)!=VTYPE_STRING||
       args->GetType(6)!=VTYPE_BOOL||!args->GetBool(6)||args->GetType(7)!=VTYPE_INT||args->GetInt(7)<0||args->GetType(8)!=VTYPE_BOOL)return false;
    const int context=args->GetInt(0),browser=args->GetInt(1);if(context<=0||browser<=0)return false;
    pin.owner.context=context;pin.owner.browser=browser;pin.owner.frame=args->GetString(3);
    if(!InitSnapshotControl::UInt(args->GetString(2),pin.owner.renderer_epoch)||!InitSnapshotControl::UInt(args->GetString(4),pin.version)||
       !InitSnapshotControl::UInt(args->GetString(5),pin.transfer)||!InitSnapshotWire::ValidOwner(pin.owner))return false;
    credential_version=args->GetInt(7);credentials_installed=args->GetBool(8);return true;
  }
};

// Renderer-thread-only unused data adapter. No browser operation registry or
// Engine clock is accessible here; browser success is decided on actual receipt
// by InitStartupReadiness below. No bootstrap code calls this adapter.
class InitStartupReceiver {
 public:
  using Status=InitStartupSelections::Status;using Peer=InitStartupRendererPeer::Ptr;
  class InstalledReceipt {
   public:
    const InitSnapshotPin&Pin()const{return lease_->Pin();}
    CefRefPtr<CefProcessMessage>Message()const{return InitStartupSemanticMessage::Build(Pin(),credential_version_,credentials_installed_);}
   private:
    friend class InitStartupReceiver;
    InstalledReceipt(Peer peer,std::shared_ptr<const InitSnapshotReceiver::Lease>lease,int version,bool installed):peer_(std::move(peer)),lease_(std::move(lease)),credential_version_(version),credentials_installed_(installed){}
    Peer peer_;std::shared_ptr<const InitSnapshotReceiver::Lease>lease_;int credential_version_;bool credentials_installed_;
  };
  struct Result {Status status=Status::Malformed;bool mapping_observed=false;std::shared_ptr<const InstalledReceipt>receipt;};
  class Reader {
   public:
    const InitStartupSelections::Installed&Snapshot()const{return selection_->Snapshot();}
   private:
    friend class InitStartupReceiver;
    Reader(std::shared_ptr<const InitStartupDocument>document,std::shared_ptr<const InitStartupSelections::Selection>selection):document_(std::move(document)),selection_(std::move(selection)){}
    std::shared_ptr<const InitStartupDocument>document_;std::shared_ptr<const InitStartupSelections::Selection>selection_;
  };
  bool Admit(const Peer&peer){
    if(!peer||!peer->Current())return false;
    auto found=peers_.find(Key(peer->Owner()));if(found!=peers_.end())return found->second==peer;
    if(peers_.size()>=InitStartupSelections::kOwners)return false;
    auto position=peers_.emplace(Key(peer->Owner()),peer);
    try{if(!receiver_.Admit(peer->Owner())||!selections_.Admit(peer->Owner())){receiver_.Retire(peer->Owner());peers_.erase(position.first);return false;}}
    catch(...){receiver_.Retire(peer->Owner());peers_.erase(position.first);throw;}
    return true;
  }
  Result Receive(const Peer&peer,CefProcessId sender,CefRefPtr<CefFrame>channel,CefRefPtr<CefProcessMessage>message,const InitSnapshotPin&expected){
    bool mapped=false;
    try{
      if(!Admitted(peer)||sender!=PID_BROWSER||!peer->Channel(channel)||expected.owner!=peer->Owner())return {Status::NotAdmitted,false,{}};
      auto input=InitSnapshotMessage::Read(message,expected);if(!input)return {};
      auto semantic=DecodeInitStartupSnapshot(input->snapshot.payload,static_cast<int>(expected.owner.context));if(!semantic)return {};
      if(!Admitted(peer))return {Status::Invalidated,false,{}};
      // No browser deadline is compared in this process. The original browser
      // operation is revalidated by its own registry upon actual receipt.
      auto installed=InitSnapshotMessage::Install(receiver_,message,expected,0,0);
      if(installed.status!=InitSnapshotReceiver::Status::Installed&&installed.status!=InitSnapshotReceiver::Status::Duplicate){
        const auto status=installed.status==InitSnapshotReceiver::Status::Capacity?Status::Capacity:installed.status==InitSnapshotReceiver::Status::Stale?Status::Stale:Status::Conflict;
        return {status,false,{}};
      }
      mapped=true;
      std::shared_ptr<const InstalledReceipt>receipt(new InstalledReceipt(peer,installed.lease,semantic->credential_version,semantic->credentials_installed));
      const auto status=selections_.Install(peer->Owner(),installed.lease,[this,&peer]{return Admitted(peer);});
      if(status!=Status::Installed&&status!=Status::Duplicate)return {status,true,{}};
      return {status,true,std::move(receipt)};
    }catch(const std::bad_alloc&){return {Status::Allocation,mapped,{}};}
  }
  std::optional<Reader>Select(const std::shared_ptr<const InitStartupDocument>&document){
    if(!document||!Admitted(document->Peer())||!document->Current())return std::nullopt;
    auto selection=selections_.Select(document->Peer()->Owner(),{document->Frame(),document->Sequence()});
    if(!selection)return std::nullopt;
    if(!document->Current()||!Admitted(document->Peer())){selections_.Release(document->Peer()->Owner(),selection->Identity());return std::nullopt;}
    return Reader{document,std::move(selection)};
  }
  bool Current(const Reader&reader)const{return reader.document_&&reader.document_->Current()&&Admitted(reader.document_->Peer())&&reader.document_->Peer()->Owner()==reader.selection_->Snapshot().Pin().owner&&reader.document_->Frame()==reader.selection_->Identity().frame&&reader.document_->Sequence()==reader.selection_->Identity().sequence&&selections_.Current(reader.selection_);}
  bool Release(const Reader&reader){return reader.document_&&reader.selection_&&selections_.Release(reader.document_->Peer()->Owner(),reader.selection_->Identity());}
  bool Retire(const Peer&peer){
    if(!peer)return false;
    auto found=peers_.find(Key(peer->Owner()));if(found==peers_.end()||found->second!=peer)return false;
    peer->Retire();selections_.Retire(peer->Owner());receiver_.Retire(peer->Owner());peers_.erase(found);return true;
  }
  std::vector<InitSnapshotPin>TakeReleased(){return receiver_.TakeReleased();}
  std::shared_ptr<const InitStartupSelections::Installed>Latest(const Peer&peer)const{return Admitted(peer)?selections_.Latest(peer->Owner()):nullptr;}
  size_t Physical()const{return receiver_.Physical();}size_t Readers()const{return selections_.Readers();}
 private:
  using OwnerKey=std::tuple<uint64_t,uint64_t,uint64_t,std::string>;
  static OwnerKey Key(const InitSnapshotOwner&owner){return {owner.context,owner.browser,owner.renderer_epoch,owner.frame};}
  bool Admitted(const Peer&peer)const{if(!peer||!peer->Current())return false;auto found=peers_.find(Key(peer->Owner()));return found!=peers_.end()&&found->second==peer;}
  InitSnapshotReceiver receiver_;InitStartupSelections selections_;std::map<OwnerKey,Peer>peers_;
};
} // namespace suitecut
