# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)
project(SuiteCutCEFRegressions LANGUAGES NONE)
include(CTest)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
find_program(SUITECUT_NODE NAMES node REQUIRED)
get_filename_component(SUITECUT_ROOT "../.." ABSOLUTE)
set(SUITECUT_CEF_ENGINE "" CACHE FILEPATH "Actual compiled CEF executable")
set(SUITECUT_CLIENT_BUILD "" CACHE PATH "Compiled client regression binaries")
set(SUITECUT_FFMPEG "" CACHE FILEPATH "Runtime ffmpeg")
set(SUITECUT_FFPROBE "" CACHE FILEPATH "Runtime ffprobe")
option(SUITECUT_CEF_REAL_BROWSER_TESTS "Register full real-browser qualification" OFF)
if(BUILD_TESTING)
  add_test(NAME cef.harness.unit COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/unit" -p "test_*.py")
  set_tests_properties(cef.harness.unit PROPERTIES TIMEOUT 120 LABELS "cef;harness;unit")
  add_test(NAME cef.catalogue COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}"
    --node "${SUITECUT_NODE}" --list)
  set_tests_properties(cef.catalogue PROPERTIES TIMEOUT 30 LABELS "cef;harness;catalogue")
  if(SUITECUT_CEF_REAL_BROWSER_TESTS)
    foreach(required IN ITEMS SUITECUT_CEF_ENGINE SUITECUT_FFMPEG SUITECUT_FFPROBE)
      if(NOT EXISTS "${${required}}" OR IS_DIRECTORY "${${required}}")
        message(FATAL_ERROR "${required} must name the actual executable; no skips or browser fallback")
      endif()
    endforeach()
    foreach(binary IN ITEMS native-client-real-browser suitecut-native-cross_browser_features
        suitecut-native-cef_artifact_workflows suitecut-native-cef_restricted_bootstrap suitecut-native-cef_media_workflows)
      if(NOT EXISTS "${SUITECUT_CLIENT_BUILD}/${binary}")
        message(FATAL_ERROR "Missing C++ workflow: ${SUITECUT_CLIENT_BUILD}/${binary}; build client-cef-binaries")
      endif()
    endforeach()
    if(NOT EXISTS "${SUITECUT_ROOT}/dist/native.js")
      message(FATAL_ERROR "Missing built SDK; run pnpm build")
    endif()
    add_test(NAME cef.browser.full COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}"
      --node "${SUITECUT_NODE}" --engine "${SUITECUT_CEF_ENGINE}"
      --client-build "${SUITECUT_CLIENT_BUILD}" --sdk "${SUITECUT_ROOT}/dist"
      --ffmpeg "${SUITECUT_FFMPEG}" --ffprobe "${SUITECUT_FFPROBE}"
      --profile full --workers 2 --seed 211)
    set_tests_properties(cef.browser.full PROPERTIES TIMEOUT 1800 PROCESSORS 2
      RUN_SERIAL TRUE LABELS "cef;integration;browser")
  endif()
endif()
