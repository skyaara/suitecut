// SPDX-License-Identifier: MIT
#pragma once
#if defined(__APPLE__)
#include "include/wrapper/cef_library_loader.h"
#include <cstdio>
#endif

inline bool LoadSuiteCutCEFForTest() {
#if defined(__APPLE__)
  if (!cef_load_library(SUITECUT_TEST_CEF_LIBRARY)) {
    std::fprintf(stderr, "Could not load the pinned native CEF test library\n");
    return false;
  }
#endif
  return true;
}
