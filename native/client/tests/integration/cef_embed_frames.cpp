// SPDX-License-Identifier: MIT
// Real-engine frames through suitecut/embed.h (macOS). Starts its own session
// on a SuiteCut runtime, attaches a library-sink view to a tab of a local page,
// and checks the IOSurfaces it receives: size, '420f', the use count, content,
// that a held surface is never drawn into, that frames keep coming after a
// resize, and that they stop after detach.
//
// usage: suitecut-native-cef_embed_frames RUNTIME ARTIFACTS
//   RUNTIME   SuiteCut.app, a directory holding suitecut-browser and
//             suitecut-session, or the suitecut-browser inside one of those
//   ARTIFACTS directory for results.json
// SUITECUT_EMBED_SIGNING_REQUIREMENT, when set, is checked against the runtime.
//
// The session's name, folder and profile are its own and removed afterwards.
#include "suitecut/embed.h"
#include "suitecut/json.hpp"
#include "posix_fixture.hpp"

#include <IOSurface/IOSurfaceRef.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

using suitecut::Json;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {
constexpr int kScale = 2;
constexpr int kLight = 200, kDark = 32;  // grey levels of the two pages; full-range luma equals them
constexpr int kLumaTolerance = 12;

// A page of one grey level with a small square moving in the corner, so the
// engine keeps painting while the test watches.
class PageServer {
 public:
  PageServer() {
    socket_ = suitecut_test::socket();
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (socket_ < 0 || ::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof address) || ::listen(socket_, 16))
      throw std::runtime_error("Cannot start the page server");
    socklen_t size = sizeof address;
    if (::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &size)) throw std::runtime_error("Cannot read the port");
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { Serve(); });
  }
  ~PageServer() {
    stopped_ = true;
    if (thread_.joinable()) thread_.join();
    if (socket_ >= 0) ::close(socket_);
  }
  std::string Url(int grey) const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/grey/" + std::to_string(grey);
  }

 private:
  void Serve() {
    while (!stopped_) {
      const int client = suitecut_test::accept(socket_);
      if (client < 0) continue;
      timeval timeout{2, 0};
      ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
      int one = 1;
      ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
      char request[4096];
      const auto n = ::recv(client, request, sizeof request - 1, 0);
      const std::string head = n > 0 ? std::string(request, static_cast<std::size_t>(n)) : "";
      int grey = 255;
      if (const auto at = head.find("GET /grey/"); at != std::string::npos) grey = std::atoi(head.c_str() + at + 10);
      const auto level = std::to_string(grey);
      const std::string body =
          "<!doctype html><meta charset=utf-8><title>grey " + level + "</title><style>html,body{margin:0;height:100%;"
          "background:rgb(" + level + "," + level + "," + level + ")}#tick{position:fixed;left:0;top:0;width:24px;"
          "height:24px;background:#e33;animation:m .5s linear infinite alternate}@keyframes m{to{transform:"
          "translateX(60px)}}</style><div id=tick></div>";
      const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nCache-Control: no-store\r\n"
                                   "Connection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
      std::size_t offset = 0;
      while (offset < response.size()) {
        const auto wrote = ::send(client, response.data() + offset, response.size() - offset, 0);
        if (wrote <= 0) break;
        offset += static_cast<std::size_t>(wrote);
      }
      ::close(client);
    }
  }
  int socket_ = -1;
  unsigned port_ = 0;
  std::atomic_bool stopped_{false};
  std::thread thread_;
};

// The mean luma of a 16×16 block at the surface's centre (plane 0 of NV12).
int centre_luma(void* surface_ref) {
  auto surface = static_cast<IOSurfaceRef>(surface_ref);
  if (IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr) != 0) return -1;  // kIOReturnSuccess
  const auto* base = static_cast<const std::uint8_t*>(IOSurfaceGetBaseAddressOfPlane(surface, 0));
  const auto stride = IOSurfaceGetBytesPerRowOfPlane(surface, 0);
  const auto width = IOSurfaceGetWidthOfPlane(surface, 0), height = IOSurfaceGetHeightOfPlane(surface, 0);
  long sum = 0;
  for (std::size_t y = height / 2 - 8; y < height / 2 + 8; ++y)
    for (std::size_t x = width / 2 - 8; x < width / 2 + 8; ++x) sum += base[y * stride + x];
  IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
  return static_cast<int>(sum / 256);
}
bool near(int luma, int level) { return luma >= level - kLumaTolerance && luma <= level + kLumaTolerance; }

struct Frame {
  sc_frame value{};
  Frame() { sc_frame_init(&value); }
  ~Frame() { sc_frame_release(&value); }
  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;
};

class Run {
 public:
  explicit Run(fs::path artifacts) : artifacts_(std::move(artifacts)) {}
  Json report{{"ok", false}, {"stage", "start"}, {"closed", false}, {"cases", Json::array{}}, {"checks", Json::array{}}};

  void check(bool ok, const std::string& name, Json detail = nullptr) {
    Json row{{"name", name}, {"ok", ok}};
    if (!detail.is_null()) row["detail"] = std::move(detail);
    report["checks"].as_array().push_back(std::move(row));
    if (!ok) throw std::runtime_error("Check failed: " + name);
  }
  void stage(const std::string& name) { report["stage"] = name; }
  void case_done(const std::string& name, Json detail) {
    Json row{{"name", name}, {"ok", true}, {"detail", std::move(detail)}};
    report["cases"].as_array().push_back(std::move(row));
  }
  void write() {
    fs::create_directories(artifacts_);
    std::ofstream(artifacts_ / "results.json") << report.dump() << "\n";
  }

  // Waits for a frame that satisfies |want|, releasing the others; returns how
  // many frames it saw. The matching frame stays in |out|.
  int until(sc_view* view, Frame& out, std::chrono::milliseconds limit, const std::function<bool(const sc_frame&)>& want) {
    const auto deadline = Clock::now() + limit;
    int seen = 0;
    while (Clock::now() < deadline) {
      sc_frame_release(&out.value);
      sc_error error;
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
      const auto status = sc_view_next_frame(view, static_cast<uint32_t>(std::max<long long>(1, left)), &out.value, &error);
      if (status == SC_TIMEOUT) break;
      if (status != SC_OK) throw std::runtime_error(std::string("next frame: ") + error.message);
      ++seen;
      if (out.value.timestamp_us < last_timestamp_) backwards_ = true;
      last_timestamp_ = out.value.timestamp_us;
      if (want(out.value)) return seen;
    }
    sc_frame_release(&out.value);
    return -seen - 1;
  }
  bool timestamps_went_backwards() const { return backwards_; }

 private:
  fs::path artifacts_;
  std::uint64_t last_timestamp_ = 0;
  bool backwards_ = false;
};

fs::path runtime_from(const fs::path& given) {
  if (fs::is_regular_file(given)) return given.parent_path();  // the engine inside a runtime
  return given;
}

int frames_main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: suitecut-native-cef_embed_frames RUNTIME ARTIFACTS\n";
    return 2;
  }
  Run run(fs::absolute(argv[2]));
  fs::remove(fs::absolute(argv[2]) / "results.json");
  char session_dir[] = "/tmp/sc-ef-XXXXXX";  // short: the socket path must fit sun_path
  char profile_dir[] = "/tmp/sc-ef-profile-XXXXXX";
  if (!::mkdtemp(session_dir) || !::mkdtemp(profile_dir)) {
    std::perror("mkdtemp");
    return 1;
  }
  sc_session* session = nullptr;
  sc_view* view = nullptr;
  int engine_pid = 0;
  try {
    const auto runtime_path = fs::absolute(runtime_from(argv[1])).lexically_normal().string();
    run.stage("inspect");
    sc_runtime_options runtime_options;
    sc_runtime_options_init(&runtime_options);
    runtime_options.path = runtime_path.c_str();
    const char* requirement = std::getenv("SUITECUT_EMBED_SIGNING_REQUIREMENT");
    if (requirement && *requirement) runtime_options.signing_requirement = requirement;
    sc_runtime_info runtime;
    sc_runtime_info_init(&runtime);
    sc_error error;
    if (sc_runtime_inspect(&runtime_options, &runtime, &error) != SC_OK)
      throw std::runtime_error(std::string("inspect: ") + error.message);
    run.report["runtime"] = Json{{"path", runtime_path}, {"service", runtime.service_version},
                                 {"protocol", static_cast<int>(runtime.protocol)},
                                 {"signatureVerified", runtime.signature_verified != 0}};
    run.check(runtime.protocol == 1, "the runtime speaks protocol 1");

    run.stage("session");
    PageServer pages;
    sc_session_options options;
    sc_session_options_init(&options);
    options.runtime = &runtime;
    options.name = "embed-frames";
    options.directory = session_dir;
    options.profile = profile_dir;
    options.owner = "frames";
    options.client_name = "cef_embed_frames";
    options.scale = kScale;
    options.timeout_ms = 20000;
    options.flags = SC_SESSION_START;
    if (sc_session_open(&options, &session, &error) != SC_OK) throw std::runtime_error(std::string("open: ") + error.message);
    sc_json* described = nullptr;
    if (sc_session_describe(session, &described, &error) != SC_OK) throw std::runtime_error("describe");
    std::size_t size = 0;
    const char* data = sc_json_data(described, &size);
    engine_pid = static_cast<int>(Json::parse(std::string_view(data, size)).at("engine").at("pid").as_int());
    sc_json_destroy(described);
    run.check(engine_pid > 0, "the session reports the engine's pid", engine_pid);

    run.stage("attach");
    sc_tab tab;
    sc_tab_init(&tab);
    if (sc_tab_open(session, pages.Url(kLight).c_str(), 0, &tab, &error) != SC_OK)
      throw std::runtime_error(std::string("open tab: ") + error.message);
    sc_surface_sink sink;
    sc_surface_sink_init(&sink);
    sc_view_options view_options;
    sc_view_options_init(&view_options);
    view_options.page = tab.page;
    view_options.sink = &sink;
    view_options.css_width = 640;
    view_options.css_height = 400;
    if (sc_view_attach(session, &view_options, &view, &error) != SC_OK)
      throw std::runtime_error(std::string("attach: ") + error.message);

    // 1. The first frames carry the page at the view's size.
    run.stage("first-frame");
    Frame held;
    const int first = run.until(view, held, 20s, [](const sc_frame& f) { return near(centre_luma(f.surface), kLight); });
    run.check(first > 0, "a frame of the light page arrives", first);
    {
      const auto& f = held.value;
      auto surface = static_cast<IOSurfaceRef>(f.surface);
      run.check(f.width == 640u * kScale && f.height == 400u * kScale, "the frame is the view's size times the scale",
                Json{{"width", f.width}, {"height", f.height}});
      run.check(f.width % 2 == 0 && f.height % 2 == 0, "NV12 sides are even");
      run.check(f.pixel_format == SC_PIXEL_NV12_FULL_601 && IOSurfaceGetPixelFormat(surface) == SC_PIXEL_NV12_FULL_601,
                "the frame and its surface are '420f'");
      run.check(IOSurfaceGetWidth(surface) == f.width && IOSurfaceGetHeight(surface) == f.height &&
                    IOSurfaceGetPlaneCount(surface) == 2,
                "the surface has the frame's size and two planes");
      run.check(IOSurfaceIsInUse(surface), "a frame's surface is held in use until it is released");
      run.check(f.timestamp_us > 0 && f.received > 0, "frames carry a timestamp and a receive count",
                Json{{"received", static_cast<std::int64_t>(f.received)}});
      run.case_done("first-frame", Json{{"framesSeen", first}, {"luma", centre_luma(f.surface)}});
    }

    // 2. A held surface is never drawn into while the page changes and
    //    frames keep arriving.
    run.stage("held-surface");
    const auto held_id = IOSurfaceGetID(static_cast<IOSurfaceRef>(held.value.surface));
    if (sc_tab_navigate(session, tab.page, pages.Url(kDark).c_str(), &tab, &error) != SC_OK)
      throw std::runtime_error(std::string("navigate: ") + error.message);
    Frame current;
    const int to_dark =
        run.until(view, current, 20s, [](const sc_frame& f) { return near(centre_luma(f.surface), kDark); });
    run.check(to_dark > 0, "frames of the dark page arrive while one is held", to_dark);
    int after = 0, reused = 0;
    const auto counting = Clock::now();
    while (after < 30 && Clock::now() - counting < 10s) {
      const int got = run.until(view, current, 2s, [](const sc_frame&) { return true; });
      if (got < 0) break;
      ++after;
      if (IOSurfaceGetID(static_cast<IOSurfaceRef>(current.value.surface)) == held_id) ++reused;
    }
    const double seconds = std::chrono::duration<double>(Clock::now() - counting).count();
    run.check(after >= 10, "frames keep arriving from the animated page", Json{{"frames", after}, {"seconds", seconds}});
    run.check(reused == 0, "a held surface is not sent again", reused);
    const int still = centre_luma(held.value.surface);
    run.check(near(still, kLight), "the held surface still shows the light page", still);
    sc_frame_release(&held.value);
    run.check(held.value.surface == nullptr, "releasing a frame clears it");
    run.case_done("held-surface", Json{{"framesToDark", to_dark}, {"framesAfter", after},
                                       {"framesPerSecond", after / std::max(seconds, 0.001)}, {"heldLuma", still}});

    // 3. After a resize, frames arrive at the new size and keep coming.
    run.stage("resize");
    if (sc_view_resize(view, 500, 320, &error) != SC_OK) throw std::runtime_error(std::string("resize: ") + error.message);
    const int resized = run.until(view, current, 15s, [](const sc_frame& f) {
      return f.width == 500u * kScale && f.height == 320u * kScale;
    });
    run.check(resized > 0, "a frame at the new size arrives", resized);
    run.check(near(centre_luma(current.value.surface), kDark), "the resized frame carries the page");
    int resized_after = 0;
    for (int i = 0; i < 10; ++i) {
      if (run.until(view, current, 2s, [](const sc_frame& f) { return f.width == 1000 && f.height == 640; }) < 0) break;
      ++resized_after;
    }
    run.check(resized_after == 10, "frames keep arriving at the new size", resized_after);
    run.case_done("resize", Json{{"framesToNewSize", resized}, {"framesAfter", resized_after}});

    // 4. After detach, no more frames come, though the page still animates.
    run.stage("detach");
    sc_frame_release(&current.value);
    if (sc_view_detach(view, &error) != SC_OK) throw std::runtime_error(std::string("detach: ") + error.message);
    int in_flight = 0;
    while (run.until(view, current, 300ms, [](const sc_frame&) { return true; }) > 0) {
      if (++in_flight > 5) break;  // a few may already be on their way
    }
    run.check(in_flight <= 5, "at most a few frames were in flight at detach", in_flight);
    const int late = run.until(view, current, 3s, [](const sc_frame&) { return true; });
    run.check(late < 0, "no frame arrives in the 3 s after detach", late);
    run.check(sc_view_detach(view, &error) == SC_OK, "detaching twice is fine");
    sc_view_destroy(view);
    view = nullptr;
    run.case_done("detach", Json{{"inFlight", in_flight}});

    // 5. The tab can be attached again, so the silence was the detach.
    run.stage("reattach");
    view_options.css_width = 320;
    view_options.css_height = 240;
    if (sc_view_attach(session, &view_options, &view, &error) != SC_OK)
      throw std::runtime_error(std::string("attach again: ") + error.message);
    const int again = run.until(view, current, 15s, [](const sc_frame& f) { return f.width == 640 && f.height == 480; });
    run.check(again > 0, "frames come again after attaching anew", again);
    sc_frame_release(&current.value);
    sc_view_detach(view, &error);
    sc_view_destroy(view);
    view = nullptr;
    run.check(!run.timestamps_went_backwards(), "frame timestamps never go backwards");
    run.case_done("reattach", Json{{"frames", again}});

    run.stage("close");
    if (sc_session_close(session, 1, &error) != SC_OK) throw std::runtime_error(std::string("close: ") + error.message);
    sc_session_destroy(session);
    session = nullptr;
    const auto gone_by = Clock::now() + 15s;
    while (::kill(engine_pid, 0) == 0 && Clock::now() < gone_by) std::this_thread::sleep_for(100ms);
    run.check(::kill(engine_pid, 0) != 0, "the engine exits when the session closes");
    run.report["closed"] = true;
    run.stage("complete");
    run.report["ok"] = true;
  } catch (const std::exception& e) {
    run.report["error"] = e.what();
    std::cerr << "cef_embed_frames: " << e.what() << " (stage " << run.report["stage"].as_string() << ")\n";
  }
  if (view) sc_view_destroy(view);
  if (session) {
    sc_error ignored;
    sc_session_close(session, 1, &ignored);
    sc_session_destroy(session);
    const auto gone_by = Clock::now() + 15s;
    while (engine_pid > 0 && ::kill(engine_pid, 0) == 0 && Clock::now() < gone_by) std::this_thread::sleep_for(100ms);
  }
  std::error_code ignored;
  fs::remove_all(session_dir, ignored);
  fs::remove_all(profile_dir, ignored);
  run.write();
  std::cout << run.report.dump() << "\n";
  return run.report["ok"].as_bool() ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) { return frames_main(argc, argv); }
