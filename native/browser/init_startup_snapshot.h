// SPDX-License-Identifier: MIT
#pragma once
#include "include/cef_parser.h"
#include "context_bindings.h"
#include "init_scripts.h"
#include "init_snapshot_receiver.h"
#include <new>
#include <functional>
#include <set>

namespace suitecut {
// Decoded data only. This component never evaluates source, releases author
// execution, admits a renderer, or establishes a debugger/factory capability.
struct InitStartupSnapshot {
  int context=0,script_version=0,binding_version=0,credential_version=0;
  bool credentials_installed=false;
  size_t source_bytes=0;
  std::vector<SuiteCutInitScripts::Entry>scripts,bindings;
};
inline std::optional<InitStartupSnapshot>DecodeInitStartupSnapshot(std::string_view payload,int context){
  if(context<=0||payload.empty()||payload.size()>InitSnapshotWire::kPayload)return std::nullopt;
  auto parsed=CefParseJSON(std::string(payload),JSON_PARSER_RFC);
  auto value=parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary():nullptr;
  if(!value||value->GetSize()!=7||value->GetType("contextId")!=VTYPE_INT||value->GetInt("contextId")!=context||
     value->GetType("version")!=VTYPE_INT||value->GetInt("version")<0||value->GetType("scripts")!=VTYPE_LIST||
     value->GetType("bindingVersion")!=VTYPE_INT||value->GetInt("bindingVersion")<0||value->GetType("bindings")!=VTYPE_LIST||
     value->GetType("credentialVersion")!=VTYPE_INT||value->GetInt("credentialVersion")<0||value->GetType("credentialsInstalled")!=VTYPE_BOOL)return std::nullopt;
  InitStartupSnapshot out;out.context=context;out.script_version=value->GetInt("version");
  out.binding_version=value->GetInt("bindingVersion");out.credential_version=value->GetInt("credentialVersion");out.credentials_installed=value->GetBool("credentialsInstalled");
  auto scripts=value->GetList("scripts"),bindings=value->GetList("bindings");
  if(scripts->GetSize()>SuiteCutInitScripts::kMaxScripts||static_cast<size_t>(out.script_version)!=scripts->GetSize()||bindings->GetSize()>SuiteCutContextBindings::kMaxEntries)return std::nullopt;
  const auto entries=[](CefRefPtr<CefListValue>input,bool sequential,size_t max_entry,size_t max_bytes,std::vector<SuiteCutInitScripts::Entry>&output,size_t&bytes){
    std::set<int>seen;
    for(size_t index=0;index<input->GetSize();++index){
      auto entry=input->GetDictionary(index);
      if(!entry||entry->GetSize()!=2||entry->GetType("id")!=VTYPE_INT||entry->GetInt("id")<=0||entry->GetType("source")!=VTYPE_STRING)return false;
      const int id=entry->GetInt("id");
      if((sequential&&id!=static_cast<int>(index+1))||!seen.insert(id).second)return false;
      auto source=entry->GetString("source").ToString();
      if(source.empty()||source.find('\0')!=std::string::npos||source.size()>max_entry||source.size()>max_bytes-bytes)return false;
      bytes+=source.size();output.push_back({id,std::move(source)});
    }
    return true;
  };
  size_t script_bytes=0,binding_bytes=0;
  if(!entries(scripts,true,SuiteCutInitScripts::kMaxScriptBytes,SuiteCutInitScripts::kMaxBytes,out.scripts,script_bytes)||
     !entries(bindings,false,SuiteCutContextBindings::kMaxEntryBytes,SuiteCutContextBindings::kMaxBytes,out.bindings,binding_bytes))return std::nullopt;
  out.source_bytes=script_bytes+binding_bytes;return out;
}

// Renderer-thread-only, opt-in data ledger. Trusted native adapters supply the
// actual owner/document/readiness identities. No renderer/author packet can
// invoke these methods by itself. The current bootstrap does not call this.
class InitStartupSelections {
 private:
  struct State {size_t snapshots=0,bytes=0,readers=0;};
 public:
  static constexpr size_t kOwners=64,kSnapshots=InitSnapshotReceipts::kTransfers,
    kBytes=InitSnapshotReceipts::kBytes,kReaders=1024;
  enum class Status {Installed,Duplicate,NotAdmitted,Malformed,Stale,Conflict,Capacity,Allocation,Invalidated};
  struct Document {
    std::string frame;uint64_t sequence=0;
    bool operator==(const Document&)const=default;
  };
  class Installed {
   public:
    Installed(const Installed&)=delete;Installed&operator=(const Installed&)=delete;
    const InitSnapshotPin&Pin()const{return lease_->Pin();}
    const InitStartupSnapshot&Data()const{return data_;}
    ~Installed(){--state_->snapshots;state_->bytes-=data_.source_bytes;}
   private:
    friend class InitStartupSelections;
    Installed(std::shared_ptr<State>state,std::shared_ptr<const InitSnapshotReceiver::Lease>lease,InitStartupSnapshot data):
      state_(std::move(state)),lease_(std::move(lease)),data_(std::move(data)){++state_->snapshots;state_->bytes+=data_.source_bytes;}
    std::shared_ptr<State>state_;
    std::shared_ptr<const InitSnapshotReceiver::Lease>lease_;
    InitStartupSnapshot data_;
  };
  class Selection {
   public:
    Selection(const Selection&)=delete;Selection&operator=(const Selection&)=delete;
    const Installed&Snapshot()const{return *snapshot_;}
    const Document&Identity()const{return document_;}
    ~Selection(){--state_->readers;}
   private:
    friend class InitStartupSelections;
    Selection(std::shared_ptr<State>state,std::shared_ptr<const Installed>snapshot,Document document,uint64_t readiness):
      state_(std::move(state)),snapshot_(std::move(snapshot)),document_(std::move(document)),readiness_(readiness){++state_->readers;}
    std::shared_ptr<State>state_;std::shared_ptr<const Installed>snapshot_;Document document_;uint64_t readiness_=0;
  };
  bool Admit(const InitSnapshotOwner&owner){
    if(!InitSnapshotWire::ValidOwner(owner))return false;
    if(owners_.contains(Lookup(owner)))return true;
    if(owners_.size()>=kOwners)return false;
    owners_.try_emplace(Key(owner));return true;
  }
  Status Install(const InitSnapshotOwner&actual,std::shared_ptr<const InitSnapshotReceiver::Lease>lease,const std::function<bool()>&current={}){
    try{
      const auto found=owners_.find(Lookup(actual));if(found==owners_.end())return Status::NotAdmitted;
      if(!lease||lease->Pin().owner!=actual)return Status::Malformed;
      auto&record=found->second;const auto&pin=lease->Pin();
      if(!pin.version||!pin.transfer)return Status::Malformed;
      if(record.current){
        const auto&old=record.current->Pin();
        if(pin.version<old.version)return Status::Stale;
        if(pin.version==old.version)return pin==old&&lease->Payload()==record.current->lease_->Payload()?Status::Duplicate:Status::Conflict;
        if(pin.transfer<=old.transfer)return Status::Conflict;
      }
      auto decoded=DecodeInitStartupSnapshot(lease->Payload(),static_cast<int>(actual.context));
      if(!decoded)return Status::Malformed;
      if(state_->snapshots>=kSnapshots||decoded->source_bytes>kBytes-state_->bytes)return Status::Capacity;
      // All throwing work precedes mutation. The old current selection and its
      // mapped reader remain intact even if decoding/allocation fails.
      std::shared_ptr<const Installed>next(new Installed(state_,std::move(lease),std::move(*decoded)));
      if(current&&!current())return Status::Invalidated;
      record.current=std::move(next);return Status::Installed;
    }catch(const std::bad_alloc&){return Status::Allocation;}
  }
  // Real native credential work increments generation BEFORE resetting the
  // device. Its later ACK may admit only this exact generation and body pin.
  bool ResetReadiness(const InitSnapshotOwner&actual,uint64_t generation,int credential_version){
    auto found=owners_.find(Lookup(actual));if(found==owners_.end()||!generation||credential_version<0||generation<=found->second.readiness_generation)return false;
    auto&record=found->second;record.readiness_generation=generation;record.readiness_credential_version=credential_version;record.ready.reset();return true;
  }
  bool AdmitReadiness(const InitSnapshotPin&pin,uint64_t generation,int credential_version){
    auto found=owners_.find(Lookup(pin.owner));if(found==owners_.end())return false;auto&record=found->second;
    if(!generation||record.readiness_generation!=generation||record.readiness_credential_version!=credential_version||!record.current||record.current->Pin()!=pin||record.current->Data().credential_version!=credential_version)return false;
    // Allocate/copy before replacing a previous positive admission.
    Readiness ready{pin,generation};record.ready=std::move(ready);return true;
  }
  std::shared_ptr<const Selection>Select(const InitSnapshotOwner&actual,Document document){
    try{
      auto found=owners_.find(Lookup(actual));if(found==owners_.end())return {};auto&record=found->second;
      if(document.frame.empty()||document.frame.size()>InitSnapshotWire::kFrame||document.frame.find('\0')!=std::string::npos||!document.sequence||document.sequence<=record.last_document)return {};
      // Consume the genuine creation attempt even on cache/readiness miss. The
      // caller must take its unchanged fallback; this realm cannot be upgraded
      // later after author execution just because a new cache entry arrives.
      record.last_document=document.sequence;
      if(!record.current||!Ready(record,*record.current)||state_->readers>=kReaders)return {};
      std::shared_ptr<const Selection>selection(new Selection(state_,record.current,std::move(document),record.readiness_generation));
      record.documents.emplace(selection->Identity().sequence,selection);return selection;
    }catch(const std::bad_alloc&){return {};}
  }
  bool Current(const std::shared_ptr<const Selection>&selection)const{
    if(!selection)return false;
    const auto found=owners_.find(Lookup(selection->Snapshot().Pin().owner));if(found==owners_.end())return false;
    const auto&record=found->second;auto document=record.documents.find(selection->Identity().sequence);
    if(document==record.documents.end()||document->second!=selection||record.readiness_generation!=selection->readiness_)return false;
    // A later snapshot install does not rewrite an already-selected document.
    // Credential reset does invalidate its readiness, including false->true.
    return !selection->Snapshot().Data().credentials_installed||selection->readiness_!=0;
  }
  bool Release(const InitSnapshotOwner&actual,const Document&document){
    auto found=owners_.find(Lookup(actual));if(found==owners_.end())return false;auto&documents=found->second.documents;auto selected=documents.find(document.sequence);
    if(selected==documents.end()||selected->second->Identity()!=document)return false;
    documents.erase(selected);return true;
  }
  bool Retire(const InitSnapshotOwner&actual){auto found=owners_.find(Lookup(actual));if(found==owners_.end())return false;owners_.erase(found);return true;}
  void Closed(){owners_.clear();}
  std::shared_ptr<const Installed>Latest(const InitSnapshotOwner&actual)const{const auto found=owners_.find(Lookup(actual));return found==owners_.end()?nullptr:found->second.current;}
  size_t Owners()const{return owners_.size();}size_t Snapshots()const{return state_->snapshots;}
  size_t Bytes()const{return state_->bytes;}size_t Readers()const{return state_->readers;}
 private:
  using OwnerKey=std::tuple<uint64_t,uint64_t,uint64_t,std::string>;
  static std::tuple<const uint64_t&,const uint64_t&,const uint64_t&,const std::string&>Lookup(const InitSnapshotOwner&owner){return std::tie(owner.context,owner.browser,owner.renderer_epoch,owner.frame);}
  static OwnerKey Key(const InitSnapshotOwner&owner){return {owner.context,owner.browser,owner.renderer_epoch,owner.frame};}
  struct Readiness {InitSnapshotPin pin;uint64_t generation=0;};
  struct Record {
    std::shared_ptr<const Installed>current;uint64_t readiness_generation=0,last_document=0;int readiness_credential_version=-1;
    std::optional<Readiness>ready;std::map<uint64_t,std::shared_ptr<const Selection>>documents;
  };
  static bool Ready(const Record&record,const Installed&installed){
    if(!installed.Data().credentials_installed&&!installed.Data().credential_version&&!record.readiness_generation)return true;
    return record.ready&&record.ready->generation==record.readiness_generation&&record.ready->pin==installed.Pin();
  }
  std::shared_ptr<State>state_=std::make_shared<State>();std::map<OwnerKey,Record,std::less<>>owners_;
};
} // namespace suitecut
