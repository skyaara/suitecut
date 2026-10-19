// SPDX-License-Identifier: MIT
#include "dom_domain_leases.h"
#include <cassert>
#include <deque>
#include <iostream>
struct Harness {
  using Ready=suitecut::DOMDomainLeases::Ready;
  struct Call{std::string session,method;double deadline;Ready done;};
  double now=1;bool submit=true;std::deque<Call> calls;std::vector<std::string> fatal;
  std::shared_ptr<suitecut::DOMDomainLeases> leases=std::make_shared<suitecut::DOMDomainLeases>(suitecut::DOMDomainLeases::Hooks{
    [this]{return now;},[this](const std::string& session,const std::string& method,double deadline,Ready done){if(!submit)return false;calls.push_back({session,method,deadline,std::move(done)});return true;},
    [this](const std::string&session,const std::string&error){fatal.push_back(session+":"+error);}});
  void Ack(const std::string&method,bool ok=true){assert(!calls.empty()&&calls.front().method==method);auto call=std::move(calls.front());calls.pop_front();call.done(ok,ok?"":"actual native failure");}
};
int main(){
 int checks=0;
 {
  Harness h;int ready=0,released=0;h.leases->Acquire("","highlight",100,[&](bool ok,const std::string&){assert(ok);++ready;});h.leases->Acquire("","coverage",100,[&](bool ok,const std::string&){assert(ok);++ready;});assert(h.calls.size()==1&&h.leases->Physical()==1);h.Ack("DOM.enable");assert(ready==2&&h.leases->Owners()==2);h.leases->Release("","coverage",[&](bool ok,const std::string&){assert(ok);++released;});assert(released==1&&h.calls.empty()&&h.leases->OtherOwners("","raw"));h.leases->Release("","highlight",[&](bool ok,const std::string&){assert(ok);++released;});assert(released==1&&h.leases->Physical()==1);h.Ack("DOM.disable");assert(released==2&&!h.leases->Owned("")&&h.leases->Physical()==0);++checks;
 }
 {
  Harness h;int canceled=0,released=0;h.leases->Acquire("child","highlight",10,[&](bool ok,const std::string&){assert(!ok);++canceled;});h.leases->Release("child","highlight",[&](bool ok,const std::string&){assert(ok);++released;});h.now=20;assert(canceled==1&&released==0&&h.leases->Physical()==1);h.Ack("DOM.enable");assert(released==0&&h.leases->Physical()==1);h.Ack("DOM.disable");assert(released==1&&h.leases->Physical()==0);++checks;
 }
 {
  Harness h;bool enabled=false;h.leases->Acquire("","coverage",10,[&](bool ok,const std::string&){enabled=ok;});h.now=11;h.Ack("DOM.enable");assert(enabled&&h.leases->OwnerReady("","coverage"));h.leases->Release("","coverage",[](bool ok,const std::string&){assert(ok);});h.Ack("DOM.disable");++checks;
 }
 {
  Harness h;h.leases->Acquire("","old",100,[](bool ok,const std::string&){assert(ok);});h.Ack("DOM.enable");h.leases->Release("","old",[](bool ok,const std::string&){assert(ok);});int expired=0;h.leases->Acquire("","new",10,[&](bool ok,const std::string&){assert(!ok);++expired;});h.now=11;h.Ack("DOM.disable");assert(expired==1&&h.calls.empty()&&h.leases->Owners()==0&&!h.leases->Owned(""));++checks;
 }
 {
  Harness h;int main=0,child=0;h.leases->Acquire("","main",100,[&](bool ok,const std::string&){assert(!ok);++main;});h.leases->Acquire("oopif","child",100,[&](bool ok,const std::string&){assert(ok);++child;});h.leases->Destroyed("");assert(main==1&&h.leases->Owners()==1&&h.leases->Physical()==1);h.Ack("DOM.enable");assert(child==0);h.Ack("DOM.enable");assert(child==1);h.leases->Destroyed("oopif");assert(h.leases->Owners()==0&&h.leases->Physical()==0);++checks;
 }
 {
  Harness h;h.leases->Acquire("","owner",100,[](bool ok,const std::string&){assert(ok);});h.Ack("DOM.enable");int released=0;h.leases->Release("","owner",[&](bool ok,const std::string&){assert(ok);++released;});h.Ack("DOM.disable",false);assert(released==0&&h.fatal.size()==1&&h.leases->Owned(""));h.leases->Destroyed("");assert(released==1&&!h.leases->Owned(""));++checks;
 }
 {
  Harness h;h.submit=false;int failed=0;h.leases->Acquire("","owner",100,[&](bool ok,const std::string&){assert(!ok);++failed;});assert(failed==1&&h.calls.empty()&&h.leases->Physical()==0&&h.leases->Owners()==0&&!h.leases->Owned(""));++checks;
 }
 {
  Harness h;h.leases->Acquire("","first",100,[&](bool ok,const std::string&){assert(ok);h.leases->Destroyed("");});int second=0;h.leases->Acquire("","second",100,[&](bool ok,const std::string&){assert(!ok);++second;});h.Ack("DOM.enable");assert(second==1&&!h.leases->Owned("")&&h.leases->Owners()==0);++checks;
 }
 {
  Harness h;int done=0;h.leases->DisableUnused("",100,[&](bool ok,const std::string&){assert(ok);++done;});assert(h.calls.front().method=="DOM.disable");h.Ack("DOM.disable");assert(done==1&&!h.leases->Owned(""));++checks;
 }
 {
  Harness h;int rejected=0;h.leases->Acquire("","",10,[&](bool ok,const std::string&){assert(!ok);++rejected;});h.now=11;h.leases->Acquire("","owner",10,[&](bool ok,const std::string&){assert(!ok);++rejected;});assert(rejected==2&&h.calls.empty());++checks;
 }
 {
  Harness h;int released=0,canceled=0,retained=0;
  for(int i=0;i<128;++i){h.leases->Acquire("","raw",1000,[&](bool ok,const std::string&){assert(!ok);++canceled;});h.leases->Release("","raw",[&](bool ok,const std::string&){assert(ok);++released;});}
  assert(h.calls.size()==1&&h.leases->Physical()==1&&h.leases->PendingReleases()==128&&h.leases->Owners()==0&&released==0&&canceled==128);
  h.leases->Acquire("","retained",1000,[&](bool ok,const std::string&){assert(ok);++retained;});int rejected=0;h.leases->Release("","retained",[&](bool ok,const std::string&error){assert(!ok&&error.find("capacity")!=std::string::npos);++rejected;});
  assert(rejected==1&&h.leases->Owners()==1&&h.leases->PendingReleases()==128&&retained==0);
  h.Ack("DOM.enable");assert(retained==1&&released==128&&h.leases->PendingReleases()==0&&h.calls.empty());
  h.leases->Release("","retained",[](bool ok,const std::string&){assert(ok);});h.Ack("DOM.disable");assert(h.leases->Owners()==0&&h.leases->PendingReleases()==0&&h.leases->Physical()==0);++checks;
 }
 {
  Harness h;int released=0;h.leases->Acquire("","owner",100,[&](bool ok,const std::string&){assert(!ok);h.leases->Destroyed("");});h.leases->Release("","owner",[&](bool ok,const std::string&){assert(ok);++released;});assert(released==1&&h.leases->Owners()==0&&h.leases->PendingReleases()==0&&h.leases->Physical()==0);h.Ack("DOM.enable");assert(released==1);++checks;
 }
 std::cout<<"Native DOM domain leases "<<checks<<" checks PASS\n";
}
