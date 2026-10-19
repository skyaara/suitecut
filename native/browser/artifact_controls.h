// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <limits>
#include <string>
#include <vector>

#include "include/cef_parser.h"
#include "include/cef_values.h"

// A UI-thread controller. The source retains operation IDs, deadlines, CDP
// ownership and response credits. Serialization prevents capture overrides from
// affecting another capture; cancellation restores them before the next job.
class SuiteCutArtifactController {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Callback = std::function<void(bool, const std::string&)>;
  using Send = std::function<void(int, const std::string&, Dict, Callback)>;
  using Fire = std::function<void(const std::string&, Dict)>;
  using ViewLease = std::function<void(int, int, int, Callback)>;
  using ViewRelease = std::function<void(int)>;
  using BeforeScreenshot = std::function<void(int, Dict, Callback)>;
  using Geometry = std::function<void(int, Dict, Callback)>;
  using Retry = std::function<void(int)>;
  using DocumentPreparation = std::function<void(int, const std::string&, Callback)>;
  using DocumentCleanup = std::function<void(int, bool)>;
  using Finish = std::function<void(int, bool, const std::string&, const std::string&)>;
  // Assertions can retain every real native stage/ACK across logical expiry.
  // Ordinary captures keep their existing sender when this seam is unset.
  enum class ResponseStage { Boolean, Viewport, Layout, PNG, PDF, Empty, Unknown };
  using AssertionSend = std::function<void(int,const std::string&,Dict,ResponseStage,Callback)>;
  struct Restoration {std::string method;Dict params;ResponseStage stage=ResponseStage::Empty;};
  struct AssertionCleanupPlan {std::vector<Restoration>root;bool documents=false,physical_view=false;};
  // A true return transfers actual dirty-resource cleanup to Source. Its done
  // callback is a physical restoration barrier, not a logical cancellation ACK.
  // false means the owner is an ordinary capture and the legacy path applies.
  using AssertionCleanup=std::function<bool(int,AssertionCleanupPlan,Callback)>;
  using AssertionCaptured=std::function<bool(int,uint64_t,bool,const std::string&)>;
  void SetAssertionCaptured(AssertionCaptured captured){assertion_captured_=std::move(captured);}
  uint64_t Generation(int owner)const{const auto found=jobs_.find(owner);return found==jobs_.end()?0:found->second.token;}
  void SetAssertionCleanup(AssertionCleanup cleanup){assertion_cleanup_=std::move(cleanup);}
  bool RestorationPhysicalOwned(int owner)const{return owner>0&&restoration_owner_==owner;}
  void SetAssertionSend(AssertionSend send,std::function<bool(int)> owns={}){assertion_send_=std::move(send);assertion_owns_=std::move(owns);}
  bool CapturePhysicalOwned(int owner)const{return owner>0&&physical_owner_==owner;}

  SuiteCutArtifactController(Send send, Fire fire, Finish finish)
      : send_(std::move(send)), fire_(std::move(fire)), finish_(std::move(finish)) {}

  // Only called with the Source's acknowledged native media configuration,
  // once when the artifact controller is created, before public jobs begin.
  bool SeedNativeMedia(Dict params){
    if(!params||!jobs_.empty()||active_||physical_owner_)return false;
    std::map<std::string,std::string>seed;
    const auto media=params->GetString("media").ToString();if(!media.empty())seed["media"]=media;
    const auto features=params->GetList("features");if(!features||features->GetSize()>4)return false;
    for(size_t i=0;i<features->GetSize();++i){const auto entry=features->GetDictionary(i);if(!entry||entry->GetType("name")!=VTYPE_STRING||entry->GetType("value")!=VTYPE_STRING)return false;const auto name=entry->GetString("name").ToString(),value=entry->GetString("value").ToString();std::string key;
      if(name=="prefers-color-scheme")key="colorScheme";else if(name=="prefers-reduced-motion")key="reducedMotion";else if(name=="forced-colors")key="forcedColors";else if(name=="prefers-contrast")key="contrast";else return false;
      if(!value.empty())seed[key]=value;
    }
    media_=std::move(seed);return true;
  }
  void SetGeometry(Geometry geometry) { geometry_ = std::move(geometry); }

  void SetViewLease(ViewLease lease, ViewRelease release) { view_lease_ = std::move(lease); release_lease_ = std::move(release); }

  void SetBeforeScreenshot(BeforeScreenshot before) { before_screenshot_ = std::move(before); }

  void SetRetry(Retry retry) { retry_ = std::move(retry); }
  void SetDocumentPreparation(DocumentPreparation prepare, DocumentCleanup cleanup) {
    prepare_documents_ = std::move(prepare); cleanup_documents_ = std::move(cleanup);
  }
  static std::string RestoreExpression(int owner) {
    return "(()=>{const m=globalThis.__suitecutArtifactStyles;"
        "const resources=m?.get(" + std::to_string(owner) + ");if(resources){"
        "for(const e of resources.styles)e.remove();for(const e of resources.masks??[])e.remove();"
        "for(const s of resources.carets??[]){if(s.value)s.element.style.setProperty('caret-color',s.value,s.priority);"
        "else s.element.style.removeProperty('caret-color');"
        "if(!s.hadStyle&&s.element.getAttribute('style')==='')s.element.removeAttribute('style');}"
        "for(const s of resources.animations){try{s.animation.play();"
        "s.animation.currentTime=s.currentTime;if(s.state==='paused')s.animation.pause();}catch{}}"
        "m.delete(" + std::to_string(owner) + ");}return true})()";
  }
  bool HookPending(int owner) const {
    const auto job = jobs_.find(owner); return job != jobs_.end() && job->second.waiting_hook;
  }

  void Begin(int owner, Dict config, int context, double device_scale, std::shared_ptr<void> input = {}, Dict viewport_metrics = nullptr) {
    const auto action = config->GetString("action").ToString();
    if ((action != "screenshot" && action != "pdf" && action != "emulateMedia") || context <= 0 ||
        !std::isfinite(device_scale) || device_scale <= 0) {
      finish_(owner, false, "Invalid native artifact operation", ""); return;
    }
    if (jobs_.size() >= 64) {
      finish_(owner, false, "Native artifact queue capacity exceeded", ""); return;
    }
    if (next_token_ == std::numeric_limits<uint64_t>::max()) {
      finish_(owner, false, "Native artifact generation capacity exhausted", ""); return;
    }
    auto job = Job{assertion_owns_&&assertion_owns_(owner)?config:config->Copy(false), context, device_scale}; job.token = ++next_token_; job.input = std::move(input);
    // Source supplies its native policy; keep a private admission-time snapshot
    // so later configuration cannot change this job's physical restoration.
    if(viewport_metrics)job.viewport_metrics=viewport_metrics->Copy(false);
    jobs_.emplace(owner, std::move(job));
    queue_.push_back(owner);
    Pump();
  }

  // Called by Source::Complete for success, error, cancellation and deadline.
  void Cancel(int owner) {
    const auto found = jobs_.find(owner);
    if (found == jobs_.end()) return;
    // Remove the logical record before any cleanup callback may synchronously
    // reenter Source::Complete/Cancel. Actual capture/restoration latches below
    // are independent and still hold the native pipeline.
    auto job=std::move(found->second);jobs_.erase(found);
    queue_.erase(std::remove(queue_.begin(),queue_.end(),owner),queue_.end());
    if (active_ == owner) {
      bool retained=false;
      if(assertion_cleanup_&&(!assertion_owns_||assertion_owns_(owner))){
        restoration_owner_=owner;restoration_token_=job.token;
        const auto token=restoration_token_;
        try{
          auto plan=RestorationPlan(owner,job);
          retained=assertion_cleanup_(owner,std::move(plan),[this,owner,token](bool ok,const std::string&){
            if(restoration_owner_!=owner||restoration_token_!=token)return;
            // A failed restore cannot hand modified resources to another
            // capture. Source retains the failure and posts actual teardown.
            if(!ok)return;
            restoration_owner_=0;restoration_token_=0;Pump();
          });
        }catch(...){
          retained=true;
          finish_(owner,false,"Native screenshot cleanup dispatch failed","");
        }
        if(!retained&&restoration_owner_==owner&&restoration_token_==token){restoration_owner_=0;restoration_token_=0;}
      }
      if(!retained){
        if(job.documents&&cleanup_documents_)cleanup_documents_(owner,false);
        const auto restores=RestorationPlan(owner,job);size_t next=0;
        if(job.viewport_override){const auto&restore=restores.root[next++];fire_(restore.method,restore.params);}
        if(job.physical_view_lease&&release_lease_)release_lease_(owner);
        for(;next<restores.root.size();++next){const auto&restore=restores.root[next];fire_(restore.method,restore.params);}
      }
      active_=0;
    }
    Pump();
  }
  void Clear() {
    if (cleanup_documents_) for (const auto& [owner, job] : jobs_) if (job.documents) cleanup_documents_(owner, true);
    const bool retain_capture=physical_owner_&&assertion_owns_&&assertion_owns_(physical_owner_);
    jobs_.clear();queue_.clear();active_=0;external_owner_=0;
    if(!retain_capture){physical_input_.reset();physical_owner_=0;physical_token_=0;}
    // Logical reset cannot acknowledge assertion capture or restoration work.
    // Source's independent scope also retains each raw native receipt.
  }
  // Only actual Source OnBeforeClose may supply this transport boundary.
  void AssertionChannelClosed(){physical_input_.reset();physical_owner_=0;physical_token_=0;restoration_owner_=0;restoration_token_=0;}
  // Native trace readbacks share the actual capture pipeline with public
  // screenshot/PDF operations. Logical trace cancellation never releases this
  // reservation: Source calls ReleaseExternalCapture on the real reply/close.
  bool ExternalCaptureAvailable() const { return !active_&&!physical_owner_&&!external_owner_&&!restoration_owner_&&queue_.empty(); }
  bool AcquireExternalCapture(int owner) {if(owner<=0||!ExternalCaptureAvailable())return false;external_owner_=owner;return true;}
  bool ReleaseExternalCapture(int owner) {if(!owner||external_owner_!=owner)return false;external_owner_=0;Pump();return true;}
  size_t Size() const { return jobs_.size(); }
  bool ViewportLocked() const {
    const auto active = jobs_.find(active_); return active != jobs_.end() && active->second.viewport_override;
  }
  std::string TimeoutMessage(int owner) const {
    const auto job = jobs_.find(owner); if (job == jobs_.end()) return "";
    const std::string action = job->second.config->GetString("action").ToString();
    return "Native " + action + ": Timeout " + std::to_string(job->second.config->GetInt("timeoutMs")) +
        "ms exceeded.\nCall log:\n - " + (job->second.waiting_fonts ? "waiting for fonts to load..." :
        (job->second.waiting_hook ? "waiting for beforeScreenshot callback" : (active_ == owner ? "waiting for native artifact rendering" : "waiting for the native artifact pipeline")));
  }
  int PhysicalCount() const { return (physical_owner_ || external_owner_ ? 1 : 0)+(restoration_owner_?1:0); }
  bool PhysicalPending(int owner) const {
    const auto job = jobs_.find(owner);
    return physical_owner_ == owner && job != jobs_.end() && job->second.token == physical_token_;
  }
  // Source invokes this if dispatch fails before a retained callback exists.
  void PhysicalDispatchFailed(int owner) {
    if (PhysicalPending(owner)) { auto retired = std::move(physical_input_); physical_owner_ = 0; physical_token_ = 0; Pump(); }
  }

 private:
  struct Job { Dict config; int context; double scale; bool background = false; bool style = false; bool waiting_fonts = false; bool waiting_hook = false; bool viewport_override = false; bool physical_view_lease = false; bool documents = false; bool documents_ready = false; std::string preparation_expression={};std::shared_ptr<void> input = {};
    double viewport_width = 0, viewport_height = 0; uint64_t token = 0; Dict viewport_metrics; };
  static Dict ViewportMetrics(const Job&job){
    // Mobile layout innerWidth can differ from the actual device viewport.
    // Restore the admitted native dimensions, not that renderer observation.
    if(job.viewport_metrics)return job.viewport_metrics->Copy(false);
    auto params=CefDictionaryValue::Create();
    params->SetInt("width",static_cast<int>(std::round(job.viewport_width)));
    params->SetInt("height",static_cast<int>(std::round(job.viewport_height)));
    params->SetDouble("deviceScaleFactor",job.scale);params->SetBool("mobile",false);
    return params;
  }
  static AssertionCleanupPlan RestorationPlan(int owner,const Job&job){
    AssertionCleanupPlan plan;plan.documents=job.documents;plan.physical_view=job.physical_view_lease;
    plan.root.reserve(3);
    if(job.viewport_override){
      plan.root.push_back({"Emulation.setDeviceMetricsOverride",ViewportMetrics(job),ResponseStage::Empty});
    }
    if(job.background)plan.root.push_back({"Emulation.setDefaultBackgroundColorOverride",CefDictionaryValue::Create(),ResponseStage::Empty});
    if(job.style){
      auto params=CefDictionaryValue::Create();params->SetInt("contextId",job.context);params->SetString("expression",RestoreExpression(owner));params->SetBool("returnByValue",true);
      plan.root.push_back({"Runtime.evaluate",params,ResponseStage::Boolean});
    }
    return plan;
  }
  static std::string JSON(Dict dictionary) {
    auto value = CefValue::Create(); value->SetDictionary(dictionary);
    return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  }
  static std::string Quote(const std::string& string) {
    auto value = CefValue::Create(); value->SetString(string);
    return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  }
  static Dict Parse(const std::string& string) {
    auto value = CefParseJSON(string, JSON_PARSER_RFC);
    return value ? value->GetDictionary() : nullptr;
  }
  static bool Number(Dict value, const char* key, double& result) {
    if (!value || (value->GetType(key) != VTYPE_DOUBLE && value->GetType(key) != VTYPE_INT)) return false;
    result = value->GetType(key) == VTYPE_INT ? value->GetInt(key) : value->GetDouble(key);
    return std::isfinite(result);
  }
  void SendRequest(int owner,const std::string&method,Dict params,ResponseStage stage,Callback callback){
    if(assertion_send_)assertion_send_(owner,method,params,stage,std::move(callback));
    else send_(owner,method,params,std::move(callback));
  }
  void Fail(int owner, const std::string& error) { finish_(owner, false, error, ""); }
  bool Current(int owner, uint64_t token) const {
    const auto job = jobs_.find(owner); return job != jobs_.end() && job->second.token == token;
  }
  // A retained physical callback bypasses Source's usual CDP error path. Retry
  // only known document/surface invalidation; Source keeps the original deadline.
  bool RetryNavigation(int owner, const std::string& message) {
    if(assertion_owns_&&assertion_owns_(owner))return false;
    if (!retry_ || (message.find("Execution context was destroyed") == std::string::npos &&
        message.find("Cannot find context") == std::string::npos &&
        message.find("Inspected target navigated") == std::string::npos &&
        message.find("Could not find object with given id") == std::string::npos &&
        message.find("Unable to capture screenshot") == std::string::npos)) return false;
    retry_(owner); return true;
  }
  void Pump() {
    if (active_ || physical_owner_ || external_owner_ || restoration_owner_ || queue_.empty()) return;
    const int owner = queue_.front(); queue_.pop_front(); active_ = owner;
    auto found = jobs_.find(owner); if (found == jobs_.end()) { active_ = 0; Pump(); return; }
    if (found->second.config->GetString("action") == "emulateMedia") { Media(owner); return; }
    found->second.waiting_fonts = true;
    // Screenshots have a paint barrier after their actual preparation. Only
    // PDF and user hooks need a rendered observation before that preparation.
    // This promise changes no DOM, even if it settles late.
    const bool wait_for_paint = found->second.config->GetString("action") == "pdf" ||
        found->second.config->GetBool("beforeScreenshot");
    auto params = CefDictionaryValue::Create(); params->SetInt("contextId", found->second.context);
    params->SetString("expression", "(async()=>{await document.fonts.ready;" +
        std::string(wait_for_paint ? "await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));" : "") +
        "return true})()");
    params->SetBool("awaitPromise", true); params->SetBool("returnByValue", true);
    SendRequest(owner, "Runtime.evaluate", params, ResponseStage::Boolean, [this, owner, token = jobs_.at(owner).token](bool ok, const std::string& json) {
      if (!Current(owner, token)) return;
      auto result = Parse(json);
      if (!ok || !result || result->HasKey("exceptionDetails")) { Fail(owner, "Native artifact render readiness failed"); return; }
      jobs_.at(owner).waiting_fonts = false;
      if (jobs_.at(owner).config->GetString("action") == "pdf") Print(owner);
      else BeforeScreenshotReady(owner);
    });
  }
  void BeforeScreenshotReady(int owner) {
    auto& job = jobs_.at(owner); const auto config = job.config;
    if (!config->HasKey("beforeScreenshot")) { PrepareScreenshot(owner); return; }
    if (config->GetType("beforeScreenshot") != VTYPE_BOOL || !before_screenshot_) {
      Fail(owner, "Native beforeScreenshot scheduling is unavailable"); return;
    }
    if (!config->GetBool("beforeScreenshot")) { PrepareScreenshot(owner); return; }
    job.waiting_hook = true; const auto token = job.token;
    before_screenshot_(owner, config, [this, owner, token](bool ok, const std::string& error) {
      const auto current = jobs_.find(owner);
      if (current == jobs_.end() || current->second.token != token) return;
      current->second.waiting_hook = false;
      if (!ok) { Fail(owner, error); return; }
      const auto config = current->second.config;
      if (config->GetType("__artifactContextId") != VTYPE_INT || config->GetInt("__artifactContextId") <= 0) {
        Fail(owner, "Native beforeScreenshot context is unavailable"); return;
      }
      current->second.context = config->GetInt("__artifactContextId");
      config->SetBool("beforeScreenshot", false);
      PrepareScreenshot(owner);
    });
  }
  void PrepareScreenshot(int owner) {
    auto& job = jobs_.at(owner); auto config = job.config;
    const auto format = config->GetString("format").ToString();
    const auto scale = config->GetString("scale").ToString();
    const auto caret = config->GetString("caret").ToString();
    const auto animations = config->GetString("animations").ToString();
    if ((format != "png" && format != "jpeg" && format != "webp") ||
        (scale != "css" && scale != "device") || (caret != "hide" && caret != "initial") ||
        (animations != "allow" && animations != "disabled") ||
        (config->HasKey("quality") && (config->GetType("quality") != VTYPE_INT ||
         config->GetInt("quality") < 0 || config->GetInt("quality") > 100 || format == "png")) ||
        (config->HasKey("style") && (config->GetType("style") != VTYPE_STRING || config->GetString("style").length() > 1048576)) ||
        config->GetType("fullPage") != VTYPE_BOOL || config->GetType("omitBackground") != VTYPE_BOOL) {
      Fail(owner, "Invalid native screenshot options"); return;
    }
    std::string css = config->GetString("style").ToString();
    if (caret == "hide") css += "\n*{caret-color:transparent!important}";
    if (css.empty() && animations == "allow" && !config->HasKey("masks")) { PrepareBackground(owner); return; }
    job.style = true; // Set before dispatch, including a synchronous CDP reply.
    auto params = CefDictionaryValue::Create(); params->SetInt("contextId", job.context);
    params->SetBool("returnByValue", true);
    params->SetString("expression", "(()=>{const m=globalThis.__suitecutArtifactStyles?" "?="
        "new Map();const resources={styles:[],animations:[],masks:[],carets:[]};m.set(" + std::to_string(owner) + ",resources);"
        "const css=" + Quote(css) + ";const roots=[document];let visited=0;"
        "if(css||" + std::string(animations == "disabled" ? "true" : "false") + "){"
        "for(let i=0;i<roots.length;i++){const root=roots[i];const walker=document.createTreeWalker(root,NodeFilter.SHOW_ELEMENT);"
        "let node;while(node=walker.nextNode()){if(++visited>100000)throw new Error('Screenshot DOM traversal limit exceeded');"
        "if(node.shadowRoot)roots.push(node.shadowRoot);}if(css){const e=document.createElement('style');e.textContent=css;"
        "resources.styles.push(e);(root===document?document.documentElement:root).append(e);}}}"
        "if(" + std::string(caret == "hide" ? "true" : "false") + "){for(const root of roots){const e=root.activeElement;"
        "if(e?.style){resources.carets.push({element:e,hadStyle:e.hasAttribute('style'),value:e.style.getPropertyValue('caret-color'),"
        "priority:e.style.getPropertyPriority('caret-color')});e.style.setProperty('caret-color','transparent','important');}}}"
        "if(" + std::string(animations == "disabled" ? "true" : "false") + "){const active=[...new Set(roots.flatMap(root=>root.getAnimations?.()??[]))];"
        "if(active.length>10000)throw new Error('Screenshot animation limit exceeded');for(const animation of active){"
        "const timing=animation.effect?.getComputedTiming();if(!timing||animation.playbackRate===0)continue;"
        "if(Number.isFinite(timing.endTime)&&animation.playbackRate!==0){try{animation.finish();}catch{}}"
        "else if(animation.playState==='running'||animation.playState==='paused'){resources.animations.push({animation,"
        "currentTime:animation.currentTime,state:animation.playState});animation.cancel();}}}return true})()");
    job.preparation_expression = params->GetString("expression").ToString();
    SendRequest(owner, "Runtime.evaluate", params, ResponseStage::Boolean, [this, owner, token = jobs_.at(owner).token](bool ok, const std::string& json) {
      if (!Current(owner, token)) return;
      auto result = Parse(json);
      if (!ok || !result || result->HasKey("exceptionDetails")) { Fail(owner, "Native screenshot stylesheet failed"); return; }
      PrepareBackground(owner);
    });
  }
  void PrepareDocuments(int owner) {
    auto& job = jobs_.at(owner); job.documents = true; job.waiting_fonts = true; const auto token = job.token;
    prepare_documents_(owner, job.preparation_expression, [this, owner, token](bool ok, const std::string& error) {
      if (!Current(owner, token)) return;
      jobs_.at(owner).waiting_fonts = false;
      if (!ok) { Fail(owner, error); return; }
      jobs_.at(owner).documents_ready = true;
      ScreenshotReady(owner);
    });
  }
  void PrepareBackground(int owner) {
    auto& job = jobs_.at(owner);
    job.background = true;
    auto params = CefDictionaryValue::Create(), color = CefDictionaryValue::Create();
    const bool transparent = job.config->GetBool("omitBackground");
    color->SetInt("r", transparent ? 0 : 255); color->SetInt("g", transparent ? 0 : 255);
    color->SetInt("b", transparent ? 0 : 255); color->SetInt("a", transparent ? 0 : 1);
    params->SetDictionary("color", color);
    SendRequest(owner, "Emulation.setDefaultBackgroundColorOverride", params, ResponseStage::Empty,
        [this, owner, token = jobs_.at(owner).token](bool ok, const std::string&) {
      if (!Current(owner, token)) return;
      if (!ok) { Fail(owner, "Native screenshot background override failed"); return; }
      ScreenshotReady(owner);
    });
  }
  void ScreenshotReady(int owner) {
    auto& job = jobs_.at(owner); job.waiting_fonts = true;
    // Render root preparation before discovering descendants: a page callback
    // can attach an iframe in response to even a caret stylesheet mutation.
    // Render once more after descendant preparation before taking the image.
    auto params = CefDictionaryValue::Create(); params->SetInt("contextId", job.context);
    params->SetString("expression", "(async()=>{document.documentElement.getBoundingClientRect();"
        "await document.fonts.ready;await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));"
        "return {width:innerWidth,height:innerHeight}})()");
    params->SetBool("awaitPromise", true); params->SetBool("returnByValue", true);
    SendRequest(owner, "Runtime.evaluate", params, ResponseStage::Viewport, [this, owner, token = jobs_.at(owner).token](bool ok, const std::string& json) {
      if (!Current(owner, token)) return;
      auto result = Parse(json);
      const auto remote = result ? result->GetDictionary("result") : nullptr;
      const auto value = remote ? remote->GetDictionary("value") : nullptr;
      double width = 0, height = 0;
      if (!ok || !result || result->HasKey("exceptionDetails") || !Number(value, "width", width) ||
          !Number(value, "height", height) || width <= 0 || height <= 0) {
        Fail(owner, "Native screenshot render readiness failed"); return;
      }
      jobs_.at(owner).waiting_fonts = false;
      jobs_.at(owner).viewport_width = width; jobs_.at(owner).viewport_height = height;
      // Root finite-animation events have now run. Prepare actual descendants
      // before another rendered root observation and element/mask geometry.
      if (!jobs_.at(owner).documents_ready && prepare_documents_) { PrepareDocuments(owner); return; }
      jobs_.at(owner).documents_ready = true;
      if (jobs_.at(owner).config->HasKey("element") || jobs_.at(owner).config->HasKey("masks")) {
        if (!geometry_) { Fail(owner, "Native screenshot element geometry unavailable"); return; }
        geometry_(owner, jobs_.at(owner).config, [this, owner, token](bool valid, const std::string& error) {
          const auto current = jobs_.find(owner);
          if (current == jobs_.end() || current->second.token != token) return;
          if (!valid) { Fail(owner, error); return; }
          ScreenshotMetrics(owner);
        });
      } else ScreenshotMetrics(owner);
    });
  }
  void ScreenshotMetrics(int owner) {
    SendRequest(owner, "Page.getLayoutMetrics", nullptr, ResponseStage::Layout, [this, owner, token = jobs_.at(owner).token](bool ok, const std::string& json) {
      if (!Current(owner, token)) return;
      auto result = Parse(json); auto& job = jobs_.at(owner);
      if (!ok || !result) { Fail(owner, "Native screenshot layout unavailable"); return; }
      auto config = job.config;
      auto viewport = result->GetDictionary("cssVisualViewport");
      auto content = result->GetDictionary("cssContentSize");
      double x = 0, y = 0, width = 0, height = 0;
      if (config->GetBool("fullPage")) {
        if (!Number(content, "x", x) || !Number(content, "y", y) || !Number(content, "width", width) || !Number(content, "height", height)) {
          Fail(owner, "Native screenshot content bounds unavailable"); return;
        }
        width = std::max(width, job.viewport_width); height = std::max(height, job.viewport_height);
      } else {
        if (!Number(viewport, "pageX", x) || !Number(viewport, "pageY", y)) {
          Fail(owner, "Native screenshot viewport bounds unavailable"); return;
        }
        width = job.viewport_width; height = job.viewport_height;
      }
      if (config->HasKey("clip")) {
        auto clip = config->GetDictionary("clip");
        double cx = 0, cy = 0, cw = 0, ch = 0;
        if (!Number(clip, "x", cx) || !Number(clip, "y", cy) || !Number(clip, "width", cw) || !Number(clip, "height", ch) || cw <= 0 || ch <= 0) {
          Fail(owner, "Invalid native screenshot clip"); return;
        }
        const double left = std::max(0.0, cx), top = std::max(0.0, cy);
        const double right = std::min(width, cx + cw), bottom = std::min(height, cy + ch);
        if (right <= left || bottom <= top) { Fail(owner, "Clipped area is either empty or outside the resulting image"); return; }
        x += left; y += top; width = right - left; height = bottom - top;
      }
      const double pixel_scale = config->GetString("scale") == "css" ? 1 : job.scale;
      if (width <= 0 || height <= 0 || width * pixel_scale > 32768 || height * pixel_scale > 32768 ||
          std::ceil(width * pixel_scale) * std::ceil(height * pixel_scale) > 64 * 1024 * 1024 ||
          std::abs(x) > 100000000 || std::abs(y) > 100000000) {
        Fail(owner, "Native screenshot dimensions exceed the 64 megapixel capture limit"); return;
      }
      auto clip = CefDictionaryValue::Create(); clip->SetDouble("x", x); clip->SetDouble("y", y);
      clip->SetDouble("width", width); clip->SetDouble("height", height);
      clip->SetDouble("scale", config->GetString("scale") == "css" ? 1 / job.scale : 1);
      auto params = CefDictionaryValue::Create(); params->SetString("format", config->GetString("format"));
      if (config->HasKey("quality")) params->SetInt("quality", config->GetInt("quality"));
      params->SetBool("fromSurface", true);
      params->SetDictionary("clip", clip);
      double page_x = 0, page_y = 0;
      if (!Number(viewport, "pageX", page_x) || !Number(viewport, "pageY", page_y)) {
        Fail(owner, "Native screenshot viewport offset unavailable"); return;
      }
      const bool beyond_viewport = x < page_x || y < page_y || x + width > page_x + job.viewport_width || y + height > page_y + job.viewport_height;
      params->SetBool("captureBeyondViewport", beyond_viewport);
      if (config->GetBool("fullPage") && beyond_viewport) {
        double render_width = 0, render_height = 0;
        if (!Number(content, "width", render_width) || !Number(content, "height", render_height)) {
          Fail(owner, "Native screenshot rasterization bounds unavailable"); return;
        }
        render_width = std::max(render_width, job.viewport_width); render_height = std::max(render_height, job.viewport_height);
        if (render_width * job.scale > 32768 || render_height * job.scale > 32768 ||
            std::ceil(render_width * job.scale) * std::ceil(render_height * job.scale) > 64 * 1024 * 1024) {
          Fail(owner, "Native screenshot rasterization exceeds the 64 megapixel surface limit"); return;
        }
        auto viewport = CefDictionaryValue::Create(); viewport->SetDouble("x", 0); viewport->SetDouble("y", 0);
        viewport->SetDouble("width", render_width); viewport->SetDouble("height", render_height); viewport->SetDouble("scale", 1);
        auto metrics = ViewportMetrics(job); metrics->SetDictionary("viewport", viewport);
        job.viewport_override = true; const auto token = job.token;
        SendRequest(owner, "Emulation.setDeviceMetricsOverride", metrics, ResponseStage::Empty, [this, owner, params, token, render_width, render_height](bool valid, const std::string&) {
          const auto current = jobs_.find(owner);
          if (current == jobs_.end() || current->second.token != token) return;
          if (!valid) { Fail(owner, "Native screenshot rasterization override failed"); return; }
          if (!view_lease_) { Fail(owner, "Native screenshot physical rasterization is unavailable"); return; }
          current->second.physical_view_lease = true;
          view_lease_(owner, static_cast<int>(std::ceil(render_width)), static_cast<int>(std::ceil(render_height)),
            [this, owner, params, token](bool painted, const std::string& error) {
              const auto current = jobs_.find(owner);
              if (current == jobs_.end() || current->second.token != token) return;
              if (!painted) { Fail(owner, error); return; }
              Capture(owner, params);
            });
        });
      } else Capture(owner, params);
    });
  }
  void Capture(int owner, Dict params) {
    const auto current = jobs_.find(owner); if (current == jobs_.end()) return;
    physical_owner_ = owner; physical_token_ = current->second.token; physical_input_ = current->second.input;
    SendRequest(owner, "Page.captureScreenshot", params, ResponseStage::PNG, [this, owner, token = current->second.token](bool success, const std::string& response) { FinishData(owner, token, success, response,true); });
  }
  void Print(int owner) {
    auto config = jobs_.at(owner).config;
    auto params = CefDictionaryValue::Create();
    for (const char* key : {"paperWidth", "paperHeight", "marginTop", "marginBottom", "marginLeft", "marginRight", "scale"}) {
      double value = 0;
      if (!Number(config, key, value) || value < 0 || value > 200 ||
          ((std::string(key) == "paperWidth" || std::string(key) == "paperHeight") && value == 0) ||
          (std::string(key) == "scale" && (value < 0.1 || value > 2))) {
        Fail(owner, "Invalid native PDF dimensions"); return;
      }
      params->SetDouble(key, value);
    }
    for (const char* key : {"displayHeaderFooter", "printBackground", "landscape", "preferCSSPageSize", "tagged", "outline"}) {
      if (config->GetType(key) != VTYPE_BOOL) { Fail(owner, "Invalid native PDF flags"); return; }
      const std::string name = std::string(key) == "tagged" ? "generateTaggedPDF" :
          std::string(key) == "outline" ? "generateDocumentOutline" : key;
      params->SetBool(name, config->GetBool(key));
    }
    for (const char* key : {"headerTemplate", "footerTemplate", "pageRanges"}) {
      if (config->GetType(key) != VTYPE_STRING || config->GetString(key).length() > (std::string(key) == "pageRanges" ? 4096 : 1048576)) {
        Fail(owner, "Invalid native PDF template or page ranges"); return;
      }
      params->SetString(key, config->GetString(key));
    }
    params->SetString("transferMode", "ReturnAsBase64");
    physical_owner_ = owner;
    physical_input_ = jobs_.at(owner).input;
    physical_token_ = jobs_.at(owner).token;
    SendRequest(owner, "Page.printToPDF", params, ResponseStage::PDF, [this, owner, token = physical_token_](bool ok, const std::string& json) { FinishData(owner, token, ok, json); });
  }
  void Media(int owner) {
    auto config = jobs_.at(owner).config;
    auto proposed = media_;
    for (const char* key : {"media", "colorScheme", "reducedMotion", "forcedColors", "contrast"}) {
      if (!config->HasKey(key)) continue;
      if (config->GetType(key) == VTYPE_NULL) { proposed.erase(key); continue; }
      if (config->GetType(key) != VTYPE_STRING) { Fail(owner, "Invalid native media setting"); return; }
      const auto value = config->GetString(key).ToString();
      const std::string name(key);
      const bool valid = name == "media" ? value == "screen" || value == "print" :
          name == "colorScheme" ? value == "light" || value == "dark" || value == "no-preference" :
          name == "reducedMotion" ? value == "reduce" || value == "no-preference" :
          name == "forcedColors" ? value == "active" || value == "none" :
          value == "more" || value == "no-preference";
      if (!valid) { Fail(owner, "Invalid native media setting"); return; }
      proposed[key] = value;
    }
    auto params = CefDictionaryValue::Create();
    params->SetString("media", proposed.contains("media") ? proposed.at("media") : "");
    auto features = CefListValue::Create();
    const std::map<std::string, std::string> names = {{"colorScheme", "prefers-color-scheme"},
        {"reducedMotion", "prefers-reduced-motion"}, {"forcedColors", "forced-colors"},
        {"contrast", "prefers-contrast"}};
    for (const auto& [key, name] : names) if (proposed.contains(key)) {
      auto item = CefDictionaryValue::Create(); item->SetString("name", name); item->SetString("value", proposed.at(key));
      features->SetDictionary(features->GetSize(), item);
    }
    params->SetList("features", features);
    physical_owner_ = owner; physical_token_ = jobs_.at(owner).token; physical_input_ = jobs_.at(owner).input;
    SendRequest(owner, "Emulation.setEmulatedMedia", params, ResponseStage::Empty, [this, owner, proposed, token = jobs_.at(owner).token](bool ok, const std::string& json) {
      std::shared_ptr<void> retired;
    if (physical_owner_ == owner && physical_token_ == token) { retired = std::move(physical_input_); physical_owner_ = 0; physical_token_ = 0; }
      const auto actual = Parse(json);
      if (ok || (actual && actual->GetBool("rootApplied"))) media_ = proposed;
      if (!Current(owner, token)) { Pump(); return; }
      if (!ok) { Fail(owner, "Native media emulation failed"); return; }
      finish_(owner, true, "", "{}");
    });
  }
  void FinishData(int owner, uint64_t token, bool ok, const std::string& json,bool screenshot=false) {
    std::shared_ptr<void> retired;
    if (physical_owner_ == owner && physical_token_ == token) { retired = std::move(physical_input_); physical_owner_ = 0; physical_token_ = 0; }
    // An assertion owns the actual capture response even after logical Cancel
    // removed this job. It must validate late PNGs and drain native pixel work.
    if(screenshot&&assertion_captured_&&assertion_captured_(owner,token,ok,json)){Pump();return;}
    const auto current = jobs_.find(owner);
    if (current == jobs_.end() || current->second.token != token) { Pump(); return; }
    auto result = Parse(json);
    if (!ok) {
      const auto error = result && result->GetType("message") == VTYPE_STRING
          ? result->GetString("message").ToString() : "Native artifact capture failed";
      if (RetryNavigation(owner, error)) return;
      Fail(owner, error);
      return;
    }
    if (!ok || !result || result->GetType("data") != VTYPE_STRING ||
        result->GetString("data").length() > ((128 * 1048576 + 2) / 3) * 4) {
      Fail(owner, "Native encoded artifact exceeds 128 MiB or is unavailable"); return;
    }
    auto response = CefDictionaryValue::Create(); response->SetString("data", result->GetString("data"));
    finish_(owner, true, "", JSON(response));
  }
  Send send_; AssertionSend assertion_send_; std::function<bool(int)> assertion_owns_; AssertionCleanup assertion_cleanup_; AssertionCaptured assertion_captured_; Fire fire_; Finish finish_; Retry retry_; DocumentPreparation prepare_documents_; DocumentCleanup cleanup_documents_; Geometry geometry_; BeforeScreenshot before_screenshot_; ViewLease view_lease_; ViewRelease release_lease_;
  std::map<int, Job> jobs_;
  std::deque<int> queue_;
  std::map<std::string, std::string> media_;
  int restoration_owner_=0;uint64_t restoration_token_=0;
  int active_ = 0;
  int physical_owner_ = 0;
  std::shared_ptr<void> physical_input_;
  int external_owner_ = 0;
  uint64_t physical_token_ = 0, next_token_ = 0;
};
