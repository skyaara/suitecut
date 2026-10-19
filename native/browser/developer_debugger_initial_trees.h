// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace suitecut {
// Metadata provenance for real initial frame-tree commands. Preparing or
// beginning dispatch is not an Inspector ACK and never owns a physical call.
// Source keeps its existing actual command registry and closure policy.
class FrameDebuggerInitialTrees {
 public:
  struct Limits{size_t trees=1024,frames=1024,bytes=1048576;};
  FrameDebuggerInitialTrees():FrameDebuggerInitialTrees(Limits{}){}
  explicit FrameDebuggerInitialTrees(Limits limits):limits_(limits){unavailable_=!limits_.trees||!limits_.frames||!limits_.bytes;}
  FrameDebuggerInitialTrees(const FrameDebuggerInitialTrees&)=delete;
  FrameDebuggerInitialTrees&operator=(const FrameDebuggerInitialTrees&)=delete;
  bool Prepare(int actual_command,const std::string&session,uint64_t actual_attachment,std::string&error){
    Observation observation(*this);
    if(!Available()||actual_command<=0||!Channel(session)||!actual_attachment)return Reject(error,"Native debugger initial-tree command/channel is unavailable");
    const auto old=trees_.find(actual_command);
    if(old!=trees_.end()){
      if(old->second.session!=session||old->second.attachment!=actual_attachment)return Reject(error,"Native debugger initial-tree command identity was reused");
      observation.Complete();return true;
    }
    if(actual_command<=last_command_||trees_.size()>=limits_.trees||channels_.contains(session))return Reject(error,"Native debugger initial-tree command order or channel capacity rejected");
    std::map<int,Tree>staged_trees;std::map<std::string,int>staged_channels;
    staged_trees.emplace(actual_command,Tree{session,actual_attachment,false,false,0,{}});staged_channels.emplace(session,actual_command);
    auto&tree=staged_trees.begin()->second;
    tree.bytes=sizeof(Tree)+tree.session.capacity()+staged_channels.begin()->first.capacity();
    if(!Fits(tree.bytes))return Reject(error,"Native debugger initial-tree metadata storage exhausted");
    const auto charge=tree.bytes;
    trees_.insert(staged_trees.extract(staged_trees.begin()));channels_.insert(staged_channels.extract(staged_channels.begin()));
    bytes_+=charge;last_command_=actual_command;observation.Complete();return true;
  }
  // Called immediately before the real SendDevToolsMessage attempt in both
  // direct and deferred paths, which may synchronously deliver an event/reply. Refusal
  // then removes metadata via Finished; this bit makes no acceptance claim.
  bool Dispatching(int actual_command,const std::string&session,uint64_t actual_attachment){
    const auto found=trees_.find(actual_command);
    if(!Available()||found==trees_.end()||!Matches(found->second,session,actual_attachment)||found->second.dispatched)return false;
    found->second.dispatched=true;return true;
  }
  bool Contains(int actual_command)const{return trees_.contains(actual_command);}
  uint64_t ExpectedAttachment(int actual_command,const std::string&session)const{
    const auto found=trees_.find(actual_command);return found==trees_.end()||found->second.session!=session?0:found->second.attachment;
  }
  bool DefaultObserved(const std::string&session,uint64_t actual_attachment,const std::string&frame,uint64_t observed_document,std::string&error){
    Observation observation(*this);
    if(!Available()||!Identity(frame,256)||!observed_document)return Reject(error,"Native debugger initial-tree default observation is invalid");
    auto*tree=Find(session,actual_attachment);if(!tree||tree->cleared){observation.Complete();return true;}
    auto found=tree->frames.find(frame);
    if(found==tree->frames.end()){
      if(!Insert(*tree,frame,Initial{observed_document,false},error))return false;
    }else if(!tree->dispatched)found->second={observed_document,false};
    else if(found->second.document!=observed_document)found->second.blocked=true;
    observation.Complete();return true;
  }
  bool FrameInvalidated(const std::string&session,uint64_t actual_attachment,const std::string&frame,std::string&error){
    Observation observation(*this);
    if(!Available()||!Identity(frame,256))return Reject(error,"Native debugger initial-tree frame invalidation is invalid");
    auto*tree=Find(session,actual_attachment);if(!tree||tree->cleared){observation.Complete();return true;}
    auto found=tree->frames.find(frame);
    if(!tree->dispatched){if(found!=tree->frames.end())Erase(*tree,found);}
    else if(found==tree->frames.end()){if(!Insert(*tree,frame,Initial{0,true},error))return false;}
    else found->second.blocked=true;
    observation.Complete();return true;
  }
  void ContextsCleared(const std::string&session,uint64_t actual_attachment){
    auto*tree=Find(session,actual_attachment);if(!tree)return;
    if(tree->dispatched)tree->cleared=true;
    else while(!tree->frames.empty())Erase(*tree,tree->frames.begin());
  }
  uint64_t InitialDocument(int actual_command,const std::string&session,uint64_t actual_attachment,const std::string&frame)const{
    if(!Available())return 0;
    const auto found=trees_.find(actual_command);
    if(found==trees_.end()||!Matches(found->second,session,actual_attachment)||!found->second.dispatched||found->second.cleared)return 0;
    const auto value=found->second.frames.find(frame);
    return value==found->second.frames.end()||value->second.blocked?0:value->second.document;
  }
  bool Finished(int actual_command,const std::string&session,uint64_t actual_attachment){
    const auto found=trees_.find(actual_command);
    if(found==trees_.end()||!Matches(found->second,session,actual_attachment))return false;
    auto&tree=found->second;
    while(!tree.frames.empty())Erase(tree,tree.frames.begin());
    bytes_-=tree.bytes;channels_.erase(tree.session);trees_.erase(found);return true;
  }
  bool Detached(const std::string&session,uint64_t actual_attachment){
    const auto found=channels_.find(session);return found!=channels_.end()&&Finished(found->second,session,actual_attachment);
  }
  void Unavailable()noexcept{unavailable_=true;}
  void Closed()noexcept{unavailable_=true;trees_.clear();channels_.clear();frames_=bytes_=0;}
  bool Available()const{return !unavailable_;}
  size_t Trees()const{return trees_.size();}
  size_t Frames()const{return frames_;}
  size_t Payload()const{return bytes_;}
 private:
  struct Initial{uint64_t document=0;bool blocked=false;};
  struct Tree{std::string session;uint64_t attachment=0;bool dispatched=false,cleared=false;size_t bytes=0;std::map<std::string,Initial>frames;};
  class Observation{public:explicit Observation(FrameDebuggerInitialTrees&owner):owner_(owner){}~Observation(){if(!complete_)owner_.Unavailable();}void Complete()noexcept{complete_=true;}private:FrameDebuggerInitialTrees&owner_;bool complete_=false;};
  static bool Identity(const std::string&text,size_t maximum){return !text.empty()&&text.size()<=maximum&&text.find('\0')==std::string::npos;}
  static bool Channel(const std::string&session){return session.empty()||Identity(session,128);}
  static bool Matches(const Tree&tree,const std::string&session,uint64_t attachment){return tree.session==session&&tree.attachment==attachment;}
  static size_t Bytes(const std::pair<const std::string,Initial>&entry){return sizeof(Initial)+entry.first.capacity();}
  static bool Reject(std::string&error,const char*text){error=text;return false;}
  bool Fits(size_t incoming)const{return incoming<=limits_.bytes&&bytes_<=limits_.bytes-incoming;}
  Tree*Find(const std::string&session,uint64_t attachment){const auto found=channels_.find(session);if(found==channels_.end())return nullptr;auto&tree=trees_.at(found->second);return tree.attachment==attachment?&tree:nullptr;}
  bool Insert(Tree&tree,const std::string&frame,Initial value,std::string&error){
    if(frames_>=limits_.frames)return Reject(error,"Native debugger initial-tree frame observation capacity exhausted");
    std::map<std::string,Initial>staged;staged.emplace(frame,value);const auto charge=Bytes(*staged.begin());
    if(!Fits(charge))return Reject(error,"Native debugger initial-tree frame identity storage exhausted");
    tree.frames.insert(staged.extract(staged.begin()));bytes_+=charge;++frames_;return true;
  }
  void Erase(Tree&tree,std::map<std::string,Initial>::iterator entry){bytes_-=Bytes(*entry);--frames_;tree.frames.erase(entry);}
  Limits limits_;std::map<int,Tree>trees_;std::map<std::string,int>channels_;int last_command_=0;size_t frames_=0,bytes_=0;bool unavailable_=false;
};
}
