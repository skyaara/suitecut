// SPDX-License-Identifier: MIT
#pragma once

#include "include/cef_app.h"

// Prototype, not a product mode: one native CEF Views window with our own
// HTML toolbar and tab strip above the page, instead of Chrome's interface.
// It answers whether tabs stay alive in the background and which Chrome
// features remain with Chrome-style or Alloy-style pages.
//
//   --suitecut-prototype=tabs --suitecut-prototype-root=<empty temp dir>
//   [--suitecut-prototype-pages=chrome|alloy] [--suitecut-prototype-switch=swap|hide]
//   [--suitecut-prototype-selftest] [--suitecut-prototype-control]
//   [--suitecut-prototype-autodialog]
//   [--suitecut-prototype-url=<first page>] [--suitecut-prototype-permissions=ask|allow|deny]
//   [--suitecut-prototype-dialogs=control] [--suitecut-prototype-size=<width>x<height>]
//
// With --suitecut-prototype-control it reads one JSON command per line on stdin
// and writes answers ({id, ok, result}) and events ({event, tab, ...}) on stdout.
// src/shared-browser.ts is the TypeScript client.
namespace suitecut::browser::prototype {

bool Requested(int argc, char** argv);
int Run(const CefMainArgs& args, int argc, char** argv);

}  // namespace suitecut::browser::prototype
