// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include "developer_debugger_frame_scopes.h"

namespace suitecut {
// Accepted Source observation metadata only. This ledger owns no domain lease,
// command or callback. Invalidating a document never proves physical closure.
class FrameDebuggerDocuments {
 public:
  using Scopes=FrameDebuggerScopes;
  struct Snapshot {
    std::string session,frame,loader,unique_context;
    int context=0;
    bool operator==(const Snapshot&)const=default;
  };
  struct Limits { size_t documents=1024,attachments=1024,bytes=4*1048576;uint64_t generations=UINT64_MAX; };
  explicit FrameDebuggerDocuments(Scopes::Host actual_host)
      :FrameDebuggerDocuments(std::move(actual_host),Limits{}){}
  FrameDebuggerDocuments(Scopes::Host actual_host,Limits limits)
      :host_(std::move(actual_host)),limits_(limits){
    unavailable_=!Identity(host_.source,128)||!host_.observer||!limits_.documents||!limits_.attachments||!limits_.generations||bytes_>limits_.bytes;
  }
  FrameDebuggerDocuments(const FrameDebuggerDocuments&)=delete;
  FrameDebuggerDocuments&operator=(const FrameDebuggerDocuments&)=delete;

  // Called only after a real accepted iframe attachment and parent validation.
  // An identical receipt for a live attachment is idempotent. A genuinely new
  // attachment after Detached receives another checked native stamp.
  bool Attached(const std::string&session,const std::string&parent,const std::string&frame,std::string&error){
    Observation observation(*this);
    if(!Available()||!Identity(session,128)||!ChannelText(parent)||session==parent||!Identity(frame,256)||!AttachmentFor(parent))return Reject(error,"Native frame debugger attachment has no accepted exact parent/channel identity");
    const auto parent_attachment=AttachmentFor(parent);
    const auto old=attachments_.find(session);
    if(old!=attachments_.end()){
      if(old->second.parent!=parent||old->second.frame!=frame||old->second.parent_attachment!=parent_attachment)return Reject(error,"Native frame debugger live attachment identity changed without actual detach");
      observation.Complete();return true;
    }
    if(attachments_.size()>=limits_.attachments||attachment_serial_>=limits_.generations)return Reject(error,"Native frame debugger attachment capacity or generation exhausted");
    // Validate ancestry from the actual accepted parent graph. Nothing follows
    // frame/target ID spelling, URL or the separate network migration graph.
    auto ancestor=parent;
    for(size_t depth=0;!ancestor.empty();++depth){
      if(depth>=attachments_.size()||ancestor==session)return Reject(error,"Native frame debugger attachment ancestry rejected");
      const auto found=attachments_.find(ancestor);
      if(found==attachments_.end())return Reject(error,"Native frame debugger attachment parent is unavailable");
      ancestor=found->second.parent;
    }
    Attachment value{parent,frame,attachment_serial_+1,parent_attachment,0};
    std::map<std::string,Attachment>staged;
    staged.emplace(session,std::move(value));
    auto&entry=*staged.begin();entry.second.bytes=sizeof(Attachment)+entry.first.capacity()+entry.second.parent.capacity()+entry.second.frame.capacity();
    if(!Fits(entry.second.bytes,0))return Reject(error,"Native frame debugger attachment identity storage exhausted");
    const auto charge=entry.second.bytes;
    attachments_.insert(staged.extract(staged.begin()));
    bytes_+=charge;++attachment_serial_;observation.Complete();return true;
  }
  // Actual root identity observation, not a request-supplied frame selection.
  bool MainFrame(const std::string&actual_frame,std::string&error){
    Observation observation(*this);
    if(!Available()||!Identity(actual_frame,256))return Reject(error,"Native frame debugger main frame identity unavailable");
    std::string staged(actual_frame);
    if(!Fits(staged.capacity(),main_.capacity()))return Reject(error,"Native frame debugger root identity storage exhausted");
    bytes_=bytes_-main_.capacity()+staged.capacity();main_.swap(staged);observation.Complete();return true;
  }
  // Only a genuine default-context creation, or an explicitly restored actual
  // Source default tuple after invalidation, may call this. Utility creation
  // and loader-only lifecycle events must not manufacture a new snapshot.
  bool DefaultCreated(const Snapshot&actual,std::string&error){
    Observation observation(*this);
    if(!Available()||!Valid(actual))return Reject(error,"Native frame debugger actual default context is unavailable");
    const auto attachment=AttachmentFor(actual.session);
    if(!attachment)return Reject(error,"Native frame debugger default context has no accepted channel attachment");
    const auto old=documents_.find(actual.frame);
    if(old!=documents_.end()&&old->second.snapshot==actual&&old->second.attachment==attachment){observation.Complete();return true;}
    if((old==documents_.end()&&documents_.size()>=limits_.documents)||document_serial_>=limits_.generations)return Reject(error,"Native frame debugger default document capacity or generation exhausted");
    Document value{actual,attachment,document_serial_+1,0};
    std::map<std::string,Document>staged;
    staged.emplace(actual.frame,std::move(value));
    auto&entry=*staged.begin();entry.second.bytes=sizeof(Document)+entry.first.capacity()+Bytes(entry.second.snapshot);
    if(entry.second.bytes>4096||!Fits(entry.second.bytes,old==documents_.end()?0:old->second.bytes))return Reject(error,"Native frame debugger document identity storage exhausted");
    const auto old_bytes=old==documents_.end()?0:old->second.bytes;
    const auto charge=entry.second.bytes;
    if(old==documents_.end())documents_.insert(staged.extract(staged.begin()));
    else old->second=std::move(entry.second);
    bytes_=bytes_-old_bytes+charge;++document_serial_;observation.Complete();return true;
  }
  // Initial frame-tree association can follow its actual context receipt.
  // Complete only that already-observed exact default tuple. A later loader
  // replacement invalidates it; this method never manufactures a new context.
  bool InitialFrameAssociated(const Snapshot&actual,uint64_t observed_document,std::string&error){
    Observation observation(*this);
    if(!Available()||!Valid(actual))return Reject(error,"Native frame debugger initial frame association is invalid");
    const auto found=documents_.find(actual.frame);
    if(!observed_document||found==documents_.end()||found->second.serial!=observed_document){observation.Complete();error="Native frame debugger initial association document receipt is stale";return false;}
    const auto&prior=found->second.snapshot;
    if(prior.session!=actual.session||prior.context!=actual.context||prior.unique_context!=actual.unique_context)return Reject(error,"Native frame debugger initial association has no matching observed default identity");
    if(prior.loader==actual.loader){observation.Complete();return true;}
    if(!prior.loader.empty()){FrameInvalidated(actual.frame);observation.Complete();return true;}
    if(!DefaultCreated(actual,error))return false;
    observation.Complete();return true;
  }
  bool Resolve(const std::string&actual_frame,Scopes::Pin&output)const{
    if(!Available())return false;
    const auto found=documents_.find(actual_frame);if(found==documents_.end())return false;
    const auto&doc=found->second;const auto&actual=doc.snapshot;
    if(AttachmentFor(actual.session)!=doc.attachment)return false;
    const bool root=actual.frame==main_;
    if(root&&!actual.session.empty())return false;
    Scopes::Pin result{{host_,actual.session},doc.attachment,doc.serial,actual.frame,actual.loader,actual.unique_context,actual.context,root};
    output=std::move(result);return true;
  }
  bool Current(const Scopes::Pin&pin)const{
    if(!Available()||pin.channel.host!=host_)return false;
    const auto found=documents_.find(pin.frame);if(found==documents_.end())return false;
    const auto&doc=found->second;const auto&actual=doc.snapshot;
    const bool root=actual.frame==main_;
    return (!root||actual.session.empty())&&pin.root==root&&pin.channel.session==actual.session&&
      pin.attachment==doc.attachment&&AttachmentFor(actual.session)==doc.attachment&&pin.document==doc.serial&&
      pin.loader==actual.loader&&pin.context==actual.context&&pin.unique_context==actual.unique_context;
  }
  uint64_t DocumentStamp(const std::string&frame)const{
    if(!Available())return 0;
    const auto found=documents_.find(frame);return found==documents_.end()||AttachmentFor(found->second.snapshot.session)!=found->second.attachment?0:found->second.serial;
  }
  // Logical invalidations only. Old immutable scope pins and accepted calls
  // remain owned by FrameDebuggerScopes until their actual cleanup settles.
  void FrameInvalidated(const std::string&frame){const auto found=documents_.find(frame);if(found!=documents_.end())Erase(found);}
  void ContextDestroyed(const std::string&session,int context,const std::string&unique={}){
    if(context<=0)return;
    for(auto it=documents_.begin();it!=documents_.end();){const auto&doc=it->second.snapshot;if(doc.session==session&&doc.context==context&&(unique.empty()||doc.unique_context==unique))it=Erase(it);else ++it;}
  }
  void ContextsCleared(const std::string&session){for(auto it=documents_.begin();it!=documents_.end();)if(it->second.snapshot.session==session)it=Erase(it);else ++it;}
  uint64_t AttachmentFor(const std::string&session)const{
    if(!Available())return 0;
    if(session.empty())return host_.observer;
    const auto found=attachments_.find(session);if(found==attachments_.end())return 0;
    auto current=found;
    for(size_t depth=0;!current->second.parent.empty();++depth){
      if(depth>=attachments_.size())return 0;
      const auto expected=current->second.parent_attachment;
      current=attachments_.find(current->second.parent);if(current==attachments_.end()||current->second.serial!=expected)return 0;
    }
    return found->second.serial;
  }
  // The adapter invokes this for every channel in its existing real detach
  // descendant walk, using the stamp snapshotted before consumer callback drain.
  // A stale old detach cannot erase a newly attached same-session incarnation.
  bool Detached(const std::string&session,uint64_t actual_attachment){
    const auto found=attachments_.find(session);
    if(found==attachments_.end()||found->second.serial!=actual_attachment)return false;
    ContextsCleared(session);bytes_-=found->second.bytes;attachments_.erase(found);return true;
  }
  // Allocation exceptions in the adapter's own snapshot/callback preparation
  // must also call Unavailable before requesting posted closure.
  void Unavailable()noexcept{unavailable_=true;}
  void Closed()noexcept{unavailable_=true;documents_.clear();attachments_.clear();main_.clear();bytes_=host_.source.capacity()+main_.capacity();}
  bool Available()const{return !unavailable_;}
  size_t Documents()const{return documents_.size();}
  size_t Attachments()const{return attachments_.size();}
  size_t Payload()const{return bytes_;}
 private:
  // Marks failed/throwing accepted metadata unavailable while retaining prior
  // storage accounting. It never purports to close the actual channel.
  class Observation{
   public:explicit Observation(FrameDebuggerDocuments&owner):owner_(owner){}
    ~Observation(){if(!complete_)owner_.Unavailable();}
    void Complete()noexcept{complete_=true;}
   private:FrameDebuggerDocuments&owner_;bool complete_=false;
  };
  struct Attachment{std::string parent,frame;uint64_t serial=0,parent_attachment=0;size_t bytes=0;};
  struct Document{Snapshot snapshot;uint64_t attachment=0,serial=0;size_t bytes=0;};
  using DocumentsMap=std::map<std::string,Document>;
  static bool Identity(const std::string&value,size_t max){return !value.empty()&&value.size()<=max&&value.find('\0')==std::string::npos;}
  static bool ChannelText(const std::string&value){return value.empty()||Identity(value,128);}
  static bool Valid(const Snapshot&value){return ChannelText(value.session)&&Identity(value.frame,256)&&value.loader.size()<=256&&value.loader.find('\0')==std::string::npos&&value.context>0&&Identity(value.unique_context,256);}
  static size_t Bytes(const Snapshot&value){return value.session.capacity()+value.frame.capacity()+value.loader.capacity()+value.unique_context.capacity();}
  bool Fits(size_t incoming,size_t outgoing)const{return outgoing<=bytes_&&incoming<=limits_.bytes&&bytes_-outgoing<=limits_.bytes-incoming;}
  static bool Reject(std::string&error,const char*text){error=text;return false;}
  DocumentsMap::iterator Erase(DocumentsMap::iterator it){bytes_-=it->second.bytes;return documents_.erase(it);}
  Scopes::Host host_;Limits limits_;std::string main_;
  DocumentsMap documents_;std::map<std::string,Attachment>attachments_;
  uint64_t attachment_serial_=0,document_serial_=0;
  size_t bytes_=host_.source.capacity()+main_.capacity();bool unavailable_=false;
};
}
