// SPDX-License-Identifier: MIT
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

#include "suitecut/browser.hpp"

using namespace std::chrono_literals;
using suitecut::Json;
namespace {
void check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}
template <class Action>
void fails(Action action, const char* description) {
  try {
    action();
  } catch (const std::exception&) {
    return;
  }
  throw std::runtime_error(description);
}
}  // namespace
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    suitecut::LaunchOptions options;
    options.executable = argv[1];
    auto browser = suitecut::Browser::launch(options);
    check(browser.version() == "controlled-facade", "Actual ready metadata");
    {
      auto nonowning_main = browser.main_page();
    }
    auto main = browser.main_page();
    check(main.evaluate("42").as_int() == 42, "Main page view must not close process");
    {
      auto context = browser.new_context();
      const auto start = std::chrono::steady_clock::now();
      auto page = context.new_page();
      check(std::chrono::steady_clock::now() - start >= 25ms,
            "Child page must await actual ready handshake");
      page.get_by_label("Name \"東京\"").fill("café 東京 🚀");
      check(page.locator("#name").input_value() == "café 東京 🚀", "Fill input wire payload");
      check(page.locator("#name").text_content().as_string() == "café 東京 🚀",
            "Raw DOM text read");
      check(page.locator("#name").inner_text() == "café 東京 🚀", "Raw DOM inner text read");
      check(page.locator("#name").attribute("data-label\"escaped").as_string() == "label \"東京\"",
            "Escaped getter function argument");
      check(page.locator("#name").count() == 1, "Native count tagged scalar");
      page.get_by_role("button", "Save").click(300ms);
      page.get_by_test_id("save").click(Json{{"force", false}});
      page.get_by_ref("rFixture_1_0").click();
      page.locator("#name").press("Enter");
      page.locator("#agree").check();
      check(page.evaluate("report").at("checked").as_bool(), "Genuine checkbox form command");
      page.locator("#agree").uncheck();
      check(!page.evaluate("report").at("checked").as_bool(), "Genuine checkbox uncheck command");
      check(page.locator("#plan").select_option("Team").at(0).as_string() == "Team",
            "Native option value-or-label envelope");
      const auto observation = page.observe();
      check(observation.at(0).at("ref").as_string() == "rFixture_1_0",
            "Native snapshot inner JSON remains real reference");
      const auto png = page.screenshot();
      check(png.size() == 8 && png[0] == 137 && png[1] == 80, "Native artifact decoding");
      page.subscribe_frames();
      check(page.evaluate("report").at("frames").as_bool(), "Frame subscription native command");
      page.subscribe_frames(false);
      check(!page.evaluate("report").at("frames").as_bool(), "Frame unsubscribe native command");
      check(page.evaluate("undefined").is_null(), "Explicit undefined mapping");
      fails([&] { page.evaluate("remote-object"); },
            "Missing remote object value must not silently become null");
      fails([&] { page.evaluate("throw-error"); }, "JS exceptions must remain errors");
      fails([&] { page.evaluate("nan"); }, "Non-JSON value must remain explicit");
      const auto raw = page.request("fixtureError");
      check(!Json::parse(raw.header).at("ok").as_bool(), "Raw native failure status is preserved");
      fails([&] { suitecut::response_json(raw); }, "Typed facade must reject native error");
      page.command("fixtureBadArtifact");
      fails([&] { page.screenshot(); }, "Invalid native base64 must not be accepted");
      auto moved = std::move(page);
      fails([&] { page.evaluate("42"); }, "Moved page must reject commands");
      check(moved.evaluate("report").at("clicks").as_int() == 3,
            "All locator targeting paths retained");
    }
    const auto report = main.evaluate("report");
    check(report.at("closedPages").as_int() == 1, "Owned page closes exactly once after move");
    check(report.at("closedContexts").as_int() == 1, "Owned context closes exactly once");
    browser.close();
    browser.close();
    std::cout << "PASS native facade protocol envelopes, ready ownership, UTF-8, strict errors and "
                 "RAII\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
