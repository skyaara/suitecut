// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "include/cef_app.h"

// Opened from Finder, the Dock or Spotlight there is no SDK on the other end.
// SuiteCut then runs as an ordinary Chrome-style browser for the person using
// it, with Chrome's multiple profiles. SDK launches always name a profile
// directory and never take this path.
namespace suitecut::browser::standalone {

bool Requested(int argc, char** argv);
int Run(const CefMainArgs& args, int argc, char** argv);

// Browser-UI-thread calls for the platform menus (the macOS main thread).
struct MenuProfile {
  std::string dir, name;
};
std::vector<MenuProfile> Profiles();
// Each open browser's native view and the profile directory it belongs to.
std::vector<std::pair<void*, std::string>> Windows();
// An empty directory opens the profile used last.
void Open(const std::string& dir);
void Add(const std::string& name);
void Rename(const std::string& dir, const std::string& name);
// With no window open (closed the last one, then clicked the Dock icon).
void Reopen();
void Quit();

}  // namespace suitecut::browser::standalone
