// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "include/cef_api_hash.h"
#include "artifact_controls.h"
#include "context_settings.h"
#include "operation_registry.h"

namespace {
using Controller = SuiteCutArtifactController;
using Dict = Controller::Dict;
struct Pending { int owner; std::string method; Dict params; Controller::Callback callback; };
struct Reply { int owner; bool ok; std::string error, json; };
struct Fixture {
  std::deque<Pending> calls;
  std::vector<Reply> replies;
  std::vector<Pending> fires;
  int content_height = 120;
  std::set<int> active;
  std::unique_ptr<Controller> controller;
  Fixture() {
    controller = std::make_unique<Controller>(
      [this](int owner, const std::string& method, Dict params, Controller::Callback callback) {
        assert(active.contains(owner));
        calls.push_back({owner, method, params ? params->Copy(false) : nullptr, std::move(callback)});
      },
      [this](const std::string& method, Dict params) {
        fires.push_back({0, method, params ? params->Copy(false) : nullptr, {}});
      },
      [this](int owner, bool ok, const std::string& error, const std::string& json) {
        assert(active.erase(owner) == 1);
        replies.push_back({owner, ok, error, json});
        controller->Cancel(owner);
      });
  }
  Dict Config(bool background = false) {
    auto config = CefDictionaryValue::Create();
    config->SetString("action", "screenshot"); config->SetString("format", "png");
    config->SetString("scale", "css"); config->SetString("caret", "hide");
    config->SetString("animations", "allow"); config->SetBool("fullPage", false);
    config->SetBool("omitBackground", background);
    return config;
  }
  void Begin(int owner, bool background = false) {
    assert(active.insert(owner).second); controller->Begin(owner, Config(background), 1, 2);
  }
  void Cancel(int owner) {
    assert(active.erase(owner) == 1);
    controller->Cancel(owner);
    for (auto it = calls.begin(); it != calls.end();) {
      if (it->owner == owner && it->method != "Page.captureScreenshot" && it->method != "Page.printToPDF" && it->method != "Emulation.setEmulatedMedia") it = calls.erase(it);
      else ++it;
    }
  }
  void Ack(const std::string& marker = "current") {
    assert(!calls.empty()); auto call = std::move(calls.front()); calls.pop_front();
    if (call.method == "Page.captureScreenshot" || call.method == "Page.printToPDF") {
      // Completion data is deliberately tagged. The generation test detects an
      // old completion being attributed to a newly admitted request with the same ID.
      call.callback(true, "{\"data\":\"" + marker + "\"}");
    } else if (call.method == "Page.getLayoutMetrics") {
      call.callback(true, "{\"cssVisualViewport\":{\"pageX\":0,\"pageY\":0,\"clientWidth\":185,\"clientHeight\":120},"
          "\"cssContentSize\":{\"x\":0,\"y\":0,\"width\":185,\"height\":" + std::to_string(content_height) + "}}");
    } else if (call.method == "Runtime.evaluate") {
      call.callback(true, "{\"result\":{\"type\":\"object\",\"value\":{\"width\":200,\"height\":120}}}");
    } else {
      call.callback(true, "{}");
    }
  }
  void ToCapture() {
    for (int step = 0; step < 32 && !calls.empty() && calls.front().method != "Page.captureScreenshot"; ++step) Ack();
    assert(!calls.empty() && calls.front().method == "Page.captureScreenshot");
  }
  void Drain(const std::string& marker = "current") {
    for (int step = 0; step < 4096 && !calls.empty(); ++step) Ack(marker);
    assert(calls.empty());
  }
  void Empty() {
    assert(active.empty()); assert(controller->Size() == 0); assert(controller->PhysicalCount() == 0);
  }
};
uint32_t Random(uint32_t& state) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
}  // namespace

int main(int argc, char** argv) {
  assert(cef_api_hash(CEF_API_VERSION, 0));
  for (const bool documents : {false, true}) for (int mode = 0; mode < 4; ++mode) {
    Fixture test; int prepared = 0, cleaned = 0;
    if (documents) test.controller->SetDocumentPreparation(
      [&prepared](int owner, const std::string&, Controller::Callback done) {
        assert(owner == 1); ++prepared; done(true, "");
      }, [&cleaned](int owner, bool teardown) { assert(owner == 1 && !teardown); ++cleaned; });
    auto config = test.Config();
    if (mode == 1) config->SetString("caret", "initial");
    if (mode == 2) config->SetString("style", "body { color: red }");
    if (mode == 3) config->SetString("animations", "disabled");
    test.active.insert(1); test.controller->Begin(1, config, 1, 2);
    int paints = 0, observations = 0;
    while (!test.calls.empty() && test.calls.front().method != "Page.captureScreenshot") {
      const auto& call = test.calls.front();
      if (call.method == "Runtime.evaluate") {
        const auto expression = call.params->GetString("expression").ToString();
        const bool paints_now = expression.find("requestAnimationFrame") != std::string::npos;
        const bool viewport = expression.find("width:innerWidth") != std::string::npos;
        if (expression.find("document.fonts.ready") != std::string::npos) {
          assert(call.params->GetBool("awaitPromise"));
          if (viewport) {
            ++observations;
            // Custom style/animation events run before child discovery. Every
            // capture retains a final post-preparation rendered observation.
            assert(paints_now);
          } else assert(!paints_now);
        }
        if (paints_now) ++paints;
      }
      test.Ack();
    }
    assert(!test.calls.empty() && test.calls.front().method == "Page.captureScreenshot");
    assert(observations == (documents ? 2 : 1));
    assert(paints == (documents ? 2 : 1));
    assert(prepared == (documents ? 1 : 0));
    test.Drain(); test.Empty(); assert(cleaned == (documents ? 1 : 0));
  }
  for(const bool cancel:{false,true}){
    Fixture test;test.content_height=1000;
    suitecut::ContextSettings settings;settings.screen=suitecut::ContextSettings::Size{390,844};
    settings.device_scale=2;settings.is_mobile=true;
    const auto policy=settings.ViewportMetrics({180,100},1);
    auto config=test.Config();config->SetBool("fullPage",true);
    test.controller->SetViewLease([](int owner,int width,int height,Controller::Callback done){
      assert(owner==1&&width==200&&height==1000);done(true,"");
    },[](int owner){assert(owner==1);});
    test.active.insert(1);test.controller->Begin(1,config,1,2,{},policy);
    // A future policy or caller mutation cannot alter this admitted job.
    policy->SetBool("mobile",false);policy->SetInt("screenWidth",1280);policy->SetDouble("deviceScaleFactor",1);
    while(!test.calls.empty()&&test.calls.front().method!="Emulation.setDeviceMetricsOverride")test.Ack();
    assert(!test.calls.empty());const auto capture_metrics=test.calls.front().params;
    assert(capture_metrics->GetBool("mobile")&&capture_metrics->GetInt("screenWidth")==390&&capture_metrics->GetInt("screenHeight")==844);
    assert(capture_metrics->GetDouble("deviceScaleFactor")==2&&capture_metrics->GetInt("width")==180&&capture_metrics->GetInt("height")==100);
    assert(capture_metrics->GetDictionary("viewport")->GetDouble("height")==1000);
    test.ToCapture();
    if(cancel){test.Cancel(1);assert(test.controller->PhysicalCount()==1);}
    test.Drain();test.Empty();
    const auto restored=std::find_if(test.fires.begin(),test.fires.end(),[](const Pending&fire){return fire.method=="Emulation.setDeviceMetricsOverride";});
    assert(restored!=test.fires.end()&&!restored->params->HasKey("viewport"));
    assert(restored->params->GetBool("mobile")&&restored->params->GetInt("screenWidth")==390&&restored->params->GetInt("screenHeight")==844);
    assert(restored->params->GetDouble("deviceScaleFactor")==2&&restored->params->GetInt("width")==180&&restored->params->GetInt("height")==100);
  }
  {
    Fixture test;auto lease=std::make_shared<int>(9);std::weak_ptr<int> retained=lease;
    test.active.insert(1);test.controller->Begin(1,test.Config(),1,1,lease);lease.reset();test.ToCapture();test.Cancel(1);
    assert(!retained.expired()&&test.controller->PhysicalCount()==1&&test.controller->Size()==0);
    test.Ack();assert(retained.expired());test.Empty();
  }
  {
    Fixture test;auto config=CefDictionaryValue::Create();config->SetString("action","pdf");
    for(const char*key:{"paperWidth","paperHeight","marginTop","marginBottom","marginLeft","marginRight","scale"})config->SetDouble(key,std::string(key)=="scale"?1:std::string(key)=="paperWidth"?8.5:std::string(key)=="paperHeight"?11:0);
    for(const char*key:{"displayHeaderFooter","printBackground","landscape","preferCSSPageSize","tagged","outline"})config->SetBool(key,false);
    config->SetString("headerTemplate",std::string(140000,'h'));config->SetString("footerTemplate",std::string(150000,'f'));config->SetString("pageRanges","");
    auto lease=std::make_shared<int>(9);std::weak_ptr<int>retained=lease;test.active.insert(1);test.controller->Begin(1,config,1,1,lease);lease.reset();
    assert(test.calls.size()==1&&test.calls.front().method=="Runtime.evaluate");
    assert(test.calls.front().params->GetString("expression").ToString().find("requestAnimationFrame")!=std::string::npos);test.Ack();
    assert(test.calls.size()==1&&test.calls.front().method=="Page.printToPDF");assert(test.calls.front().params->GetString("headerTemplate").length()==140000&&test.calls.front().params->GetString("footerTemplate").length()==150000);
    test.Cancel(1);assert(!retained.expired()&&test.controller->PhysicalCount()==1);test.Ack();assert(retained.expired());test.Empty();
  }
  {
    Fixture test;test.Begin(1);test.ToCapture();auto lease=std::make_shared<int>(9);std::weak_ptr<int>retained=lease;
    test.active.insert(2);test.controller->Begin(2,test.Config(),1,1,lease);lease.reset();assert(!retained.expired());test.Cancel(2);assert(retained.expired());test.Cancel(1);test.Ack();test.Empty();
  }

  {
    Fixture test;
    auto first=CefDictionaryValue::Create();first->SetString("action","emulateMedia");first->SetString("colorScheme","dark");
    test.active.insert(1);test.controller->Begin(1,first,1,1);
    assert(test.controller->PhysicalCount()==1);test.Cancel(1);
    auto next=CefDictionaryValue::Create();next->SetString("action","emulateMedia");next->SetString("media","print");
    test.active.insert(2);test.controller->Begin(2,next,1,1);
    assert(test.calls.size()==1&&test.calls.front().owner==1);
    test.Ack();
    assert(test.calls.size()==1&&test.calls.front().owner==2);
    assert(test.calls.front().params->GetString("media")=="print");
    auto features=test.calls.front().params->GetList("features");
    assert(features->GetSize()==1&&features->GetDictionary(0)->GetString("value")=="dark");
    test.Ack();test.Empty();
  }
  {
    Fixture test;
    auto first=CefDictionaryValue::Create();first->SetString("action","emulateMedia");first->SetString("colorScheme","dark");
    test.active.insert(1);test.controller->Begin(1,first,1,1);
    auto failed=std::move(test.calls.front());test.calls.pop_front();failed.callback(false,"{\"rootApplied\":true}");
    assert(test.replies.size()==1&&!test.replies[0].ok);
    auto next=CefDictionaryValue::Create();next->SetString("action","emulateMedia");next->SetString("media","print");
    test.active.insert(2);test.controller->Begin(2,next,1,1);
    assert(test.calls.front().params->GetList("features")->GetDictionary(0)->GetString("value")=="dark");
    test.Ack();test.Empty();
  }
  {
    Fixture test; test.Begin(1); test.ToCapture();
    auto clip = test.calls.front().params->GetDictionary("clip");
    assert(clip->GetDouble("width") == 200); assert(clip->GetDouble("height") == 120);
    assert(clip->GetDouble("scale") == 0.5);
    assert(!test.calls.front().params->GetBool("captureBeyondViewport"));
    test.Cancel(1); assert(test.controller->PhysicalCount() == 1);
    // A canceled capture still owns the physical backend, and a later request
    // may reuse its protocol ID without receiving that capture's result.
    test.Begin(1); assert(test.calls.size() == 1);
    test.Ack("old"); assert(test.replies.empty());
    test.Drain("new"); assert(test.replies.size() == 1 && test.replies.front().ok);
    assert(test.replies.front().json.find("new") != std::string::npos); test.Empty();
  }
  {
    Fixture test; test.Begin(1, true); test.ToCapture();
    for (int owner = 2; owner <= 64; ++owner) test.Begin(owner);
    test.Begin(65); assert(test.replies.size() == 1 && !test.replies.front().ok);
    for (int owner = 2; owner <= 64; ++owner) test.Cancel(owner);
    assert(test.controller->Size() == 1); test.Drain(); test.Empty();
  }
  {
    Fixture test; Controller::Callback geometry;
    test.controller->SetGeometry([&geometry](int owner, Dict config, Controller::Callback done) {
      assert(owner == 1 && config->HasKey("element")); geometry = std::move(done);
    });
    auto config = test.Config(); config->SetDictionary("element", CefDictionaryValue::Create());
    test.active.insert(1); test.controller->Begin(1, config, 1, 2);
    while (!geometry && !test.calls.empty()) test.Ack();
    assert(geometry && test.calls.empty());
    test.Cancel(1); test.Begin(1); assert(test.calls.size() == 1);
    // A geometry callback may complete after cancellation and protocol-ID reuse.
    // It must not dispatch layout/capture commands for the newly admitted job.
    geometry(true, ""); assert(test.calls.size() == 1); assert(test.replies.empty());
    test.Drain("fresh"); assert(test.replies.size() == 1 && test.replies.front().ok); test.Empty();
  }
  {
    Fixture test; test.content_height = 1000;
    auto config = test.Config(); config->SetBool("fullPage", true);
    test.active.insert(1); test.controller->Begin(1, config, 1, 2);
    while (!test.calls.empty() && test.calls.front().method != "Emulation.setDeviceMetricsOverride") test.Ack();
    assert(test.controller->ViewportLocked());
    auto override = std::move(test.calls.front()); test.calls.pop_front();
    test.Cancel(1); assert(!test.controller->ViewportLocked());
    assert(std::any_of(test.fires.begin(), test.fires.end(), [](const Pending& fire) {
      return fire.method == "Emulation.setDeviceMetricsOverride" && fire.params->GetInt("width") == 200 &&
          fire.params->GetInt("height") == 120 && !fire.params->HasKey("viewport");
    }));
    test.Begin(1); const auto count = test.calls.size(); override.callback(true,"{}");
    assert(test.calls.size() == count); test.Drain(); test.Empty();
  }
  {
    Fixture test; test.content_height = 1000; Controller::Callback painted; int released = 0;
    test.controller->SetViewLease([&painted](int owner,int width,int height,Controller::Callback done) {
      assert(owner == 1 && width == 200 && height == 1000);painted = std::move(done);
    }, [&released](int owner) { assert(owner == 1);++released; });
    auto config = test.Config(); config->SetBool("fullPage",true);
    test.active.insert(1); test.controller->Begin(1,config,1,2);
    while (!painted && !test.calls.empty()) test.Ack();
    assert(painted && test.calls.empty() && test.controller->ViewportLocked());
    test.Cancel(1);assert(released == 1 && !test.controller->ViewportLocked());
    test.Begin(1);const auto count = test.calls.size();painted(true,"");assert(test.calls.size() == count);
    test.Drain();test.Empty();
  }
  {
    Fixture test; Controller::Callback resumed;
    test.controller->SetBeforeScreenshot([&resumed](int owner,Dict,Controller::Callback done) {
      assert(owner == 1);resumed = std::move(done);
    });
    auto config = test.Config();config->SetBool("beforeScreenshot",true);
    test.active.insert(1);test.controller->Begin(1,config,1,2);
    assert(test.calls.front().params->GetString("expression").ToString().find("requestAnimationFrame") != std::string::npos);
    while (!resumed && !test.calls.empty()) test.Ack();
    assert(resumed && test.calls.empty() && test.controller->HookPending(1));test.Cancel(1);test.Begin(1);
    const auto count = test.calls.size();resumed(true,"");assert(test.calls.size() == count);
    test.Drain();test.Empty();
  }
  {
    Fixture test; test.content_height = 1000;
    test.controller->SetViewLease([](int owner,int width,int height,Controller::Callback done) {
      assert(owner == 1 && width == 200 && height == 1000); done(true,"");
    }, [](int owner) { assert(owner == 1); });
    auto config = test.Config(); config->SetBool("fullPage",true);
    test.active.insert(1); test.controller->Begin(1,config,1,2); test.ToCapture();
    assert(test.calls.front().params->GetBool("captureBeyondViewport")); test.Drain(); test.Empty();
  }
  {
    Fixture test; test.Begin(1);
    auto stale = std::move(test.calls.front()); test.calls.pop_front();
    test.Cancel(1); test.Begin(1); const auto count = test.calls.size();
    stale.callback(true,"{\"result\":{\"value\":true}}");
    assert(test.calls.size() == count && test.replies.empty()); test.Drain(); test.Empty();
  }
  {
    Fixture test; SuiteCutOperationRegistry registry;
    const auto admitted = registry.Begin(1,100,50); assert(admitted);
    auto canonical = test.Config(); canonical->SetBool("beforeScreenshot",true);
    int hooks = 0, retries = 0;
    test.controller->SetBeforeScreenshot([&hooks,canonical](int owner,Dict config,Controller::Callback done) {
      assert(owner == 1); ++hooks;
      // Source stores completion in its canonical configuration before acquiring
      // the resumed document, rather than trusting a canceled job's private copy.
      canonical->SetBool("beforeScreenshot",false); config->SetInt("__artifactContextId",2); done(true,"");
    });
    test.controller->SetRetry([&test,&registry,&retries,canonical,admitted](int owner) {
      assert(owner == 1 && test.controller->PhysicalCount() == 0);
      assert(registry.IsCurrent(owner,admitted->generation));
      assert(registry.Get(owner)->deadline_ms == admitted->deadline_ms);
      ++retries; test.controller->Cancel(owner); test.controller->Begin(owner,canonical,3,2);
    });
    test.active.insert(1); test.controller->Begin(1,canonical,1,2); test.ToCapture();
    for (int attempt = 0; attempt < 4; ++attempt) {
      auto physical = std::move(test.calls.front()); test.calls.pop_front();
      physical.callback(false,"{\"message\":\"Execution context was destroyed.\"}");
      assert(test.replies.empty()); test.ToCapture(); assert(hooks == 1);
    }
    assert(retries == 4 && registry.IsExpired(1,admitted->generation,150));
    // Expiration/cancellation while the last physical readback is pending keeps
    // the latch until that callback arrives; it cannot cause another restart.
    test.Cancel(1); assert(registry.Finish(1)); test.Ack("expired");
    assert(retries == 4 && test.replies.empty()); test.Empty();
  }
  {
    Fixture test; int retries = 0;
    test.controller->SetRetry([&retries](int) { ++retries; });
    test.Begin(1); test.ToCapture(); auto physical = std::move(test.calls.front()); test.calls.pop_front();
    physical.callback(false,"{\"message\":\"Unsupported image format\"}");
    assert(retries == 0 && test.replies.size() == 1 && !test.replies.front().ok); test.Empty();
  }
  {
    Fixture test; Controller::Callback prepared; int cleanups = 0;
    test.controller->SetDocumentPreparation([&prepared](int owner,const std::string& expression,Controller::Callback done) {
      assert(owner == 1 && expression.find("caret-color:transparent") != std::string::npos); prepared = std::move(done);
    }, [&cleanups](int owner,bool teardown) { assert(owner == 1 && !teardown); ++cleanups; });
    test.Begin(1); while (!prepared && !test.calls.empty()) test.Ack();
    assert(prepared && test.calls.empty());
    test.Cancel(1); assert(cleanups == 1); test.controller->SetDocumentPreparation({},{}); test.Begin(1);
    const auto count = test.calls.size(); prepared(true,""); assert(test.calls.size() == count);
    test.Drain(); test.Empty();
  }
  {
    Fixture test; int prepares = 0,cleanups = 0;
    test.controller->SetDocumentPreparation([&prepares](int owner,const std::string&,Controller::Callback done) {
      assert(owner == 1); ++prepares; done(true,"");
    }, [&cleanups](int owner,bool teardown) { assert(owner == 1 && !teardown); ++cleanups; });
    test.Begin(1); test.ToCapture(); assert(prepares == 1);
    test.Cancel(1); assert(cleanups == 1 && test.controller->PhysicalCount() == 1);
    test.Ack("late"); assert(test.replies.empty()); test.Empty();
  }
  {
    Fixture test; Controller::Callback prepared; int teardowns = 0;
    test.controller->SetDocumentPreparation([&prepared](int owner,const std::string&,Controller::Callback done) {
      assert(owner == 1); prepared = std::move(done);
    }, [&teardowns](int owner,bool teardown) { assert(owner == 1 && teardown); ++teardowns; });
    test.Begin(1); while (!prepared && !test.calls.empty()) test.Ack(); assert(prepared);
    test.controller->Clear(); test.active.clear(); assert(teardowns == 1);
    prepared(true,""); assert(test.calls.empty() && test.replies.empty()); test.Empty();
  }
  {
    Fixture test;assert(test.controller->AcquireExternalCapture(900));assert(test.controller->PhysicalCount()==1);assert(!test.controller->AcquireExternalCapture(901));
    test.Begin(1);assert(test.calls.empty());test.Cancel(1);assert(test.calls.empty()&&test.controller->PhysicalCount()==1);
    test.Begin(2);assert(test.calls.empty());assert(!test.controller->ReleaseExternalCapture(901));assert(test.calls.empty());assert(test.controller->ReleaseExternalCapture(900));assert(!test.calls.empty());
    assert(!test.controller->AcquireExternalCapture(902));test.Drain();assert(test.replies.size()==1&&test.replies[0].owner==2);test.Empty();
    assert(test.controller->AcquireExternalCapture(903));test.controller->Clear();assert(!test.controller->ReleaseExternalCapture(903)&&test.controller->PhysicalCount()==0);
  }
  uint64_t iterations = 100000;
  if (argc == 2) iterations = std::strtoull(argv[1], nullptr, 10);
  uint32_t random = 0x14cef156;
  for (uint64_t iteration = 0; iteration < iterations; ++iteration) {
    Fixture test; const bool background = Random(random) & 1; test.Begin(1, background);
    const int progress = Random(random) % 8;
    for (int step = 0; step < progress && !test.calls.empty() && test.calls.front().method != "Page.captureScreenshot"; ++step) test.Ack();
    if ((Random(random) & 3) == 0) {
      test.ToCapture(); test.Cancel(1); test.Begin(1, !background);
      test.Ack("stale"); assert(test.replies.empty());
      test.Drain("fresh"); assert(test.replies.size() == 1 && test.replies.front().json.find("fresh") != std::string::npos);
    } else if (Random(random) & 1) {
      test.Cancel(1);
      test.Drain(); assert(test.replies.empty());
    } else {
      test.Drain(); assert(test.replies.size() == 1 && test.replies.front().ok);
    }
    test.Empty();
  }
  std::printf("Artifact controller: %llu randomized cancellation/generation cycles passed\n", static_cast<unsigned long long>(iterations));
}
