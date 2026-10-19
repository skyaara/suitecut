// SPDX-License-Identifier: MIT
#include "screenshot_assertion_test_host.h"
namespace {
const std::string png="{\"data\":\"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jk1sAAAAASUVORK5CYII=\"}";
void Baseline(Harness&h,Harness::Identity owner){
  assert(h.ScreenshotAssertionPrepared(owner));
  assert(h.ScreenshotAssertionPixelsBegin(owner,0,100,false));
  assert(!h.ScreenshotAssertionPixelsObserved(owner,100));
  assert(h.ScreenshotAssertionPixelsSubmitted(owner,100));
  h.ScreenshotAssertionPixelsSettled(owner,100,true,true,"");
  assert(h.screenshot_assertion_scopes_.at(owner.owner).baseline_valid);
}
void Capture(Harness&h,Harness::Identity owner,uint64_t sample=11,uint64_t generation=9){
 auto params=CefDictionaryValue::Create();
 h.ScreenshotAssertionSubmit(owner,"main","","Page.captureScreenshot",params,Harness::Stage::PNG,false,
   [&h](bool,bool,const auto&){++h.consumers;},sample,generation);
}
}
int main(){cef_api_hash(CEF_API_VERSION,0);unsigned groups=0;using Policy=Harness::Policy;using Outcome=Policy::Outcome;
 {Harness h;auto owner=h.Start();assert(!h.ScreenshotAssertionCaptureEnter(owner,11,9,1,"main",""));assert(!h.ScreenshotAssertionPixelsBegin(owner,0,100,false));assert(!h.screenshot_assertion_retirement_.Physical());++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);Capture(h,owner);assert(h.ScreenshotAssertionCapturePending(owner,11));assert(!h.ScreenshotAssertionCaptureDeliverable(owner,11,9));h.now=50;h.Ack(1,true,png);assert(h.deadlines==1&&h.ScreenshotAssertionCaptureDeliverable(owner,11,9));assert(!h.ScreenshotAssertionCaptureDeliverable(owner,11,10));h.ScreenshotAssertionCaptureDelivered(owner,11);assert(!h.ScreenshotAssertionCapturePending(owner,11));assert(h.ScreenshotAssertionPixelsBegin(owner,11,101,true));assert(!h.ScreenshotAssertionPixelsObserved(owner,101));assert(h.ScreenshotAssertionPixelsSubmitted(owner,101));h.screenshot_assertion_retirement_.CleanupClosed(owner);assert(h.screenshot_assertion_retirement_.Ready(owner,50)==Outcome::Pending);h.ScreenshotAssertionPixelsSettled(owner,101,true,true,"");assert(h.screenshot_assertion_retirement_.Ready(owner,50)==Outcome::Deadline&&!h.screenshot_assertion_retirement_.Get(owner)->observed);++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);Capture(h,owner);assert(!h.ScreenshotAssertionCaptureReceipt(owner,11,2,"",true,true));assert(!h.ScreenshotAssertionCaptureReceipt(owner,11,1,"childSession",true,true));auto wrong=owner;++wrong.generation;assert(!h.ScreenshotAssertionCaptureReceipt(wrong,11,1,"",true,true));assert(h.screenshot_assertion_retirement_.Physical()==1);h.Ack(1,true,png);assert(!h.ScreenshotAssertionCaptureReceipt(owner,11,1,"",true,true));++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);h.submit_ok=false;Capture(h,owner);assert(h.screenshot_assertion_captures_.empty()&&!h.screenshot_assertion_retirement_.Physical());assert(h.screenshot_assertion_retirement_.Get(owner)->requested==Outcome::HardFailure);++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);h.synchronous=true;h.submit_ok=false;h.synchronous_json=png;Capture(h,owner);assert(h.consumers==1&&h.ScreenshotAssertionCaptureDeliverable(owner,11,9));assert(h.screenshot_assertion_captures_.at({7,11}).received&&!h.screenshot_assertion_retirement_.Physical());++groups;}
 {Harness h;auto owner=h.Start();assert(h.ScreenshotAssertionPrepared(owner));assert(h.ScreenshotAssertionPixelsBegin(owner,0,100,false));assert(h.ScreenshotAssertionPixelsSubmitted(owner,100));h.ScreenshotAssertionPixelsSettled(owner,100,true,true,"Actual decoder failed after constructing a result");assert(!h.screenshot_assertion_scopes_.at(7).baseline_valid&&!h.screenshot_assertion_retirement_.Get(owner)->validated&&h.screenshot_assertion_retirement_.Get(owner)->requested==Outcome::HardFailure);++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);h.now=50;Capture(h,owner);assert(!h.submissions&&h.screenshot_assertion_captures_.empty()&&h.screenshot_assertion_retirement_.Get(owner)->requested==Outcome::HardFailure);++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);Capture(h,owner);++h.documents["main"].context_generation;h.now=50;h.Ack(1,true,png);assert(!h.ScreenshotAssertionPixelsBegin(owner,11,101,true)&&h.screenshot_assertion_retirement_.Get(owner)->requested==Outcome::HardFailure);++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);Capture(h,owner);h.Ack(1,true,png);assert(h.ScreenshotAssertionPixelsBegin(owner,11,101,false));assert(!h.ScreenshotAssertionPixelsBegin(owner,11,102,false));assert(!h.ScreenshotAssertionPixelsObserved(owner,101));assert(h.ScreenshotAssertionPixelsSubmitted(owner,101));h.screenshot_assertion_retirement_.Cancel(owner,"actual owner cancellation",1);assert(!h.ScreenshotAssertionPixelsBegin(owner,11,102,true)&&h.screenshot_assertion_retirement_.Physical()==1);h.ScreenshotAssertionPixelsSettled(owner,101,true,true,"");h.screenshot_assertion_retirement_.CleanupClosed(owner);assert(h.screenshot_assertion_retirement_.Ready(owner,1)==Outcome::Canceled);++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);Capture(h,owner);h.Ack(1,true,png);assert(h.ScreenshotAssertionPixelsBegin(owner,11,101,false)&&h.ScreenshotAssertionPixelsSubmitted(owner,101)&&h.ScreenshotAssertionPixelsObserved(owner,101));h.ScreenshotAssertionPixelsSettled(owner,101,true,true,"");assert(h.screenshot_assertion_captures_.empty()&&h.screenshot_assertion_retirement_.Get(owner)->observed);++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);Capture(h,owner);h.now=50;h.Ack(1,true,"{\"data\":17}");assert(!h.ScreenshotAssertionPixelsBegin(owner,11,101,true)&&h.screenshot_assertion_retirement_.Get(owner)->requested==Outcome::HardFailure);++groups;}
 {Harness h;auto owner=h.Start();Baseline(h,owner);Capture(h,owner);h.now=50;h.Ack(1,true,png);assert(h.ScreenshotAssertionPixelsBegin(owner,11,101,true)&&h.ScreenshotAssertionPixelsSubmitted(owner,101));h.screenshot_assertion_retirement_.ChannelClosed(3,h.now);h.ScreenshotAssertionCaptureChannelClosed();assert(h.screenshot_assertion_captures_.empty()&&h.screenshot_assertion_retirement_.Physical()==1);h.ScreenshotAssertionPixelsSettled(owner,101,true,true,"");assert(!h.screenshot_assertion_retirement_.Physical()&&h.screenshot_assertion_retirement_.Ready(owner,50)==Outcome::HardFailure);++groups;}
 std::cout<<"PASS "<<groups<<" Source-included exact capture/sample/codec receipt controls; real CEF schemas and controlled physical jobs, no browser capture claim\n";
}
