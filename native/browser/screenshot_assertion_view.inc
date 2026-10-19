// SPDX-License-Identifier: MIT
// Included inside Source and its controlled paint host. Void WasResized and
// Invalidate calls submit work; only real PET_VIEW or channel death settles it.
struct ScreenshotAssertionViewRestore {
  suitecut::ScreenshotAssertionRetirement::Identity identity;
  suitecut::ScreenshotAssertionRetirement::Ticket physical;
  SuiteCutArtifactController::Callback done;
  int width=0,height=0;bool submitted=false;
};
std::map<int,ScreenshotAssertionViewRestore>screenshot_assertion_views_;
void ScreenshotAssertionRestoreView(suitecut::ScreenshotAssertionRetirement::Identity identity,SuiteCutArtifactController::Callback done){
  using Policy=suitecut::ScreenshotAssertionRetirement;
  if(!browser_||artifact_view_owner_!=identity.owner||!ScreenshotAssertionPinsCurrent(identity)||screenshot_assertion_views_.contains(identity.owner)||
     !std::isfinite(scale_)||scale_<=0||width_<=0||height_<=0||width_*scale_>32768||height_*scale_>32768){done(false,"Native screenshot physical view ownership changed");return;}
  auto ticket=screenshot_assertion_retirement_.Begin(identity,Policy::Work::View,0,"",Now());
  if(!ticket){done(false,"Native screenshot physical view restoration capacity unavailable");return;}
  try{
    const auto [position,inserted]=screenshot_assertion_views_.emplace(identity.owner,ScreenshotAssertionViewRestore{identity,*ticket,std::move(done),int(std::round(width_*scale_)),int(std::round(height_*scale_)),false});
    if(!inserted){screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);return;}
    if(!screenshot_assertion_retirement_.EnterSubmission(*ticket)){screenshot_assertion_views_.erase(position);screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);return;}
    position->second.submitted=true;
    // The observer is installed first, including the synchronous callback case.
    ArtifactReleaseView(identity.owner);
  }catch(const std::bad_alloc&){
    screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);
    screenshot_assertion_retirement_.Fail(identity,"Native screenshot view restoration allocation failed",Now());
    NetworkFatal("Native screenshot view restoration allocation failed");
  }
}
void ScreenshotAssertionViewPaint(PaintElementType type,int width,int height){
  if(type!=PET_VIEW)return;
  for(auto it=screenshot_assertion_views_.begin();it!=screenshot_assertion_views_.end();){
    if(!it->second.submitted||std::abs(width-it->second.width)>1||std::abs(height-it->second.height)>1){++it;continue;}
    auto work=std::move(it->second);it=screenshot_assertion_views_.erase(it);
    const bool ok=ScreenshotAssertionPinsCurrent(work.identity);
    const std::string error=ok?"":"Native screenshot document changed at physical view restoration paint";
    screenshot_assertion_retirement_.Acknowledge(work.physical,ok,error,Now());
    if(work.done){work.done(ok,error);}
    ScreenshotAssertionReceiptFinished(work.identity);
  }
}
void ScreenshotAssertionViewChannelClosed(){
  // Policy ChannelClosed already retired actual transport/view obligations.
  auto retired=std::move(screenshot_assertion_views_);screenshot_assertion_views_.clear();
  for(auto&[owner,work]:retired){(void)owner;if(work.done)work.done(false,"Actual native screenshot channel closed");}
}
