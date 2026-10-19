// SPDX-License-Identifier: MIT
// Controlled protocol seam. This is not a browser and makes no browser claim.
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "suitecut/json.hpp"

using suitecut::Json;
namespace {
void packet(Json header, const std::string& data = {}) {
  header["version"] = 1;
  header["bytes"] = data.size();
  const auto text = header.dump();
  const auto length = static_cast<std::uint32_t>(text.size());
  const char prefix[4]{static_cast<char>(length), static_cast<char>(length >> 8),
                       static_cast<char>(length >> 16), static_cast<char>(length >> 24)};
  std::cout.write(prefix, 4).write(text.data(), static_cast<std::streamsize>(text.size()));
  std::cout.write(data.data(), static_cast<std::streamsize>(data.size())).flush();
}
void ready(int page) {
  packet(Json{{"type", "ready"},
              {"pageId", page},
              {"cefVersion", "controlled-facade"},
              {"timestampMs", 1},
              {"capabilities", Json{{"frameSubscription", true}}}});
}
void expect(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace

int main() {
  try {
    ready(1);
    int context = 1, page = 1, closed_pages = 0, closed_contexts = 0, clicks = 0;
    bool checked = false, frames = false, bad_artifact = false;
    std::string input, selected;
    std::string line;
    while (std::getline(std::cin, line)) {
      const auto request = Json::parse(line);
      const auto id = request.at("id").as_int();
      const auto command = request.at("command").as_string();
      Json result;
      int child_ready = 0;
      bool ok = true;
      std::string error;
      try {
        expect(request.at("version").as_int() == 1, "Missing native protocol version");
        expect(request.at("pageId").as_int() > 0, "Missing page owner");
        if (command == "context") {
          const auto action = request.at("action").as_string();
          const auto& params = request.at("params");
          if (action == "create")
            result = Json{{"contextId", ++context}};
          else if (action == "newPage") {
            expect(params.at("contextId").as_int() > 1, "Wrong child context");
            child_ready = ++page;
            result = Json{{"pageId", page}, {"contextId", params.at("contextId")}};
          } else if (action == "closePage") {
            ++closed_pages;
          } else if (action == "close") {
            ++closed_contexts;
          } else
            throw std::runtime_error("Unexpected controlled context action");
        } else if (command == "locator" || command == "form" || command == "queryEvaluate") {
          expect(request.at("query").is_array(), "Missing actual selector query");
          expect(request.at("framePath").as_array().empty(), "Wrong frame path");
          expect(request.at("strict").as_bool(), "Strictness was not retained");
          if (command == "queryEvaluate") {
            Json value;
            if (request.contains("count")) {
              expect(request.at("count").as_bool(), "Wrong count mode");
              value = Json::array{Json("primitive"), Json(1)};
            } else {
              expect(request.at("dom").as_bool() && request.at("domGetter").as_bool(),
                     "Wrong DOM getter mode");
              const auto& function = request.at("function").as_string();
              value = function.find("getAttribute") != std::string::npos ? Json("label \"東京\"")
                                                                         : Json(input);
            }
            result = Json{{"result", Json{{"value", std::move(value)}}}};
          } else {
            const auto action = request.at("action").as_string();
            if (action == "fill" || action == "type")
              input = request.at("value").as_string();
            else if (action == "clear")
              input.clear();
            else if (action == "click")
              ++clicks;
            else if (action == "inputValue")
              result = input;
            else if (action == "press")
              expect(request.at("key").as_string() == "Enter", "Wrong genuine key");
            else if (action == "setChecked") {
              expect(command == "form" && !request.at("force").as_bool() &&
                         !request.at("trial").as_bool(),
                     "Wrong checkbox form envelope");
              checked = request.at("checked").as_bool();
            } else if (action == "selectOption") {
              expect(command == "form" && !request.at("force").as_bool(),
                     "Wrong select form envelope");
              selected = request.at("options").at(0).at("valueOrLabel").as_string();
              result = Json::array{Json(selected)};
            } else if (action != "focus" && action != "hover" && action != "scrollIntoViewIfNeeded")
              throw std::runtime_error("Unexpected controlled locator action");
          }
        } else if (command == "evaluate") {
          const auto expression = request.at("expression").as_string();
          if (expression == "throw-error")
            result = Json{{"exceptionDetails", Json{{"text", "Controlled JS failure"}}}};
          else if (expression == "remote-object")
            result = Json{{"result", Json{{"type", "object"}, {"objectId", "retained-object"}}}};
          else if (expression == "nan")
            result = Json{{"result", Json{{"type", "number"}, {"unserializableValue", "NaN"}}}};
          else if (expression == "undefined")
            result = Json{{"result", Json{{"type", "undefined"}}}};
          else if (expression == "report")
            result = Json{{"result", Json{{"value", Json{{"closedPages", closed_pages},
                                                         {"closedContexts", closed_contexts},
                                                         {"clicks", clicks},
                                                         {"checked", checked},
                                                         {"frames", frames},
                                                         {"selected", selected}}}}}};
          else
            result = Json{{"result", Json{{"value", 42}}}};
        } else if (command == "snapshotReferences") {
          expect(request.at("referenceFormat").as_string() == "json",
                 "Wrong stable-reference format");
          result = Json{{"snapshotJSON",
                         "[{\"role\":\"button\",\"name\":\"Save\",\"ref\":\"rFixture_1_0\"}]"}};
        } else if (command == "artifact") {
          expect(request.at("action").as_string() == "screenshot" &&
                     request.at("format").as_string() == "png",
                 "Wrong screenshot envelope");
          expect(request.at("scale").as_string() == "device" &&
                     request.at("caret").as_string() == "hide" &&
                     request.at("animations").as_string() == "allow" &&
                     !request.at("fullPage").as_bool() && !request.at("omitBackground").as_bool() &&
                     request.at("maskColor").as_string() == "#FF00FF",
                 "Missing required native screenshot defaults");
          result = Json{{"data", bad_artifact ? "AAAA====" : "iVBORw0KGgo="}};
        } else if (command == "frameSubscription")
          frames = request.at("enabled").as_bool();
        else if (command == "fixtureBadArtifact")
          bad_artifact = true;
        else if (command == "fixtureError") {
          ok = false;
          error = "Controlled native failure";
          result = Json{{"reason", "preserved"}};
        } else if (command != "viewport" && command != "bringToFront" &&
                   command != "navigateFrame" && command != "devtools" && command != "input" &&
                   command != "cancel")
          throw std::runtime_error("Unexpected controlled command");
      } catch (const std::exception& failure) {
        ok = false;
        error = failure.what();
      }
      packet(Json{{"type", "response"}, {"id", id}, {"ok", ok}, {"error", error}}, result.dump());
      if (child_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds{35});
        ready(child_ready);
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
