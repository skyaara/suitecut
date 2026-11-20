// SPDX-License-Identifier: MIT
// A controlled engine for session service tests. It is not a browser and
// makes no browser claim: it answers the commands the session sends, keeps
// a title per page, and writes every command it receives to
// <profile>/commands.log so tests can check what reached the engine.
//
// Evaluate expressions that steer it:
//   slow:N       answer after N milliseconds, without blocking other commands
//   popup        open a popup: a page-created event whose opener is this page
//   close-self   report this page closed, as if the page called window.close()
//   exit         stop the engine without answering
//   anything else answers 42
// Navigating to a URL containing "fail" answers with an engine error. A
// command for a page that is not open fails, as it does in the engine.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "suitecut/json.hpp"

using suitecut::Json;
namespace {
std::mutex out_mutex;
void packet(Json header, const std::string& data = {}) {
  header["version"] = 1;
  header["bytes"] = data.size();
  const auto text = header.dump();
  const auto length = static_cast<std::uint32_t>(text.size());
  const char prefix[4]{static_cast<char>(length), static_cast<char>(length >> 8),
                       static_cast<char>(length >> 16), static_cast<char>(length >> 24)};
  std::lock_guard lock(out_mutex);
  std::cout.write(prefix, 4).write(text.data(), static_cast<std::streamsize>(text.size()));
  std::cout.write(data.data(), static_cast<std::streamsize>(data.size())).flush();
}
void ready(int page) {
  packet(Json{{"type", "ready"},
              {"pageId", page},
              {"cefVersion", "session-fake"},
              {"timestampMs", 1},
              {"capabilities", Json{{"frameSubscription", true}}}});
}
void respond(std::int64_t id, bool ok, const std::string& error, const Json& result) {
  packet(Json{{"type", "response"}, {"id", id}, {"ok", ok}, {"error", error}}, result.dump());
}
void context_event(const Json& event) { packet(Json{{"type", "context"}}, event.dump()); }
}  // namespace

int main(int argc, char** argv) {
  std::string profile;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument.starts_with("--suitecut-profile=")) profile = argument.substr(19);
  }
  std::ofstream log;
  if (!profile.empty()) log.open(profile + "/commands.log", std::ios::app);
  try {
    ready(1);
    int next_page = 1;
    std::map<int, std::string> titles{{1, ""}};
    std::vector<std::thread> slow;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (log) log << line << '\n' << std::flush;
      const auto request = Json::parse(line);
      const auto id = request.at("id").as_int();
      const auto command = request.at("command").as_string();
      const int page = request.contains("pageId") ? static_cast<int>(request.at("pageId").as_int()) : 0;
      Json result = Json::object{};
      bool ok = true;
      std::string error;
      int child_ready = 0;
      std::vector<Json> after;
      if (command != "context" && !titles.contains(page)) {
        ok = false;
        error = "No page " + std::to_string(page);
      } else if (command == "context") {
        const auto action = request.at("action").as_string();
        const auto& params = request.at("params");
        if (action == "newPage") {
          child_ready = ++next_page;
          titles[child_ready] = "";
          result = Json{{"pageId", child_ready}, {"contextId", params.at("contextId")}};
        } else if (action == "closePage") {
          // As the engine does, a closed page is reported as page-closed.
          const int closing = static_cast<int>(params.at("pageId").as_int());
          if (titles.erase(closing)) after.push_back(Json{{"event", "page-closed"}, {"pageId", closing}});
        } else if (action == "pages") {
          Json::array listed;
          for (const auto& [known, unused] : titles) {
            (void)unused;
            listed.push_back(Json{{"pageId", known}});
          }
          result = Json{{"pages", std::move(listed)}};
        }
      } else if (command == "navigate" || command == "navigateFrame") {
        const auto url = request.at("url").as_string();
        if (url.find("fail") != std::string::npos) {
          ok = false;
          error = "net::ERR_NAME_NOT_RESOLVED";
        } else {
          titles[page] = "Title of " + url;
          packet(Json{{"type", "navigation"}, {"pageId", page}, {"url", url}});
        }
      } else if (command == "historyBack" || command == "historyForward" || command == "reload") {
        // History is not modelled: the page stays where it is.
      } else if (command == "devtools") {
        const auto& params = request.at("params");
        if (request.at("method").as_string() == "Runtime.evaluate" &&
            params.at("expression").as_string() == "document.title")
          result = Json{{"result", Json{{"type", "string"}, {"value", titles[page]}}}};
      } else if (command == "evaluate") {
        const auto expression = request.at("expression").as_string();
        if (expression.starts_with("slow:")) {
          const auto delay = std::chrono::milliseconds(std::atoll(expression.c_str() + 5));
          slow.emplace_back([id, delay] {
            std::this_thread::sleep_for(delay);
            respond(id, true, "", Json{{"result", Json{{"type", "string"}, {"value", "slow"}}}});
          });
          continue;
        }
        if (expression == "exit") std::_Exit(0);
        if (expression == "popup") {
          const int popup = ++next_page;
          titles[popup] = "";
          after.push_back(Json{{"event", "page-created"}, {"pageId", popup}, {"openerId", page}});
        } else if (expression == "close-self") {
          titles.erase(page);
          after.push_back(Json{{"event", "page-closed"}, {"pageId", page}});
        }
        result = Json{{"result", Json{{"type", "number"}, {"value", 42}}}};
      } else if (command == "artifact") {
        result = Json{{"data", "iVBORw0KGgo="}};
      } else if (command != "viewport" && command != "input" && command != "surfaceOutput" &&
                 command != "frameSubscription" && command != "bringToFront" && command != "cancel") {
        ok = false;
        error = "Unexpected session-fake command: " + command;
      }
      respond(id, ok, error, result);
      if (child_ready) ready(child_ready);
      for (const auto& event : after) context_event(event);
    }
    for (auto& thread : slow) thread.join();
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
