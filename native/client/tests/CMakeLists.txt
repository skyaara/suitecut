# SPDX-License-Identifier: MIT
# Explicit inventories: deleting or misspelling a test is a configure error.
# Keep runtime outputs compatible with the standalone browser qualification runner.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
# Only white-box tests see these private headers; consumers see include/ alone.
add_library(suitecut_client_test_internals INTERFACE)
target_include_directories(suitecut_client_test_internals INTERFACE
  "${PROJECT_SOURCE_DIR}/src/transport" "${PROJECT_SOURCE_DIR}/src/wire"
  "${PROJECT_SOURCE_DIR}/src/backends/chromium" "${PROJECT_SOURCE_DIR}/src/backends/firefox"
  "${PROJECT_SOURCE_DIR}/src/agent")
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME client_layout COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check-layout.py")
  add_test(NAME client_layout_negative_controls COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/architecture/test_layout.py")
  set_tests_properties(client_layout client_layout_negative_controls PROPERTIES
    TIMEOUT 30 LABELS "client;architecture")
endif()
set(client_unit_sources
  unit/agent_test.cpp
  unit/bidi_har_test.cpp
  unit/browser_facade_test.cpp
  unit/control_cli_test.cpp
  unit/credentials_test.cpp
  unit/decoder_test.cpp
  unit/fuzz_agent_observation_test.cpp
  unit/fuzz_json_decoder_test.cpp
  unit/image_test.cpp
  unit/json_test.cpp
  unit/network_body_transport_test.cpp
  unit/observation_test.cpp
  unit/review_c_abi_wait_test.cpp
  unit/simple_agent_test.cpp
  unit/transport_lifecycle_test.cpp
  unit/visual_test.cpp
)
foreach(fixture IN ITEMS transport_fake_engine browser_facade_engine)
  add_executable(${fixture} support/${fixture}.cpp)
  target_link_libraries(${fixture} PRIVATE SuiteCut::Client)
endforeach()

foreach(source IN LISTS client_unit_sources)
  get_filename_component(name "${source}" NAME_WE)
  add_executable(${name} "${source}")
  target_link_libraries(${name} PRIVATE SuiteCut::Client suitecut_client_test_internals)
  if(name STREQUAL "credentials_test" OR name STREQUAL "control_cli_test" OR name STREQUAL "bidi_har_test")
    target_link_libraries(${name} PRIVATE OpenSSL::Crypto)
  elseif(name STREQUAL "image_test")
    target_link_libraries(${name} PRIVATE ZLIB::ZLIB)
  endif()
  if(name STREQUAL "bidi_har_test" AND BUILD_SHARED_LIBS)
    # Mach-O two-level namespaces do not interpose mocks inside a shared library.
    target_sources(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/firefox/bidi_har.cpp")
  endif()
  if(name STREQUAL "control_cli_test")
    target_sources(${name} PRIVATE "${PROJECT_SOURCE_DIR}/examples/control_cli.cpp")
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/examples")
  endif()
  if(name STREQUAL "browser_facade_test")
    add_test(NAME ${name} COMMAND ${name} $<TARGET_FILE:browser_facade_engine>)
    add_dependencies(${name} browser_facade_engine)
  elseif(name STREQUAL "transport_lifecycle_test" OR name STREQUAL "network_body_transport_test")
    add_test(NAME ${name} COMMAND ${name} $<TARGET_FILE:transport_fake_engine>)
    add_dependencies(${name} transport_fake_engine)
  else()
    add_test(NAME ${name} COMMAND ${name})
  endif()
  # Assertions stay enabled even in Release; future assert-based tests cannot go green silently.
  if(MSVC)
    target_compile_options(${name} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${name} PRIVATE -UNDEBUG)
  endif()
  set_tests_properties(${name} PROPERTIES TIMEOUT 60 LABELS "client;unit")
endforeach()
add_executable(c_abi_test unit/c_abi_test.c)
target_link_libraries(c_abi_test PRIVATE SuiteCut::Client)
add_test(NAME c_abi_test COMMAND c_abi_test)
set_tests_properties(c_abi_test PROPERTIES TIMEOUT 60 LABELS "client;unit;abi")

set(client_integration_sources
  integration/cef_artifact_workflows.cpp
  integration/cef_media_workflows.cpp
  integration/cef_restricted_bootstrap.cpp
  integration/chromium_attach.cpp
  integration/chromium_real_browser.cpp
  integration/chromium_workflows.cpp
  integration/cross_browser_features.cpp
  integration/firefox_attach.cpp
  integration/firefox_drag_workflows.cpp
  integration/firefox_workflows.cpp
  integration/gap_agent_workflows.cpp
  integration/media_workflows.cpp
  integration/real_browser.cpp
  integration/tls_workflows.cpp
)
set(client_integration_targets)
foreach(source IN LISTS client_integration_sources)
  get_filename_component(name "${source}" NAME_WE)
  if(name STREQUAL "real_browser")
    set(target native-client-real-browser)
  elseif(name STREQUAL "chromium_real_browser")
    set(target suitecut-native-chromium-real-browser)
  elseif(name STREQUAL "chromium_workflows")
    set(target suitecut-native-chromium-workflows)
  elseif(name STREQUAL "chromium_attach")
    set(target suitecut-native-chromium-attach)
  else()
    set(target suitecut-native-${name})
  endif()
  add_executable(${target} "${source}")
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
  target_link_libraries(${target} PRIVATE SuiteCut::Client suitecut_client_test_internals)
  list(APPEND client_integration_targets ${target})
endforeach()
# Browser workflows run through tests/cef's supervisor, not an ambient browser.
add_custom_target(client-integration-binaries DEPENDS ${client_integration_targets})
add_custom_target(client-unit-binaries DEPENDS c_abi_test)
foreach(source IN LISTS client_unit_sources)
  get_filename_component(name "${source}" NAME_WE)
  add_dependencies(client-unit-binaries ${name})
endforeach()
add_custom_target(client-cef-binaries DEPENDS native-client-real-browser
  suitecut-native-cross_browser_features suitecut-native-cef_artifact_workflows
  suitecut-native-cef_restricted_bootstrap suitecut-native-cef_media_workflows)
