# SPDX-License-Identifier: MIT
# Private object modules share one runtime binary and physical lifetime owner.
# They are not exported as separately versioned CEF-facing public libraries.
option(SUITECUT_BROWSER_PRECOMPILED_HEADERS "Precompile private page/controller headers" ON)

function(suitecut_browser_module name)
  add_library(suitecut-browser-${name} OBJECT ${ARGN})
  SET_LIBRARY_TARGET_PROPERTIES(suitecut-browser-${name})
  target_compile_features(suitecut-browser-${name} PRIVATE cxx_std_20)
  target_compile_options(suitecut-browser-${name} PRIVATE -fexceptions)
  target_include_directories(suitecut-browser-${name} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CEF_ROOT}" "${LIBYUV_ROOT}/include")
  target_link_libraries(suitecut-browser-${name} PRIVATE
    suitecut-api-cookie-policy suitecut-http-content-codec Threads::Threads
    OpenSSL::SSL OpenSSL::Crypto yuv)
  foreach(source IN LISTS ARGN)
    set_property(GLOBAL APPEND PROPERTY suitecut_browser_module_sources "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
  endforeach()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(suitecut-browser-${name} PRIVATE
      --param=ggc-min-expand=20 --param=ggc-min-heapsize=65536)
  endif()
  if(NOT name STREQUAL "pch")
    target_sources(suitecut-browser PRIVATE $<TARGET_OBJECTS:suitecut-browser-${name}>)
  endif()
endfunction()

suitecut_browser_module(protocol protocol/values.cc protocol/writer.cc)
suitecut_browser_module(runtime runtime/tasks.cc runtime/accessors.cc runtime/engine.cc
  runtime/lifecycle.cc runtime/commands.cc runtime/accepted_work.cc)
suitecut_browser_module(page page/lifecycle.cc page/bootstrap.cc page/capture.cc
  page/navigation.cc page/devtools.cc page/events.cc page/frames.cc page/handles.cc
  page/input.cc page/operations.cc page/dom.cc page/network.cc page/commands.cc)
suitecut_browser_module(process process/app.cc process/main.cc)

# Keep expensive private header parsing bounded without making correctness
# depend on a PCH. Protocol builds independently with its own direct includes.
if(SUITECUT_BROWSER_PRECOMPILED_HEADERS)
  suitecut_browser_module(pch runtime/precompiled.cc)
  target_precompile_headers(suitecut-browser-pch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/page/source.h")
  foreach(module IN ITEMS runtime page process)
    target_precompile_headers(suitecut-browser-${module} REUSE_FROM suitecut-browser-pch)
  endforeach()
endif()

# Discover only to verify the explicit inventories, never to select sources.
get_property(declared GLOBAL PROPERTY suitecut_browser_module_sources)
file(GLOB_RECURSE actual CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.cc" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/page/*.cc" "${CMAKE_CURRENT_SOURCE_DIR}/process/*.cc")
if(NOT SUITECUT_BROWSER_PRECOMPILED_HEADERS)
  list(REMOVE_ITEM actual "${CMAKE_CURRENT_SOURCE_DIR}/runtime/precompiled.cc")
endif()
list(SORT declared)
list(SORT actual)
if(NOT "${declared}" STREQUAL "${actual}")
  message(FATAL_ERROR "CEF module inventories must account for every implementation source exactly once")
endif()
