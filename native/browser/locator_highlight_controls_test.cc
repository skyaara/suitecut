// SPDX-License-Identifier: MIT
#include "locator_highlight_controls.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <deque>
#include <iostream>
struct Harness {
 using Controls=suitecut::LocatorHighlights;using Ready=Controls::Ready;using Dict=Controls::Dict;using Document=Controls::Document;
 struct Call{std::string session,method;Dict params;double deadline;Ready done;};
 struct Result{bool ok;std::string error,json;};
 double now=1;bool current=true,submit=true;std::deque<Call> calls;std::vector<Result> results;std::vector<std::string> fatal;
 std::shared_ptr<suitecut::DOMDomainLeases> dom=std::make_shared<suitecut::DOMDomainLeases>(suitecut::DOMDomainLeases::Hooks{[this]{return now;},[this](const std::string&s,const std::string&m,double d,Ready r){return Send(s,m,nullptr,d,std::move(r));},[this](const std::string&,const std::string&e){fatal.push_back(e);}});
 std::shared_ptr<Controls> controls=std::make_shared<Controls>(Controls::Hooks{[this]{return now;},[this](const Document&){return current;},[this](const std::string&s,const std::string&m,Dict p,double d,Ready r){return Send(s,m,p,d,std::move(r));},[this](const std::string&e){fatal.push_back(e);},[]{},dom,"ActualSource"});
 ~Harness(){controls->Closed();dom->Closed();}
 bool Send(const std::string&s,const std::string&m,Dict p,double d,Ready r){if(!submit)return false;calls.push_back({s,m,p?p->Copy(false):nullptr,d,std::move(r)});return true;}
 static Document Doc(std::string session=""){return{"actualFrame",session,"actualLoader","actualMainUnique","actualUtilityUnique",11,12,1};}
 Controls::Reply Reply(){return[this](bool ok,const std::string&e,const std::string&j){results.push_back({ok,e,j});};}
 void Show(int owner=1,uint64_t generation=1,double deadline=100,Document doc=Doc()){controls->Show(owner,generation,std::move(doc),42,deadline,Reply());}
 void Ack(const std::string&method,bool ok=true){assert(!calls.empty()&&calls.front().method==method);auto call=std::move(calls.front());calls.pop_front();call.done(ok,ok?"":"actual native failure");}
 std::string Token()const{assert(!results.empty());auto text=results.back().json;const auto begin=text.find("hActualSource_");assert(begin!=std::string::npos);return text.substr(begin,text.find('"',begin)-begin);}
 std::string Shown(){Show();assert(controls->Resident()==1&&controls->Active()==0&&results.empty());Ack("DOM.enable");Ack("Overlay.enable");assert(calls.front().params->GetInt("backendNodeId")==42);Ack("Overlay.highlightNode");assert(results.back().ok);return Token();}
 std::string Claimed(){const auto token=Shown();controls->Claim(token,Reply());assert(results.back().ok&&!controls->NeedsTick());return token;}
 void Clean(){Ack("Overlay.hideHighlight");Ack("Overlay.disable");Ack("DOM.disable");assert(controls->Resident()==0&&dom->Owners()==0&&dom->Physical()==0&&calls.empty());}
};
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));
 int checks=0;
 {
  Harness h;const auto token=h.Claimed();assert(h.controls->Active()==1);h.controls->Hide("",token,100,h.Reply());assert(h.results.size()==2);h.Clean();assert(h.results.size()==3&&h.results.back().ok);++checks;
 }
 {
  Harness h;h.Show();h.controls->Cancel(1,1);assert(h.dom->Physical()==1&&h.controls->Resident()==1&&h.results.empty());h.Ack("DOM.enable");assert(h.calls.front().method=="DOM.disable");h.Ack("DOM.disable");assert(h.controls->Resident()==0&&h.results.empty()&&h.dom->Physical()==0);++checks;
 }
 {
  Harness h;h.Show();h.Ack("DOM.enable");h.now=101;h.Ack("Overlay.enable");assert(h.results.size()==1&&!h.results.back().ok&&h.results.back().error=="Native operation timed out");assert(h.calls.front().method=="Overlay.disable");h.Ack("Overlay.disable");h.Ack("DOM.disable");assert(h.controls->Resident()==0);++checks;
 }
 {
  Harness h;h.Show();h.Ack("DOM.enable");h.Ack("Overlay.enable");h.controls->Cancel(1,1);h.Ack("Overlay.highlightNode");assert(h.results.empty()&&h.controls->Resident()==1);h.Clean();++checks;
 }
 {
  Harness h;const auto token=h.Shown();h.now=101;h.controls->Claim(token,h.Reply());assert(!h.results.back().ok&&h.results.back().error=="Native operation timed out");h.Clean();++checks;
 }
 {
  Harness h;const auto token=h.Shown();(void)token;h.now=101;h.controls->Tick();assert(h.controls->Resident()==1);h.Clean();++checks;
 }
 {
  Harness h;const auto old=h.Claimed();h.Show(2,2,200);assert(h.calls.front().method=="Overlay.highlightNode");h.Ack("Overlay.highlightNode");const auto next=h.Token();assert(next!=old);h.controls->Claim(next,h.Reply());h.controls->Hide("",old,200,h.Reply());assert(h.results.back().ok&&h.calls.empty()&&h.controls->Active()==1);h.controls->Hide("",next,200,h.Reply());h.Clean();++checks;
 }
 {
  Harness h;const auto token=h.Claimed();h.current=false;h.controls->InvalidateFrame("actualFrame");h.Clean();h.controls->Hide("",token,100,h.Reply());assert(h.results.back().ok&&h.calls.empty());++checks;
 }
 {
  Harness h;const auto token=h.Claimed();h.controls->Hide("",token,10,h.Reply());h.now=11;h.Ack("Overlay.hideHighlight");h.Ack("Overlay.disable");h.Ack("DOM.disable");assert(!h.results.back().ok&&h.results.back().error=="Native operation timed out"&&h.controls->Resident()==0);++checks;
 }
 {
  Harness h;const auto token=h.Claimed();h.controls->Hide("",token,100,h.Reply());h.Ack("Overlay.hideHighlight",false);assert(h.fatal.size()==1&&h.controls->Resident()==1&&h.dom->Owners()==1&&!h.results.back().ok);h.controls->Detached("");h.dom->Destroyed("");assert(h.controls->Resident()==0&&h.dom->Owners()==0);++checks;
 }
 {
  Harness h;h.Show();h.controls->Detached("");h.dom->Destroyed("");assert(h.results.size()==1&&!h.results.back().ok&&h.dom->Physical()==0);h.Ack("DOM.enable");assert(h.calls.empty()&&h.controls->Resident()==0);++checks;
 }
 {
  Harness h;int covered=0;h.dom->Acquire("","coverage",100,[&](bool ok,const std::string&){assert(ok);++covered;});h.Ack("DOM.enable");h.Show();h.Ack("Overlay.enable");h.Ack("Overlay.highlightNode");auto token=h.Token();h.controls->Claim(token,h.Reply());h.controls->Hide("",token,100,h.Reply());h.Ack("Overlay.hideHighlight");h.Ack("Overlay.disable");assert(h.controls->Resident()==0&&h.dom->Owners()==1&&h.calls.empty()&&covered==1);h.dom->Release("","coverage",[](bool ok,const std::string&){assert(ok);});h.Ack("DOM.disable");++checks;
 }
 {
  Harness h;h.submit=false;h.Show();assert(h.results.size()==1&&!h.results.back().ok&&h.controls->Resident()==0&&h.dom->Owners()==0&&h.calls.empty());++checks;
 }
 {
  Harness h;h.controls->Empty("",100,h.Reply());assert(h.results.back().ok&&h.results.back().json.find("\"empty\":true")!=std::string::npos&&h.controls->Resident()==0&&h.calls.empty());++checks;
 }
 {
  Harness h;h.Show(1,1,100,Harness::Doc("oopif"));h.Ack("DOM.enable");h.Ack("Overlay.enable");h.Ack("Overlay.highlightNode");auto token=h.Token();h.controls->Claim(token,h.Reply());h.controls->Detached("");h.dom->Destroyed("");assert(h.controls->Active()==1&&h.controls->SessionOwned("oopif"));h.controls->Detached("oopif");h.dom->Destroyed("oopif");assert(h.controls->Resident()==0&&h.dom->Owners()==0);++checks;
 }
 std::cout<<"Native locator highlights "<<checks<<" checks PASS\n";
}
