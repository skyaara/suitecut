// SPDX-License-Identifier: MIT
#include "handle_disposal_controls.h"
#include <cassert>
#include <deque>
#include <tuple>
using Controls=suitecut::HandleDisposalControls;
struct Fixture{
 std::deque<Controls::Ack> pending;std::vector<int> retired;std::vector<std::tuple<int,uint64_t,bool>> done;int failed=0;
 Controls controls{[this](int,const Controls::Handle&,Controls::Ack ack){pending.push_back(std::move(ack));},[this](const auto&handle){retired.push_back(handle.id);},[this](int owner,uint64_t generation,bool ok,const auto&){done.emplace_back(owner,generation,ok);},[this](const auto&){++failed;}};
 void Ack(bool ok=true,bool destroyed=false){auto callback=std::move(pending.front());pending.pop_front();callback(ok,destroyed,"Actual release failure");}
};
int main(){
 {Fixture f;std::vector<Controls::Handle> handles;for(int id=1;id<=1024;++id)handles.push_back({id,"actual-group-"+std::to_string(id),""});assert(f.controls.Begin(1,1,handles,10));assert(f.controls.Size()==1&&f.controls.Handles()==1024&&f.controls.Physical()==1&&f.retired.empty());for(int id=1;id<=1024;++id){f.Ack();assert(f.retired.size()==static_cast<size_t>(id));}assert(f.controls.Size()==0&&f.controls.Physical()==0&&f.controls.Handles()==0&&f.done.size()==1&&std::get<2>(f.done.front()));}
 {Fixture f;for(int id=1;id<=64;++id)assert(f.controls.Begin(id,id,{{id,"group","session"}},0));assert(!f.controls.Begin(65,65,{{65,"group","session"}},0));assert(f.controls.Physical()==64);f.Ack();assert(f.controls.Begin(65,65,{{65,"group","session"}},0));assert(f.controls.Physical()==64);while(!f.pending.empty())f.Ack();assert(f.controls.Size()==0);}
 {Fixture f;assert(!f.controls.Begin(1,1,{{1,"group",""},{1,"group",""}},0));assert(f.controls.Begin(1,1,{{1,"group",""}},0));assert(!f.controls.Begin(2,2,{{1,"group",""}},0));assert(f.controls.Begin(1,2,{{2,"group",""}},0));f.Ack(false);assert(f.retired.empty()&&f.controls.Failures()==1);f.Ack(false,true);assert(f.retired==std::vector<int>{2});assert(std::get<1>(f.done[0])==1&&std::get<1>(f.done[1])==2);}
 {Fixture f;assert(f.controls.Begin(1,1,{{1,"group",""},{2,"group",""}},0));f.controls.Tick(29999);assert(!f.failed);f.controls.Tick(30000);f.controls.Tick(31000);assert(f.failed==1&&f.controls.Physical()==1&&f.controls.Handles()==2&&f.retired.empty());f.controls.Close();f.Ack();assert(f.retired.empty()&&f.done.empty()&&f.controls.Physical()==0);}
 {Controls::Ack late;{Fixture f;assert(f.controls.Begin(1,1,{{1,"group",""}},0));late=std::move(f.pending.front());}late(true,false,"");}
}
