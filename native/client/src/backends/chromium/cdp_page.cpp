// SPDX-License-Identifier: MIT
#include "cdp_internal.hpp"
#include "cdp_frame.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <thread>

namespace suitecut::detail {
namespace {
using Clock = std::chrono::steady_clock;
void check_deadline(Deadline deadline) {
  cdp_check_active(deadline);
}
std::string text(const Json& fields, const char* key, std::string fallback = {}) {
  return fields.contains(key) ? fields.at(key).as_string() : std::move(fallback);
}
bool flag(const Json& fields, const char* key, bool fallback = false) {
  return fields.contains(key) ? fields.at(key).as_bool() : fallback;
}
double number(const Json& fields, const char* key) {
  const auto value = fields.at(key).as_double();
  if (!std::isfinite(value)) throw std::invalid_argument(std::string(key) + " must be finite");
  return value;
}
void main_page(const CdpPage& page) {
  if (!page.frame_id.empty()) throw std::invalid_argument("This Chromium command requires a page, not a frame");
}
Json find_frame(const Json& tree, const std::string& wanted) {
  if (tree.at("frame").at("id").as_string() == wanted) return tree.at("frame");
  if (tree.contains("childFrames")) {
    for (const auto& child : tree.at("childFrames").as_array()) {
      auto found = find_frame(child, wanted);
      if (!found.is_null()) return found;
    }
  }
  return Json{};
}
Json frame(CdpRuntime& runtime, CdpPage& page, Deadline deadline) {
  const auto tree = runtime.call(page, "Page.getFrameTree", Json::object{}, deadline).at("frameTree");
  if (page.frame_id.empty()) return tree.at("frame");
  auto found = find_frame(tree, page.frame_id);
  if (found.is_null()) throw std::runtime_error("Chromium frame is detached or unavailable");
  return found;
}
void pause(Deadline deadline) {
  check_deadline(deadline);
  std::this_thread::sleep_until(std::min(deadline, Clock::now() + std::chrono::milliseconds(10)));
}
std::string navigation_wait(const Json& fields) {
  const auto wait = text(fields, "waitUntil", "load");
  if (wait != "load" && wait != "domcontentloaded" && wait != "commit" && wait != "networkidle")
    throw std::invalid_argument("Unknown Chromium navigation waitUntil: " + wait);
  return wait;
}
bool context_changed(const std::exception& error) {
  const std::string what = error.what();
  return what.find("Execution context was destroyed") != std::string::npos ||
         what.find("Cannot find context") != std::string::npos ||
         what.find("Cannot find default execution context") != std::string::npos ||
         what.find("Inspected target navigated") != std::string::npos;
}
bool load_ready(CdpRuntime& runtime, CdpPage& page, const std::string& wait, Deadline deadline) {
  if (wait == "commit") return true;
  if (wait == "networkidle") {
    if (!page.frame_id.empty())
      throw std::invalid_argument("Chromium networkidle waits on a selected frame are unsupported");
    return runtime.network_idle(page, deadline);
  }
  // interactive precedes deferred scripts and DOMContentLoaded, while complete
  // can precede load handlers. Navigation timing closes both gaps.
  const auto state = runtime.value(page,
      "(()=>{const n=performance.getEntriesByType('navigation')[0];"
      "return n? n." + std::string(wait == "load" ? "loadEventEnd" : "domContentLoadedEventEnd") +
      ">0 : document.readyState==='complete';})()", deadline, true);
  return state.is_bool() && state.as_bool();
}
Json wait_load(CdpRuntime& runtime, CdpPage& page, const std::string& wait, Deadline deadline) {
  for (;;) {
    check_deadline(deadline);
    try {
      const auto before = frame(runtime, page, deadline);
      if (load_ready(runtime, page, wait, deadline)) {
        const auto after = frame(runtime, page, deadline);
        if (text(before, "loaderId") == text(after, "loaderId")) return Json{};
      }
    } catch (const std::exception& error) {
      if (!context_changed(error)) throw;
    }
    pause(deadline);
  }
}
Json navigate(CdpRuntime& runtime, CdpPage& page, const Json& fields, Deadline deadline) {
  const auto wait = navigation_wait(fields);
  if (wait == "networkidle" && !page.frame_id.empty())
    throw std::invalid_argument("Chromium networkidle waits on a selected frame are unsupported");
  Json params{{"url", fields.at("url").as_string()}};
  if (!page.frame_id.empty()) params["frameId"] = page.frame_id;
  if (fields.contains("referer")) params["referrer"] = fields.at("referer").as_string();
  const auto previous = frame(runtime, page, deadline);
  auto result = runtime.call(page, "Page.navigate", std::move(params), deadline);
  if (result.contains("errorText") && !result.at("errorText").as_string().empty())
    throw std::runtime_error("Chromium navigation failed: " + result.at("errorText").as_string());
  if (flag(result, "isDownload"))
    throw std::runtime_error("Chromium navigation started a download instead of a document");
  // CDP omits loaderId for same-document navigations; there is no new load to wait for.
  if (!result.contains("loaderId") || result.at("loaderId").as_string().empty()) {
    if (wait == "networkidle") wait_load(runtime, page, wait, deadline);
    return result;
  }
  const auto loader = result.at("loaderId").as_string();
  const auto old_loader = text(previous, "loaderId");
  bool committed = false;
  for (;;) {
    check_deadline(deadline);
    auto current = frame(runtime, page, deadline);
    const auto current_loader = text(current, "loaderId");
    if (current.at("id") != result.at("frameId"))
      throw std::runtime_error("Chromium navigation main frame changed");
    if (current_loader == loader) {
      if (!committed) {
        page.isolated_context = 0;
        page.isolated_loader.clear();
        page.isolated_frame.clear();
        committed = true;
      }
      if (wait == "commit") return result;
      bool ready = false;
      try {
        ready = load_ready(runtime, page, wait, deadline);
      } catch (const std::exception& error) {
        // The default execution context can disappear while a document commits.
        // Recheck the loader below; never accept an old document's readyState.
        if (!context_changed(error)) throw;
        check_deadline(deadline);
      }
      const auto after = frame(runtime, page, deadline);
      if (text(after, "loaderId") != loader)
        throw std::runtime_error("Chromium navigation was superseded by another document");
      if (ready) return result;
    } else if (committed || (!current_loader.empty() && current_loader != old_loader)) {
      throw std::runtime_error("Chromium navigation was superseded by another document");
    }
    pause(deadline);
  }
}

Json history_navigation(CdpRuntime& runtime, CdpPage& page, const Json& fields, Deadline deadline) {
  const auto wait = navigation_wait(fields);
  const auto command = fields.at("command").as_string();
  const auto before = frame(runtime, page, deadline);
  const auto old_loader = text(before, "loaderId");
  int target_index = -1;
  if (command == "reload") {
    runtime.call(page, "Page.reload", Json{{"ignoreCache", flag(fields, "ignoreCache")}}, deadline);
  } else {
    const auto history = runtime.call(page, "Page.getNavigationHistory", Json::object{}, deadline);
    const auto index = history.at("currentIndex").as_int();
    const auto wanted = index + (command == "historyBack" ? -1 : 1);
    if (wanted < 0 || static_cast<std::size_t>(wanted) >= history.at("entries").as_array().size())
      return Json{};
    target_index = static_cast<int>(wanted);
    runtime.call(page, "Page.navigateToHistoryEntry",
        Json{{"entryId", history.at("entries").at(static_cast<std::size_t>(wanted)).at("id")}}, deadline);
  }
  for (;;) {
    check_deadline(deadline);
    const auto current = frame(runtime, page, deadline);
    bool committed = text(current, "loaderId") != old_loader;
    if (target_index >= 0) {
      const auto history = runtime.call(page, "Page.getNavigationHistory", Json::object{}, deadline);
      committed = history.at("currentIndex").as_int() == target_index;
    }
    if (committed) {
      if (text(current, "loaderId") != old_loader) {
        page.isolated_context = 0;
        page.isolated_loader.clear();
        page.isolated_frame.clear();
      }
      wait_load(runtime, page, wait, deadline);
      return Json{{"url", current.at("url")}, {"frameId", current.at("id")},
                  {"loaderId", text(current, "loaderId")}};
    }
    pause(deadline);
  }
}

Json wait_condition(CdpRuntime& runtime, CdpPage& page, const Json& fields, Deadline deadline) {
  const auto command = fields.at("command").as_string();
  if (command == "waitLoadState") return wait_load(runtime, page, navigation_wait(fields), deadline);
  std::string expression;
  if (command == "waitForFunction") {
    expression = "(async()=>{const f=(" + fields.at("expression").as_string() +
        ");const v=await(typeof f==='function'?f():f);return v?{ready:true,value:v}:{ready:false};})()";
  } else if (command == "waitForURL") {
    const auto match = text(fields, "match", "exact");
    const auto wanted = Json(fields.at("url").as_string()).dump();
    if (match == "exact") expression = "location.href===" + wanted;
    else if (match == "regex")
      expression = "new RegExp(" + wanted + "," + Json(text(fields, "flags")).dump() + ").test(location.href)";
    else if (match == "glob")
      expression = "(()=>{const g=" + wanted + ";let p='^';for(let i=0;i<g.length;i++){"
          "const c=g[i];if(c==='*'){if(g[i+1]==='*'){p+='.*';i++;}else p+='[^/]*';}"
          "else if(c==='?')p+='.';else p+=" + Json("\\^$.*+?()[]{}|").dump() +
          ".includes(c)?" + Json("\\").dump() + "+c:c;}"
          "return new RegExp(p+'$').test(location.href);})()";
    else throw std::invalid_argument("URL match must be exact, glob or regex");
    navigation_wait(fields);
  } else if (command == "waitForSelector") {
    const auto state = text(fields, "state", "visible");
    if (state != "visible" && state != "hidden" && state != "attached" && state != "detached")
      throw std::invalid_argument("Selector state must be visible, hidden, attached or detached");
    expression = "(()=>{const e=document.querySelector(" + Json(fields.at("selector").as_string()).dump() +
        ");const visible=!!e&&e.getClientRects().length>0&&getComputedStyle(e).visibility!=='hidden'"
        "&&getComputedStyle(e).visibility!=='collapse';return " +
        std::string(state == "attached" ? "!!e" : state == "detached" ? "!e" : state == "visible" ? "visible" : "!visible") + ";})()";
  } else if (command == "waitForText") {
    expression = "(()=>{const actual=(document.body?.innerText||'').replace(/\\s+/g,' ').trim();"
        "const wanted=" + Json(fields.at("text").as_string()).dump() + ".replace(/\\s+/g,' ').trim();return " +
        std::string(flag(fields, "exact") ? "actual===wanted" : "actual.includes(wanted)") + ";})()";
  } else throw std::invalid_argument("Unsupported Chromium wait command: " + command);
  for (;;) {
    check_deadline(deadline);
    try {
      // Authored predicates share the page's JavaScript globals, matching
      // Page.evaluate; DOM-only wait implementation stays in the isolated world.
      const auto value = runtime.value(page, expression, deadline, command != "waitForFunction");
      if (command == "waitForFunction") {
        if (value.is_object() && flag(value, "ready"))
          return value.contains("value") ? value.at("value") : Json{};
      } else if (value.is_bool() && value.as_bool()) {
        if (command == "waitForURL") {
          wait_load(runtime, page, navigation_wait(fields), deadline);
          // A redirect or script navigation while load was pending can replace
          // the URL; require the matcher to still hold at completion.
          const auto matched = runtime.value(page, expression, deadline, true);
          if (!matched.is_bool() || !matched.as_bool()) { pause(deadline); continue; }
        }
        return Json{};
      }
    } catch (const std::exception& error) {
      if (!context_changed(error)) throw;
    }
    pause(deadline);
  }
}

// Restore temporary capture state even when the original command has expired.
// Cleanup gets a single bounded allowance; it never retries indefinitely.
Deadline cleanup_deadline() {
  return Clock::now() + std::chrono::milliseconds(500);
}
Json screenshot(CdpRuntime& runtime, CdpPage& page, const Json& fields, Deadline deadline) {
  for (const auto& [key, value] : fields.as_object()) {
    if (key == "command" || key == "pageId" || key == "timeoutMs" || key == "api" ||
        key == "id" || key == "version" || key == "framePath" || key == "action" || key == "format" ||
        key == "quality" || key == "scale" || key == "fullPage" || key == "clip" ||
        key == "omitBackground" || key == "caret" || key == "animations" || key == "maskColor" ||
        key == "style" || key == "mask" || key == "masks")
      continue;
    (void)value;
    throw std::invalid_argument("Unsupported Chromium screenshot option: " + key);
  }
  const auto format = text(fields, "format", "png");
  if (format != "png" && format != "jpeg" && format != "webp")
    throw std::invalid_argument("Screenshot format must be png, jpeg or webp");
  const auto scale = text(fields, "scale", "device");
  if (scale != "device" && scale != "css")
    throw std::invalid_argument("Screenshot scale must be css or device");
  const auto caret = text(fields, "caret", "hide");
  if (caret != "hide" && caret != "initial")
    throw std::invalid_argument("Screenshot caret must be hide or initial");
  const auto animations = text(fields, "animations", "allow");
  if (animations != "allow" && animations != "disabled")
    throw std::invalid_argument("Screenshot animations must be allow or disabled");
  const auto css = text(fields, "style") + (caret == "hide" ? "\n*{caret-color:transparent!important}" : "");
  if (css.size() > 1048576) throw std::invalid_argument("Screenshot stylesheet exceeds 1 MiB");
  const auto mask_color = text(fields, "maskColor", "#FF00FF");
  if (mask_color.size() > 4096 || !runtime.value(page,
      "CSS.supports('color'," + Json(mask_color).dump() + ")", deadline, true).as_bool())
    throw std::invalid_argument("Screenshot maskColor must be a valid CSS color");
  if (fields.contains("mask") && fields.contains("masks"))
    throw std::invalid_argument("Specify only one of screenshot mask or masks");
  Json::array masks;
  if (fields.contains("mask") || fields.contains("masks")) {
    const auto& raw = fields.at(fields.contains("masks") ? "masks" : "mask");
    if (!raw.is_array() || raw.as_array().size() > 1024)
      throw std::invalid_argument("Screenshot masks must be an array of at most 1024 queries");
    for (const auto& mask : raw.as_array()) {
      if (mask.is_string()) masks.emplace_back(Json{{"selector", mask}});
      else if (mask.is_array()) masks.emplace_back(Json{{"query", mask}});
      else if (mask.is_object()) {
        Json query;
        for (const auto& [key, value] : mask.as_object()) {
          if (key == "framePath" && value.is_array() && value.as_array().empty()) continue;
          if (key != "query" && key != "selector")
            throw std::invalid_argument("Unsupported screenshot mask field (framed masks are unsupported): " + key);
          query[key] = value;
        }
        if (!query.contains("query") && !query.contains("selector"))
          throw std::invalid_argument("Screenshot mask requires a selector or query");
        masks.push_back(std::move(query));
      } else throw std::invalid_argument("Screenshot masks require selector strings, query arrays or query objects");
    }
  }
  if (!masks.empty()) {
    // Bootstrap the same authored semantic/shadow/ref query engine used by locators.
    cdp_dom_command(runtime, page, Json{{"command", "queryEvaluate"}, {"count", true},
        {"query", Json::array{Json{{"engine", "css"}, {"selector", ":root"}}}}}, deadline);
  }
  const bool full = flag(fields, "fullPage"), transparent = flag(fields, "omitBackground");
  if (transparent && format == "jpeg")
    throw std::invalid_argument("JPEG screenshots cannot omit the background");
  Json params{{"format", format}, {"fromSurface", true}, {"captureBeyondViewport", full}};
  if (fields.contains("quality")) {
    const auto quality = fields.at("quality").as_int();
    if (quality < 0 || quality > 100 || format == "png")
      throw std::invalid_argument("Screenshot quality must be 0..100 and requires jpeg or webp");
    params["quality"] = quality;
  }
  const auto metrics = runtime.call(page, "Page.getLayoutMetrics", Json::object{}, deadline);
  // Visual viewport client dimensions exclude classic scrollbars. Captures use
  // the complete window backing, including those scrollbar pixels.
  const auto window = runtime.value(page,
      "({width:window.innerWidth,height:window.innerHeight,dpr:window.devicePixelRatio})",
      deadline, true);
  const auto& viewport = metrics.at("cssVisualViewport");
  const auto& bounds = full ? metrics.at("cssContentSize") : viewport;
  double x = number(bounds, full ? "x" : "pageX");
  double y = number(bounds, full ? "y" : "pageY");
  const auto window_width = number(window, "width"), window_height = number(window, "height");
  if (window_width <= 0 || window_height <= 0)
    throw std::runtime_error("Invalid screenshot window dimensions");
  double width = full ? number(bounds, "width") : window_width;
  double height = full ? number(bounds, "height") : window_height;
  if (full) {
    width = std::max(width, window_width);
    height = std::max(height, window_height);
  }
  if (fields.contains("clip")) {
    const auto& clip = fields.at("clip");
    if (!clip.is_object() || clip.as_object().size() != 4)
      throw std::invalid_argument("Screenshot clip requires only x, y, width and height");
    const auto cx = number(clip, "x"), cy = number(clip, "y");
    const auto cw = number(clip, "width"), ch = number(clip, "height");
    if (cw <= 0 || ch <= 0) throw std::invalid_argument("Screenshot clip dimensions must be positive");
    const auto left = std::max(0.0, cx), top = std::max(0.0, cy);
    const auto right = std::min(width, cx + cw), bottom = std::min(height, cy + ch);
    if (right <= left || bottom <= top)
      throw std::invalid_argument("Screenshot clip is empty or outside the resulting image");
    x += left; y += top; width = right - left; height = bottom - top;
  }
  // Device metrics are explicitly set to scale 1 by this backend. Read the actual
  // value so raw CDP overrides cannot silently break css-scale screenshots.
  const auto dpr = number(window, "dpr");
  if (!std::isfinite(dpr) || dpr <= 0) throw std::runtime_error("Invalid screenshot device scale");
  const double pixels = scale == "css" ? 1 : dpr;
  if (width <= 0 || height <= 0 || width * pixels > 32768 || height * pixels > 32768 ||
      std::ceil(width * pixels) * std::ceil(height * pixels) > 64 * 1024 * 1024 ||
      std::abs(x) > 100000000 || std::abs(y) > 100000000)
    throw std::invalid_argument("Screenshot dimensions exceed the 64 megapixel capture limit");
  params["clip"] = Json{{"x", x}, {"y", y}, {"width", width}, {"height", height},
                         {"scale", scale == "css" ? 1 / dpr : 1}};

  static std::atomic<unsigned long long> next_style{0};
  const auto style_id = "suitecut-cdp-caret-" + std::to_string(++next_style);
  bool background_changed = false, prepared = false;
  auto restore = [&] {
    std::exception_ptr failure;
    const auto until = cleanup_deadline();
    if (background_changed) {
      try {
        // Cleanup must survive a canceled Work; normal runtime.call observes its
        // cancellation guard before sending any further protocol messages.
        runtime.wire->call("Emulation.setDefaultBackgroundColorOverride", Json::object{}, page.session, until);
        background_changed = false;
      }
      catch (...) { failure = std::current_exception(); }
    }
    if (prepared) {
      try {
        Json params{{"expression",
          "(()=>{const m=globalThis.__suitecutCdpCapture1,r=m?.get(" + Json(style_id).dump() + ");if(!r)return;"
          "const errors=[];const attempt=f=>{try{f();}catch(e){errors.push(String(e));}};"
          "for(const e of [...r.styles,...r.overlays])attempt(()=>e.remove());"
          "for(const c of r.carets)attempt(()=>{if(c.value)c.element.style.setProperty('caret-color',c.value,c.priority);"
          "else c.element.style.removeProperty('caret-color');if(!c.hadStyle&&!c.element.getAttribute('style'))c.element.removeAttribute('style');});"
          "for(const a of r.animations)attempt(()=>{a.animation.currentTime=a.currentTime;"
          "if(a.state==='running')a.animation.play();else if(a.state==='paused')a.animation.pause();"
          "else if(a.state==='finished')a.animation.finish();});m.delete(" + Json(style_id).dump() + ");"
          "if(errors.length)throw new Error(errors.join('; '));})()"},
          {"returnByValue", true}, {"awaitPromise", false}};
        if (page.isolated_context) params["contextId"] = page.isolated_context;
        const auto cleaned = runtime.wire->call("Runtime.evaluate", std::move(params), page.session, until);
        if (cleaned.contains("exceptionDetails"))
          throw std::runtime_error("Chromium screenshot cleanup failed: " + cleaned.at("exceptionDetails").dump());
        prepared = false;
      } catch (...) { if (!failure) failure = std::current_exception(); }
    }
    if (failure) std::rethrow_exception(failure);
  };
  Json result;
  try {
    if (!css.empty() || animations == "disabled" || !masks.empty()) {
      prepared = true;
      runtime.value(page,
        "(()=>{const m=globalThis.__suitecutCdpCapture1||(globalThis.__suitecutCdpCapture1=new Map());"
        "const resources={styles:[],overlays:[],carets:[],animations:[]};m.set(" + Json(style_id).dump() + ",resources);"
        "const roots=[document];let visited=0;for(let i=0;i<roots.length;i++){"
        "for(const e of roots[i].querySelectorAll('*')){if(++visited>100000)throw new Error('Screenshot DOM traversal exceeds 100000 elements');"
        "if(e.shadowRoot)roots.push(e.shadowRoot);}}"
        "const disable=" + std::string(animations == "disabled" ? "true" : "false") + ";"
        "if(disable&&roots.some(r=>r.querySelector('iframe,frame')))throw new Error('Disabling screenshot animations across frame documents is unsupported');"
        "const css=" + Json(css).dump() + ";for(const root of roots){if(css){const s=document.createElement('style');"
        "s.setAttribute('data-suitecut-capture'," + Json(style_id).dump() + ");"
        "s.textContent=css;resources.styles.push(s);(root===document?document.documentElement:root).appendChild(s);}"
        "if(" + std::string(caret == "hide" ? "true" : "false") + "){const e=root.activeElement;if(e?.style){"
        "resources.carets.push({element:e,hadStyle:e.hasAttribute('style'),value:e.style.getPropertyValue('caret-color'),priority:e.style.getPropertyPriority('caret-color')});"
        "e.style.setProperty('caret-color','transparent','important');}}}"
        "if(disable){const active=[...new Set(roots.flatMap(r=>r.getAnimations?.()??[]))];"
        "if(active.length>10000)throw new Error('Screenshot animation limit exceeds 10000');"
        "for(const a of active){if(a.playState==='idle'||a.playState==='finished')continue;const t=a.effect?.getComputedTiming();"
        "if(!t)continue;resources.animations.push({animation:a,currentTime:a.currentTime,state:a.playState});"
        "a.pause();a.currentTime=Number.isFinite(t.endTime)&&a.playbackRate>=0?t.endTime:0;}}"
        "const rects=[];for(const mask of " + Json(masks).dump() + "){const elements=globalThis.__suitecutNativeChromeDOM1.all(mask);"
        "for(const e of elements){if(rects.length>=8192)throw new Error('Screenshot mask rectangle limit exceeds 8192');"
        "const r=e.getBoundingClientRect();if(r.width>0&&r.height>0)rects.push({x:r.left+scrollX,y:r.top+scrollY,width:r.width,height:r.height});}}"
        "for(const r of rects){const e=document.createElement('div');e.setAttribute('aria-hidden','true');"
        "e.setAttribute('data-suitecut-capture'," + Json(style_id).dump() + ");"
        "e.style.cssText='position:absolute!important;pointer-events:none!important;z-index:2147483647!important;"
        "margin:0!important;border:0!important;padding:0!important;box-sizing:border-box!important;"
        "animation:none!important;transition:none!important;transform:none!important;opacity:1!important;"
        "visibility:visible!important;display:block!important;';"
        "for(const [k,v] of Object.entries({left:r.x+'px',top:r.y+'px',width:r.width+'px',height:r.height+'px',background:" + Json(mask_color).dump() +
        "}))e.style.setProperty(k,v,'important');resources.overlays.push(e);document.documentElement.appendChild(e);}})()", deadline, true);
    }
    if (transparent) {
      background_changed = true;
      runtime.call(page, "Emulation.setDefaultBackgroundColorOverride",
                   Json{{"color", Json{{"r", 0}, {"g", 0}, {"b", 0}, {"a", 0}}}}, deadline);
    }
    result = runtime.call(page, "Page.captureScreenshot", std::move(params), deadline);
    if (!result.contains("data") || !result.at("data").is_string() || result.at("data").as_string().empty())
      throw std::runtime_error("Chromium returned an empty screenshot");
  } catch (...) {
    const auto failure = std::current_exception();
    try { restore(); } catch (...) {}
    std::rethrow_exception(failure);
  }
  restore();
  return Json{{"data", result.at("data")}};
}
Json pdf(CdpRuntime& runtime, CdpPage& page, const Json& fields, Deadline deadline) {
  Json params{{"transferMode", "ReturnAsBase64"}};
  const std::set<std::string> numbers{"paperWidth", "paperHeight", "marginTop", "marginBottom",
      "marginLeft", "marginRight", "scale"};
  const std::set<std::string> booleans{"displayHeaderFooter", "printBackground", "landscape",
      "preferCSSPageSize", "tagged", "outline", "generateTaggedPDF", "generateDocumentOutline"};
  const std::set<std::string> strings{"headerTemplate", "footerTemplate", "pageRanges"};
  for (const auto& [key, value] : fields.as_object()) {
    if (key == "command" || key == "action" || key == "pageId" || key == "timeoutMs" ||
        key == "api" || key == "id" || key == "version" || key == "framePath") continue;
    if (numbers.contains(key)) {
      const auto n = number(fields, key.c_str());
      if (n < 0 || n > 200 || ((key == "paperWidth" || key == "paperHeight") && n == 0) ||
          (key == "scale" && (n < 0.1 || n > 2)))
        throw std::invalid_argument("Invalid PDF dimension: " + key);
      params[key] = n;
    } else if (booleans.contains(key)) {
      const auto name = key == "tagged" ? "generateTaggedPDF" : key == "outline" ? "generateDocumentOutline" : key;
      if (params.contains(name)) throw std::invalid_argument("Duplicate PDF option: " + name);
      params[name] = value.as_bool();
    } else if (strings.contains(key)) {
      const auto& s = value.as_string();
      if (s.size() > (key == "pageRanges" ? 4096u : 1048576u))
        throw std::invalid_argument("PDF option is too long: " + key);
      params[key] = s;
    } else throw std::invalid_argument("Unsupported Chromium PDF option: " + key);
  }
  auto result = runtime.call(page, "Page.printToPDF", std::move(params), deadline);
  if (!result.contains("data") || !result.at("data").is_string() || result.at("data").as_string().empty())
    throw std::runtime_error("Chromium returned an empty PDF");
  return Json{{"data", result.at("data")}};
}
void download_scope(CdpRuntime& runtime, CdpPage& page, Deadline deadline) {
  if (page.download_frame.empty()) page.download_frame = frame(runtime, page, deadline).at("id").as_string();
  if (page.download_directory.empty()) {
    for (const auto& [id, candidate] : runtime.pages) {
      (void)id;
      if (candidate.context == page.context && !candidate.download_directory.empty()) {
        page.download_directory = candidate.download_directory;
        break;
      }
    }
  }
}
bool safe_download_id(const std::string& id) {
  return !id.empty() && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}
void sync_downloads(CdpRuntime& runtime, CdpPage& page, Deadline deadline) {
  download_scope(runtime, page, deadline);
  const auto begins = runtime.browser_events("Browser.downloadWillBegin");
  for (const auto& event : begins.as_array()) {
    const auto& begin = event.at("params");
    if (text(begin, "frameId") != page.download_frame) continue;
    const auto guid = begin.at("guid").as_string();
    if (!safe_download_id(guid)) throw std::runtime_error("Chromium returned an invalid download GUID");
    if (page.downloads.contains(guid)) continue;
    if (page.downloads.size() >= 4096)
      throw std::runtime_error("Chromium page download record budget exceeded");
    Json info{{"downloadId", guid}, {"guid", guid}, {"url", begin.at("url")},
      {"suggestedFilename", begin.at("suggestedFilename")}, {"state", "inProgress"},
      {"receivedBytes", 0}, {"totalBytes", 0}, {"path", Json{}}};
    if (!page.download_directory.empty())
      info["_localPath"] = (std::filesystem::path(page.download_directory) / guid).string();
    page.downloads.emplace(guid, std::move(info));
  }
  const auto progresses = runtime.browser_events("Browser.downloadProgress");
  for (const auto& event : progresses.as_array()) {
    const auto& progress = event.at("params");
    const auto found = page.downloads.find(progress.at("guid").as_string());
    if (found == page.downloads.end()) continue;
    auto& info = found->second;
    info["state"] = progress.at("state");
    for (const auto* key : {"receivedBytes", "totalBytes"})
      if (progress.contains(key)) info[key] = progress.at(key);
    if (text(info, "state") == "completed" && info.contains("_localPath")) {
      std::error_code error;
      if (std::filesystem::is_regular_file(info.at("_localPath").as_string(), error) && !error)
        info["path"] = info.at("_localPath");
    }
  }
}
Json public_download(const Json& record) {
  auto result = record;
  result.as_object().erase("_localPath");
  return result;
}
std::filesystem::path absolute_path(const std::string& path, const char* description) {
  if (path.empty() || path.size() > 4096 || path.find('\0') != std::string::npos ||
      !std::filesystem::path(path).is_absolute())
    throw std::invalid_argument(std::string(description) + " must be an absolute filesystem path");
  return std::filesystem::path(path).lexically_normal();
}
Json download_command(CdpRuntime& runtime, CdpPage& page, const Json& fields, Deadline deadline) {
  const auto action = fields.at("action").as_string();
  if (action == "setDirectory" || action == "configure") {
    const auto directory = absolute_path(fields.at("path").as_string(), "Download directory");
    std::filesystem::create_directories(directory);
    const auto canonical = std::filesystem::canonical(directory).string();
    // Preserve GUID -> directory ownership before changing a context-wide policy.
    for (auto& [id, candidate] : runtime.pages) {
      (void)id;
      if (candidate.context == page.context) sync_downloads(runtime, candidate, deadline);
    }
    Json params{{"behavior", "allowAndName"}, {"downloadPath", canonical}, {"eventsEnabled", true}};
    if (page.context) params["browserContextId"] = runtime.contexts.at(page.context);
    check_deadline(deadline);
    runtime.wire->call("Browser.setDownloadBehavior", std::move(params), "", deadline);
    for (auto& [id, candidate] : runtime.pages) {
      (void)id;
      if (candidate.context == page.context) candidate.download_directory = canonical;
    }
    return Json{{"path", canonical}};
  }
  sync_downloads(runtime, page, deadline);
  if (action == "list" || (action == "status" && !fields.contains("downloadId"))) {
    Json::array result;
    for (const auto& [id, info] : page.downloads) { (void)id; result.push_back(public_download(info)); }
    return Json(std::move(result));
  }
  const auto id = fields.at("downloadId").as_string();
  auto found = page.downloads.find(id);
  if (found == page.downloads.end()) throw std::invalid_argument("Unknown download for this page: " + id);
  if (action == "status") return public_download(found->second);
  if (action == "cancel") {
    Json params{{"guid", id}};
    if (page.context) params["browserContextId"] = runtime.contexts.at(page.context);
    check_deadline(deadline);
    return runtime.wire->call("Browser.cancelDownload", std::move(params), "", deadline);
  }
  if (action != "path" && action != "saveAs")
    throw std::invalid_argument("Unsupported Chromium download action: " + action);
  for (;;) {
    check_deadline(deadline);
    sync_downloads(runtime, page, deadline);
    auto& info = page.downloads.at(id);
    if (text(info, "state") == "canceled") throw std::runtime_error("Chromium download was canceled");
    if (text(info, "state") == "completed") {
      if (!info.at("path").is_string())
        throw std::runtime_error("Completed download file is unavailable in the configured download directory");
      const auto source = info.at("path").as_string();
      if (action == "path") return Json{{"path", source}};
      const auto destination = absolute_path(fields.at("path").as_string(), "Download destination");
      const auto canonical_source = std::filesystem::canonical(source);
      if (std::filesystem::exists(destination) && std::filesystem::equivalent(canonical_source, destination))
        return Json{{"path", destination.string()}};
      // Copy in bounded chunks so cancellation/deadline stops file work promptly.
      std::ifstream input(canonical_source, std::ios::binary);
      std::ofstream output(destination, std::ios::binary | std::ios::trunc);
      if (!input || !output) throw std::runtime_error("Cannot open download source or destination");
      char buffer[65536];
      while (input) {
        check_deadline(deadline);
        input.read(buffer, sizeof buffer);
        output.write(buffer, input.gcount());
        if (!output) throw std::runtime_error("Cannot write download destination");
      }
      if (!input.eof()) throw std::runtime_error("Cannot read completed download");
      output.close();
      if (!output) throw std::runtime_error("Cannot finish download destination");
      return Json{{"path", destination.string()}};
    }
    pause(deadline);
  }
}
Json emulate(CdpRuntime& runtime, CdpPage& page, const Json& fields, Deadline deadline) {
  const auto action = text(fields, "action", "media");
  std::set<std::string> allowed{"command", "action", "pageId", "id", "version", "timeoutMs", "api", "framePath"};
  std::string method;
  Json params = Json::object{};
  auto proposed_media = page.emulated_media;
  if (action == "media") {
    allowed.insert({"media", "colorScheme", "reducedMotion", "forcedColors", "contrast"});
    // Omitted options retain their previous value; null clears only that
    // option, matching the existing native CEF media contract.
    for (const auto* key : {"media", "colorScheme", "reducedMotion", "forcedColors", "contrast"}) {
      if (!fields.contains(key)) continue;
      if (fields.at(key).is_null()) proposed_media.as_object().erase(key);
      else proposed_media[key] = fields.at(key).as_string();
    }
    const auto media = text(proposed_media, "media");
    if (media != "" && media != "screen" && media != "print")
      throw std::invalid_argument("Emulated media must be screen, print or empty");
    params["media"] = media;
    Json::array features;
    for (const auto* key : {"colorScheme", "reducedMotion", "forcedColors", "contrast"}) {
      if (!proposed_media.contains(key)) continue;
      const auto value = proposed_media.at(key).as_string();
      const std::string name = key;
      bool valid = name == "colorScheme" ? value == "light" || value == "dark" || value == "no-preference" :
          name == "reducedMotion" ? value == "reduce" || value == "no-preference" :
          name == "forcedColors" ? value == "active" || value == "none" :
          value == "more" || value == "no-preference";
      if (!valid) throw std::invalid_argument("Invalid emulated media feature: " + name);
      features.emplace_back(Json{{"name", name == "colorScheme" ? "prefers-color-scheme" :
          name == "reducedMotion" ? "prefers-reduced-motion" : name == "forcedColors" ? "forced-colors" : "prefers-contrast"},
          {"value", value}});
    }
    params["features"] = Json(std::move(features));
    method = "Emulation.setEmulatedMedia";
  } else if (action == "geolocation") {
    allowed.insert({"latitude", "longitude", "accuracy"});
    const auto latitude = number(fields, "latitude"), longitude = number(fields, "longitude");
    const auto accuracy = fields.contains("accuracy") ? number(fields, "accuracy") : 0;
    if (latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180 || accuracy < 0)
      throw std::invalid_argument("Invalid emulated geolocation coordinates or accuracy");
    params = Json{{"latitude", latitude}, {"longitude", longitude}, {"accuracy", accuracy}};
    method = "Emulation.setGeolocationOverride";
  } else if (action == "clearGeolocation") {
    method = "Emulation.clearGeolocationOverride";
  } else if (action == "timezone") {
    allowed.insert("timezoneId");
    const auto zone = fields.at("timezoneId").as_string();
    if (zone.size() > 1024) throw std::invalid_argument("Emulated timezone ID is too long");
    params["timezoneId"] = zone;
    method = "Emulation.setTimezoneOverride";
  } else if (action == "userAgent") {
    allowed.insert({"userAgent", "acceptLanguage", "platform"});
    for (const auto* key : {"userAgent", "acceptLanguage", "platform"}) {
      if (!fields.contains(key)) continue;
      const auto value = fields.at(key).as_string();
      if (value.size() > 8192) throw std::invalid_argument(std::string("Emulated ") + key + " is too long");
      params[key] = value;
    }
    if (!params.contains("userAgent")) throw std::invalid_argument("Emulation requires userAgent");
    method = "Emulation.setUserAgentOverride";
  } else throw std::invalid_argument("Unsupported Chromium emulation action: " + action);
  for (const auto& [key, value] : fields.as_object()) {
    (void)value;
    if (!allowed.contains(key)) throw std::invalid_argument("Unsupported Chromium emulation field: " + key);
  }
  auto result = runtime.call(page, method, std::move(params), deadline);
  if (action == "media") page.emulated_media = std::move(proposed_media);
  return result;
}
} // namespace

Json cdp_page_command(CdpRuntime& runtime, CdpPage& page, const Json& fields, Deadline deadline) {
  check_deadline(deadline);
  auto frame_scope = cdp_resolve_frame(runtime, page, fields, deadline);
  const auto command = fields.at("command").as_string();
  if (command == "evaluate")
    return runtime.evaluate(page, fields.at("expression").as_string(), deadline, flag(fields, "isolated"));
  if (command == "navigateFrame" || command == "navigate") return navigate(runtime, page, fields, deadline);
  if (command == "waitLoadState" || command == "waitForURL" || command == "waitForFunction" ||
      command == "waitForSelector" || command == "waitForText")
    return wait_condition(runtime, page, fields, deadline);
  main_page(page);
  if (command == "emulation") return emulate(runtime, page, fields, deadline);
  if (command == "download") return download_command(runtime, page, fields, deadline);
  if (command == "reload" || command == "historyBack" || command == "historyForward")
    return history_navigation(runtime, page, fields, deadline);
  if (command == "navigationHistory")
    return runtime.call(page, "Page.getNavigationHistory", Json::object{}, deadline);
  if (command == "viewport") {
    const auto width = fields.at("width").as_int(), height = fields.at("height").as_int();
    if (width < 1 || height < 1 || width > 32768 || height > 32768)
      throw std::invalid_argument("Viewport dimensions must be positive integers no greater than 32768");
    return runtime.call(page, "Emulation.setDeviceMetricsOverride",
        Json{{"width", width}, {"height", height}, {"deviceScaleFactor", 1}, {"mobile", false}}, deadline);
  }
  if (command == "bringToFront")
    return runtime.call(page, "Page.bringToFront", Json::object{}, deadline);
  if (command == "frameSubscription")
    throw std::invalid_argument("Chromium frame subscriptions are unsupported");
  if (command == "artifact") {
    const auto action = fields.at("action").as_string();
    if (action == "screenshot") return screenshot(runtime, page, fields, deadline);
    if (action == "pdf") return pdf(runtime, page, fields, deadline);
    throw std::invalid_argument("Unsupported Chromium artifact action: " + action);
  }
  throw std::invalid_argument("Unsupported Chromium page command: " + command);
}
} // namespace suitecut::detail
