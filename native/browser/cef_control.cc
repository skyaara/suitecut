// SPDX-License-Identifier: MIT
// Minimal initialization control: no SuiteCut automation, capture, or media.
#include <cstdio>
#include "include/cef_app.h"
#include "include/cef_command_line.h"

int main(int argc, char** argv) {
  CefMainArgs args(argc, argv);
  const int result = CefExecuteProcess(args, nullptr, nullptr);
  if (result >= 0) return result;
  auto command = CefCommandLine::CreateCommandLine();
  command->InitFromArgv(argc, argv);
  const auto profile = command->GetSwitchValue("control-profile");
  if (profile.empty()) { std::fprintf(stderr, "Pass --control-profile=/absolute/path\n"); return 2; }
  CefSettings settings;
  CefString(&settings.root_cache_path) = profile;
  settings.windowless_rendering_enabled = true;
  std::fprintf(stderr, "CEF-only: initializing\n");
  if (!CefInitialize(args, settings, nullptr, nullptr)) return 1;
  std::fprintf(stderr, "CEF-only: initialized\n");
  command = nullptr;
  CefShutdown();
  std::fprintf(stderr, "CEF-only: shutdown\n");
  return 0;
}
