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
//
// On macOS, surfaceOutput with a bootstrap name sends a real 420f IOSurface
// frame there in the engine's 64-byte message, sized as the page's viewport
// times --suitecut-scale, and again after each viewport change. Each real
// frame is followed by a forged one (2x2) from a forked child process, which
// a receiver that checks the sender must drop.
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

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <servers/bootstrap.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using suitecut::Json;
namespace {
#ifdef __APPLE__
struct FrameMessage {
  mach_msg_header_t header;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t surface;
  std::uint32_t magic, width, height, reserved;
  std::uint64_t timestamp;
};
bool send_frame(mach_port_t to, mach_port_t right, std::uint32_t width, std::uint32_t height) {
  FrameMessage message{};
  message.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, MACH_MSGH_BITS_COMPLEX);
  message.header.msgh_size = sizeof message;
  message.header.msgh_remote_port = to;
  message.header.msgh_id = 0x47425331;
  message.body.msgh_descriptor_count = 1;
  message.surface.name = right;
  message.surface.disposition = MACH_MSG_TYPE_MOVE_SEND;
  message.surface.type = MACH_MSG_PORT_DESCRIPTOR;
  message.magic = 0x47425331;
  message.width = width;
  message.height = height;
  message.timestamp = mach_absolute_time() / 1000;
  return mach_msg(&message.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof message, 0, MACH_PORT_NULL, 1000,
                  MACH_PORT_NULL) == MACH_MSG_SUCCESS;
}
// One real frame from this process, then a forged one from a child.
void send_frames(const std::string& name, int width, int height) {
  mach_port_t to = MACH_PORT_NULL;
  if (bootstrap_look_up(bootstrap_port, name.c_str(), &to) != KERN_SUCCESS) return;
  const auto w = static_cast<std::uint32_t>(width) & ~1u, h = static_cast<std::uint32_t>(height) & ~1u;
  const int format = 0x34323066, bytes_per_element[2] = {1, 2}, widths[2] = {static_cast<int>(w), static_cast<int>(w / 2)},
            heights[2] = {static_cast<int>(h), static_cast<int>(h / 2)};
  CFMutableArrayRef planes = CFArrayCreateMutable(nullptr, 2, &kCFTypeArrayCallBacks);
  for (int i = 0; i < 2; ++i) {
    CFMutableDictionaryRef plane =
        CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const int row = widths[i] * bytes_per_element[i];
    const auto put = [&](CFStringRef key, int value) {
      CFNumberRef number = CFNumberCreate(nullptr, kCFNumberIntType, &value);
      CFDictionarySetValue(plane, key, number);
      CFRelease(number);
    };
    put(kIOSurfacePlaneWidth, widths[i]);
    put(kIOSurfacePlaneHeight, heights[i]);
    put(kIOSurfacePlaneBytesPerElement, bytes_per_element[i]);
    put(kIOSurfacePlaneBytesPerRow, row);
    CFArrayAppendValue(planes, plane);
    CFRelease(plane);
  }
  CFMutableDictionaryRef properties =
      CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  const auto put = [&](CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(nullptr, kCFNumberIntType, &value);
    CFDictionarySetValue(properties, key, number);
    CFRelease(number);
  };
  put(kIOSurfaceWidth, static_cast<int>(w));
  put(kIOSurfaceHeight, static_cast<int>(h));
  put(kIOSurfacePixelFormat, format);
  CFDictionarySetValue(properties, kIOSurfacePlaneInfo, planes);
  IOSurfaceRef surface = IOSurfaceCreate(properties);
  CFRelease(planes);
  CFRelease(properties);
  if (surface) {
    if (!send_frame(to, IOSurfaceCreateMachPort(surface), w, h)) {}
    CFRelease(surface);
  }
  // A forged frame: any send right, from another process.
  const pid_t child = ::fork();
  if (child == 0) {
    mach_port_t target = MACH_PORT_NULL, fake = MACH_PORT_NULL;
    if (bootstrap_look_up(bootstrap_port, name.c_str(), &target) == KERN_SUCCESS &&
        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &fake) == KERN_SUCCESS &&
        mach_port_insert_right(mach_task_self(), fake, fake, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS)
      send_frame(target, fake, 2, 2);
    ::_exit(0);
  }
  if (child > 0) ::waitpid(child, nullptr, 0);
  mach_port_deallocate(mach_task_self(), to);
}
#endif

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
  int scale = 1, width = 1280, height = 720;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument.starts_with("--suitecut-profile=")) profile = argument.substr(19);
    if (argument.starts_with("--suitecut-scale=")) scale = std::stoi(argument.substr(17));
    if (argument.starts_with("--suitecut-width=")) width = std::stoi(argument.substr(17));
    if (argument.starts_with("--suitecut-height=")) height = std::stoi(argument.substr(18));
  }
  std::map<int, std::pair<int, int>> viewports;  // CSS size per page
  std::map<int, std::string> outputs;            // bootstrap name per page with a view
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
      int child_ready = 0, frames_for = 0;
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
      } else if (command == "viewport") {
        viewports[page] = {static_cast<int>(request.at("width").as_int()), static_cast<int>(request.at("height").as_int())};
        frames_for = page;
      } else if (command == "surfaceOutput") {
        if (request.contains("port") && request.at("port").is_string())
          outputs[page] = request.at("port").as_string();
        else
          outputs.erase(page);
        frames_for = page;
      } else if (command != "viewport" && command != "input" && command != "surfaceOutput" &&
                 command != "frameSubscription" && command != "bringToFront" && command != "cancel") {
        ok = false;
        error = "Unexpected session-fake command: " + command;
      }
      respond(id, ok, error, result);
#ifdef __APPLE__
      if (frames_for && outputs.contains(frames_for)) {
        const auto size = viewports.contains(frames_for) ? viewports[frames_for] : std::pair{width, height};
        send_frames(outputs[frames_for], size.first * scale, size.second * scale);
      }
#endif
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
