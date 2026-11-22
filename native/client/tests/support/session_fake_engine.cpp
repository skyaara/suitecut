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
//   dialog:TYPE:MESSAGE  open a JavaScript dialog (alert, confirm, prompt or
//                beforeunload); while the page's dialogs are subscribed it is
//                held for an answer and reported, otherwise answered by default
//   cursor:NAME  report a cursor change (host events only)
//   ime          report an IME composition range (host events only)
//   file:MODE    a page's file chooser (open, openMultiple, openFolder, save)
//   permission:NAME, auth, auth:agent (the context has credentials for it),
//   certificate  a person's prompt; with host events and the page's prompts
//                subscribed it is held and reported, as the engine does
//   download     start a download; it is reported created at once
//   anything else answers 42
// With --suitecut-host-events=1 it reports hostEvents in ready, sends host
// packets (loading, progress, title) around each navigation, takes host
// commands, and reports held dialogs closed when a navigation resets them.
// A host answer's password is written to commands.log as "<redacted>", so
// tests can look for the real one anywhere else.
// Navigating to a URL containing "fail" answers with an engine error; one
// ending in /auth or /auth-agent then asks for a login (as auth, auth:agent),
// and one ending in /download starts a download. A
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
bool host_events = false;
void ready(int page) {
  Json capabilities{{"frameSubscription", true}};
  if (host_events) capabilities["hostEvents"] = true;
  packet(Json{{"type", "ready"},
              {"pageId", page},
              {"cefVersion", "session-fake"},
              {"timestampMs", 1},
              {"capabilities", std::move(capabilities)}});
}
void respond(std::int64_t id, bool ok, const std::string& error, const Json& result) {
  packet(Json{{"type", "response"}, {"id", id}, {"ok", ok}, {"error", error}}, result.dump());
}
void context_event(const Json& event) { packet(Json{{"type", "context"}}, event.dump()); }
void host_event(int page, const Json& event) { packet(Json{{"type", "host"}, {"pageId", page}}, event.dump()); }
void dialog_event(int page, const Json& dialog) { packet(Json{{"type", "dialog"}, {"pageId", page}}, dialog.dump()); }
void download_event(int page, const Json& download) {
  packet(Json{{"type", "download"}, {"pageId", page}}, download.dump());
}
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
    if (argument == "--suitecut-host-events=1") host_events = true;
  }
  std::map<int, std::pair<int, int>> viewports;  // CSS size per page
  std::map<int, bool> person;                    // a person's prompts held, per page
  struct Held {
    int page;
    Json opened;
  };
  std::map<std::string, Held> held;  // a person's prompts: engine id -> page, opened event
  int prompt_sequence = 0, download_sequence = 0;
  std::map<int, std::string> outputs;            // bootstrap name per page with a view
  std::ofstream log;
  if (!profile.empty()) log.open(profile + "/commands.log", std::ios::app);
  try {
    ready(1);
    int next_page = 1;
    std::map<int, std::string> titles{{1, ""}};
    std::map<int, bool> subscribed;                // dialogs held for an answer, per page
    std::map<std::string, std::pair<int, Json>> dialogs;  // held: id -> page, opened event
    int dialog_sequence = 0;
    // As the engine's reset on navigation: each held dialog is dropped, and
    // with host events reported closed.
    auto reset_dialogs = [&](int on, std::vector<std::pair<int, Json>>& out) {
      for (auto it = dialogs.begin(); it != dialogs.end();) {
        if (it->second.first != on) {
          ++it;
          continue;
        }
        if (host_events) {
          auto closed = it->second.second;
          closed["state"] = "closed";
          closed["reason"] = "reset";
          out.emplace_back(on, std::move(closed));
        }
        it = dialogs.erase(it);
      }
    };
    std::vector<std::thread> slow;
    std::string line;
    while (std::getline(std::cin, line)) {
      const auto request = Json::parse(line);
      if (log) {
        if (request.contains("answer") && request.at("answer").is_object() && request.at("answer").contains("password")) {
          auto redacted = request;
          redacted["answer"]["password"] = "<redacted>";
          log << redacted.dump() << '\n' << std::flush;
        } else {
          log << line << '\n' << std::flush;
        }
      }
      const auto id = request.at("id").as_int();
      const auto command = request.at("command").as_string();
      const int page = request.contains("pageId") ? static_cast<int>(request.at("pageId").as_int()) : 0;
      Json result = Json::object{};
      bool ok = true;
      std::string error;
      int child_ready = 0, frames_for = 0;
      std::vector<Json> after;
      std::vector<std::pair<int, Json>> after_dialogs, after_host;
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
          reset_dialogs(page, after_dialogs);
          // A navigation drops a file chooser, as the engine's reset does.
          for (auto it = held.begin(); it != held.end();) {
            if (it->second.page != page || it->second.opened.at("kind") != Json("file")) {
              ++it;
              continue;
            }
            host_event(page, Json{{"event", "prompt"}, {"id", it->first}, {"kind", "file"}, {"state", "closed"}, {"reason", "reset"}});
            it = held.erase(it);
          }
          for (auto& [on, closed] : after_dialogs) dialog_event(on, closed);
          after_dialogs.clear();
          if (host_events) {
            host_event(page, Json{{"event", "loading"}, {"loading", true}, {"canGoBack", false}, {"canGoForward", false}});
            host_event(page, Json{{"event", "progress"}, {"progress", 0.5}});
          }
          packet(Json{{"type", "navigation"}, {"pageId", page}, {"url", url}});
          if (url.ends_with("/auth") || url.ends_with("/auth-agent")) {
            if (host_events && person[page]) {
              const auto engine_id = std::to_string(++prompt_sequence);
              Json opened{{"event", "prompt"}, {"id", engine_id}, {"state", "opened"}, {"kind", "auth"},
                          {"url", url}, {"proxy", false}, {"host", "fake.test"}, {"port", 443},
                          {"realm", "Fake"}, {"scheme", "basic"}, {"agentCredentials", url.ends_with("/auth-agent")}};
              held[engine_id] = Held{page, opened};
              after_host.emplace_back(page, std::move(opened));
            }
          } else if (url.ends_with("/download")) {
            download_event(page, Json{{"event", "created"},
                                      {"id", "download-" + std::to_string(++download_sequence)},
                                      {"pageId", page},
                                      {"url", url},
                                      {"suggestedFilename", "report.pdf"},
                                      {"mimeType", "application/pdf"},
                                      {"totalBytes", 3},
                                      {"failure", Json{}}});
          }
          if (host_events) {
            host_event(page, Json{{"event", "title"}, {"title", titles[page]}});
            host_event(page, Json{{"event", "progress"}, {"progress", 1.0}});
            host_event(page, Json{{"event", "loading"}, {"loading", false}, {"canGoBack", true}, {"canGoForward", false}});
          }
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
        } else if (expression.starts_with("dialog:")) {
          const auto rest = expression.substr(7);
          const auto colon = rest.find(':');
          const auto type = rest.substr(0, colon);
          const auto message = colon == std::string::npos ? std::string() : rest.substr(colon + 1);
          if (subscribed[page]) {
            const auto dialog_id = std::to_string(++dialog_sequence);
            Json opened{{"id", dialog_id}, {"type", type}, {"url", "https://fake.test/"}, {"message", message},
                        {"defaultValue", type == "prompt" ? "default" : ""}, {"state", "opened"}};
            dialogs[dialog_id] = {page, opened};
            after_dialogs.emplace_back(page, std::move(opened));
          }
        } else if (expression.starts_with("cursor:") && host_events) {
          after_host.emplace_back(page, Json{{"event", "cursor"}, {"cursor", expression.substr(7)}});
        } else if (host_events && (expression.starts_with("file:") || expression.starts_with("permission:") ||
                                   expression.starts_with("auth") || expression == "certificate")) {
          if (person[page]) {
            const auto engine_id = std::to_string(++prompt_sequence);
            Json opened{{"event", "prompt"}, {"id", engine_id}, {"state", "opened"}};
            if (expression.starts_with("file:")) {
              opened["kind"] = "file";
              opened["mode"] = expression.substr(5);
              opened["title"] = "";
              opened["defaultPath"] = "";
              opened["accept"] = Json::array{".txt"};
            } else if (expression.starts_with("permission:")) {
              opened["kind"] = "permission";
              opened["origin"] = "https://fake.test";
              opened["permissions"] = Json::array{expression.substr(11)};
            } else if (expression.starts_with("auth")) {
              opened["kind"] = "auth";
              opened["url"] = "https://fake.test/private";
              opened["proxy"] = false;
              opened["host"] = "fake.test";
              opened["port"] = 443;
              opened["realm"] = "Fake";
              opened["scheme"] = "basic";
              opened["agentCredentials"] = expression == "auth:agent";
            } else {
              opened["kind"] = "certificate";
              opened["host"] = "fake.test";
              opened["port"] = 443;
              opened["proxy"] = false;
              opened["certificates"] = Json::array{
                  Json{{"index", 0}, {"subject", "Person"}, {"issuer", "Fake CA"}, {"validFrom", 0}, {"expires", 4102444800000.0}}};
            }
            held[engine_id] = Held{page, opened};
            after_host.emplace_back(page, std::move(opened));
          }
        } else if (expression == "download") {
          download_event(page, Json{{"event", "created"},
                                    {"id", "download-" + std::to_string(++download_sequence)},
                                    {"pageId", page},
                                    {"url", "https://fake.test/report.pdf"},
                                    {"suggestedFilename", "report.pdf"},
                                    {"mimeType", "application/pdf"},
                                    {"totalBytes", 3},
                                    {"failure", Json{}}});
        } else if (expression == "ime" && host_events) {
          after_host.emplace_back(page, Json{{"event", "ime"}, {"range", Json::array{0, 3}},
                                             {"bounds", Json::array{Json::array{10, 20, 30, 40}}}});
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
      } else if (command == "dialog") {
        const auto action = request.at("action").as_string();
        if (action == "subscribe") {
          subscribed[page] = request.at("enabled").as_bool();
          // As the engine: unsubscribing dismisses what is held, unreported.
          if (!subscribed[page])
            for (auto it = dialogs.begin(); it != dialogs.end();)
              it = it->second.first == page ? dialogs.erase(it) : std::next(it);
        } else if (action == "answer") {
          const auto found = dialogs.find(request.at("dialogId").as_string());
          if (found == dialogs.end() || found->second.first != page) {
            ok = false;
            error = "Dialog is already handled or was canceled";
          } else {
            auto closed = found->second.second;
            closed["state"] = "closed";
            after_dialogs.emplace_back(page, std::move(closed));
            dialogs.erase(found);
          }
        } else {
          ok = false;
          error = "Invalid native dialog command";
        }
      } else if (command == "host") {
        const auto action = request.at("action").as_string();
        if (!host_events) {
          ok = false;
          error = "Unexpected session-fake command: host";
        } else if (action == "prompts") {
          person[page] = request.at("enabled").as_bool();
          // As the engine: unsubscribing answers each held prompt by policy.
          if (!person[page])
            for (auto it = held.begin(); it != held.end();) {
              if (it->second.page != page) {
                ++it;
                continue;
              }
              after_host.emplace_back(page, Json{{"event", "prompt"}, {"id", it->first}, {"kind", it->second.opened.at("kind")},
                                                 {"state", "closed"}, {"reason", "policy"}});
              it = held.erase(it);
            }
        } else if (action == "answer") {
          const auto found = held.find(request.at("prompt").as_string());
          const auto& answer = request.at("answer");
          if (found == held.end() || found->second.page != page) {
            ok = false;
            error = "The prompt was answered or has closed";
          } else if (found->second.opened.at("kind") == Json("auth") && !answer.contains("policy") &&
                     !answer.contains("cancel") &&
                     (!answer.contains("password") || !answer.at("password").is_string())) {
            ok = false;
            error = "An authentication answer has username and password, or cancel";
          } else {
            const bool policy = answer.contains("policy");
            after_host.emplace_back(page, Json{{"event", "prompt"}, {"id", found->first}, {"kind", found->second.opened.at("kind")},
                                               {"state", "closed"}, {"reason", policy ? "policy" : "answered"}});
            held.erase(found);
          }
        } else if (action != "stop" && action != "ime" && action != "edit") {
          ok = false;
          error = "A host command is stop, ime, edit, prompts or answer";
        } else if (action == "stop") {
          after_host.emplace_back(page, Json{{"event", "loading"}, {"loading", false}, {"canGoBack", true}, {"canGoForward", false}});
        }
      } else if (command == "download") {
        // path says where it settled; saveAs writes the file where asked;
        // cancel succeeds.
        const auto action = request.at("action").as_string();
        if (action == "path") {
          result = Json{{"path", "/fake/download"}};
        } else if (action == "saveAs") {
          std::ofstream saved(request.at("path").as_string(), std::ios::binary);
          saved << "pdf";
          if (!saved) {
            ok = false;
            error = "Native download could not be saved";
          }
        } else if (action != "cancel") {
          ok = false;
          error = "Unsupported native download action";
        }
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
      for (const auto& [on, dialog] : after_dialogs) dialog_event(on, dialog);
      for (const auto& [on, event] : after_host) host_event(on, event);
    }
    for (auto& thread : slow) thread.join();
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
