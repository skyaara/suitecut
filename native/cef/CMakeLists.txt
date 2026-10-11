cmake_minimum_required(VERSION 3.21)
project(suitecut_cef VERSION 0.1.0 LANGUAGES C CXX)
# This entry point is deliberately encoder-free. Use native/browser directly
# for the full SuiteCut media engine.
set(SUITECUT_WITH_MEDIA OFF CACHE BOOL "Standalone CEF integration" FORCE)
enable_testing()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native/browser/CMakeLists.txt")
  # Standalone source archive.
  add_subdirectory(native/browser)
else()
  # Repository checkout.
  add_subdirectory(../browser native/browser)
endif()
