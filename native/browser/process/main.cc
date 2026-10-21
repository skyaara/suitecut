// SPDX-License-Identifier: MIT
#include "process/main.h"
#include "process/app.h"
#include "runtime/dependencies.h"
using namespace suitecut::browser;

// The browser process keeps the normal compiler stack protection. It is a
// separate non-inlined frame so no embedder buffers exist while a zygote child
// resets its stack canary inside CefExecuteProcess.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int RunSuiteCutBrowserMain(
    const CefMainArgs& args, int argc, char** argv
#if defined(__linux__)
    ,
    suitecut::runner::RetainedSourceEndpoint::Config runner_process_config
#endif
) {
#if defined(__linux__)
  std::optional<suitecut::runner::RetainedSourceEndpoint> runner_process_endpoint;
  try {
    runner_process_endpoint.emplace(runner_process_config);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "Native runner process endpoint: %s\n", error.what());
    return 1;
  }
#endif
  // CEF and subprocess libraries may print diagnostics to stdout. Reserve a
  // close-on-exec descriptor for media, then redirect ordinary stdout to stderr.
  const int output = dup(STDOUT_FILENO);
  if (output < 0 || fcntl(output, F_SETFD, FD_CLOEXEC) < 0 ||
      dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
    return 1;
  CefSettings settings;
  auto command = CefCommandLine::CreateCommandLine();
  command->InitFromArgv(argc, argv);
  const auto profile = command->GetSwitchValue("suitecut-profile").ToString();
  // Startup failures precede the asynchronous writer. A bounded initial error
  // packet fits in the empty stdout pipe and cannot race any media/control I/O.
  auto startup_error = [output](const char* message) {
    const std::string header =
        "{\"version\":1,\"type\":\"error\",\"bytes\":0,\"error\":\"" + std::string(message) + "\"}";
    const uint32_t size = static_cast<uint32_t>(header.size());
    const uint8_t prefix[] = {static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8),
                              static_cast<uint8_t>(size >> 16), static_cast<uint8_t>(size >> 24)};
    auto write_all = [output](const void* value, size_t length) {
      const auto* bytes = static_cast<const uint8_t*>(value);
      while (length) {
        const auto count = write(output, bytes, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return;
        bytes += count;
        length -= static_cast<size_t>(count);
      }
    };
    write_all(prefix, sizeof(prefix));
    write_all(header.data(), header.size());
    close(output);
    return 1;
  };
  if (profile.empty() || profile.front() != '/' || profile.size() > 4096)
    return startup_error("Native profile path must be an absolute directory");
  const bool persistent = command->GetSwitchValue("suitecut-persistent-profile") == "1";
  if (command->HasSwitch("suitecut-persistent-profile") &&
      command->GetSwitchValue("suitecut-persistent-profile") != "1" &&
      command->GetSwitchValue("suitecut-persistent-profile") != "0")
    return startup_error("Invalid native persistent profile configuration");
  std::error_code profile_error;
  std::filesystem::create_directories(profile, profile_error);
  if (profile_error || !std::filesystem::is_directory(profile, profile_error) || profile_error)
    return startup_error("Could not prepare native profile directory");
  const auto actual_profile = std::filesystem::canonical(profile, profile_error);
  if (profile_error) return startup_error("Could not resolve native profile directory");
  SuiteCutProfileLock profile_lock;
  std::string lock_error;
  if (persistent && !profile_lock.Acquire(actual_profile.string(), lock_error))
    return startup_error(lock_error.c_str());
  Writer writer(output);
  CefRefPtr<App> app = new App(writer);
#if defined(__linux__)
  app->BindAcceptedEndpoint(runner_process_endpoint ? &*runner_process_endpoint : nullptr);
#endif
  if (command->HasSwitch("suitecut-downloads-path")) {
    const auto path = command->GetSwitchValue("suitecut-downloads-path").ToString();
    if (path.empty() || path.size() > 4096 || path.front() != '/') return 1;
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error || !std::filesystem::is_directory(path, error) || error) return 1;
  }
  CefString(&settings.root_cache_path) = actual_profile.string();
  if (persistent) CefString(&settings.cache_path) = actual_profile.string();
  settings.windowless_rendering_enabled = true;
  settings.log_severity = LOGSEVERITY_DISABLE;
  settings.persist_session_cookies = persistent;
  // Ordinary launches keep an empty cache_path (incognito). Explicit persistent
  // launches use the selected real directory; CEF owns its storage and locks.
#if defined(__linux__)
  // Process startup only. Existing SDK browser/page readiness is independent.
  if (runner_process_endpoint->Enabled() && !runner_process_endpoint->WriteStartup()) {
    std::fprintf(stderr, "Native runner process startup receipt write failed\n");
    return 1;
  }
#endif
  if (!CefInitialize(args, settings, app, nullptr)) return 1;
  CefRunMessageLoop();
  app->ReleaseSource();
  app = nullptr;
  CefShutdown();
  writer.Stop();
#if defined(__linux__)
  // Actual process shutdown boundary, not idle, CloseReceipt or physical exit.
  if (runner_process_endpoint->Enabled() && !runner_process_endpoint->WriteShutdownReached(0)) {
    std::fprintf(stderr, "Native runner process shutdown-boundary receipt write failed\n");
    return 1;
  }
#endif
  return 0;
}

// Chromium intentionally replaces the stack canary after a Linux zygote fork.
// A protected wrapper spanning CefExecuteProcess would compare its old canary
// when the utility subprocess returns, producing a false stack-smashing abort.
// Only this buffer-free dispatch frame opts out; every browser/control frame
// remains protected and Chromium's sandbox and canary reset remain enabled.
#if defined(__linux__) && defined(__GNUC__)
__attribute__((noinline, no_stack_protector))
#endif
int RunSuiteCutBrowser(int argc, char** argv) {
  const int encoder = suitecut::MediaEncoderHelperMain(argc, argv);
  if (encoder >= 0) return encoder;
  signal(SIGPIPE, SIG_IGN);
#if defined(__linux__)
  // Trivial configuration only; no endpoint owner/buffer spans zygote dispatch.
  suitecut::runner::RetainedSourceEndpoint::Config runner_process_config;
  try {
    runner_process_config = suitecut::runner::RetainedSourceEndpoint::Prepare(argc, argv);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "Native runner process endpoint preparation: %s\n", error.what());
    return 1;
  }
#endif
  CefMainArgs args(argc, argv);
#if !defined(__APPLE__)
  const int code = CefExecuteProcess(args, new SuiteCutRendererApp(), nullptr);
  if (code >= 0) return code;
#endif
  return RunSuiteCutBrowserMain(args, argc, argv
#if defined(__linux__)
                                ,
                                runner_process_config
#endif
  );
}
