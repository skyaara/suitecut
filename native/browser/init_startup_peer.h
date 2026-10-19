// SPDX-License-Identifier: MIT
#pragma once
#include "init_snapshot_transport.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_v8.h"
#include <memory>

namespace suitecut {
// Distinct native types on the two sides. No IPC dictionary, URL or integer can
// construct a peer; actual native browser/main-frame objects are mandatory.
// The existing native controller supplies the context/receiver incarnation.
struct InitStartupBrowserSide {};
struct InitStartupRendererSide {};
template<class Side>class InitStartupPeer {
 public:
  using Ptr=std::shared_ptr<InitStartupPeer>;
  static Ptr Observe(const InitSnapshotOwner&owner,CefRefPtr<CefBrowser>browser,CefRefPtr<CefFrame>channel){
    if(!InitSnapshotWire::ValidOwner(owner)||!browser||!channel)return {};
    Ptr peer(new InitStartupPeer(owner,std::move(browser),std::move(channel)));
    return peer->Current()?peer:nullptr;
  }
  const InitSnapshotOwner&Owner()const{return owner_;}
  bool Current()const{
    if(retired_||!browser_->IsValid()||browser_->GetIdentifier()!=static_cast<int>(owner_.browser)||!channel_->IsValid()||!channel_->IsMain()||channel_->GetIdentifier()!=owner_.frame)return false;
    auto actual=channel_->GetBrowser();auto main=browser_->GetMainFrame();
    return actual&&actual->IsValid()&&actual->IsSame(browser_)&&main&&main->IsValid()&&main->GetIdentifier()==owner_.frame;
  }
  bool Channel(CefRefPtr<CefFrame>frame)const{
    if(!Current()||!frame||!frame->IsValid()||!frame->IsMain()||frame->GetIdentifier()!=owner_.frame)return false;
    auto actual=frame->GetBrowser();return actual&&actual->IsValid()&&actual->IsSame(browser_);
  }
  bool Owns(CefRefPtr<CefFrame>frame)const{
    if(!Current()||!frame||!frame->IsValid())return false;
    auto actual=frame->GetBrowser();return actual&&actual->IsValid()&&actual->IsSame(browser_);
  }
  void Retire(){retired_=true;}
 private:
  InitStartupPeer(InitSnapshotOwner owner,CefRefPtr<CefBrowser>browser,CefRefPtr<CefFrame>channel):owner_(std::move(owner)),browser_(std::move(browser)),channel_(std::move(channel)){}
  InitSnapshotOwner owner_;CefRefPtr<CefBrowser>browser_;CefRefPtr<CefFrame>channel_;bool retired_=false;
};
using InitStartupBrowserPeer=InitStartupPeer<InitStartupBrowserSide>;
using InitStartupRendererPeer=InitStartupPeer<InitStartupRendererSide>;

// Renderer-process main-thread ONLY. Browser-side admission code never calls
// this class or GetV8Context. It proves native identity, not pre-author capture.
class InitStartupDocument {
 public:
  static std::shared_ptr<const InitStartupDocument>Observe(InitStartupRendererPeer::Ptr peer,CefRefPtr<CefFrame>frame,CefRefPtr<CefV8Context>context,uint64_t sequence){
    if(!peer||!frame||!context||!sequence||!peer->Owns(frame)||!context->IsValid())return {};
    auto document=std::shared_ptr<const InitStartupDocument>(new InitStartupDocument(std::move(peer),std::move(frame),std::move(context),sequence));
    return document->Current()?document:nullptr;
  }
  bool Current()const{
    if(frame_id_.empty()||frame_id_.size()>InitSnapshotWire::kFrame||frame_id_.find('\0')!=std::string::npos||!peer_->Owns(frame_)||frame_->GetIdentifier()!=frame_id_||!context_->IsValid())return false;
    auto actual=frame_->GetV8Context();
    if(!actual||!actual->IsValid()||!actual->IsSame(context_))return false;
    auto owner=context_->GetFrame();auto browser=context_->GetBrowser(),actual_browser=frame_->GetBrowser();
    return owner&&owner->IsValid()&&owner->GetIdentifier()==frame_id_&&browser&&browser->IsValid()&&actual_browser&&browser->IsSame(actual_browser);
  }
  const InitStartupRendererPeer::Ptr&Peer()const{return peer_;}
  const std::string&Frame()const{return frame_id_;}uint64_t Sequence()const{return sequence_;}
 private:
  InitStartupDocument(InitStartupRendererPeer::Ptr peer,CefRefPtr<CefFrame>frame,CefRefPtr<CefV8Context>context,uint64_t sequence):
    peer_(std::move(peer)),frame_(std::move(frame)),context_(std::move(context)),frame_id_(frame_->GetIdentifier()),sequence_(sequence){}
  InitStartupRendererPeer::Ptr peer_;CefRefPtr<CefFrame>frame_;CefRefPtr<CefV8Context>context_;std::string frame_id_;uint64_t sequence_;
};
} // namespace suitecut
