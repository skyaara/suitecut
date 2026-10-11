// SPDX-License-Identifier: MIT
#include <csignal>
#include "include/base/cef_compiler_specific.h"
#include "include/cef_app.h"
int RunSuiteCutBrowser(int argc, char** argv);

// Chromium changes the stack canary after a zygote fork. Only this small
// subprocess dispatcher crosses that boundary; browser/media code keeps SSP.
NO_STACK_PROTECTOR
int main(int argc, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  CefMainArgs args(argc, argv);
  const int code = CefExecuteProcess(args, nullptr, nullptr);
  if (code >= 0) return code;
  return RunSuiteCutBrowser(argc, argv);
}
