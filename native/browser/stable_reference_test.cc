// SPDX-License-Identifier: MIT
#include "snapshot_reference_controls.h"
#include <cassert>
#include <iostream>
#include <vector>
using Registry=suitecut::SnapshotReferences;
struct Harness {
  using Ack=std::function<void(bool,const std::string&)>;
  std::vector<Ack> releases;
  Registry refs{"stableSource",[](int,uint64_t,const auto&){return true;},
    [this](const auto&,Ack done){releases.push_back(std::move(done));},
    [](const auto&){assert(false);}};
  static Registry::Document Doc(){return{"frame","session","loader","mainUnique","utilityUnique",1,2,3};}
  uint64_t Stage(std::vector<int> backends,Registry::Document doc=Doc()){
    std::vector<Registry::Node> nodes;for(int backend:backends)nodes.push_back({backend,1});
    const auto ticket=refs.Begin(1,1,std::move(doc),std::move(nodes),10000,0);
    assert(ticket&&refs.Dispatched(ticket)&&refs.Installed(ticket,true,"retained-array",0));return ticket;
  }
  uint64_t Publish(std::vector<int> backends,Registry::Document doc=Doc()){
    const auto ticket=Stage(std::move(backends),std::move(doc));assert(refs.Commit(ticket,1));return ticket;
  }
  void AckAll(){for(size_t i=0;i<releases.size();++i)if(releases[i]){auto done=std::move(releases[i]);done(true,"");}}
};
int main(){int groups=0;
  {Harness h;const auto first=h.Publish({10,20});const auto ten=h.refs.Reference(first,0),twenty=h.refs.Reference(first,1);
    auto old=h.refs.Lookup(ten,Harness::Doc());assert(old);
    const auto second=h.Publish({20,10,30});assert(h.refs.Reference(second,0)==twenty&&h.refs.Reference(second,1)==ten);
    auto current=h.refs.Lookup(ten,Harness::Doc());assert(current&&current.group->ticket==second&&current.index==1);
    assert(old.group->ticket==first&&old.index==0&&old.group->nodes[old.index].backend==10);
    assert(h.releases.empty());old={};assert(h.releases.size()==1);h.AckAll();
    assert(h.refs.Lookup(ten,Harness::Doc())&&h.refs.Lookup(twenty,Harness::Doc()));++groups;}
  {Harness h;const auto first=h.Publish({10,20});const auto ten=h.refs.Reference(first,0),twenty=h.refs.Reference(first,1);
    h.Publish({20});assert(!h.refs.Lookup(ten,Harness::Doc())&&h.refs.Lookup(twenty,Harness::Doc()));h.AckAll();
    const auto third=h.Publish({10,20});assert(h.refs.Reference(third,0)!=ten&&h.refs.Reference(third,1)==twenty);
    assert(!h.refs.Lookup(ten,Harness::Doc()));h.AckAll();++groups;}
  {Harness h;const auto first=h.Publish({10});const auto ref=h.refs.Reference(first,0);
    const auto candidate=h.Stage({10});assert(h.refs.GroupFor(candidate)->references[0]==ref);
    h.refs.Cancel(1,1,1);h.AckAll();assert(h.refs.Lookup(ref,Harness::Doc())&&h.refs.Active()==1);
    const auto rejected=h.Stage({10});assert(!h.refs.CommitBatch({rejected,999999},2));h.AckAll();
    assert(h.refs.Lookup(ref,Harness::Doc())&&h.refs.Resident()==1);++groups;}
  for(int field=0;field<8;++field){Harness h;const auto first=h.Publish({10});const auto ref=h.refs.Reference(first,0);
    auto doc=Harness::Doc();switch(field){case 0:doc.frame+="new";break;case 1:doc.session+="new";break;
      case 2:doc.loader+="new";break;case 3:doc.main_unique+="new";break;case 4:doc.utility_unique+="new";break;
      case 5:++doc.main;break;case 6:++doc.utility;break;case 7:++doc.context_generation;break;}
    const auto next=h.Publish({10},doc);assert(h.refs.Reference(next,0)!=ref&&!h.refs.Lookup(ref,doc));
    h.refs.InvalidateFrame(Harness::Doc().frame,2);h.AckAll();assert(!h.refs.Lookup(ref,Harness::Doc()));}++groups;
  {Harness h;const auto first=h.Publish({10});const auto ref=h.refs.Reference(first,0);
    auto child=Harness::Doc();child.frame="child";const auto child_first=h.Publish({10},child);
    const auto child_ref=h.refs.Reference(child_first,0);assert(ref!=child_ref);
    const auto main=h.Stage({10}),other=h.Stage({10},child);assert(h.refs.CommitBatch({other,main},2));
    assert(h.refs.Reference(main,0)==ref&&h.refs.Reference(other,0)==child_ref);h.AckAll();
    assert(h.refs.Lookup(ref,Harness::Doc())&&h.refs.Lookup(child_ref,child));++groups;}
  {Harness h;const auto first=h.Publish({10});const auto ref=h.refs.Reference(first,0);
    for(int i=0;i<2000;++i){const auto next=h.Publish({10});assert(h.refs.Reference(next,0)==ref);h.AckAll();
      assert(h.refs.Groups()==1&&h.refs.Resident()==1&&h.refs.Active()==1&&h.refs.Physical()==0&&h.refs.Lookup(ref,Harness::Doc()));}
    h.refs.InvalidateFrame(Harness::Doc().frame,3);h.AckAll();assert(h.refs.Groups()==0&&h.refs.Resident()==0&&!h.refs.Lookup(ref,Harness::Doc()));++groups;}
  {Harness h;const auto first=h.Publish({10});const auto ref=h.refs.Reference(first,0);
    const auto one=h.Stage({10}),two=h.Stage({10});assert(h.refs.Commit(two,2)&&!h.refs.Commit(one,3));
    h.AckAll();assert(h.refs.Lookup(ref,Harness::Doc())&&h.refs.Groups()==1);++groups;}
  assert(Registry::GlobalResident()==0&&Registry::GlobalInstallPhysical()==0&&Registry::GlobalReleasePhysical()==0);
  std::cout<<"PASS "<<groups<<" stable native reference ownership groups\n";
}
