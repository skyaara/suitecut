// SPDX-License-Identifier: MIT
#include "selector_wait_controls.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <vector>
struct Harness{
 double now=10;int errors=0;
 struct Request{int owner;std::string group,session;std::function<void()> acknowledge;};std::deque<Request> requests;
 suitecut::SelectorWaitControls controls{[this]{return now;},[this](const std::string&){++errors;},[this](int owner,const std::string& group,const std::string& session,std::function<void()> done){requests.push_back({owner,group,session,std::move(done)});}};
 void Ack(){assert(!requests.empty());auto request=std::move(requests.front());requests.pop_front();request.acknowledge();}
};
int main(){
 {Harness h;auto& c=h.controls;assert(!c.Admit(0,1));assert(!c.Admit(1,0));assert(c.Admit(1,1));assert(c.Admit(1,1));assert(!c.Admit(1,2));auto call=c.Begin(1);assert(call&&c.Size()==1&&c.PhysicalSize()==1);c.Cancel(1);assert(c.Size()==1&&!c.Begin(1)&&!c.Admit(1,2));call->Settle();call->Settle();assert(c.Size()==0&&c.PhysicalSize()==0);assert(c.Admit(1,2));c.Cancel(1);assert(!c.Size());}
 {Harness h;auto& c=h.controls;std::vector<std::shared_ptr<suitecut::SelectorWaitControls::Physical>> held;for(int i=1;i<=64;++i){assert(c.Admit(i,1));assert(c.Group(i,"actual-group","actual-session"));held.push_back(c.Begin(i));assert(held.back());c.Cancel(i);}assert(!c.Admit(65,1)&&c.Size()==64&&c.PhysicalSize()==64);held.clear();assert(c.Size()==64&&c.PhysicalSize()==64&&h.requests.size()==64&&!c.Admit(65,1));while(!h.requests.empty())h.Ack();assert(!c.Size()&&!c.PhysicalSize()&&c.Admit(65,1));c.Cancel(65);}
 {Harness h;auto& c=h.controls;assert(c.Admit(1,1)&&c.Group(1,"first-group","actual-session")&&c.Group(1,"second-group","actual-session"));auto call=c.Begin(1);c.Pause(1);assert(c.Retrying(1)&&!c.Ready(1)&&!c.Begin(1));call.reset();assert(h.requests.size()==1&&!c.Ready(1));h.Ack();assert(h.requests.size()==1&&!c.Ready(1));h.Ack();assert(c.Ready(1)&&c.Retrying(1));c.Resume(1);assert(!c.Retrying(1)&&c.Begin(1));c.Cancel(1);assert(!c.Size());}
 {Harness h;auto& c=h.controls;assert(c.Admit(1,1)&&c.Group(1,"automation","actual-session")&&c.Group(1,"retained-handle","actual-session"));c.Keep(1,"retained-handle","actual-session");c.Cancel(1);assert(h.requests.size()==1&&h.requests.front().group=="automation"&&c.PhysicalSize()==1);h.Ack();assert(!c.Size()&&!c.PhysicalSize()&&h.requests.empty());}
 {Harness h;auto& c=h.controls;assert(c.Admit(1,1)&&c.Group(1,"group","session"));std::vector<std::shared_ptr<suitecut::SelectorWaitControls::Physical>> held;for(int i=0;i<64;++i){held.push_back(c.Begin(1));assert(held.back());}assert(!c.Begin(1));c.Cancel(1);assert(c.NeedsTick());h.now+=30001;c.Tick();c.Tick();assert(h.errors==1&&!c.NeedsTick()&&c.Size()==1&&c.PhysicalSize()==64);held.clear();assert(c.Size()==1&&c.PhysicalSize()==1);h.Ack();assert(!c.Size()&&!c.PhysicalSize());}
 {double now=0;int errors=0;suitecut::SelectorWaitControls* address=nullptr;suitecut::SelectorWaitControls c([&]{return now;},[&](const std::string&){++errors;address->Close();});address=&c;assert(c.Admit(1,1));auto call=c.Begin(1);c.Cancel(1);now+=30001;c.Tick();assert(errors==1&&!c.Size()&&c.PhysicalSize()==1&&!c.Admit(2,1));call.reset();assert(!c.PhysicalSize());}
 {std::shared_ptr<suitecut::SelectorWaitControls::Physical> held;{Harness h;auto& c=h.controls;assert(c.Admit(1,1));held=c.Begin(1);c.Close();assert(!c.Size()&&c.PhysicalSize()==1&&!c.Admit(2,1));}held.reset();std::function<void()> late;{Harness h;auto& c=h.controls;assert(c.Admit(1,1)&&c.Group(1,"group","session"));c.Cancel(1);late=std::move(h.requests.front().acknowledge);c.Close();assert(c.PhysicalSize()==1);}late();}
 {Harness h;auto& c=h.controls;assert(c.Admit(1,1));assert(!c.Group(1,"","session")&&!c.Group(1,std::string(257,'g'),"session")&&!c.Group(1,"group",std::string(257,'s')));for(int i=0;i<64;++i)assert(c.Group(1,"group"+std::to_string(i),"session"));assert(c.Group(1,"group0","session")&&!c.Group(1,"group-overflow","session"));c.Cancel(1);while(!h.requests.empty())h.Ack();assert(!c.Size()&&!c.PhysicalSize());}
 std::cout<<"PASS8 selector wait physical admission, ACK-owned group cleanup, retry boundaries, committed handle transfer, bounds, cancellation, expiry and destruction\n";
}
