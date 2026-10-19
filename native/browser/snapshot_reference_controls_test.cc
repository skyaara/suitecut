// SPDX-License-Identifier: MIT
#include "snapshot_reference_controls.h"
#include <cassert>
#include <iostream>
#include <vector>
using Registry=suitecut::SnapshotReferences;
struct Harness {
  using Ack=std::function<void(bool,const std::string&)>;
  std::vector<Ack>ack;std::vector<std::string>errors;bool current=true;
  Registry refs{"actualSource",[this](int,uint64_t,const Registry::Document&){return current;},[this](const Registry::Group&,Ack done){ack.push_back(std::move(done));},[this](const std::string&error){errors.push_back(error);}};
  static Registry::Document Doc(int index=1){return{"frame"+std::to_string(index),"actualSession","loader1","mainUnique1","utilityUnique1",11,12,7};}
  uint64_t Begin(Registry::Document doc=Doc(),int owner=1,uint64_t generation=1){return refs.Begin(owner,generation,std::move(doc),{{41,1}},1000,0);}
  uint64_t Active(Registry::Document doc=Doc(),int owner=1,uint64_t generation=1){const auto id=Begin(std::move(doc),owner,generation);assert(id&&refs.Dispatched(id));assert(refs.Installed(id,true,"actual-array",0));assert(refs.Commit(id,1));return id;}
  void AckAt(size_t index,bool ok=true){assert(index<ack.size());auto done=ack[index];done(ok,ok?"":"actual release failure");}
  void Empty(){assert(refs.Active()==0&&refs.Resident()==0&&refs.Groups()==0&&refs.Bytes()==0&&refs.Physical()==0&&refs.InstallPhysical()==0&&refs.Borrows()==0);}
};
int main(){
  {
    Harness h;const auto first=h.Active();const auto ref=h.refs.Reference(first,0);assert(!ref.empty());const auto second=h.Begin();assert(h.refs.Lookup(ref,Harness::Doc()));assert(h.refs.Dispatched(second));assert(h.refs.Installed(second,true,"actual-second",2));assert(h.refs.Commit(second,3));assert(h.refs.Lookup(ref,Harness::Doc()));assert(h.refs.Reference(second,0)==ref);assert(h.refs.Resident()==2&&h.refs.Physical()==1);h.AckAt(0);assert(h.refs.Resident()==1&&h.refs.Active()==1&&h.refs.Lookup(ref,Harness::Doc()));h.refs.ChannelDestroyed("actualSession");h.Empty();
  }
  {
    Harness h;const auto id=h.Begin();assert(h.refs.Dispatched(id));h.refs.Cancel(1,1,1);assert(h.refs.InstallPhysical()==1&&h.refs.Resident()==1&&h.ack.empty());assert(!h.refs.Installed(id,true,"late-actual-array",2));assert(h.refs.Physical()==1&&h.refs.Resident()==1);h.AckAt(0);h.Empty();
  }
  {
    Harness h;const auto first=h.Active();auto lease=h.refs.Lookup(h.refs.Reference(first,0),Harness::Doc());assert(lease&&lease.group->nodes[lease.index].backend==41);h.Active();assert(h.ack.empty()&&h.refs.Borrows()==1&&h.refs.Resident()==2);lease={};assert(h.refs.Borrows()==0&&h.refs.Physical()==1);h.AckAt(0);h.refs.ChannelDestroyed("actualSession");h.Empty();
  }
  {
    Harness h;for(int i=0;i<64;++i)h.Active(Harness::Doc(i),i+1,i+1);for(int i=0;i<64;++i)h.refs.InvalidateFrame(Harness::Doc(i).frame,2);assert(h.refs.Physical()==32&&h.ack.size()==32&&h.refs.Resident()==64);for(size_t i=0;i<64;++i)h.AckAt(i);assert(h.ack.size()==64);h.Empty();
  }
  {
    Harness h;std::vector<uint64_t>ids;for(int i=0;i<64;++i){ids.push_back(h.Begin(Harness::Doc(i),i+1,i+1));assert(h.refs.Dispatched(ids.back()));h.refs.Cancel(i+1,i+1,1);}assert(h.refs.InstallPhysical()==64);const auto denied=h.Begin(Harness::Doc(65),65,65);assert(denied&&!h.refs.Dispatched(denied));h.refs.Cancel(65,65,1);assert(h.refs.InstallPhysical()==64&&h.refs.Resident()==64);h.refs.ChannelDestroyed("actualSession");h.Empty();assert(!h.refs.Installed(ids[0],true,"late-channel-array",3));
  }
  {
    Harness h;const auto id=h.Begin();assert(h.refs.Dispatched(id));h.refs.DocumentDestroyed(Harness::Doc(),1);assert(h.refs.InstallPhysical()==1&&h.refs.Resident()==1&&h.ack.empty());assert(!h.refs.Installed(id,true,"after-real-context-death",2));assert(h.refs.Physical()==1);h.AckAt(0);h.Empty();
  }
  {
    Harness h;h.Active();h.refs.InvalidateFrame(Harness::Doc().frame,1);h.refs.DocumentDestroyed(Harness::Doc(),2);assert(h.refs.Physical()==1&&h.refs.Resident()==1);h.AckAt(0);h.Empty();
  }
  {
    Harness h;h.Active();h.refs.InvalidateFrame(Harness::Doc().frame,1);h.AckAt(0,false);assert(h.refs.Failed()&&h.errors.size()==1&&h.refs.Resident()==1&&h.refs.Physical()==0);assert(!h.Begin());h.AckAt(0);assert(h.errors.size()==1);h.refs.ChannelDestroyed("actualSession");h.Empty();
  }
  {
    Harness h;const auto id=h.Begin();assert(h.refs.Dispatched(id));h.refs.Cancel(1,1,0);h.refs.Tick(29999);assert(h.errors.empty());h.refs.Tick(30000);assert(h.errors.size()==1&&h.refs.InstallPhysical()==1&&h.refs.Resident()==1);h.refs.ChannelDestroyed("actualSession");h.Empty();
  }
  {
    Harness h;const auto first=h.Active();auto old=Harness::Doc(),fresh=old;fresh.context_generation++;fresh.main_unique="mainUnique2";fresh.utility_unique="utilityUnique2";assert(!h.refs.Lookup(h.refs.Reference(first,0),fresh));const auto next=h.Active(fresh,2,2);const auto current=h.refs.Reference(next,0);h.refs.DocumentDestroyed(old,2);assert(h.refs.Lookup(current,fresh));h.refs.ChannelDestroyed("actualSession");h.Empty();h.AckAt(0);h.Empty();
  }
  {
    Harness h;const auto old=h.Begin();assert(h.refs.Dispatched(old));const auto blank=h.refs.Begin(2,2,Harness::Doc(),{},1000,0);assert(blank&&h.refs.Dispatched(blank)&&h.refs.Installed(blank,true,"actual-empty",1)&&h.refs.Commit(blank,2));assert(h.refs.Active()==0&&h.refs.InstallPhysical()==1);assert(!h.refs.Installed(old,true,"actual-old",3)&&!h.refs.Commit(old,3));assert(h.ack.size()==2);h.AckAt(0);h.AckAt(1);h.Empty();
  }
  {
    Harness h;auto doc=Harness::Doc();doc.utility_unique.clear();assert(!h.Begin(doc));assert(!h.refs.Begin(1,1,Harness::Doc(),{{7,1},{7,1}},1000,0));assert(!h.refs.Begin(1,1,Harness::Doc(),{{7,3}},1000,0));assert(!h.refs.Begin(1,1,Harness::Doc(),{{7,1}},5,5));h.Empty();
  }
  {
    Harness first,second,third;
    for(int i=0;i<64;++i){const auto a=first.Begin(Harness::Doc(i),i+1,i+1),b=second.Begin(Harness::Doc(i),i+1,i+1);assert(first.refs.Dispatched(a)&&second.refs.Dispatched(b));first.refs.Cancel(i+1,i+1,0);second.refs.Cancel(i+1,i+1,0);}
    assert(Registry::GlobalInstallPhysical()==128&&Registry::GlobalResident()==128);
    const auto denied=third.Begin();assert(denied&&!third.refs.Dispatched(denied));third.refs.Cancel(1,1,0);
    first.refs.ChannelDestroyed("actualSession");assert(Registry::GlobalInstallPhysical()==64);
    const auto accepted=third.Begin();assert(accepted&&third.refs.Dispatched(accepted));third.refs.Cancel(1,1,0);assert(Registry::GlobalInstallPhysical()==65);
    second.refs.ChannelDestroyed("actualSession");third.refs.ChannelDestroyed("actualSession");first.Empty();second.Empty();third.Empty();assert(Registry::GlobalInstallPhysical()==0&&Registry::GlobalResident()==0);
  }
  {
    Harness first,second,third;
    for(int i=0;i<32;++i){first.Active(Harness::Doc(i),i+1,i+1);second.Active(Harness::Doc(i),i+1,i+1);}
    third.Active();
    for(int i=0;i<32;++i){first.refs.InvalidateFrame(Harness::Doc(i).frame,1);second.refs.InvalidateFrame(Harness::Doc(i).frame,1);}
    third.refs.InvalidateFrame(Harness::Doc().frame,1);assert(Registry::GlobalReleasePhysical()==64&&third.ack.empty()&&third.refs.Resident()==1);
    first.AckAt(0);third.refs.Tick(2);assert(third.ack.size()==1&&Registry::GlobalReleasePhysical()==64);
    first.refs.ChannelDestroyed("actualSession");second.refs.ChannelDestroyed("actualSession");third.AckAt(0);first.Empty();second.Empty();third.Empty();assert(Registry::GlobalReleasePhysical()==0&&Registry::GlobalResident()==0);
  }
  {
    Harness h;const auto first=h.Active();const auto token=h.refs.Reference(first,0);const auto staged=h.Begin();assert(staged&&h.refs.Dispatched(staged)&&h.refs.Installed(staged,true,"actual-native-formatter-failure-candidate",2));
    h.refs.Cancel(1,1,3); // A genuine precommit formatter failure cancels staging only.
    assert(h.refs.Lookup(token,Harness::Doc())&&h.refs.Active()==1&&h.refs.Resident()==2&&h.refs.Physical()==1);h.AckAt(0);assert(h.refs.Active()==1&&h.refs.Resident()==1);h.refs.ChannelDestroyed("actualSession");h.Empty();
  }
  {
    Harness h;const auto ticket=h.Begin();assert(h.refs.Dispatched(ticket));h.refs.Cancel(1,1,1);h.refs.Rejected(ticket);h.Empty();assert(h.ack.empty()&&!Registry::GlobalInstallPhysical());h.refs.Rejected(ticket);h.Empty();
  }
  {
    Harness h;const auto ticket=h.Begin();assert(ticket);h.refs.Rejected(ticket);h.Empty();assert(h.ack.empty());
  }
  {
    Harness h;const auto ticket=h.Begin();assert(h.refs.Dispatched(ticket)&&h.refs.Installed(ticket,true,"actual-staging-read",1));auto read=h.refs.Use(ticket);assert(read&&h.refs.Borrows()==1);h.refs.Cancel(1,1,2);assert(h.ack.empty()&&h.refs.Resident()==1&&!h.refs.Use(ticket));read.reset();assert(h.refs.Physical()==1&&h.ack.size()==1);h.AckAt(0);h.Empty();
  }
  {
    Harness h;const auto ticket=h.Active();std::vector<std::shared_ptr<void>> leases;for(size_t i=0;i<Registry::kBorrows;++i){leases.push_back(h.refs.Use(ticket));assert(leases.back());}assert(!h.refs.Use(ticket));h.refs.ChannelDestroyed("actualSession");h.Empty();leases.clear();h.Empty();
  }
  {
    Harness h;auto main=Harness::Doc(1);main.session="";const auto main_ticket=h.Active(main,1,1);auto main_borrow=h.refs.Lookup(h.refs.Reference(main_ticket,0),main);auto child=Harness::Doc(2);const auto child_ticket=h.Active(child,2,2);auto child_borrow=h.refs.Lookup(h.refs.Reference(child_ticket,0),child);auto child_pending=Harness::Doc(3);const auto pending=h.Begin(child_pending,3,3);assert(h.refs.Dispatched(pending));
    h.refs.ChannelDestroyed("");assert(h.refs.Resident()==2&&h.refs.Active()==1&&h.refs.Borrows()==1&&h.refs.InstallPhysical()==1&&h.ack.empty());main_borrow={};assert(h.refs.Borrows()==1);
    h.refs.InvalidateFrame(child.frame,2);h.refs.Cancel(3,3,2);assert(h.ack.empty()&&h.refs.InstallPhysical()==1);child_borrow={};assert(h.ack.size()==1&&h.refs.Physical()==1);assert(!h.refs.Installed(pending,true,"actual-child-late-after-main-death",3));assert(h.ack.size()==2&&h.refs.Physical()==2);h.AckAt(0);h.AckAt(1);h.Empty();
  }
  {
    Harness h;auto main=Harness::Doc(1);main.session="";const auto main_ticket=h.Active(main,1,1),child_ticket=h.Active(Harness::Doc(2),2,2);const auto main_token=h.refs.Reference(main_ticket,0),child_token=h.refs.Reference(child_ticket,0);auto borrow=h.refs.Lookup(main_token,main);assert(borrow);h.refs.ChannelDestroyed("actualSession");assert(h.refs.Resident()==1&&h.refs.Active()==1&&h.refs.Borrows()==1&&h.refs.Lookup(main_token,main)&&!h.refs.Lookup(child_token,Harness::Doc(2)));borrow={};h.refs.ChannelDestroyed("");h.Empty();
  }
  {
    Harness h;const auto prior=h.Active();const auto token=h.refs.Reference(prior,0);const auto candidate=h.Begin(Harness::Doc(),2,2);assert(candidate&&h.refs.Dispatched(candidate));h.refs.Rejected(candidate);assert(h.refs.Active()==1&&h.refs.Resident()==1&&h.refs.Lookup(token,Harness::Doc())&&h.ack.empty()&&!h.refs.InstallPhysical());h.refs.ChannelDestroyed("actualSession");h.Empty();
  }
  std::cout<<"PASS22 controlled native reference registry lifecycle groups; no actual browser-reference integration claim\n";
}
