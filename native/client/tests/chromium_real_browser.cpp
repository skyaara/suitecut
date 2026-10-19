// SPDX-License-Identifier: MIT
// Standalone public-API proof against an installed, unmodified Chrome/Chromium.
#include <suitecut/browser.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using suitecut::Json;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

const char* fixture = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<title>Native Chromium λ 漢字</title>
<style>body{font:16px sans-serif;margin:20px;min-height:1800px}label,button,select{display:block;margin:8px}
#moving,#trap{position:absolute;left:80px;top:400px;width:170px;height:55px}#trap{z-index:1}#moving{z-index:2}
#overlay{position:fixed;inset:0;z-index:100;background:#ddd}</style>
<label>Name<input id="name" placeholder="Your name"></label>
<label><input id="consent" type="checkbox">Consent</label>
<label>Region<select id="region"><option value="west">West</option><option value="east">East</option></select></label>
<label>Colors<select id="colors" multiple><option value="r">Red</option><option value="b">Blue</option></select></label>
<button id="commit" data-testid="commit">Commit transaction</button><output id="status">Waiting</output>
<div id="shadow"></div><div class="item">first</div><div class="item">second</div>
<button id="trap">Wrong target</button><button id="moving">Moving target</button>
<script>
window.clicks=0;window.lastTrusted=false;window.queuedEffects=0;
window.race={moves:0,downs:0,clicks:0,traps:0,overlayDowns:0};
document.querySelector('#commit').onclick=e=>{clicks++;lastTrusted=e.isTrusted;document.querySelector('#status').textContent='Saved '+document.querySelector('#name').value};
const shadow=document.querySelector('#shadow').attachShadow({mode:'open'});
shadow.innerHTML='<button id="shadow-button" aria-label="Shadow action">Shadow action</button>';
shadow.querySelector('button').onclick=e=>{window.shadowTrusted=e.isTrusted};
const mode=new URL(location.href).hash.slice(1),moving=document.querySelector('#moving');
document.querySelector('#trap').onmousedown=()=>race.traps++;
moving.onmousedown=()=>race.downs++;moving.onclick=e=>{race.clicks++;race.trusted=e.isTrusted};
moving.onmousemove=()=>{race.moves++;if(mode==='shift'&&race.moves===1){moving.style.left='600px';moving.style.top='500px'}
if(mode==='overlay'&&!document.querySelector('#overlay')){const cover=document.createElement('div');cover.id='overlay';cover.onmousedown=()=>race.overlayDowns++;document.body.append(cover)}};
</script></html>)HTML";

std::string DataUrl(const std::string& html) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result = "data:text/html;charset=utf-8,";
  for (unsigned char c : html) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
      result.push_back(static_cast<char>(c));
    else { result.push_back('%'); result.push_back(hex[c >> 4]); result.push_back(hex[c & 15]); }
  }
  return result;
}
void Check(bool condition, const std::string& name, Json& report) {
  if (!condition) throw std::runtime_error("Check failed: " + name);
  report["checks"].as_array().emplace_back(name);
  std::cout << Json{{"event", "check"}, {"name", name}}.dump() << std::endl;
}
template<class Operation>
void Reject(Operation operation, const std::string& name, Json& report,
            const std::string& required = {}) {
  std::string error;
  try { operation(); } catch (const std::exception& caught) { error = caught.what(); }
  Check(!error.empty() && (required.empty() || error.find(required) != std::string::npos), name, report);
}
std::string FindRef(const Json& value, const std::string& name) {
  if (value.is_array()) {
    for (const auto& child : value.as_array()) {
      auto result = FindRef(child, name); if (!result.empty()) return result;
    }
  } else if (value.is_object()) {
    if (value.contains("role") && value.contains("name") && value.contains("ref") &&
        value.at("role") == Json("button") && value.at("name") == Json(name))
      return value.at("ref").as_string();
    if (value.contains("children")) return FindRef(value.at("children"), name);
  }
  return {};
}
std::uint32_t BigEndian(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) throw std::runtime_error("Truncated PNG header");
  return (std::uint32_t(bytes[offset]) << 24) | (std::uint32_t(bytes[offset + 1]) << 16) |
         (std::uint32_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
}
bool IsPng(const std::vector<std::uint8_t>& bytes) {
  const std::vector<std::uint8_t> signature{137, 80, 78, 71, 13, 10, 26, 10};
  return bytes.size() > 100 && std::equal(signature.begin(), signature.end(), bytes.begin()) &&
         std::string(bytes.begin() + 12, bytes.begin() + 16) == "IHDR";
}
void RecordPng(Json& report, const std::string& name, const std::vector<std::uint8_t>& bytes,
               int expected_width, int expected_height, bool minimum_height = false) {
  const bool valid = IsPng(bytes);
  Json info{{"bytes", bytes.size()}, {"validPng", valid},
            {"width", valid ? Json(BigEndian(bytes, 16)) : Json()},
            {"height", valid ? Json(BigEndian(bytes, 20)) : Json()},
            {"expectedWidth", expected_width},
            {minimum_height ? "minimumHeight" : "expectedHeight", expected_height}};
  report["screenshots"][name] = info;
  std::cout << Json{{"event", "screenshot"}, {"name", name}, {"dimensions", info}}.dump() << std::endl;
}
void SaveBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!stream) throw std::runtime_error("Cannot save " + path.string());
}
bool HasCookie(const Json& result, const std::string& name) {
  for (const auto& cookie : result.at("cookies").as_array())
    if (cookie.at("name").as_string() == name) return true;
  return false;
}
} // namespace

int main(int argc, char** argv) {
  std::string executable, output = "chromium-results.json";
  std::vector<std::string> browser_arguments;
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 == argc) { std::cerr << "Expected --browser/--output/--browser-arg value\n"; return 2; }
    const std::string option = argv[i], value = argv[i + 1];
    if (option == "--browser") executable = value;
    else if (option == "--output") output = value;
    else if (option == "--browser-arg") browser_arguments.push_back(value);
    else { std::cerr << "Unknown option: " << option << '\n'; return 2; }
  }
  if (executable.empty()) { std::cerr << "--browser /absolute/path/to/chrome is required\n"; return 2; }
  Json report{{"client", "native-cpp"}, {"backend", "chromium"}, {"compiler", __VERSION__},
              {"passed", false}, {"checks", Json::array{}}, {"screenshots", Json::object{}},
              {"scope", "Launched external Chrome over private CDP pipe; no Node or CEF runtime."}};
  std::optional<suitecut::Browser> browser;
  const auto started = Clock::now();
  int status = 0;
  try {
    const auto directory = std::filesystem::absolute(output).parent_path();
    std::filesystem::create_directories(directory);
    suitecut::LaunchOptions options;
    options.executable = std::filesystem::absolute(executable).string();
    options.arguments = browser_arguments;
    options.width = 1280; options.height = 720; options.headless = true;
    options.startup_timeout = 15s; options.shutdown_timeout = 3s;
    browser.emplace(suitecut::Browser::launch_chromium(options));
    report["ready"] = browser->ready();
    Check(browser->ready().at("backend") == Json("chromium") && !browser->version().empty(),
          "ready metadata identifies external Chromium", report);
    auto page = browser->main_page();
    page.set_timeout(3s);
    const auto url = DataUrl(fixture);
    page.navigate(url);
    Check(page.evaluate("document.title").as_string() == "Native Chromium λ 漢字",
          "navigation waits for the actual document", report);
    Check(page.evaluate("Promise.resolve({answer:42,text:'λ 漢字 😀'})") ==
          Json{{"answer", 42}, {"text", "λ 漢字 😀"}}, "Promise and Unicode evaluation", report);
    Reject([&] { page.evaluate("(()=>{throw new Error('intentional-js-error')})()"); },
           "JavaScript exceptions are surfaced", report, "intentional-js-error");
    const auto raw = suitecut::response_json(page.evaluate_raw("NaN"));
    Check(raw.at("result").at("unserializableValue") == Json("NaN"),
          "raw evaluation preserves CDP metadata", report);
    page.get_by_label("Name").fill("Native λ 漢字 😀");
    Check(page.get_by_placeholder("Your name").input_value() == "Native λ 漢字 😀",
          "label locator fills Unicode with Chromium editing", report);
    page.get_by_role("button", "Commit transaction").click();
    Check(page.get_by_test_id("commit").inner_text() == "Commit transaction" &&
          page.locator("#status").text_content() == Json("Saved Native λ 漢字 😀") &&
          page.evaluate("lastTrusted&&clicks===1").as_bool(), "trusted click and form outcome", report);
    page.get_by_role("checkbox", "Consent").check();
    Check(page.evaluate("document.querySelector('#consent').checked").as_bool(), "checkbox check", report);
    page.get_by_role("checkbox", "Consent").uncheck();
    Check(!page.evaluate("document.querySelector('#consent').checked").as_bool(), "checkbox uncheck", report);
    page.get_by_label("Region").select_option("east");
    Check(page.locator("#region").input_value() == "east", "select option by value", report);
    page.get_by_label("Region").select_options(Json::array{Json{{"label", "West"}}});
    Check(page.locator("#region").input_value() == "west", "select option by explicit label", report);
    Check(page.get_by_label("Colors").select_options(std::vector<std::string>{"Red", "b"}) ==
          Json(Json::array{"r", "b"}), "multiple select by label and value", report);
    Check(page.locator(".item").count() == 2 && page.locator(".item").nth(1).inner_text() == "second",
          "CSS and nth locators", report);
    page.get_by_role("button", "Shadow action").click();
    Check(page.evaluate("window.shadowTrusted===true").as_bool() &&
          page.locator("#shadow-button").attribute("aria-label") == Json("Shadow action"),
          "role and CSS queries pierce open shadow roots", report);
    page.get_by_label("Name").focus();
    page.keyboard_press("Control+A"); page.insert_text("Inserted Ω 😀");
    Check(page.locator("#name").input_value() == "Inserted Ω 😀", "key chord and Unicode insert", report);
    page.locator("#name").press("End"); page.locator("#name").type("!");
    Check(page.locator("#name").input_value() == "Inserted Ω 😀!", "locator press and type", report);

    const auto reference = FindRef(page.observe(true), "Commit transaction");
    Check(!reference.empty(), "semantic observation issues a real element reference", report);
    page.get_by_ref(reference).click();
    Check(page.evaluate("clicks===2&&lastTrusted").as_bool(), "observed reference drives trusted input", report);
    page.evaluate("document.querySelector('#commit').replaceWith(document.querySelector('#commit').cloneNode(true))");
    Reject([&] { page.get_by_ref(reference).with_timeout(200ms).click(); },
           "replacement invalidates the old element reference", report, "stale");
    Check(page.evaluate("clicks===2").as_bool(), "stale reference sends no click", report);
    const auto navigation_ref = FindRef(page.observe(), "Commit transaction");
    page.navigate("about:blank"); page.navigate(url);
    Reject([&] { page.get_by_ref(navigation_ref).with_timeout(200ms).hover(); },
           "navigation invalidates references including hover", report, "stale");
    Check(page.evaluate("clicks===0").as_bool(), "navigation reference cannot affect the replacement document", report);

    const auto png = page.screenshot();
    SaveBytes(directory / "chromium-viewport.png", png);
    RecordPng(report, "viewport", png, 1280, 720);
    Check(IsPng(png) && BigEndian(png, 16) == 1280 && BigEndian(png, 20) == 720,
          "PNG screenshot has actual viewport dimensions", report);
    const auto full = page.screenshot(Json{{"fullPage", true}});
    RecordPng(report, "fullPage", full, 1280, 1800, true);
    Check(IsPng(full) && BigEndian(full, 16) == 1280 && BigEndian(full, 20) >= 1800,
          "full-page screenshot includes offscreen document", report);
    const auto clip = page.screenshot(Json{{"clip", Json{{"x", 20}, {"y", 30}, {"width", 120}, {"height", 90}}}});
    RecordPng(report, "clip", clip, 120, 90);
    Check(IsPng(clip) && BigEndian(clip, 16) == 120 && BigEndian(clip, 20) == 90,
          "screenshot clip has requested dimensions", report);
    const auto jpeg = page.screenshot(Json{{"format", "jpeg"}, {"quality", 75}});
    Check(jpeg.size() > 100 && jpeg[0] == 255 && jpeg[1] == 216 && jpeg[2] == 255,
          "JPEG screenshot has JPEG bytes", report);
    const auto webp = page.screenshot(Json{{"format", "webp"}, {"quality", 80}});
    Check(webp.size() > 100 && std::string(webp.begin(), webp.begin() + 4) == "RIFF" &&
          std::string(webp.begin() + 8, webp.begin() + 12) == "WEBP", "WebP screenshot has WebP bytes", report);
    page.set_viewport(800, 600);
    Check(page.evaluate("innerWidth===800&&innerHeight===600").as_bool(), "viewport changes affect page geometry", report);
    const auto resized = page.screenshot();
    RecordPng(report, "resized", resized, 800, 600);
    Check(IsPng(resized) && BigEndian(resized, 16) == 800 && BigEndian(resized, 20) == 600,
          "viewport changes affect screenshot dimensions", report);
    page.set_viewport(1280, 720);
    page.evaluate("(()=>{const e=document.createElement('div');e.id='mask-proof';e.style.cssText='position:fixed;left:1000px;top:20px;width:80px;height:80px;background:rgb(255,0,0)';document.body.append(e)})()");
    const auto nodes_before_mask = page.evaluate("document.querySelectorAll('*').length");
    const Json mask_options{{"mask", Json::array{"#mask-proof"}}, {"maskColor", "#00FF00"},
      {"style", "#mask-proof{background:rgb(0,0,255)!important}"},
      {"clip", Json{{"x", 1000}, {"y", 20}, {"width", 80}, {"height", 80}}}};
    const auto masked = suitecut::response_json(page.screenshot_raw(mask_options)).at("data").as_string();
    const auto mask_pixel = page.evaluate("new Promise((resolve,reject)=>{const image=new Image;image.onload=()=>{const canvas=document.createElement('canvas');canvas.width=image.width;canvas.height=image.height;const context=canvas.getContext('2d');context.drawImage(image,0,0);resolve(Array.from(context.getImageData(40,40,1,1).data))};image.onerror=()=>reject(Error('PNG decode failed'));image.src=" + Json("data:image/png;base64," + masked).dump() + "})");
    report["maskCenterRgba"] = mask_pixel;
    Check(mask_pixel == Json(Json::array{0, 255, 0, 255}), "screenshot mask paints actual decoded PNG pixels", report);
    Check(page.evaluate("document.querySelectorAll('*').length") == nodes_before_mask &&
          page.evaluate("document.querySelectorAll('[data-suitecut-capture]').length===0").as_bool() &&
          page.evaluate("getComputedStyle(document.querySelector('#mask-proof')).backgroundColor") == Json("rgb(255, 0, 0)"),
          "screenshot mask and custom style are removed after capture", report);
    const auto animation_before = page.evaluate("(()=>{const e=document.querySelector('#mask-proof');window.finiteProof=e.animate([{opacity:0},{opacity:1}],{duration:1000,fill:'both'});window.infiniteProof=e.animate([{transform:'translateX(0px)'},{transform:'translateX(1px)'}],{duration:1000,iterations:Infinity});finiteProof.pause();infiniteProof.pause();return Promise.all([finiteProof.ready,infiniteProof.ready]).then(()=>{finiteProof.currentTime=250;infiniteProof.currentTime=333;return [finiteProof.playState,finiteProof.currentTime,infiniteProof.playState,infiniteProof.currentTime]})})()");
    const auto frozen = page.screenshot(Json{{"animations", "disabled"}});
    Check(IsPng(frozen) && page.evaluate("[finiteProof.playState,finiteProof.currentTime,infiniteProof.playState,infiniteProof.currentTime]") == animation_before,
          "disabled-animation screenshot restores finite and infinite animation state", report);
    page.evaluate("finiteProof.cancel();infiniteProof.cancel();document.querySelector('#mask-proof').remove()");
    Check(page.devtools("Page.getFrameTree").at("frameTree").at("frame").contains("id"),
          "raw DevTools returns actual frame metadata", report);

    auto context_a = browser->new_context(), context_b = browser->new_context();
    auto isolated_a = context_a.new_page(), isolated_b = context_b.new_page();
    isolated_a.navigate(url); isolated_b.navigate(url);
    isolated_a.evaluate("window.contextMarker='a'");
    Check(isolated_b.evaluate("typeof window.contextMarker").as_string() == "undefined" &&
          page.evaluate("typeof window.contextMarker").as_string() == "undefined", "context pages have independent document globals", report);
    context_a.command("addCookies", Json{{"cookies", Json::array{Json{{"name", "native-proof"},
      {"value", "a"}, {"url", "https://suitecut.invalid/"}}}}});
    Check(HasCookie(context_a.command("cookies"), "native-proof") &&
          !HasCookie(context_b.command("cookies"), "native-proof"), "browser contexts isolate cookie storage", report);
    isolated_a.close();
    Reject([&] { isolated_a.evaluate("1"); }, "closed page rejects use", report, "closed");
    context_a.close(); context_b.close();
    Reject([&] { isolated_b.evaluate("1"); }, "closed context invalidates its pages", report, "closed");
    Check(page.evaluate("document.title").as_string() == "Native Chromium λ 漢字", "context teardown preserves main page", report);

    const auto transport = browser->transport();
    const auto evaluate = [&](const std::string& expression, suitecut::Timeout timeout) {
      return transport->submit_json(Json{{"command", "evaluate"}, {"pageId", page.id()}, {"expression", expression}}, timeout);
    };
    auto confirmation = evaluate("confirm('native dialog proof')", 3s);
    const auto dialog_deadline = Clock::now() + 1s;
    bool dialog_opened = false;
    while (Clock::now() < dialog_deadline && !dialog_opened) {
      const auto event = transport->poll(10ms);
      if (!event) continue;
      const auto body = suitecut::response_json(*event);
      if (body.is_object() && body.contains("method") &&
          body.at("method") == Json("Page.javascriptDialogOpening")) {
        report["dialogEvent"] = body;
        dialog_opened = body.contains("params") &&
          body.at("params").at("message") == Json("native dialog proof") &&
          body.at("params").at("type") == Json("confirm");
      }
    }
    Check(dialog_opened, "raw CDP event polling surfaces an actual confirm dialog", report);
    page.devtools("Page.handleJavaScriptDialog", Json{{"accept", true}});
    Check(confirmation.result.wait_for(1s) == std::future_status::ready &&
          suitecut::response_json(confirmation.result.get()).at("result").at("value") == Json(true),
          "DevTools continuation accepts a modal while its evaluation is pending", report);
    std::vector<suitecut::Request> requests;
    for (int index = 0; index < 10; ++index) requests.push_back(evaluate(std::to_string(index) + "+42", 3s));
    bool values_match = true;
    for (int index = 0; index < 10; ++index)
      values_match = suitecut::response_json(requests[index].result.get()).at("result").at("value").as_int() == index + 42 && values_match;
    Check(values_match, "ten simultaneous pending queries retain their own results", report);
    page.get_by_label("Name").focus();
    auto slow = evaluate("new Promise(resolve=>setTimeout(()=>resolve(7),700))", 2s);
    // The first request occupies the serial action queue. Expiry and cancellation
    // must settle later futures without waiting for it or dispatching their work.
    auto expired = evaluate("++window.queuedEffects", 70ms);
    const auto deadline_started = Clock::now();
    Check(expired.result.wait_for(350ms) == std::future_status::ready,
          "shorter queued deadline settles independently", report);
    Reject([&] { (void)suitecut::response_json(expired.result.get()); },
           "queued request reports its original timeout", report, "timed out");
    report["queuedTimeoutMs"] = std::chrono::duration<double, std::milli>(Clock::now() - deadline_started).count();
    auto cancelled_input = transport->submit_json(Json{{"command", "input"}, {"pageId", page.id()},
      {"kind", "keyboard"}, {"action", "insertText"}, {"text", "must-not-arrive"}}, 2s);
    transport->cancel(cancelled_input.id);
    transport->cancel(slow.id);
    Check(slow.result.wait_for(200ms) == std::future_status::ready &&
          cancelled_input.result.wait_for(200ms) == std::future_status::ready,
          "explicit cancellation settles active and queued futures", report);
    Reject([&] { (void)suitecut::response_json(slow.result.get()); }, "slow Promise reports cancellation", report, "cancel");
    Reject([&] { (void)suitecut::response_json(cancelled_input.result.get()); }, "queued input reports cancellation", report, "cancel");
    Check(page.evaluate("queuedEffects===0&&document.querySelector('#name').value===''").as_bool(),
          "expired queries and cancelled input are never dispatched or retried", report);
    Check(page.evaluate("40+2").as_int() == 42, "transport remains usable after deadline and cancellation", report);

    page.navigate("about:blank"); page.navigate(url + "#shift");
    page.get_by_role("button", "Moving target").click();
    Check(page.evaluate("race.clicks===1&&race.downs===1&&race.traps===0&&race.trusted===true").as_bool(),
          "hover movement revalidates target before trusted pointer down", report);
    page.navigate("about:blank"); page.navigate(url + "#overlay");
    Reject([&] { page.get_by_role("button", "Moving target").with_timeout(250ms).click(); },
           "hover-created overlay blocks the click", report);
    Check(page.evaluate("race.downs===0&&race.overlayDowns===0&&race.traps===0&&race.clicks===0").as_bool(),
          "blocked click sends no input to overlay or background", report);
    Reject([&] { page.command("route", Json{{"action", "enable"}}); },
           "unimplemented generic engine family fails explicitly", report, "Unsupported");
    // Both "unsupported" and "not supported" describe the backend capability.
    Reject([&] { page.subscribe_frames(); }, "CEF frame streaming fails explicitly", report, "support");
    Reject([&] { page.screenshot(Json{{"animations", "invalid"}}); },
           "invalid screenshot animation option fails explicitly", report, "must be");
    const auto stats = transport->stats();
    report["transportStatsBeforeClose"] = Json{{"commands", stats.commands}, {"responses", stats.responses},
      {"pending", stats.pending}, {"queuedEventBytes", stats.queued_event_bytes},
      {"droppedEvents", stats.dropped_events}};
    // A long dispatched input delay must be interrupted by close, rather than
    // keeping the worker alive until the caller-supplied delay elapses.
    auto delayed_input = transport->submit_json(Json{{"command", "input"}, {"pageId", page.id()},
      {"kind", "keyboard"}, {"action", "press"}, {"key", "Shift"}, {"delay", 120000}}, 130s);
    std::this_thread::sleep_for(50ms);
    const auto closing = Clock::now();
    browser->close();
    const auto close_elapsed = Clock::now() - closing;
    report["closeWithDelayedInputMs"] = std::chrono::duration<double, std::milli>(close_elapsed).count();
    Check(close_elapsed < options.shutdown_timeout + 1s, "close interrupts a dispatched long input delay within its shutdown budget", report);
    Check(delayed_input.result.wait_for(200ms) == std::future_status::ready,
          "close settles the delayed input future", report);
    std::string delayed_error;
    try { (void)suitecut::response_json(delayed_input.result.get()); }
    catch (const std::exception& error) { delayed_error = error.what(); }
    Check(delayed_error.find("closed") != std::string::npos || delayed_error.find("cancel") != std::string::npos,
          "interrupted input reports close or cancellation", report);
    Reject([&] { page.evaluate("1"); }, "browser close invalidates retained page views", report);
    report["passed"] = true;
  } catch (const std::exception& error) {
    report["error"] = error.what(); status = 1;
  }
  if (browser) {
    try { browser->close(); }
    catch (const std::exception& error) { report["closeError"] = error.what(); report["passed"] = false; status = 1; }
  }
  report["elapsedMs"] = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
  std::ofstream stream(output); stream << report.dump() << '\n';
  if (!stream) { std::cerr << "Cannot save report: " << output << '\n'; return 1; }
  std::cout << report.dump() << std::endl;
  return status;
}
