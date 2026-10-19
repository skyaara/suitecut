// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace suitecut {
// Source passes only actual Inspector document/backend identities. This registry
// never creates DOM IDs/attributes, queries author markup, or guesses a node.
// Active references follow the same genuine backend node across successful
// snapshots of the exact same document. Removed nodes lose their references.
// Group replacement/cancellation retains actual physical ownership
// until installation and release acknowledgments (or real channel destruction).
class SnapshotReferences {
 public:
  struct Document {
    std::string frame,session,loader,main_unique,utility_unique;
    int main=0,utility=0;
    uint64_t context_generation=0;
    bool operator==(const Document&)const=default;
  };
  struct Node {int backend=0,type=0;};
  struct Group {
    uint64_t ticket=0,generation=0;int owner=0;
    Document document;std::string name,array;std::vector<Node> nodes;
    // Native-owned assignment, immutable from reservation through retirement.
    std::vector<std::string> references;
    double deadline=0,retirement_deadline=0;
    size_t bytes=0,borrows=0;
    bool dispatched=false,installed=false,active=false,retiring=false,pending_release=false,release_failed=false,document_dead=false,late_install=false,channel_dead=false;
  };
  struct Borrow {
    std::shared_ptr<const Group> group;size_t index=0;
    std::shared_ptr<void> lease;
    explicit operator bool()const{return static_cast<bool>(group);}
  };
  using Current=std::function<bool(int,uint64_t,const Document&)>;
  using Release=std::function<void(const Group&,std::function<void(bool,const std::string&)>)>;
  using Fatal=std::function<void(const std::string&)>;
  static constexpr size_t kActiveNodes=4096,kResidentNodes=8192,kActiveGroups=64,kResidentGroups=128,kPhysicalReleases=32,kBorrows=64,kBytes=8*1024*1024;
  static constexpr size_t kGlobalNodes=32768,kGlobalGroups=256,kGlobalInstalls=128,kGlobalReleases=64,kGlobalBorrows=128,kGlobalBytes=32*1024*1024;
  SnapshotReferences(std::string name_space,Current current,Release release,Fatal fatal)
    :namespace_(std::move(name_space)),current_(std::move(current)),release_(std::move(release)),fatal_(std::move(fatal)){}
  ~SnapshotReferences(){Destroyed();life_->alive=false;}
  uint64_t Begin(int owner,uint64_t generation,Document document,std::vector<Node> nodes,double deadline,double now){
    if(closed_||failed_||owner<=0||!generation||!ValidNamespace()||!Valid(document)||nodes.size()>kActiveNodes||groups_.size()>=kResidentGroups||GlobalState().groups>=kGlobalGroups||nodes.size()>kGlobalNodes-GlobalState().nodes||nodes.size()>kResidentNodes-resident_nodes_||sequence_==std::numeric_limits<uint64_t>::max()||!std::isfinite(now)||!std::isfinite(deadline)||(deadline&&deadline<=now))return 0;
    std::set<int>unique;for(const auto&node:nodes)if(node.backend<=0||node.type!=1||!unique.insert(node.backend).second)return 0;
    const size_t bytes=document.frame.size()+document.session.size()+document.loader.size()+document.main_unique.size()+document.utility_unique.size()+namespace_.size()+2048+nodes.size()*(sizeof(Node)+sizeof(std::string)+704);
    if(bytes>kBytes-resident_bytes_||bytes>kGlobalBytes-GlobalState().bytes)return 0;
    auto group=std::make_shared<Group>();group->ticket=++sequence_;group->generation=generation;group->owner=owner;group->document=std::move(document);group->nodes=std::move(nodes);group->deadline=deadline;group->bytes=bytes;group->name="suitecut-snapshot-"+namespace_+"-"+std::to_string(group->ticket);
    // Only reuse identities in the currently published exact document. No
    // historical backend map survives omission/navigation, bounding ownership
    // by resident groups and preventing a stale token from naming a new node.
    std::map<int,std::string> previous;
    for(const auto&[id,other]:groups_)if(other->active&&other->document==group->document)
      for(size_t i=0;i<other->nodes.size();++i)previous.emplace(other->nodes[i].backend,other->references[i]);
    group->references.reserve(group->nodes.size());
    for(size_t i=0;i<group->nodes.size();++i){const auto old=previous.find(group->nodes[i].backend);
      group->references.push_back(old==previous.end()?"r"+namespace_+"_"+std::to_string(group->ticket)+"_"+std::to_string(i):old->second);}
    resident_bytes_+=bytes;resident_nodes_+=group->nodes.size();GlobalState().bytes+=bytes;GlobalState().nodes+=group->nodes.size();++GlobalState().groups;groups_.emplace(group->ticket,group);return group->ticket;
  }
  // Source calls immediately before the genuine group-install command dispatch.
  bool Dispatched(uint64_t ticket){auto group=Find(ticket);if(!group||group->dispatched||group->retiring||InstallPhysical()>=64||GlobalState().installs>=kGlobalInstalls)return false;group->dispatched=true;++GlobalState().installs;return true;}
  // Explicit submission rejection is not an ACK and cannot have created a
  // remote group. Roll back only an uninstalled reservation, even after cancel.
  void Rejected(uint64_t ticket){
    const auto group=Find(ticket);if(!group||group->installed)return;
    if(group->dispatched){group->dispatched=false;--GlobalState().installs;}
    Remove(group);Pump();
  }
  // Called exactly on actual command settlement. Even a failed installation can
  // have created remote values, so its real group is released before retirement.
  bool Installed(uint64_t ticket,bool success,std::string array,double now){
    auto group=Find(ticket);if(!group||!group->dispatched||group->installed)return false;group->installed=true;--GlobalState().installs;
    if(success&&!array.empty()&&array.size()<=512)group->array=std::move(array);else Retire(group,now);
    if(group->retiring){Pump();return false;}return true;
  }
  bool Commit(uint64_t ticket,double now){
    auto group=Find(ticket);if(!group||!group->installed||group->array.empty()||group->retiring)return false;
    const auto old=latest_.find(group->document.frame);
    if(!std::isfinite(now)||(group->deadline&&now>=group->deadline)||!current_(group->owner,group->generation,group->document)||(old!=latest_.end()&&old->second>=ticket)){
      Retire(group,now);Pump();return false;
    }
    size_t old_nodes=0,old_groups=0;
    for(const auto&[id,other]:groups_)if(other->active&&other->document.frame==group->document.frame){old_nodes+=other->nodes.size();++old_groups;}
    if(group->nodes.size()>kActiveNodes-(active_nodes_-old_nodes)||(!group->nodes.empty()&&old_groups==0&&active_groups_>=kActiveGroups)){Retire(group,now);Pump();return false;}
    std::vector<std::shared_ptr<Group>> previous;for(const auto&[id,other]:groups_)if(other->ticket<ticket&&other->document.frame==group->document.frame)previous.push_back(other);
    for(const auto&other:previous)Retire(other,now);
    latest_[group->document.frame]=ticket;
    if(group->nodes.empty())Retire(group,now);else{group->active=true;++active_groups_;active_nodes_+=group->nodes.size();for(size_t i=0;i<group->nodes.size();++i)tokens_.emplace(Token(*group,i),std::pair{ticket,i});}
    Pump();return true;
  }
  // A multi-frame snapshot becomes public as one native UI-thread transaction.
  // Preflight every installed group/document and the final aggregate quotas
  // before retiring any previously successful publication. Physical borrows and
  // old-group release receipts remain independently owned after that switch.
  bool CommitBatch(const std::vector<uint64_t>& tickets,double now){
    if(closed_||failed_||tickets.empty()||tickets.size()>32)return false;
    std::vector<std::shared_ptr<Group>> batch;std::set<uint64_t> seen;
    std::map<std::string,std::shared_ptr<Group>> frames;bool valid=std::isfinite(now);
    for(const auto ticket:tickets){
      const auto group=Find(ticket);
      if(!group||!seen.insert(ticket).second){valid=false;continue;}
      batch.push_back(group);
      if(!frames.emplace(group->document.frame,group).second)valid=false;
    }
    const auto reject=[&]{
      // An already published group is never invalidated by an invalid replay.
      for(const auto& group:batch)if(!group->active)Retire(group,std::isfinite(now)?now:0);
      Pump();return false;
    };
    if(!valid||batch.empty())return reject();
    const auto owner=batch.front()->owner;const auto generation=batch.front()->generation;
    const auto deadline=batch.front()->deadline;size_t incoming_nodes=0,incoming_groups=0;
    for(const auto& group:batch){
      const auto latest=latest_.find(group->document.frame);
      if(!group->installed||group->array.empty()||group->retiring||group->active||
         group->owner!=owner||group->generation!=generation||group->deadline!=deadline||
         (group->deadline&&now>=group->deadline)||!current_(group->owner,group->generation,group->document)||
         (latest!=latest_.end()&&latest->second>=group->ticket))return reject();
      incoming_nodes+=group->nodes.size();if(!group->nodes.empty())++incoming_groups;
    }
    size_t outgoing_nodes=0,outgoing_groups=0;
    std::vector<std::shared_ptr<Group>> previous;
    for(const auto&[id,group]:groups_){
      const auto replacement=frames.find(group->document.frame);
      if(replacement==frames.end()||group->ticket>=replacement->second->ticket)continue;
      previous.push_back(group);
      if(group->active){outgoing_nodes+=group->nodes.size();++outgoing_groups;}
    }
    if(incoming_nodes>kActiveNodes-(active_nodes_-outgoing_nodes)||
       incoming_groups>kActiveGroups-(active_groups_-outgoing_groups))return reject();
    // Quota swaps must not depend on parent/child activation order. Retire every
    // affected old publication first, then publish every validated new group.
    for(const auto& group:previous)Retire(group,now);
    for(const auto& group:batch){
      latest_[group->document.frame]=group->ticket;
      if(group->nodes.empty())Retire(group,now);
      else{group->active=true;++active_groups_;active_nodes_+=group->nodes.size();
        for(size_t i=0;i<group->nodes.size();++i)tokens_.emplace(Token(*group,i),std::pair{group->ticket,i});}
    }
    Pump();return true;
  }
  std::string Prefix(uint64_t ticket)const{return groups_.contains(ticket)?"r"+namespace_+"_"+std::to_string(ticket)+"_":std::string();}
  std::string Reference(uint64_t ticket,size_t index)const{const auto found=groups_.find(ticket);return found!=groups_.end()&&found->second->active&&index<found->second->nodes.size()?Token(*found->second,index):std::string();}
  std::shared_ptr<const Group> GroupFor(uint64_t ticket)const{const auto found=groups_.find(ticket);return found==groups_.end()?nullptr:found->second;}
  bool NeedsTick()const{if(closed_||failed_)return false;for(const auto&[id,group]:groups_)if(group->retiring)return true;return false;}
  bool DocumentFor(const std::string& token,Document& document)const{if(closed_||failed_||token.empty()||token.size()>192)return false;const auto found=tokens_.find(token);if(found==tokens_.end())return false;const auto group=groups_.find(found->second.first);if(group==groups_.end()||!group->second->active)return false;document=group->second->document;return true;}
  Borrow Lookup(const std::string& token,const Document&document){
    if(closed_||failed_||token.empty()||token.size()>192||borrows_>=kBorrows||GlobalState().borrows>=kGlobalBorrows)return{};
    const auto found=tokens_.find(token);if(found==tokens_.end())return{};auto group=Find(found->second.first);
    if(!group||!group->active||group->document!=document)return{};
    auto lease=Use(group->ticket);if(!lease)return{};
    return{group,found->second.second,std::move(lease)};
  }
  // Physical reads of installed staging/active groups need a lease too. The
  // lease outlives logical cancellation and is released by the actual sender.
  std::shared_ptr<void> Use(uint64_t ticket){
    const auto group=Find(ticket);
    if(closed_||failed_||!group||!group->installed||group->array.empty()||group->retiring||group->channel_dead||borrows_>=kBorrows||GlobalState().borrows>=kGlobalBorrows)return{};
    ++borrows_;++group->borrows;++GlobalState().borrows;const auto life=life_;
    return std::shared_ptr<void>(new uint64_t(group->ticket),[this,life,group](void*value){delete static_cast<uint64_t*>(value);if(!life->alive||closed_||group->channel_dead)return;--borrows_;--group->borrows;--GlobalState().borrows;Pump();});
  }
  void Cancel(int owner,uint64_t generation,double now){std::vector<std::shared_ptr<Group>> candidates;for(const auto&[id,group]:groups_)if(!group->active&&group->owner==owner&&group->generation==generation)candidates.push_back(group);for(const auto&group:candidates)Retire(group,now);Pump();}
  void InvalidateFrame(const std::string&frame,double now){std::vector<std::shared_ptr<Group>> candidates;for(const auto&[id,group]:groups_)if(group->document.frame==frame)candidates.push_back(group);for(const auto&group:candidates)Retire(group,now);Pump();}
  // Actual session/context destruction is separate from logical cancellation.
  void ChannelDestroyed(const std::string&session){std::vector<std::shared_ptr<Group>> candidates;for(const auto&[id,group]:groups_)if(group->document.session==session)candidates.push_back(group);for(const auto&group:candidates){group->channel_dead=true;borrows_-=group->borrows;GlobalState().borrows-=group->borrows;group->borrows=0;Remove(group);}Pump();}
  void DocumentDestroyed(const Document&document,double now){std::vector<std::shared_ptr<Group>> candidates;for(const auto&[id,group]:groups_)if(group->document==document)candidates.push_back(group);for(const auto&group:candidates){group->document_dead=true;group->late_install=group->dispatched&&!group->installed;Retire(group,now);}Pump();}
  void Tick(double now){if(closed_||failed_)return;for(const auto&[id,group]:groups_)if(group->retiring&&now>=group->retirement_deadline){failed_=true;fatal_("Native snapshot reference physical retirement deadline exceeded");return;}Pump();}
  // Only actual Source/browser destruction calls this (including the controller
  // destructor after its real owner dies), never logical page.close. All calls
  // are serialized on the CEF UI thread, including process-global accounting.
  void Destroyed(){if(closed_)return;closed_=true;std::vector<std::shared_ptr<Group>> groups;for(const auto&[id,group]:groups_)groups.push_back(group);for(const auto&group:groups){group->channel_dead=true;GlobalState().borrows-=group->borrows;group->borrows=0;Remove(group);}tokens_.clear();latest_.clear();resident_nodes_=resident_bytes_=active_nodes_=active_groups_=physical_=borrows_=0;}
  static size_t GlobalInstallPhysical(){return GlobalState().installs;}static size_t GlobalReleasePhysical(){return GlobalState().releases;}static size_t GlobalResident(){return GlobalState().nodes;}
  size_t InstallPhysical()const{size_t result=0;for(const auto&[id,group]:groups_)if(group->dispatched&&!group->installed)++result;return result;}
  size_t Active()const{return active_nodes_;}size_t Resident()const{return resident_nodes_;}size_t Groups()const{return groups_.size();}size_t Bytes()const{return resident_bytes_;}size_t Physical()const{return physical_;}size_t Borrows()const{return borrows_;}bool Failed()const{return failed_;}
 private:
  struct Global{size_t nodes=0,groups=0,bytes=0,installs=0,releases=0,borrows=0;};
  static Global& GlobalState(){static Global state;return state;}
  struct Life{bool alive=true;};
  bool ValidNamespace()const{return !namespace_.empty()&&namespace_.size()<=64&&std::all_of(namespace_.begin(),namespace_.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');});}
  static bool Valid(const Document&doc){return !doc.frame.empty()&&doc.frame.size()<=256&&doc.session.size()<=256&&doc.loader.size()<=256&&doc.main>0&&doc.utility>0&&!doc.main_unique.empty()&&doc.main_unique.size()<=256&&!doc.utility_unique.empty()&&doc.utility_unique.size()<=256;}
  std::shared_ptr<Group> Find(uint64_t ticket){const auto found=groups_.find(ticket);return found==groups_.end()?nullptr:found->second;}
  std::string Token(const Group&group,size_t index)const{return group.references[index];}
  void EraseToken(const Group&group,size_t index){
    const auto found=tokens_.find(Token(group,index));
    // A physical release ACK for an older group must not erase the same token
    // after an atomic publication transferred it to the new retained array.
    if(found!=tokens_.end()&&found->second.first==group.ticket)tokens_.erase(found);
  }
  void Retire(const std::shared_ptr<Group>&group,double now){
    if(group->retiring)return;
    group->retiring=true;group->retirement_deadline=now+30000;
    if(group->active){group->active=false;--active_groups_;active_nodes_-=group->nodes.size();for(size_t i=0;i<group->nodes.size();++i)EraseToken(*group,i);}
    if(!group->dispatched)Remove(group);
  }
  void Remove(const std::shared_ptr<Group>&group){
    const auto found=groups_.find(group->ticket);if(found==groups_.end()||found->second!=group)return;
    if(group->active){--active_groups_;active_nodes_-=group->nodes.size();}
    if(group->pending_release){--physical_;--GlobalState().releases;}
    if(group->dispatched&&!group->installed)--GlobalState().installs;
    for(size_t i=0;i<group->nodes.size();++i)EraseToken(*group,i);
    resident_nodes_-=group->nodes.size();resident_bytes_-=group->bytes;GlobalState().nodes-=group->nodes.size();GlobalState().bytes-=group->bytes;--GlobalState().groups;groups_.erase(found);
    bool remains=false;for(const auto&[id,other]:groups_)if(other->document.frame==group->document.frame){remains=true;break;}if(!remains)latest_.erase(group->document.frame);
  }
  void Pump(){
    if(closed_||failed_||pumping_)return;
    pumping_=true;
    for(auto it=groups_.begin();it!=groups_.end();){auto group=it->second;++it;if(group->retiring&&group->document_dead&&!group->late_install&&group->installed&&!group->borrows&&!group->pending_release)Remove(group);}
    while(physical_<kPhysicalReleases&&GlobalState().releases<kGlobalReleases){
      std::shared_ptr<Group> next;for(const auto&[id,group]:groups_)if(group->retiring&&group->installed&&!group->pending_release&&!group->release_failed&&!group->borrows){next=group;break;}
      if(!next)break;
      next->pending_release=true;++physical_;++GlobalState().releases;const auto life=life_;
      release_(*next,[this,life,next](bool ok,const std::string&error){
        if(!life->alive||closed_)return;
        const auto found=groups_.find(next->ticket);if(found==groups_.end()||found->second!=next||!next->pending_release)return;
        next->pending_release=false;--physical_;--GlobalState().releases;
        if(ok)Remove(next);else{next->release_failed=true;failed_=true;fatal_(error.empty()?"Native snapshot reference release failed":error);}
        Pump();
      });
      if(failed_)break;
    }
    pumping_=false;
  }
  std::string namespace_;Current current_;Release release_;Fatal fatal_;
  std::shared_ptr<Life>life_=std::make_shared<Life>();std::map<uint64_t,std::shared_ptr<Group>>groups_;std::map<std::string,std::pair<uint64_t,size_t>>tokens_;std::map<std::string,uint64_t>latest_;
  uint64_t sequence_=0;size_t resident_nodes_=0,resident_bytes_=0,active_nodes_=0,active_groups_=0,physical_=0,borrows_=0;bool closed_=false,failed_=false,pumping_=false;
};
}
