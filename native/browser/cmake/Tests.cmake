# SPDX-License-Identifier: MIT
# CEF exposes a dlopen-based wrapper on macOS, and a linkable libcef on Linux.
function(suitecut_link_cef_test target)
  target_link_libraries(${target} PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS})
  if(APPLE)
    target_compile_definitions(${target} PRIVATE
      SUITECUT_TEST_CEF_LIBRARY="${CEF_ROOT}/Release/Chromium Embedded Framework.framework/Chromium Embedded Framework")
  else()
    target_link_libraries(${target} PRIVATE libcef_lib)
    set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN")
  endif()
endfunction()
