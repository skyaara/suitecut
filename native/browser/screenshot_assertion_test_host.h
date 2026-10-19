// SPDX-License-Identifier: MIT
#pragma once
#include "include/cef_api_hash.h"
#include "operation_registry.h"
#include "selector_wait_controls.h"
#include "snapshot_reference_controls.h"
#include "screenshot_assertion_retirement.h"
#include "screenshot_assertion_response.h"
#include <cassert>
#include <iostream>
#include <set>

struct Harness {
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Policy=suitecut::ScreenshotAssertionRetirement;
  using Identity=Policy::Identity;
  using Stage=SuiteCutArtifactController::ResponseStage;
  using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;
  using Callback=std::function<void(bool,const std::string&)>;
  struct DevToolsCall {int owner;Callback callback;std::string session;bool retain_after_cancel=false;size_t response_limit=0;bool retained_control=false,lossless_scalars=false;};
  static constexpr size_t kMaxResponseBytes=4*1024*1024;
  int page_id_=3,next=1,submissions=0,consumers=0,deadlines=0,receipts=0;
  uint64_t renderer_views_=4;double now=0;double scale_=1;int width_=200,height_=120,artifact_view_owner_=0,view_releases=0;bool synchronous_paint=false;bool browser_=true,submit_ok=true,synchronous=false,expire_at_selector=false;
  std::string main_frame_id_="main",synchronous_json="{\"result\":{\"type\":\"boolean\",\"value\":true}}";
  std::map<std::string,SnapshotReferenceDocument>documents;
  std::set<std::string>sessions_{"childSession"};
  std::map<int,DevToolsCall>devtools_;
  struct ArtifactDocumentResource {std::string frame,session,loader;int context=0;};
  struct ArtifactDocumentWork {uint64_t generation=0;std::vector<ArtifactDocumentResource>resources;};
  std::map<int,std::shared_ptr<ArtifactDocumentWork>>artifact_document_work_;
  enum PaintElementType {PET_VIEW,PET_POPUP};
  std::vector<std::string>fatal_errors;
  void NetworkFatal(const std::string&error){fatal_errors.push_back(error);}
  void ArtifactReleaseView(int owner){assert(artifact_view_owner_==owner);artifact_view_owner_=0;++view_releases;if(synchronous_paint)ScreenshotAssertionViewPaint(PET_VIEW,200,120);}

  SuiteCutOperationRegistry operations_;
  std::vector<std::function<void()>>group_acks;
  suitecut::SelectorWaitControls selectors{[this]{return now;},[](const auto&){assert(false);},[this](int,const auto&,const auto&,auto done){group_acks.push_back(std::move(done));}};
  Harness(){documents["main"]={"main","","loader-main","mainUnique","utilityUnique",4,5,1};documents["child"]={"child","childSession","loader-child","childMain","childUtility",6,7,2};}
  double Now()const{return now;}
  void WakeDeadlines(){}
  int AllocateDevToolsId(){return next?next++:0;}
  std::optional<SnapshotReferenceDocument> ReferenceDocument(const std::string&frame)const{const auto found=documents.find(frame);return found==documents.end()?std::nullopt:std::optional(found->second);}
  suitecut::SelectorWaitControls&SelectorWaits(){return selectors;}
  bool ReferenceTemporaryGroups(int){if(expire_at_selector)now=50;return true;}
  bool SubmitDevTools(int id,const std::string&,Dict,const std::string&){
    ++submissions;const auto&call=devtools_.at(id);assert(call.retain_after_cancel&&call.retained_control&&call.lossless_scalars&&call.response_limit==kMaxResponseBytes);
    if(synchronous){Ack(id,true,synchronous_json);}return submit_ok;
  }
  void Ack(int id=1,bool ok=true,const std::string&json="{\"result\":{\"type\":\"boolean\",\"value\":true}}"){
    auto found=devtools_.find(id);assert(found!=devtools_.end());auto callback=std::move(found->second.callback);devtools_.erase(found);callback(ok,json);
  }
  Identity Start(){const auto operation=operations_.Begin(7,now,50);assert(operation);assert(ScreenshotAssertionScopeBegin(7,operation->generation,50));return {page_id_,7,operation->generation};}
  void ScreenshotAssertionOriginalDeadline(Identity identity){++deadlines;screenshot_assertion_retirement_.Freeze(identity,false,now);}
  void ScreenshotAssertionReceiptFinished(Identity){++receipts;}
#include "screenshot_assertion_scope.inc"
#include "screenshot_assertion_capture.inc"
#include "screenshot_assertion_sender.inc"
#include "screenshot_assertion_view.inc"
#include "screenshot_assertion_cleanup.inc"
  void Send(Identity identity,Stage stage=Stage::Boolean,bool cleanup=false,std::string frame="main",std::string session="",bool group=false){
    auto params=CefDictionaryValue::Create();if(group)params->SetString("objectGroup","actual-native-query-group");
    ScreenshotAssertionSubmit(identity,frame,session,stage==Stage::PNG?"Page.captureScreenshot":"Runtime.evaluate",params,stage,cleanup,[this](bool,bool,const auto&){++consumers;});
  }
};