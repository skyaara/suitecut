// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "suitecut/json.hpp"
#include "suitecut/transport.hpp"

namespace suitecut {
using Timeout = std::chrono::milliseconds;

// Ownership wrappers are move-only where they own resources. Synchronize
// mutation/close of an individual wrapper; the shared Transport is thread-safe.

// Parse the original engine response, retaining structured native results.
Json response_json(const Packet& packet);
class Frame;

class Locator {
 public:
  Locator nth(int index) const;
  Locator locator(const std::string& css) const;
  Locator with_timeout(Timeout timeout) const;
  void click(Json options = Json::object{}) const;
  void click(Timeout timeout) const;
  Json drag_to(const Locator& target, Json options = Json::object{}) const;
  void fill(const std::string& value) const;
  void clear() const;
  void press(const std::string& key) const;
  void type(const std::string& value) const;
  void focus() const;
  void hover() const;
  void scroll_into_view() const;
  void check(bool checked = true) const;
  void uncheck() const { check(false); }
  Json select_option(const std::string& value_or_label) const;
  Json select_options(const std::vector<std::string>& values_or_labels) const;
  // Native option objects support value, label and index without conversion.
  Json select_options(Json choices) const;
  std::int64_t count() const;
  Json text_content() const;  // String or null, as reported by the actual node.
  std::string inner_text() const;
  std::string inner_html() const;
  std::string input_value() const;
  Json attribute(const std::string& name) const;
  Json evaluate(const std::string& function) const;
  void wait_for(const std::string& state = "visible") const;
  bool is_visible() const;
  bool is_hidden() const;
  bool is_enabled() const;
  bool is_disabled() const;
  bool is_checked() const;
  bool is_editable() const;
  // Absolute file paths; an empty list clears the native file input.
  void set_input_files(const std::vector<std::string>& paths) const;
  // Capture only this real element's subtree, useful for large documents.
  Json observe(bool boxes = false, int depth = -1) const;
  Json command(const std::string& family, Json fields = Json::object{}) const;

 private:
  friend class Page;
  friend class Frame;
  Locator(std::shared_ptr<Transport> transport, int page, Json query, std::string description,
          Timeout timeout, std::string frame = "", Json frame_path = Json::array{});
  Json target(Json fields) const;
  Json read(const std::string& function) const;
  bool state(const std::string& name) const;
  std::shared_ptr<Transport> transport_;
  int page_;
  Json query_;
  std::string description_;
  Timeout timeout_;
  std::string frame_;
  Json frame_path_ = Json::array{};
};

// Move-only page ownership. Main-page views do not close the browser on scope
// exit; owned child pages close themselves. Explicit main_page.close() closes
// the process, matching the native process-page lifetime.
class Page {
 public:
  ~Page();
  Page(Page&&) noexcept;
  Page& operator=(Page&&) noexcept;
  Page(const Page&) = delete;
  Page& operator=(const Page&) = delete;
  int id() const noexcept { return page_; }
  void close(Timeout timeout = Timeout{5000});
  Json navigate(const std::string& url, const std::string& wait_until = "load") const;
  Json go_back(const std::string& wait_until = "load") const;
  Json go_forward(const std::string& wait_until = "load") const;
  Json reload(const std::string& wait_until = "load") const;
  Json navigation_history() const; // Chromium history entries and current index.
  std::string url() const;
  std::string title() const;
  std::string content() const;
  // Chromium native waits; URL matching is exact and predicates return JSON.
  void wait_for_url(const std::string& url, const std::string& wait_until = "load") const;
  Json wait_for_function(const std::string& expression) const;
  void wait_for_load_state(const std::string& state = "load") const;
  void wait_for_selector(const std::string& css, const std::string& state = "visible") const;
  void wait_for_text(const std::string& text, bool exact = false) const;
  // JSON-compatible JS result; undefined becomes null. Non-JSON values throw.
  Json evaluate(const std::string& expression) const;
  // Original engine packet, including JS exception/unserializable metadata.
  Packet evaluate_raw(const std::string& expression) const;
  void set_content(const std::string& html) const;
  void set_viewport(int width, int height) const;
  void bring_to_front() const;
  void keyboard_press(const std::string& key, int delay_ms = 0) const;
  void keyboard_down(const std::string& key) const;
  void keyboard_up(const std::string& key) const;
  void type_text(const std::string& text, int delay_ms = 0) const;
  void insert_text(const std::string& text) const;
  void mouse_click(double x, double y, Json options = Json::object{}) const;
  void mouse_move(double x, double y, int steps = 1) const;
  void mouse_down(const std::string& button = "left") const;
  void mouse_up(const std::string& button = "left") const;
  void mouse_wheel(double delta_x, double delta_y) const;
  Frame frame(const std::string& frame_id) const;
  Frame frame_locator(const std::string& css) const;
  Json frames() const;
  Locator locator(const std::string& css) const;
  Locator locator_query(Json query, std::string description = "native locator") const;
  Locator get_by_role(const std::string& role, const std::string& name = "",
                      bool exact = true) const;
  Locator get_by_label(const std::string& text, bool exact = true) const;
  Locator get_by_text(const std::string& text, bool exact = true) const;
  Locator get_by_placeholder(const std::string& text, bool exact = true) const;
  Locator get_by_alt_text(const std::string& text, bool exact = true) const;
  Locator get_by_title(const std::string& text, bool exact = true) const;
  Locator get_by_test_id(const std::string& value) const;
  Locator get_by_ref(const std::string& ref) const;
  // Native accessibility forest with genuine stable references, not invented IDs.
  Json observe(bool boxes = false, int depth = -1) const;
  // Native screenshot options: format=png/jpeg/webp, fullPage, clip, quality, etc.
  std::vector<std::uint8_t> screenshot(Json options = Json::object{}) const;
  Packet screenshot_raw(Json options = Json::object{}) const;
  std::vector<std::uint8_t> pdf(Json options = Json::object{}) const;
  Packet pdf_raw(Json options = Json::object{}) const;
  // Native downloads. The directory applies to this browser context.
  // Paths are absolute; completion waits use this Page's timeout. Download IDs
  // come from downloads() or transport events.
  void set_download_directory(const std::string& path) const;
  Json downloads() const;
  Json download_status(const std::string& download_id) const;
  void cancel_download(const std::string& download_id) const;
  std::string download_path(const std::string& download_id) const;
  void save_download(const std::string& download_id, const std::string& path) const;
  // Chromium network journals belong to this page. Static routes and network
  // settings apply to its browser context. Callbacks stay in host code.
  Json route(const std::string& pattern, Json options = Json::object{}) const;
  void unroute(std::int64_t route_id = 0) const;
  Json routes() const;
  Json requests(bool clear = false) const;
  Json responses(bool clear = false) const;
  Json response_body(const std::string& request_id) const;
  void set_extra_http_headers(Json headers) const;
  void set_http_credentials(Json credentials) const;
  void set_offline(bool offline) const;
  // In-memory HAR1.2. content is embed (default) or omit; unsupported options
  // fail explicitly. Response bodies remain subject to native resource limits.
  // CEF admits one capture per context and exports this page's owned records.
  void start_har(Json options = Json::object{}) const;
  Json stop_har() const;
  Json export_har() const;
  Json dialogs() const;
  void handle_dialog(bool accept, const std::string& prompt_text = "") const;
  Json console_messages() const;
  Json page_errors() const;
  void clear_console() const;
  // Initialization scripts apply to existing and future pages in this context.
  std::string add_init_script(const std::string& source) const;
  void remove_init_script(const std::string& identifier) const;
  Json init_scripts() const;
  // Omitted media settings retain their override; null clears one setting.
  void emulate_media(Json options = Json::object{}) const;
  void emulate_geolocation(double latitude, double longitude, double accuracy = 0) const;
  void clear_geolocation() const;
  void emulate_timezone(const std::string& timezone_id) const;
  void emulate_user_agent(const std::string& user_agent, const std::string& accept_language = "",
                          const std::string& platform = "") const;
  // Chromium context cookies and current origin's local/session storage; this is
  // browser state, not an encrypted credential vault.
  Json storage_state() const;
  void restore_storage_state(Json state) const;
  void subscribe_frames(bool enabled = true) const;
  Json devtools(const std::string& method, Json params = Json::object{}) const;
  // Actual WebDriver BiDi protocol (Firefox); Chromium CDP remains devtools().
  Json bidi(const std::string& method, Json params = Json::object{}) const;
  // Chromium capture uses bounded source frames and sample-and-hold resampling.
  // Requested output FPS does not imply the browser produced unique frames.
  Json start_recording(const std::string& path, double fps = 30, Json options = Json::object{}) const;
  Json stop_recording() const;
  Json recording_status() const;
  void cancel_recording() const;
  void start_tracing(const std::string& categories = "") const;
  Json stop_tracing(const std::string& path) const;
  void start_profiler(int sampling_interval_us = 1000) const;
  Json stop_profiler(const std::string& path) const;
  Json command(const std::string& family, Json fields = Json::object{}) const;
  Packet request(const std::string& family, Json fields = Json::object{}) const;
  Timeout timeout() const noexcept { return timeout_; }
  // A non-owning view with an independent timeout; destruction does not close
  // the page. Explicit close still closes the shared underlying browser page.
  Page with_timeout(Timeout timeout) const;
  void set_timeout(Timeout timeout);

 private:
  friend class Browser;
  friend class Context;
  friend class Frame;
  Page(std::shared_ptr<Transport> transport, int page, bool owned);
  std::shared_ptr<Transport> transport_;
  int page_ = 0;
  bool owned_ = false;
  Timeout timeout_{30000};
  std::string frame_;
  Json frame_path_ = Json::array{};
};

// Non-owning, copyable frame scope. A selector path is resolved anew for each
// operation, including out-of-process child frames.
class Frame {
 public:
  // Empty for selector paths until the engine resolves them.
  const std::string& id() const noexcept { return frame_; }
  Frame with_timeout(Timeout timeout) const;
  Frame frame_locator(const std::string& css) const;
  Locator locator(const std::string& css) const;
  Locator locator_query(Json query, std::string description = "native frame locator") const;
  Locator get_by_role(const std::string& role, const std::string& name = "", bool exact = true) const;
  Locator get_by_label(const std::string& text, bool exact = true) const;
  Locator get_by_text(const std::string& text, bool exact = true) const;
  Locator get_by_placeholder(const std::string& text, bool exact = true) const;
  Locator get_by_alt_text(const std::string& text, bool exact = true) const;
  Locator get_by_title(const std::string& text, bool exact = true) const;
  Locator get_by_test_id(const std::string& value) const;
  Locator get_by_ref(const std::string& ref) const;
  Json evaluate(const std::string& expression) const;
  Packet evaluate_raw(const std::string& expression) const;
  std::string url() const;
  std::string title() const;
  std::string content() const;
  void wait_for_selector(const std::string& css, const std::string& state = "visible") const;
  void wait_for_text(const std::string& text, bool exact = false) const;
  Json observe(bool boxes = false, int depth = -1) const;
  Json command(const std::string& family, Json fields = Json::object{}) const;
 private:
  friend class Page;
  Frame(std::shared_ptr<Transport> transport, int page, std::string frame, Json path, Timeout timeout);
  Page view() const;
  std::shared_ptr<Transport> transport_;
  int page_ = 0;
  std::string frame_;
  Json frame_path_ = Json::array{};
  Timeout timeout_{30000};
};

class Context {
 public:
  ~Context();
  Context(Context&&) noexcept;
  Context& operator=(Context&&) noexcept;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  int id() const noexcept { return context_; }
  Page new_page(Timeout timeout = Timeout{30000}) const;
  Json pages(Timeout timeout = Timeout{30000}) const;
  Json cookies(Timeout timeout = Timeout{30000}) const;
  void add_cookies(Json cookies, Timeout timeout = Timeout{30000}) const;
  void clear_cookies(Json filter = Json::object{}, Timeout timeout = Timeout{30000}) const;
  void grant_permissions(const std::vector<std::string>& permissions, const std::string& origin = "",
                         Timeout timeout = Timeout{30000}) const;
  void clear_permissions(Timeout timeout = Timeout{30000}) const;
  // Chromium context cookies and a current page origin's local/session storage.
  Json storage_state(Timeout timeout = Timeout{30000}) const;
  void restore_storage_state(Json state, Timeout timeout = Timeout{30000}) const;
  void set_extra_http_headers(Json headers, Timeout timeout = Timeout{30000}) const;
  void set_offline(bool offline, Timeout timeout = Timeout{30000}) const;
  void set_http_credentials(Json credentials, Timeout timeout = Timeout{30000}) const;
  void add_init_script(const std::string& source, Timeout timeout = Timeout{30000}) const;
  void remove_init_script(const std::string& identifier, Timeout timeout = Timeout{30000}) const;
  void close(Timeout timeout = Timeout{5000});
  // Context actions use native wire options, covering cookies, permissions,
  // downloads, routes, storage, emulation and every other engine context family.
  Json command(const std::string& action, Json params = Json::object{},
               Timeout timeout = Timeout{30000}) const;

 private:
  friend class Browser;
  Context(std::shared_ptr<Transport> transport, int context);
  std::shared_ptr<Transport> transport_;
  int context_ = 0;
};

class Browser {
 public:
  static Browser launch(const LaunchOptions& options);
  // Launch an installed Chrome/Chromium through its private native CDP pipe.
  static Browser launch_chromium(const LaunchOptions& options);
  // Attach without owning the external browser; close only disconnects it.
  static Browser connect_chromium(const std::string& endpoint, const LaunchOptions& options = {});
  // Firefox native WebDriver BiDi; attachments preserve the caller browser.
  static Browser launch_firefox(const LaunchOptions& options);
  static Browser connect_firefox(const std::string& endpoint, const LaunchOptions& options = {});
  ~Browser();
  Browser(Browser&&) noexcept;
  Browser& operator=(Browser&&) noexcept;
  Browser(const Browser&) = delete;
  Browser& operator=(const Browser&) = delete;
  Page main_page() const;
  Page new_page(Timeout timeout = Timeout{30000}) const;
  Json pages(Timeout timeout = Timeout{30000}) const;
  Context new_context(Json options = Json::object{}, Timeout timeout = Timeout{30000}) const;
  Json ready() const;
  std::string version() const;
  void close();
  // Public persistent binary transport provides generic asynchronous submit,
  // cancellation and bounded event/frame polling for all native engine APIs.
  std::shared_ptr<Transport> transport() const { return transport_; }

 private:
  explicit Browser(std::shared_ptr<Transport> transport);
  std::shared_ptr<Transport> transport_;
};
}  // namespace suitecut
