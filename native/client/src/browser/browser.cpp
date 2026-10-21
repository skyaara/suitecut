// SPDX-License-Identifier: MIT
#include "suitecut/browser.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace suitecut {
namespace {
void object(const Json& value) {
  if (!value.is_object())
    throw std::invalid_argument("Native command fields must be a JSON object");
}
void valid_timeout(Timeout timeout) {
  if (timeout.count() <= 0) throw std::invalid_argument("Native client timeout must be positive");
}
void live(const std::shared_ptr<Transport>& transport, int page) {
  if (!transport || page <= 0)
    throw std::runtime_error("Native browser resource is closed or moved");
}
Packet send(const std::shared_ptr<Transport>& transport, int page, const std::string& family,
            Json fields, Timeout timeout) {
  live(transport, page);
  object(fields);
  valid_timeout(timeout);
  fields["command"] = family;
  fields["pageId"] = page;
  fields["timeoutMs"] = timeout.count();
  return transport->call_json(std::move(fields), timeout);
}
Json context_call(const std::shared_ptr<Transport>& transport, const std::string& action,
                  Json params, Timeout timeout) {
  object(params);
  return response_json(send(transport, 1, "context",
                            Json{{"action", action}, {"params", std::move(params)}}, timeout));
}
int default_context(const std::shared_ptr<Transport>& transport) {
  live(transport,1);
  const auto ready = Json::parse(transport->ready().header);
  return ready.contains("backend") && ready.at("backend") != Json("cef") ? 0 : 1;
}
int resource_id(const Json& value, const char* field) {
  const auto id = value.at(field).as_int();
  if (id <= 0 || id > std::numeric_limits<int>::max())
    throw std::runtime_error("Invalid native resource identity");
  return static_cast<int>(id);
}
int new_page_id(const std::shared_ptr<Transport>& transport, int context, Timeout timeout) {
  valid_timeout(timeout);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  const auto reply = context_call(transport, "newPage", Json{{"contextId", context}}, timeout);
  const int page = resource_id(reply, "pageId");
  try {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) throw std::runtime_error("Native page creation original deadline expired");
    transport->wait_ready(page, std::chrono::ceil<Timeout>(deadline - now));
  } catch (...) {
    try { context_call(transport, "closePage", Json{{"pageId", page}}, Timeout{5000}); } catch (...) {}
    throw;
  }
  return page;
}
Json evaluated(Json result) {
  if (result.contains("exceptionDetails"))
    throw std::runtime_error("Native JavaScript evaluation failed: " +
                             result.at("exceptionDetails").dump());
  auto& remote = result.at("result");
  if (remote.contains("unserializableValue"))
    throw std::runtime_error("JavaScript value is not JSON-compatible; use evaluate_raw");
  if (remote.contains("value")) return std::move(remote.at("value"));
  if (remote.contains("type") && remote.at("type").as_string() == "undefined") return Json{};
  if (remote.contains("subtype") && remote.at("subtype").as_string() == "null") return Json{};
  throw std::runtime_error(
      "JavaScript result has no JSON value; use evaluate_raw for remote objects");
}
Json scoped(std::string engine, const std::string& text, bool exact) {
  return Json::array{Json{{"engine", "css"}, {"selector", ":scope"}},
                     Json{{"engine", std::move(engine)}, {"text", text}, {"exact", exact}}};
}
std::vector<std::uint8_t> decode_base64(const std::string& text) {
  constexpr std::size_t maximum = 128 * 1024 * 1024;
  if (text.empty() || text.size() % 4 || text.size() > ((maximum + 2) / 3) * 4)
    throw std::runtime_error("Invalid or oversized native artifact encoding");
  const auto digit = [](unsigned char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  const std::size_t padding = (text.back() == '=') + (text[text.size() - 2] == '=');
  const auto length = text.size() / 4 * 3 - padding;
  if (length > maximum) throw std::runtime_error("Native artifact exceeds 128 MiB");
  std::vector<std::uint8_t> bytes(length);
  std::size_t output = 0;
  for (std::size_t i = 0; i < text.size(); i += 4) {
    const int a = digit(text[i]), b = digit(text[i + 1]);
    const bool last = i + 4 == text.size();
    if (a < 0 || b < 0) throw std::runtime_error("Invalid native artifact base64");
    const int c = text[i + 2] == '=' ? -1 : digit(text[i + 2]),
              d = text[i + 3] == '=' ? -1 : digit(text[i + 3]);
    if (c < 0 && (text[i + 2] != '=' || !last || text[i + 3] != '=' || (b & 15)))
      throw std::runtime_error("Invalid native artifact base64 padding");
    if (c >= 0 && d < 0 && (text[i + 3] != '=' || !last || (c & 3)))
      throw std::runtime_error("Invalid native artifact base64 padding");
    bytes.at(output++) = static_cast<std::uint8_t>((a << 2) | (b >> 4));
    if (c >= 0) {
      bytes.at(output++) = static_cast<std::uint8_t>((b << 4) | (c >> 2));
      if (d >= 0) bytes.at(output++) = static_cast<std::uint8_t>((c << 6) | d);
    }
  }
  return bytes;
}
Json download_info(Json info) {
  object(info);
  if (!info.contains("downloadId") && info.contains("id")) info["downloadId"] = info.at("id");
  if (!info.contains("downloadId") || !info.at("downloadId").is_string())
    throw std::runtime_error("Native download response has no identity");
  if (info.contains("state")) {
    const auto state = info.at("state").as_string();
    if (state == "complete") info["state"] = "completed";
    else if (state == "failed" && info.contains("failure") && info.at("failure").is_string() &&
             info.at("failure").as_string() == "canceled") info["state"] = "canceled";
  }
  return info;
}
void emulation_text(const std::string& text, std::size_t maximum, const char* name) {
  if (text.size() > maximum || std::any_of(text.begin(),text.end(),[](unsigned char c){return c<0x20||c==0x7f;}))
    throw std::invalid_argument(std::string("Invalid emulated ") + name);
}
}  // namespace

Json response_json(const Packet& packet) {
  const auto header = Json::parse(packet.header);
  if (header.contains("ok") && !header.at("ok").as_bool())
    throw std::runtime_error(header.contains("error") ? header.at("error").as_string()
                                                      : "Native command failed");
  if (packet.payload.empty()) return Json{};
  return Json::parse(std::string_view(reinterpret_cast<const char*>(packet.payload.data()),
                                      packet.payload.size()));
}

Locator::Locator(std::shared_ptr<Transport> transport, int page, Json query,
                 std::string description, Timeout timeout, std::string frame, Json frame_path)
    : transport_(std::move(transport)),
      page_(page),
      query_(std::move(query)),
      description_(std::move(description)),
      timeout_(timeout), frame_(std::move(frame)), frame_path_(std::move(frame_path)) {}
Json Locator::target(Json fields) const {
  object(fields);
  fields["selector"] = description_;
  fields["query"] = query_;
  fields["framePath"] = frame_path_;
  if (!frame_.empty()) fields["frame"] = frame_;
  fields["strict"] = true;
  return fields;
}
Json Locator::command(const std::string& family, Json fields) const {
  return response_json(send(transport_, page_, family, target(std::move(fields)), timeout_));
}
Json Locator::observe(bool boxes, int depth) const {
  Json fields{{"referenceFormat", "json"}, {"accessibilityBoxes", boxes}};
  if (depth >= 0) fields["referenceDepth"] = depth;
  return Json::parse(command("snapshotReferences", std::move(fields)).at("snapshotJSON").as_string());
}
Locator Locator::nth(int index) const {
  auto query = query_;
  query.as_array().emplace_back(Json{{"engine", "nth"}, {"index", index}});
  return Locator(transport_, page_, std::move(query), description_, timeout_, frame_, frame_path_);
}
Locator Locator::locator(const std::string& css) const {
  auto query = query_;
  if (query.as_array().size() >= 64) throw std::invalid_argument("Native locator exceeds 64 steps");
  query.as_array().emplace_back(Json{{"engine", "css"}, {"selector", css}});
  return Locator(transport_, page_, std::move(query), description_ + " >> " + css, timeout_, frame_, frame_path_);
}
Locator Locator::with_timeout(Timeout timeout) const {
  valid_timeout(timeout);
  return Locator(transport_, page_, query_, description_, timeout, frame_, frame_path_);
}
void Locator::click(Json options) const {
  object(options);
  options["action"] = "click";
  command("locator", std::move(options));
}
void Locator::click(Timeout timeout) const { with_timeout(timeout).click(); }
Json Locator::drag_to(const Locator& target, Json options) const {
  object(options);
  live(transport_, page_);
  live(target.transport_, target.page_);
  if (transport_ != target.transport_ || page_ != target.page_)
    throw std::invalid_argument("Native drag locators must belong to the same page");
  options["action"] = "dragTo";
  Json target_fields{{"query", target.query_}, {"framePath", target.frame_path_}};
  if (!target.frame_.empty()) target_fields["frame"] = target.frame_;
  options["target"] = std::move(target_fields);
  return command("locator", std::move(options));
}
void Locator::fill(const std::string& value) const {
  command("locator", Json{{"action", "fill"}, {"value", value}});
}
void Locator::clear() const { command("locator", Json{{"action", "clear"}}); }
void Locator::press(const std::string& key) const {
  command("locator", Json{{"action", "press"}, {"key", key}, {"delay", 0}});
}
void Locator::type(const std::string& value) const {
  command("locator", Json{{"action", "type"}, {"value", value}, {"delay", 0}});
}
void Locator::focus() const { command("locator", Json{{"action", "focus"}}); }
void Locator::hover() const { command("locator", Json{{"action", "hover"}}); }
void Locator::scroll_into_view() const {
  command("locator", Json{{"action", "scrollIntoViewIfNeeded"}});
}
void Locator::check(bool checked) const {
  command("form",
          Json{{"action", "setChecked"}, {"checked", checked}, {"force", false}, {"trial", false}});
}
Json Locator::select_option(const std::string& value) const {
  return select_options(std::vector<std::string>{value});
}
Json Locator::select_options(const std::vector<std::string>& values) const {
  Json::array choices;
  choices.reserve(values.size());
  for (const auto& value : values) choices.emplace_back(Json{{"valueOrLabel", value}});
  return select_options(Json(std::move(choices)));
}
Json Locator::select_options(Json choices) const {
  if (!choices.is_array())
    throw std::invalid_argument("Select choices must be an array of native option objects");
  return command(
      "form", Json{{"action", "selectOption"}, {"options", std::move(choices)}, {"force", false}});
}
std::int64_t Locator::count() const {
  const auto result = evaluated(command("queryEvaluate", Json{{"count", true}}));
  if (!result.is_array() || result.as_array().size() != 2 ||
      result.at(0).as_string() != "primitive")
    throw std::runtime_error("Invalid native locator count response");
  const auto count = result.at(1).as_int();
  if (count < 0) throw std::runtime_error("Negative native locator count");
  return count;
}
Json Locator::read(const std::string& function) const {
  return evaluated(
      command("queryEvaluate", Json{{"dom", true}, {"domGetter", true}, {"function", function}}));
}
Json Locator::text_content() const {
  return read("function(element){return element.textContent;}");
}
std::string Locator::inner_text() const {
  return read("function(element){return element.innerText;}").as_string();
}
std::string Locator::inner_html() const { return read("function(element){return element.innerHTML;}").as_string(); }
std::string Locator::input_value() const {
  return command("locator", Json{{"action", "inputValue"}}).as_string();
}
Json Locator::attribute(const std::string& name) const {
  return read("function(element){return element.getAttribute(" + Json(name).dump() + ");}");
}
Json Locator::evaluate(const std::string& function) const { return read(function); }
void Locator::wait_for(const std::string& value) const {
  if (value != "attached" && value != "detached" && value != "visible" && value != "hidden")
    throw std::invalid_argument("Native locator wait state must be attached, detached, visible or hidden");
  command("waitFor", Json{{"state", value}});
}
bool Locator::state(const std::string& name) const {
  return evaluated(command("queryEvaluate", Json{{"elementState", true}, {"state", name}})).as_bool();
}
bool Locator::is_visible() const { return state("visible"); }
bool Locator::is_hidden() const { return state("hidden"); }
bool Locator::is_enabled() const { return state("enabled"); }
bool Locator::is_disabled() const { return state("disabled"); }
bool Locator::is_checked() const { return state("checked"); }
bool Locator::is_editable() const { return state("editable"); }
void Locator::set_input_files(const std::vector<std::string>& paths) const {
  if (paths.size() > 1000) throw std::invalid_argument("Native upload exceeds 1000 files");
  Json::array files;
  for (const auto& path : paths) {
    if (path.empty() || path.find('\0') != std::string::npos || !std::filesystem::path(path).is_absolute())
      throw std::invalid_argument("Native upload paths must be absolute and contain no NUL");
    files.emplace_back(Json{{"path", path}, {"directory", false}});
  }
  command("form", Json{{"action", "setInputFiles"}, {"files", std::move(files)}});
}

Page::Page(std::shared_ptr<Transport> transport, int page, bool owned)
    : transport_(std::move(transport)), page_(page), owned_(owned) {}
Page::~Page() {
  if (owned_) try {
      close();
    } catch (...) {
    }
}
Page::Page(Page&& other) noexcept
    : transport_(std::move(other.transport_)),
      page_(std::exchange(other.page_, 0)),
      owned_(std::exchange(other.owned_, false)),
      timeout_(other.timeout_), frame_(std::move(other.frame_)), frame_path_(std::move(other.frame_path_)) {}
Page& Page::operator=(Page&& other) noexcept {
  if (this != &other) {
    if (owned_) try {
        close();
      } catch (...) {
      }
    transport_ = std::move(other.transport_);
    page_ = std::exchange(other.page_, 0);
    owned_ = std::exchange(other.owned_, false);
    timeout_ = other.timeout_;
    frame_ = std::move(other.frame_);
    frame_path_ = std::move(other.frame_path_);
  }
  return *this;
}
void Page::close(Timeout timeout) {
  if (page_ <= 0) return;
  if (page_ == 1)
    transport_->close();
  else
    context_call(transport_, "closePage", Json{{"pageId", page_}}, timeout);
  page_ = 0;
  owned_ = false;
}
Packet Page::request(const std::string& family, Json fields) const {
  object(fields);
  if (!frame_.empty()) fields["frame"] = frame_;
  if (!frame_path_.as_array().empty()) fields["framePath"] = frame_path_;
  return send(transport_, page_, family, std::move(fields), timeout_);
}
Json Page::command(const std::string& family, Json fields) const {
  return response_json(request(family, std::move(fields)));
}
Page Page::with_timeout(Timeout timeout) const {
  live(transport_, page_);
  valid_timeout(timeout);
  Page view(transport_, page_, false);
  view.timeout_ = timeout;
  view.frame_ = frame_;
  view.frame_path_ = frame_path_;
  return view;
}
void Page::set_timeout(Timeout timeout) {
  valid_timeout(timeout);
  timeout_ = timeout;
}
Json Page::navigate(const std::string& url, const std::string& wait) const {
  return command("navigateFrame", Json{{"url", url}, {"waitUntil", wait}, {"api", "page.goto"}});
}
Json Page::go_back(const std::string& wait) const {
  return command("historyBack", Json{{"waitUntil", wait}, {"api", "page.goBack"}});
}
Json Page::go_forward(const std::string& wait) const {
  return command("historyForward", Json{{"waitUntil", wait}, {"api", "page.goForward"}});
}
Json Page::reload(const std::string& wait) const {
  return command("reload", Json{{"waitUntil", wait}, {"api", "page.reload"}});
}
Json Page::navigation_history() const { return command("navigationHistory"); }
std::string Page::url() const { return evaluate("location.href").as_string(); }
std::string Page::title() const { return evaluate("document.title").as_string(); }
std::string Page::content() const { return evaluate("document.documentElement.outerHTML").as_string(); }
void Page::wait_for_url(const std::string& url, const std::string& wait) const {
  command("waitForURL", Json{{"url", url}, {"waitUntil", wait}});
}
Json Page::wait_for_function(const std::string& expression) const {
  const auto ready=Json::parse(transport_->ready().header);
  if(!ready.contains("backend")||ready.at("backend")==Json("cef")) {
    const auto deadline=std::chrono::steady_clock::now()+timeout_;
    auto handle=command("handle",Json{{"action","create"},{"predicate",true},{"pollInterval",20},{"expression",expression}});
    const auto id=handle.at("handle");
    try {
      const auto remaining=std::chrono::ceil<Timeout>(deadline-std::chrono::steady_clock::now());
      if(remaining.count()<=0)throw std::runtime_error("wait_for_function original deadline expired");
      auto value=evaluated(with_timeout(remaining).command("handle",Json{{"action","evaluate"},{"handle",id},
        {"function","function(){return this.value;}"},{"handles",Json::array{}}}));
      command("handle",Json{{"action","dispose"},{"handle",id}});
      return value;
    }catch(...) {
      try{command("handle",Json{{"action","dispose"},{"handle",id}});}catch(...){}
      throw;
    }
  }
  return command("waitForFunction", Json{{"expression", expression}});
}
void Page::wait_for_load_state(const std::string& state) const {
  command("waitLoadState", Json{{"waitUntil", state},{"api",frame_.empty()?"page.waitForLoadState":"frame.waitForLoadState"}});
}
void Page::wait_for_selector(const std::string& css, const std::string& state) const {
  locator(css).wait_for(state);
}
void Page::wait_for_text(const std::string& text, bool exact) const {
  get_by_text(text, exact).wait_for("visible");
}
Packet Page::evaluate_raw(const std::string& expression) const {
  return request("evaluate", Json{{"expression", expression}});
}
Json Page::evaluate(const std::string& expression) const {
  return evaluated(response_json(evaluate_raw(expression)));
}
void Page::set_content(const std::string& html) const {
  evaluate("(()=>{document.open();document.write(" + Json(html).dump() + ");document.close();})()");
}
void Page::set_viewport(int width, int height) const {
  if (width < 1 || height < 1) throw std::invalid_argument("Viewport dimensions must be positive");
  command("viewport", Json{{"width", width}, {"height", height}});
}
void Page::bring_to_front() const { command("bringToFront", Json{{"api", "page.bringToFront"}}); }
void Page::keyboard_press(const std::string& key, int delay) const {
  if (delay < 0 || delay > 120000) throw std::invalid_argument("Native input delay must be 0 to 120000 ms");
  command("input", Json{{"kind", "keyboard"}, {"action", "press"}, {"key", key}, {"delay", delay}});
}
void Page::keyboard_down(const std::string& key) const {
  command("input", Json{{"kind", "keyboard"}, {"action", "down"}, {"key", key}});
}
void Page::keyboard_up(const std::string& key) const {
  command("input", Json{{"kind", "keyboard"}, {"action", "up"}, {"key", key}});
}
void Page::type_text(const std::string& text, int delay) const {
  if (delay < 0 || delay > 120000) throw std::invalid_argument("Native input delay must be 0 to 120000 ms");
  command("input", Json{{"kind", "keyboard"}, {"action", "type"}, {"text", text}, {"delay", delay}});
}
void Page::insert_text(const std::string& text) const {
  command("input", Json{{"kind", "keyboard"}, {"action", "insertText"}, {"text", text}});
}
void Page::mouse_click(double x, double y, Json options) const {
  object(options);
  options["kind"] = "mouse"; options["action"] = "click";
  options["x"] = x; options["y"] = y;
  if (!options.contains("button")) options["button"] = "left";
  if (!options.contains("clickCount")) options["clickCount"] = 1;
  command("input", std::move(options));
}
void Page::mouse_move(double x, double y, int steps) const {
  if (steps < 1 || steps > 10000) throw std::invalid_argument("Native mouse steps must be 1 to 10000");
  command("input", Json{{"kind", "mouse"}, {"action", "move"}, {"x", x}, {"y", y}, {"steps", steps}});
}
void Page::mouse_down(const std::string& button) const {
  command("input", Json{{"kind", "mouse"}, {"action", "down"}, {"button", button}});
}
void Page::mouse_up(const std::string& button) const {
  command("input", Json{{"kind", "mouse"}, {"action", "up"}, {"button", button}});
}
void Page::mouse_wheel(double x, double y) const {
  command("input", Json{{"kind", "mouse"}, {"action", "wheel"}, {"deltaX", x}, {"deltaY", y}});
}
Frame Page::frame(const std::string& id) const {
  live(transport_, page_);
  if (id.empty()) throw std::invalid_argument("Native frame identity must not be empty");
  return Frame(transport_, page_, id, Json::array{}, timeout_);
}
Frame Page::frame_locator(const std::string& css) const {
  live(transport_, page_);
  auto path = frame_path_;
  if (path.as_array().size() >= 64) throw std::invalid_argument("Native frame path exceeds 64 steps");
  path.as_array().emplace_back(Json::array{Json{{"engine", "css"}, {"selector", css}}});
  return Frame(transport_, page_, frame_, std::move(path), timeout_);
}
Json Page::frames() const { return command("frames"); }
Locator Page::locator(const std::string& css) const {
  return locator_query(Json::array{Json{{"engine", "css"}, {"selector", css}}}, css);
}
Locator Page::locator_query(Json query, std::string description) const {
  live(transport_, page_);
  if (!query.is_array() || query.as_array().size() > 64)
    throw std::invalid_argument("Locator query must be an array of at most64 steps");
  return Locator(transport_, page_, std::move(query), std::move(description), timeout_, frame_, frame_path_);
}
Locator Page::get_by_role(const std::string& role, const std::string& name, bool exact) const {
  Json step{{"engine", "role"}, {"role", role}};
  if (!name.empty()) step["name"] = Json{{"text", name}, {"exact", exact}};
  return locator_query(
      Json::array{Json{{"engine", "css"}, {"selector", ":scope"}}, std::move(step)},
      "role=" + role);
}
Locator Page::get_by_label(const std::string& text, bool exact) const {
  return locator_query(scoped("label", text, exact), "label=" + text);
}
Locator Page::get_by_text(const std::string& text, bool exact) const {
  return locator_query(scoped("text", text, exact), "text=" + text);
}
Locator Page::get_by_placeholder(const std::string& text, bool exact) const {
  return locator_query(scoped("placeholder", text, exact), "placeholder=" + text);
}
Locator Page::get_by_alt_text(const std::string& text, bool exact) const {
  return locator_query(scoped("alt", text, exact), "alt=" + text);
}
Locator Page::get_by_title(const std::string& text, bool exact) const {
  return locator_query(scoped("title", text, exact), "title=" + text);
}
Locator Page::get_by_test_id(const std::string& value) const {
  return locator_query(
      Json::array{Json{{"engine", "css"}, {"selector", ":scope"}},
                  Json{{"engine", "testId"}, {"text", value}, {"attribute", "data-testid"}}},
      "testId=" + value);
}
Locator Page::get_by_ref(const std::string& ref) const {
  return locator_query(Json::array{Json{{"engine", "reference"}, {"ref", ref}}}, "ref=" + ref);
}
Json Page::observe(bool boxes, int depth) const {
  Json fields{{"selector", "body, frameset"},
              {"query", Json::array{Json{{"engine", "css"}, {"selector", "body, frameset"}}}},
              {"framePath", Json::array{}},
              {"referenceFormat", "json"},
              {"accessibilityBoxes", boxes}};
  if (depth >= 0) fields["referenceDepth"] = depth;
  const auto reply = command("snapshotReferences", std::move(fields));
  return Json::parse(reply.at("snapshotJSON").as_string());
}
Packet Page::screenshot_raw(Json options) const {
  object(options);
  options["action"] = "screenshot";
  if (!options.contains("format")) options["format"] = "png";
  if (!options.contains("scale")) options["scale"] = "device";
  if (!options.contains("caret")) options["caret"] = "hide";
  if (!options.contains("animations")) options["animations"] = "allow";
  if (!options.contains("fullPage")) options["fullPage"] = false;
  if (!options.contains("omitBackground")) options["omitBackground"] = false;
  if (!options.contains("maskColor")) options["maskColor"] = "#FF00FF";
  const auto& format = options.at("format").as_string();
  if (!options.contains("quality") && (format == "jpeg" || format == "webp"))
    options["quality"] = format == "webp" ? 100 : 80;
  if (format == "jpeg") options["omitBackground"] = false;
  return request("artifact", std::move(options));
}
std::vector<std::uint8_t> Page::screenshot(Json options) const {
  object(options);
  const auto format = options.contains("format") ? options.at("format").as_string() : "png";
  if (format != "png" && format != "jpeg" && format != "webp")
    throw std::invalid_argument("Screenshot format must be png, jpeg or webp");
  auto bytes =
      decode_base64(response_json(screenshot_raw(std::move(options))).at("data").as_string());
  const std::array<std::uint8_t, 8> png{137, 80, 78, 71, 13, 10, 26, 10};
  const bool valid =
      format == "png"
          ? bytes.size() >= png.size() && std::equal(png.begin(), png.end(), bytes.begin())
      : format == "jpeg"
          ? bytes.size() >= 3 && bytes[0] == 255 && bytes[1] == 216 && bytes[2] == 255
          : bytes.size() >= 12 &&
                std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) == "RIFF" &&
                std::string_view(reinterpret_cast<const char*>(bytes.data() + 8), 4) == "WEBP";
  if (!valid) throw std::runtime_error("Native screenshot signature differs from requested format");
  return bytes;
}
Packet Page::pdf_raw(Json options) const {
  object(options);
  const auto ready=Json::parse(transport_->ready().header);
  if(!ready.contains("backend")||ready.at("backend")==Json("cef")) {
    const Json defaults{{"paperWidth",8.5},{"paperHeight",11},{"marginTop",.4},{"marginBottom",.4},
      {"marginLeft",.4},{"marginRight",.4},{"scale",1},{"displayHeaderFooter",false},
      {"printBackground",false},{"landscape",false},{"preferCSSPageSize",false},{"tagged",false},
      {"outline",false},{"headerTemplate",""},{"footerTemplate",""},{"pageRanges",""}};
    for(const auto& [key,value]:defaults.as_object())if(!options.contains(key))options[key]=value;
    for(const auto& [input,output]:{std::pair{"generateTaggedPDF","tagged"},std::pair{"generateDocumentOutline","outline"}})
      if(options.contains(input)){options[output]=options.at(input);options.as_object().erase(input);}
  }
  options["action"] = "pdf";
  return request("artifact", std::move(options));
}
std::vector<std::uint8_t> Page::pdf(Json options) const {
  auto bytes = decode_base64(response_json(pdf_raw(std::move(options))).at("data").as_string());
  if (bytes.size() < 5 || std::string_view(reinterpret_cast<const char*>(bytes.data()), 5) != "%PDF-")
    throw std::runtime_error("Native PDF response has an invalid signature");
  return bytes;
}
void Page::set_download_directory(const std::string& path) const {
  command("download", Json{{"action", "setDirectory"}, {"path", path}});
}
Json Page::downloads() const {
  auto result = command("download", Json{{"action", "list"}});
  if (result.contains("downloads")) result = Json(result.at("downloads"));
  if (!result.is_array()) throw std::runtime_error("Invalid native download list response");
  for (auto& item : result.as_array()) item = download_info(std::move(item));
  return result;
}
Json Page::download_status(const std::string& id) const {
  return download_info(command("download", Json{{"action", "status"}, {"downloadId", id}}));
}
void Page::cancel_download(const std::string& id) const {
  command("download", Json{{"action", "cancel"}, {"downloadId", id}});
}
std::string Page::download_path(const std::string& id) const {
  return command("download", Json{{"action", "path"}, {"downloadId", id}}).at("path").as_string();
}
void Page::save_download(const std::string& id, const std::string& path) const {
  command("download", Json{{"action", "saveAs"}, {"downloadId", id}, {"path", path}});
}
Json Page::route(const std::string& pattern, Json options) const {
  object(options);
  options["action"] = "route"; options["pattern"] = pattern;
  if (!options.contains("behavior")) options["behavior"] = "continue";
  return command("network", std::move(options));
}
void Page::unroute(std::int64_t id) const {
  if (id < 0) throw std::invalid_argument("Native route identity must be nonnegative");
  Json fields{{"action", "unroute"}};
  if (id) fields["routeId"] = id;
  command("network", std::move(fields));
}
Json Page::routes() const { return command("network", Json{{"action", "routes"}}).at("routes"); }
Json Page::requests(bool clear) const {
  return command("network", Json{{"action", "requests"}, {"clear", clear}}).at("requests");
}
Json Page::responses(bool clear) const {
  return command("network", Json{{"action", "responses"}, {"clear", clear}}).at("responses");
}
Json Page::response_body(const std::string& id) const {
  return command("network", Json{{"action", "body"}, {"requestId", id}});
}
void Page::set_extra_http_headers(Json headers) const {
  object(headers);
  command("network", Json{{"action", "headers"}, {"headers", std::move(headers)}});
}
void Page::set_http_credentials(Json credentials) const {
  if (!credentials.is_null()) object(credentials);
  command("network", Json{{"action", "credentials"}, {"credentials", std::move(credentials)}});
}
void Page::set_offline(bool value) const { command("network", Json{{"action", "offline"}, {"offline", value}}); }
void Page::start_har(Json options) const {
  object(options);
  for (const auto& [key,value] : options.as_object()) {
    if (key != "content" || !value.is_string() || (value.as_string() != "embed" && value.as_string() != "omit"))
      throw std::invalid_argument("HAR options support content embed or omit");
  }
  if (!options.contains("content")) options["content"] = "embed";
  options["action"] = "harStart";
  command("network", std::move(options));
}
Json Page::stop_har() const { return command("network", Json{{"action", "harStop"}}); }
Json Page::export_har() const { return command("network", Json{{"action", "exportHar"}}); }
Json Page::dialogs() const { return command("dialog", Json{{"action", "list"}}).at("dialogs"); }
void Page::handle_dialog(bool accept, const std::string& prompt) const {
  command("dialog", Json{{"action", "handle"}, {"accept", accept}, {"promptText", prompt}});
}
Json Page::console_messages() const {
  const auto result = command("console", Json{{"action", "list"}, {"filter", "all"}});
  return result.contains("messages") ? result.at("messages") : result;
}
Json Page::page_errors() const {
  const auto result = command("console", Json{{"action", "errors"}, {"filter", "all"}});
  return result.contains("errors") ? result.at("errors") : result;
}
void Page::clear_console() const { command("console", Json{{"action", "clear"}}); }
std::string Page::add_init_script(const std::string& source) const {
  return command("initScript", Json{{"action", "add"}, {"source", source}}).at("identifier").as_string();
}
void Page::remove_init_script(const std::string& id) const {
  command("initScript", Json{{"action", "remove"}, {"identifier", id}});
}
Json Page::init_scripts() const { return command("initScript", Json{{"action", "list"}}).at("scripts"); }
void Page::emulate_media(Json options) const {
  object(options);
  for (const auto& [name,value] : options.as_object()) {
    if (name != "media" && name != "colorScheme" && name != "reducedMotion" && name != "forcedColors" && name != "contrast")
      throw std::invalid_argument("Unsupported native media option");
    if (value.is_null()) continue;
    if (!value.is_string()) throw std::invalid_argument("Native media settings must be strings or null");
    const auto& text = value.as_string();
    const bool valid = name == "media" ? text == "screen" || text == "print" :
        name == "colorScheme" ? text == "light" || text == "dark" || text == "no-preference" :
        name == "reducedMotion" ? text == "reduce" || text == "no-preference" :
        name == "forcedColors" ? text == "active" || text == "none" : text == "more" || text == "no-preference";
    if (!valid) throw std::invalid_argument("Invalid native media setting");
  }
  options["action"] = "media";
  command("emulation", std::move(options));
}
void Page::emulate_geolocation(double latitude, double longitude, double accuracy) const {
  if (!std::isfinite(latitude) || !std::isfinite(longitude) || !std::isfinite(accuracy) ||
      latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180 || accuracy < 0)
    throw std::invalid_argument("Invalid emulated geolocation coordinates or accuracy");
  command("emulation", Json{{"action", "geolocation"}, {"latitude", latitude}, {"longitude", longitude}, {"accuracy", accuracy}});
}
void Page::clear_geolocation() const { command("emulation", Json{{"action", "clearGeolocation"}}); }
void Page::emulate_timezone(const std::string& zone) const {
  emulation_text(zone,256,"timezone");
  command("emulation", Json{{"action", "timezone"}, {"timezoneId", zone}});
}
void Page::emulate_user_agent(const std::string& user_agent, const std::string& language, const std::string& platform) const {
  emulation_text(user_agent,4096,"user agent"); emulation_text(language,4096,"accept language"); emulation_text(platform,256,"platform");
  Json options{{"action", "userAgent"}, {"userAgent", user_agent}};
  if (!language.empty()) options["acceptLanguage"] = language;
  if (!platform.empty()) options["platform"] = platform;
  command("emulation", std::move(options));
}
Json Page::storage_state() const {
  return command("context", Json{{"action", "storageState"}, {"params", Json{{"pageId", page_}}}});
}
void Page::restore_storage_state(Json state) const {
  object(state);
  command("context", Json{{"action", "setStorageState"}, {"params", Json{{"pageId", page_}, {"state", std::move(state)}}}});
}
void Page::subscribe_frames(bool enabled) const {
  command("frameSubscription", Json{{"enabled", enabled}});
}
Json Page::start_recording(const std::string& path, double fps, Json options) const {
  object(options); options["action"]="start"; options["path"]=path; options["fps"]=fps;
  return command("recording",std::move(options));
}
Json Page::stop_recording() const { return command("recording",Json{{"action","stop"}}); }
Json Page::recording_status() const { return command("recording",Json{{"action","status"}}); }
void Page::cancel_recording() const { command("recording",Json{{"action","cancel"}}); }
void Page::start_tracing(const std::string& categories) const {
  command("tracing",Json{{"action","start"},{"categories",categories}});
}
Json Page::stop_tracing(const std::string& path) const { return command("tracing",Json{{"action","stop"},{"path",path}}); }
void Page::start_profiler(int sampling_interval_us) const {
  command("profiler",Json{{"action","start"},{"samplingIntervalUs",sampling_interval_us}});
}
Json Page::stop_profiler(const std::string& path) const { return command("profiler",Json{{"action","stop"},{"path",path}}); }
Json Page::bidi(const std::string& method, Json params) const {
  if (method.empty() || !params.is_object()) throw std::invalid_argument("BiDi requires a method and object params");
  return command("bidi", Json{{"method",method},{"params",std::move(params)}});
}
Json Page::devtools(const std::string& method, Json params) const {
  object(params);
  return command("devtools", Json{{"method", method}, {"params", std::move(params)}});
}

Frame::Frame(std::shared_ptr<Transport> transport, int page, std::string frame, Json path, Timeout timeout)
    : transport_(std::move(transport)), page_(page), frame_(std::move(frame)),
      frame_path_(std::move(path)), timeout_(timeout) {}
Page Frame::view() const {
  live(transport_, page_);
  Page page(transport_, page_, false);
  page.timeout_ = timeout_; page.frame_ = frame_; page.frame_path_ = frame_path_;
  return page;
}
Frame Frame::with_timeout(Timeout timeout) const {
  valid_timeout(timeout);
  return Frame(transport_, page_, frame_, frame_path_, timeout);
}
Frame Frame::frame_locator(const std::string& css) const { return view().frame_locator(css); }
Locator Frame::locator(const std::string& css) const { return view().locator(css); }
Locator Frame::locator_query(Json query, std::string description) const {
  return view().locator_query(std::move(query), std::move(description));
}
Locator Frame::get_by_role(const std::string& role, const std::string& name, bool exact) const {
  return view().get_by_role(role, name, exact);
}
Locator Frame::get_by_label(const std::string& text, bool exact) const { return view().get_by_label(text, exact); }
Locator Frame::get_by_text(const std::string& text, bool exact) const { return view().get_by_text(text, exact); }
Locator Frame::get_by_placeholder(const std::string& text, bool exact) const { return view().get_by_placeholder(text, exact); }
Locator Frame::get_by_alt_text(const std::string& text, bool exact) const { return view().get_by_alt_text(text, exact); }
Locator Frame::get_by_title(const std::string& text, bool exact) const { return view().get_by_title(text, exact); }
Locator Frame::get_by_test_id(const std::string& text) const { return view().get_by_test_id(text); }
Locator Frame::get_by_ref(const std::string& ref) const { return view().get_by_ref(ref); }
Json Frame::evaluate(const std::string& expression) const { return view().evaluate(expression); }
Packet Frame::evaluate_raw(const std::string& expression) const { return view().evaluate_raw(expression); }
std::string Frame::url() const { return view().url(); }
std::string Frame::title() const { return view().title(); }
std::string Frame::content() const { return view().content(); }
void Frame::wait_for_selector(const std::string& css, const std::string& state) const { locator(css).wait_for(state); }
void Frame::wait_for_text(const std::string& text, bool exact) const { get_by_text(text, exact).wait_for(); }
Json Frame::observe(bool boxes, int depth) const { return view().observe(boxes, depth); }
Json Frame::command(const std::string& family, Json fields) const { return view().command(family, std::move(fields)); }

Context::Context(std::shared_ptr<Transport> transport, int context)
    : transport_(std::move(transport)), context_(context) {}
Context::~Context() {
  try {
    close();
  } catch (...) {
  }
}
Context::Context(Context&& other) noexcept
    : transport_(std::move(other.transport_)), context_(std::exchange(other.context_, 0)) {}
Context& Context::operator=(Context&& other) noexcept {
  if (this != &other) {
    try {
      close();
    } catch (...) {
    }
    transport_ = std::move(other.transport_);
    context_ = std::exchange(other.context_, 0);
  }
  return *this;
}
Json Context::command(const std::string& action, Json params, Timeout timeout) const {
  live(transport_, context_);
  object(params);
  params["contextId"] = context_;
  return context_call(transport_, action, std::move(params), timeout);
}
Page Context::new_page(Timeout timeout) const {
  live(transport_, context_);
  return Page(transport_, new_page_id(transport_, context_, timeout), true);
}
Json Context::pages(Timeout timeout) const {
  const auto result = command("pages", Json::object{}, timeout);
  return result.contains("pages") ? result.at("pages") : result;
}
Json Context::cookies(Timeout timeout) const {
  const auto result = command("cookies", Json::object{}, timeout);
  return result.contains("cookies") ? result.at("cookies") : result;
}
void Context::add_cookies(Json cookies, Timeout timeout) const {
  if (!cookies.is_array()) throw std::invalid_argument("Native cookies must be an array");
  command("addCookies", Json{{"cookies", std::move(cookies)}}, timeout);
}
void Context::clear_cookies(Json filter, Timeout timeout) const {
  object(filter);
  command("clearCookies", Json{{"filter", std::move(filter)}}, timeout);
}
void Context::grant_permissions(const std::vector<std::string>& permissions, const std::string& origin,
                                Timeout timeout) const {
  Json::array values;
  for (const auto& value : permissions) values.emplace_back(value);
  Json fields{{"permissions", std::move(values)}};
  if (!origin.empty()) fields["origin"] = origin;
  command("grantPermissions", std::move(fields), timeout);
}
void Context::clear_permissions(Timeout timeout) const { command("clearPermissions", Json::object{}, timeout); }
Json Context::storage_state(Timeout timeout) const { return command("storageState", Json::object{}, timeout); }
void Context::restore_storage_state(Json state, Timeout timeout) const {
  object(state);
  command("setStorageState", Json{{"state", std::move(state)}}, timeout);
}
void Context::set_extra_http_headers(Json headers, Timeout timeout) const {
  object(headers);
  command("setExtraHTTPHeaders", Json{{"headers", std::move(headers)}}, timeout);
}
void Context::set_offline(bool value, Timeout timeout) const {
  command("setOffline", Json{{"offline", value}}, timeout);
}
void Context::set_http_credentials(Json credentials, Timeout timeout) const {
  if (!credentials.is_null()) object(credentials);
  command("setHTTPCredentials", Json{{"credentials", std::move(credentials)}}, timeout);
}
void Context::add_init_script(const std::string& source, Timeout timeout) const {
  command("addInitScript", Json{{"source", source}}, timeout);
}
void Context::remove_init_script(const std::string& id, Timeout timeout) const {
  command("removeInitScript", Json{{"identifier", id}}, timeout);
}
void Context::close(Timeout timeout) {
  if (context_ <= 0) return;
  command("close", Json::object{}, timeout);
  context_ = 0;
}

Browser::Browser(std::shared_ptr<Transport> transport) : transport_(std::move(transport)) {}
Browser Browser::launch(const LaunchOptions& options) {
  return Browser(Transport::launch(options));
}
Browser Browser::launch_chromium(const LaunchOptions& options) {
  auto config = options;
  config.backend = BrowserBackend::chromium;
  return launch(config);
}
Browser Browser::connect_chromium(const std::string& endpoint, const LaunchOptions& options) {
  if (endpoint.empty()) throw std::invalid_argument("Native Chromium endpoint must not be empty");
  auto config = options;
  config.backend = BrowserBackend::chromium;
  config.cdp_endpoint = endpoint;
  return launch(config);
}
Browser Browser::launch_firefox(const LaunchOptions& options) {
  auto config = options; config.backend = BrowserBackend::firefox;
  return launch(config);
}
Browser Browser::connect_firefox(const std::string& endpoint, const LaunchOptions& options) {
  auto config = options; config.backend = BrowserBackend::firefox; config.cdp_endpoint = endpoint;
  return launch(config);
}
Browser::~Browser() {
  try {
    close();
  } catch (...) {
  }
}
Browser::Browser(Browser&& other) noexcept : transport_(std::move(other.transport_)) {}
Browser& Browser::operator=(Browser&& other) noexcept {
  if (this != &other) {
    try {
      close();
    } catch (...) {
    }
    transport_ = std::move(other.transport_);
  }
  return *this;
}
Page Browser::main_page() const {
  live(transport_, 1);
  return Page(transport_, 1, false);
}
Page Browser::new_page(Timeout timeout) const {
  live(transport_, 1);
  return Page(transport_, new_page_id(transport_, default_context(transport_), timeout), true);
}
Json Browser::pages(Timeout timeout) const {
  const auto result = context_call(transport_, "pages", Json{{"contextId", default_context(transport_)}}, timeout);
  return result.contains("pages") ? result.at("pages") : result;
}
Context Browser::new_context(Json options, Timeout timeout) const {
  const auto reply = context_call(transport_, "create", std::move(options), timeout);
  return Context(transport_, resource_id(reply, "contextId"));
}
Json Browser::ready() const {
  live(transport_, 1);
  return Json::parse(transport_->ready().header);
}
std::string Browser::version() const {
  const auto info = ready();
  return info.at(info.contains("browserVersion") ? "browserVersion" : "cefVersion").as_string();
}
void Browser::close() {
  if (transport_) {
    transport_->close();
    transport_.reset();
  }
}
}  // namespace suitecut
