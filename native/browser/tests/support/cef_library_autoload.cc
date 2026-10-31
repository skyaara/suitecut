// SPDX-License-Identifier: MIT
// macOS loads CEF as a framework at run time. Tests that link libcef_lib rather
// than calling LoadSuiteCutCEFForTest() get it loaded here, before main.
#include <cstdlib>
#include "cef_library.h"

namespace {
[[maybe_unused]] const bool loaded = LoadSuiteCutCEFForTest() || (std::abort(), false);
}  // namespace
