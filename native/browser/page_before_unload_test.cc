// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "page_before_unload.h"
#include "dialog_controls.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <vector>

class ObservedDialog final:public CefJSDialogCallback{
 public:
  int calls=0;bool accepted=false;std::function<void(bool)> observe;
  void Continue(bool accept,const CefString&)override{++calls;accepted=accept;if(observe)observe(accept);}
 private:IMPLEMENT_REFCOUNTING(ObservedDialog);
};
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  using suitecut::PageBeforeUnload;
  {
    PageBeforeUnload attempt;
    assert(!attempt.Begin("expired",10,10)&&!attempt.Pending());
    const auto first=attempt.Begin("original",100,1);assert(first);
    assert(attempt.Begin("replacement",200,2)==first);
    const auto approved=attempt.Approve();assert(approved&&approved->reason=="original"&&approved->deadline==100);
    assert(!attempt.Pending()&&!attempt.Approve());
    const auto second=attempt.Begin("next",300,101);assert(second>first);
    attempt.Cancel(first);assert(attempt.Pending()==second);
    attempt.Force();assert(!attempt.Pending()&&!attempt.Approve());
  }
  for(const bool accept:{false,true}){
    PageBeforeUnload attempt;const auto token=attempt.Begin("real request",60000,0);assert(token);
    std::vector<SuiteCutDialogControls::Dict>events;
    SuiteCutDialogControls dialogs([&](auto event){events.push_back(event->Copy(false));});dialogs.Subscribe(true);
    CefRefPtr<ObservedDialog>callback=new ObservedDialog;
    callback->observe=[&](bool accepted){assert(accepted?attempt.Pending()==token:!attempt.Pending());};
    assert(dialogs.Show("beforeunload","https://example.test/","leave?","",callback,0,[&](bool accepted){if(!accepted)attempt.Cancel(token);}));
    assert(events.size()==1&&events[0]->GetString("state")=="opened"&&callback->calls==0);
    std::string error;assert(dialogs.Answer(events[0]->GetString("id"),accept,"",error));
    assert(callback->calls==1&&callback->accepted==accept&&!dialogs.AcceptedWorkReady());
    assert(!dialogs.Answer(events[0]->GetString("id"),accept,"",error)&&callback->calls==1);
    if(accept){const auto approval=attempt.Approve();assert(approval&&approval->reason=="real request");}
    else assert(!attempt.Approve());
    dialogs.Closed();assert(dialogs.AcceptedWorkReady()&&events.size()==2&&events[1]->GetString("state")=="closed");
  }
  for(const int mode:{0,1,2}){
    PageBeforeUnload attempt;const auto token=attempt.Begin("pending",60000,0);assert(token);
    SuiteCutDialogControls dialogs([](auto){});dialogs.Subscribe(true);
    CefRefPtr<ObservedDialog>callback=new ObservedDialog;
    assert(dialogs.Show("beforeunload","","leave?","",callback,0,[&](bool accepted){if(!accepted)attempt.Cancel(token);}));
    if(mode==0){dialogs.Tick(29999);assert(callback->calls==0&&attempt.Pending());dialogs.Tick(30000);dialogs.Closed();}
    else if(mode==1)dialogs.Clear(); // Genuine CEF reset: no invented Continue.
    else dialogs.Subscribe(false);
    assert(!attempt.Pending()&&!attempt.Approve()&&dialogs.AcceptedWorkReady());
    assert(callback->calls==(mode==1?0:1)&&!callback->accepted);
  }
  {
    PageBeforeUnload attempt;const auto first=attempt.Begin("first",60000,0);
    SuiteCutDialogControls dialogs([](auto){});dialogs.Subscribe(true);
    CefRefPtr<ObservedDialog>callback=new ObservedDialog;
    assert(dialogs.Show("beforeunload","","leave?","",callback,0,[&](bool accepted){if(!accepted)attempt.Cancel(first);}));
    attempt.Force();const auto second=attempt.Begin("later",60000,1);assert(second>first);
    dialogs.Clear(true);assert(callback->calls==1&&!callback->accepted&&attempt.Pending()==second);
  }
  std::cout<<"Beforeunload native attempt and real dialog callback controls passed\n";
}
