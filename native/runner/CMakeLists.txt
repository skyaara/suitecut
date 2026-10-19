cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(suitecut_native_runner LANGUAGES CXX)
  enable_testing()
endif()
add_library(suitecut-runner-core INTERFACE)
target_include_directories(suitecut-runner-core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(suitecut-runner-core INTERFACE cxx_std_20)
include("${CMAKE_CURRENT_LIST_DIR}/attachment_mutations.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/output_retention.cmake")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-fixture-catalog-test fixture_catalog_test.cc)
  target_link_libraries(suitecut-runner-fixture-catalog-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-fixture-catalog-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-fixture-catalog COMMAND suitecut-runner-fixture-catalog-test)
  add_executable(suitecut-runner-fixture-options-test fixture_options_test.cc)
  target_link_libraries(suitecut-runner-fixture-options-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-fixture-options-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-fixture-options COMMAND suitecut-runner-fixture-options-test)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner native_runner.cc)
  target_link_libraries(suitecut-runner PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner PRIVATE -Wall -Wextra -Werror -fexceptions)
  foreach(component IN ITEMS fixture_graph retry_plan serial_group runner_retry_plan serial_group_report resource_locks resource_lock_report worker_process runner_report runner_interrupt scoped_fixtures hook_phases hook_worker_dispatch test_selection)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  endforeach()
  add_test(NAME native-runner-fixtures COMMAND suitecut-runner-fixture_graph-test)
  add_test(NAME native-runner-retries COMMAND suitecut-runner-retry_plan-test)
  add_test(NAME native-runner-serial-group-policy COMMAND suitecut-runner-serial_group-test)
  add_test(NAME native-runner-composed-retry-policy COMMAND suitecut-runner-runner_retry_plan-test)
  add_test(NAME native-runner-serial-group-reports COMMAND suitecut-runner-serial_group_report-test)
  add_test(NAME native-runner-resource-lock-policy COMMAND suitecut-runner-resource_locks-test)
  add_test(NAME native-runner-resource-lock-reports COMMAND suitecut-runner-resource_lock_report-test)
  add_test(NAME native-runner-interrupt-signals COMMAND suitecut-runner-runner_interrupt-test)
  find_program(SUITECUT_RUNNER_PYTHON NAMES python3)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-reports.py"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-report-data"
      --executable "$<TARGET_FILE:suitecut-runner-runner_report-test>")
  else()
    add_test(NAME native-runner-reports COMMAND suitecut-runner-runner_report-test
      "${CMAKE_CURRENT_BINARY_DIR}/native-runner-report-data")
  endif()
  option(SUITECUT_RUNNER_HOST_PROOFS "Test actual host callbacks and OS worker lifetime when Node is available" ON)
  find_program(SUITECUT_RUNNER_NODE NAMES node)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_NODE)
    add_test(NAME native-runner-process COMMAND suitecut-runner-worker_process-test "${SUITECUT_RUNNER_NODE}")
    add_executable(suitecut-runner-callback-proof callback_process_proof.cc)
    target_link_libraries(suitecut-runner-callback-proof PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-callback-proof PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    foreach(scenario IN ITEMS retry held)
      add_test(NAME native-runner-host-${scenario} COMMAND suitecut-runner-callback-proof
        "${SUITECUT_RUNNER_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/native-runner-callback-proof.mjs"
        "${CMAKE_CURRENT_BINARY_DIR}/native-runner-${scenario}.jsonl" "${scenario}")
    endforeach()
    set_tests_properties(native-runner-process native-runner-host-retry native-runner-host-held PROPERTIES TIMEOUT 20)
  else()
    message(STATUS "Native runner host proofs not registered: Node unavailable or SUITECUT_RUNNER_HOST_PROOFS=OFF")
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js")
  add_test(NAME native-runner-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-public.py"
    --executable "$<TARGET_FILE:suitecut-runner>"
    --node "${SUITECUT_RUNNER_NODE}"
    --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-public-data")
  # Aggregate12 cases:11*20s +45s guards =265s, plus orchestration.
  set_tests_properties(native-runner-public PROPERTIES TIMEOUT 300)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js")
  add_test(NAME native-runner-interruption COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-interrupt.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-interrupt-data")
  add_test(NAME native-runner-wrapper-signals COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-wrapper-signals.py"
    --node "${SUITECUT_RUNNER_NODE}" --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-wrapper-signal-data")
  add_test(NAME native-runner-parent-death COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-parent-death.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-parent-death-data")
  set_tests_properties(native-runner-interruption native-runner-wrapper-signals native-runner-parent-death PROPERTIES TIMEOUT 25)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-html-test runner_html_test.cc)
  target_link_libraries(suitecut-runner-html-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-html-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-html COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-html.py"
      --executable "$<TARGET_FILE:suitecut-runner-html-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-html-data")
    if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_NODE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
      add_test(NAME native-runner-html-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-html-public.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
        --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-html-public-data")
      set_tests_properties(native-runner-html-public PROPERTIES TIMEOUT 25)
    endif()
  else()
    add_test(NAME native-runner-html COMMAND suitecut-runner-html-test "${CMAKE_CURRENT_BINARY_DIR}/native-runner-html-data")
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME native-runner-hook-fixtures COMMAND suitecut-runner-scoped_fixtures-test)
  add_test(NAME native-runner-hook-phases COMMAND suitecut-runner-hook_phases-test)
  add_test(NAME native-runner-hook-worker-dispatch COMMAND suitecut-runner-hook_worker_dispatch-test)
  set_tests_properties(native-runner-hook-worker-dispatch PROPERTIES TIMEOUT 30)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_NODE)
    add_executable(suitecut-runner-host-reset-test host_reset_test.cc)
    target_link_libraries(suitecut-runner-host-reset-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-host-reset-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-control-reset COMMAND suitecut-runner-host-reset-test "${SUITECUT_RUNNER_NODE}")
    if(SUITECUT_RUNNER_PYTHON AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
      add_test(NAME native-runner-hooks-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-hooks.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
        --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-hooks-data")
      set_tests_properties(native-runner-hooks-public PROPERTIES TIMEOUT 45)
    endif()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME native-runner-selection COMMAND suitecut-runner-test_selection-test)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-selection-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-selection.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-selection-data")
  set_tests_properties(native-runner-selection-public PROPERTIES TIMEOUT 30)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-owned-process-inventory-test owned_process_inventory_test.cc)
  target_link_libraries(suitecut-runner-owned-process-inventory-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-owned-process-inventory-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-owned-process-inventory COMMAND suitecut-runner-owned-process-inventory-test)
  add_executable(suitecut-runner-descendants-test descendant_owner_test.cc)
  target_link_libraries(suitecut-runner-descendants-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-descendants-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-descendants COMMAND suitecut-runner-descendants-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_NODE)
    add_test(NAME native-runner-descendants-node COMMAND suitecut-runner-descendants-test --node "${SUITECUT_RUNNER_NODE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/native-runner-descendant-proof.mjs")
    if(SUITECUT_RUNNER_PYTHON AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
      add_test(NAME native-runner-descendants-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-descendants.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
        --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-descendants-data")
      set_tests_properties(native-runner-descendants-public PROPERTIES TIMEOUT 25)
    endif()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-skips-test runner_skips_test.cc)
  target_link_libraries(suitecut-runner-skips-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-skips-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-skips COMMAND suitecut-runner-skips-test "${CMAKE_CURRENT_BINARY_DIR}/native-runner-skips-data")
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-skips-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-skips.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
      --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-skips-public-data")
    set_tests_properties(native-runner-skips-public PROPERTIES TIMEOUT 20)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-focus-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-focus.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-focus-data")
  set_tests_properties(native-runner-focus-public PROPERTIES TIMEOUT 30)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-failure-limit-test failure_limit_test.cc)
  target_link_libraries(suitecut-runner-failure-limit-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-failure-limit-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-failure-limit COMMAND suitecut-runner-failure-limit-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-failure-limit-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-failure-limit.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
      --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-failure-limit-data")
    set_tests_properties(native-runner-failure-limit-public PROPERTIES TIMEOUT 40)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS suite_tree suite_worker_plan retry_exclusion)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-${component} COMMAND suitecut-runner-${component}-test)
  endforeach()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-suites-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-suites.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-suites-data")
    set_tests_properties(native-runner-suites-public PROPERTIES TIMEOUT 35)
    add_test(NAME native-runner-suite-modifiers-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-suite-modifiers.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-suite-modifiers-data")
    set_tests_properties(native-runner-suite-modifiers-public PROPERTIES TIMEOUT 30)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-suite-deadline-test suite_deadline_test.cc)
  target_link_libraries(suitecut-runner-suite-deadline-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-suite-deadline-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-suite-deadline COMMAND suitecut-runner-suite-deadline-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-global-timeout-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-global-timeout.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --host-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
      --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-global-timeout-data")
    set_tests_properties(native-runner-global-timeout-public PROPERTIES TIMEOUT 40)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-hook-metadata-test hook_metadata_test.cc)
  target_link_libraries(suitecut-runner-hook-metadata-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-hook-metadata-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-hook-metadata COMMAND suitecut-runner-hook-metadata-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-hook-info-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-hook-info.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-hook-info-data")
    set_tests_properties(native-runner-hook-info-public PROPERTIES TIMEOUT 25)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-phase-observation-test phase_observation_test.cc)
  target_link_libraries(suitecut-runner-phase-observation-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-phase-observation-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-phase-observation COMMAND suitecut-runner-phase-observation-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-phase-info-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-phase-info.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-phase-info-data")
    set_tests_properties(native-runner-phase-info-public PROPERTIES TIMEOUT 30)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-live-timeout-test live_timeout_test.cc)
  target_link_libraries(suitecut-runner-live-timeout-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-live-timeout-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-live-timeout COMMAND suitecut-runner-live-timeout-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    foreach(proof IN ITEMS live-timeout timeout-ownership)
      add_test(NAME native-runner-${proof}-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-${proof}.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-${proof}-data")
      set_tests_properties(native-runner-${proof}-public PROPERTIES TIMEOUT 30)
    endforeach()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-dynamic-skip-test dynamic_skip_test.cc)
  target_link_libraries(suitecut-runner-dynamic-skip-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-dynamic-skip-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-dynamic-skip COMMAND suitecut-runner-dynamic-skip-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    foreach(proof IN ITEMS dynamic-skip skip-ownership)
      add_test(NAME native-runner-${proof}-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-${proof}.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-${proof}-data")
      set_tests_properties(native-runner-${proof}-public PROPERTIES TIMEOUT 40)
    endforeach()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-dynamic-fail-test dynamic_fail_test.cc)
  target_link_libraries(suitecut-runner-dynamic-fail-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-dynamic-fail-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-dynamic-fail COMMAND suitecut-runner-dynamic-fail-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    foreach(proof IN ITEMS dynamic-fail failure-ownership)
      add_test(NAME native-runner-${proof}-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-${proof}.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-${proof}-data")
      set_tests_properties(native-runner-${proof}-public PROPERTIES TIMEOUT 40)
    endforeach()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-static-cleanup-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-static-cleanup.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-static-cleanup-data")
  set_tests_properties(native-runner-static-cleanup-public PROPERTIES TIMEOUT 20)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS beforeall_skip beforeall_skip_report)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  endforeach()
  add_test(NAME native-runner-beforeall-skip COMMAND suitecut-runner-beforeall_skip-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-beforeall-skip-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-beforeall-skip-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-beforeall_skip_report-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-beforeall-skip-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    foreach(proof IN ITEMS beforeall-skip beforeall-skip-ownership)
      add_test(NAME native-runner-${proof}-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-${proof}.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-${proof}-data")
      set_tests_properties(native-runner-${proof}-public PROPERTIES TIMEOUT 40)
    endforeach()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-modifier-annotations-test modifier_annotations_test.cc)
  target_link_libraries(suitecut-runner-modifier-annotations-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-modifier-annotations-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-modifier-annotations COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-modifier-metadata-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-modifier-annotations-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-modifier-annotation-report-data")
  else()
    add_test(NAME native-runner-modifier-annotations COMMAND suitecut-runner-modifier-annotations-test)
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    foreach(proof IN ITEMS modifier-metadata modifier-metadata-ownership)
      add_test(NAME native-runner-${proof}-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-${proof}.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-${proof}-data")
      set_tests_properties(native-runner-${proof}-public PROPERTIES TIMEOUT 40)
    endforeach()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS test_steps runner_steps step_wire test_steps_report)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  endforeach()
  add_test(NAME native-runner-steps COMMAND suitecut-runner-test_steps-test)
  add_test(NAME native-runner-step-composition COMMAND suitecut-runner-runner_steps-test)
  add_test(NAME native-runner-step-wire COMMAND suitecut-runner-step_wire-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-step-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-steps-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-test_steps_report-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-step-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-steps-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-steps.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-steps-data")
    set_tests_properties(native-runner-steps-public PROPERTIES TIMEOUT 45)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS test_step_skip runner_step_skip test_step_skip_report)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  endforeach()
  add_test(NAME native-runner-step-test-skip COMMAND suitecut-runner-test_step_skip-test)
  add_test(NAME native-runner-step-test-skip-composition COMMAND suitecut-runner-runner_step_skip-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-step-test-skip-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-test-skip-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-test_step_skip_report-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-step-test-skip-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    foreach(group IN ITEMS public ownership)
      if(group STREQUAL "public")
        set(step_skip_checker "check-native-runner-step-test-skip.py")
      else()
        set(step_skip_checker "check-native-runner-step-test-skip-ownership.py")
      endif()
      add_test(NAME native-runner-step-test-skip-${group} COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/${step_skip_checker}"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-step-test-skip-${group}-data")
      set_tests_properties(native-runner-step-test-skip-${group} PROPERTIES TIMEOUT 45)
    endforeach()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS local_step_skip runner_local_step_skip local_step_skip_report)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  endforeach()
  add_test(NAME native-runner-step-local-skip COMMAND suitecut-runner-local_step_skip-test)
  add_test(NAME native-runner-step-local-skip-composition COMMAND suitecut-runner-runner_local_step_skip-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-step-local-skip-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-local-skip-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-local_step_skip_report-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-step-local-skip-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    foreach(group IN ITEMS public ownership)
      if(group STREQUAL "public")
        set(local_skip_checker "check-native-runner-step-local-skip.py")
      else()
        set(local_skip_checker "check-native-runner-step-local-skip-ownership.py")
      endif()
      add_test(NAME native-runner-step-local-skip-${group} COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/${local_skip_checker}"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-step-local-skip-${group}-data")
      set_tests_properties(native-runner-step-local-skip-${group} PROPERTIES TIMEOUT 45)
    endforeach()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS declared_step_skip runner_declared_step_skip declared_step_skip_report)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  endforeach()
  add_test(NAME native-runner-step-declared-skip COMMAND suitecut-runner-declared_step_skip-test)
  add_test(NAME native-runner-step-declared-skip-composition COMMAND suitecut-runner-runner_declared_step_skip-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-step-declared-skip-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-declared-skip-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-declared_step_skip_report-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-step-declared-skip-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    foreach(group IN ITEMS public ownership)
      if(group STREQUAL "public")
        set(declared_skip_checker "check-native-runner-step-declared-skip.py")
      else()
        set(declared_skip_checker "check-native-runner-step-declared-skip-ownership.py")
      endif()
      add_test(NAME native-runner-step-declared-skip-${group} COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/${declared_skip_checker}"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-step-declared-skip-${group}-data")
      set_tests_properties(native-runner-step-declared-skip-${group} PROPERTIES TIMEOUT 45)
    endforeach()
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" CACHE PATH
    "Storage for native attachment test artifacts and preserved fault evidence")
  foreach(component IN ITEMS attachment_files runner_attachments attachment_report)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  endforeach()
  add_test(NAME native-runner-attachment-files COMMAND suitecut-runner-attachment_files-test)
  add_test(NAME native-runner-attachment-actor COMMAND suitecut-runner-runner_attachments-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-attachment-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-attachment_report-test>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachment-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_library(suitecut-runner-attachment-io-fault SHARED attachment_io_fault.cc)
    target_link_libraries(suitecut-runner-attachment-io-fault PRIVATE dl)
    target_compile_options(suitecut-runner-attachment-io-fault PRIVATE -Wall -Wextra -Werror)
    add_test(NAME native-runner-attachments-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachments.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachments-public-data")
    add_test(NAME native-runner-attachments-ownership COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-ownership.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --library "$<TARGET_FILE:suitecut-runner-attachment-io-fault>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachments-ownership-data")
    set_tests_properties(native-runner-attachments-public native-runner-attachments-ownership PROPERTIES TIMEOUT 90)
    add_test(NAME native-runner-control-eof-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-control-eof.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-control-eof-data")
    add_test(NAME native-runner-attachment-control-eof COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-control-eof.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --library "$<TARGET_FILE:suitecut-runner-attachment-io-fault>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachment-control-eof-data")
    add_test(NAME native-runner-control-eof-kill COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-control-eof-kill.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --library "$<TARGET_FILE:suitecut-runner-attachment-io-fault>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-control-eof-kill-data")
    set_tests_properties(native-runner-control-eof-public native-runner-attachment-control-eof native-runner-control-eof-kill PROPERTIES TIMEOUT 45)

  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS attachment_test_skip attachment_skip_drain)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-${component} COMMAND suitecut-runner-${component}-test)
  endforeach()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-attachment-test-skip-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-test-skip.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachment-test-skip-data")
    add_test(NAME native-runner-attachment-test-skip-ownership COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-test-skip-ownership.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --library "$<TARGET_FILE:suitecut-runner-attachment-io-fault>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachment-test-skip-ownership-data")
    set_tests_properties(native-runner-attachment-test-skip-public native-runner-attachment-test-skip-ownership PROPERTIES TIMEOUT 45)
  endif()
endif()

# Pure native project graph, metadata and actual serial scheduling proof.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS project_plan project_metadata project_report project_selection project_pool project_actor_completion test_work_groups work_group_pool work_group_locks work_group_report suite_file_owner file_group_pool file_group_locks file_group_report worker_reuse_pool worker_reuse_inventory worker_reuse_report worker_reuse_fences worker_reuse_soft_fences native_continuation project_descendants parallel_identity)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-${component} COMMAND suitecut-runner-${component}-test)
  endforeach()
  set_tests_properties(native-runner-worker_reuse_inventory PROPERTIES TIMEOUT 15)
  add_executable(suitecut-runner-work_group_process-test work_group_process_test.cc)
  target_link_libraries(suitecut-runner-work_group_process-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-work_group_process-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-work-group-process COMMAND suitecut-runner-work_group_process-test)
  set_tests_properties(native-runner-work-group-process PROPERTIES TIMEOUT 15)
  add_executable(suitecut-runner-file_group_process-test file_group_process_test.cc)
  target_link_libraries(suitecut-runner-file_group_process-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-file_group_process-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-file-group-process COMMAND suitecut-runner-file_group_process-test)
  set_tests_properties(native-runner-file-group-process PROPERTIES TIMEOUT 15)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js")
  add_test(NAME native-runner-projects-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-projects.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-projects-public-data")
  set_tests_properties(native-runner-projects-public PROPERTIES TIMEOUT 45)
  add_test(NAME native-runner-project-selectors-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-project-selectors.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-project-selectors-public-data")
  set_tests_properties(native-runner-project-selectors-public PROPERTIES TIMEOUT 45)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-step_attachments-test step_attachments_test.cc)
  target_link_libraries(suitecut-runner-step_attachments-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-step_attachments-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-step-attachments COMMAND suitecut-runner-step_attachments-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-step-attachments-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-attachments.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-step-attachments-data")
    add_test(NAME native-runner-step-attachments-ownership COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-attachments-ownership.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --library "$<TARGET_FILE:suitecut-runner-attachment-io-fault>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-step-attachments-ownership-data")
    set_tests_properties(native-runner-step-attachments-public native-runner-step-attachments-ownership PROPERTIES TIMEOUT 45)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js")
  add_test(NAME native-runner-project-parallel-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-project-parallel.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-project-parallel-public-data")
  set_tests_properties(native-runner-project-parallel-public PROPERTIES TIMEOUT 60)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-attachment_local_step_skip-test attachment_local_step_skip_test.cc)
  target_link_libraries(suitecut-runner-attachment_local_step_skip-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-attachment_local_step_skip-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-attachment-local-step-skip COMMAND suitecut-runner-attachment_local_step_skip-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-attachment-local-step-skip-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-attachment-local-skip.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachment-local-step-skip-data")
    foreach(scope IN ITEMS ownership ack)
      add_test(NAME native-runner-attachment-local-step-skip-${scope} COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-attachment-local-skip-${scope}.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --library "$<TARGET_FILE:suitecut-runner-attachment-io-fault>"
        --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachment-local-step-skip-${scope}-data")
    endforeach()
    set_tests_properties(native-runner-attachment-local-step-skip-public native-runner-attachment-local-step-skip-ownership native-runner-attachment-local-step-skip-ack PROPERTIES TIMEOUT 45)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-step_location-test step_location_test.cc)
  target_link_libraries(suitecut-runner-step_location-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-step_location-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-step-location-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-location-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-step_location-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-step-location-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-step-location-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-locations.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-step-location-public-data")
    set_tests_properties(native-runner-step-location-public PROPERTIES TIMEOUT 60)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js")
  add_test(NAME native-runner-serial-groups-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-serial-groups.py"
    --executable "$<TARGET_FILE:suitecut-runner>"
    --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-serial-groups-public-data")
  set_tests_properties(native-runner-serial-groups-public PROPERTIES TIMEOUT 60)
  add_test(NAME native-runner-resource-locks-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-resource-locks.py"
    --executable "$<TARGET_FILE:suitecut-runner>"
    --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-resource-locks-public-data")
  set_tests_properties(native-runner-resource-locks-public PROPERTIES TIMEOUT 60)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS runner_output_directories output_directory_phase output_directory_ownership)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  endforeach()
  add_test(NAME native-runner-output-directories COMMAND suitecut-runner-runner_output_directories-test)
  add_test(NAME native-runner-output-preparation-budget COMMAND suitecut-runner-output_directory_phase-test)
  add_test(NAME native-runner-output-directory-ownership COMMAND suitecut-runner-output_directory_ownership-test "$<TARGET_FILE:suitecut-runner-attachment-io-fault>")
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-output-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-output.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-output-public-data")
    add_test(NAME native-runner-output-public-ownership COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-output-ownership.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --library "$<TARGET_FILE:suitecut-runner-attachment-io-fault>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-output-public-ownership-data")
    set_tests_properties(native-runner-output-public native-runner-output-public-ownership PROPERTIES TIMEOUT 60)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-output-directory-option COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-output-directory-option.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-output-directory-option-data")
  set_tests_properties(native-runner-output-directory-option PROPERTIES TIMEOUT 45)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-boxed-step-test boxed_step_test.cc)
  target_link_libraries(suitecut-runner-boxed-step-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-boxed-step-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-boxed-step COMMAND suitecut-runner-boxed-step-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-boxed-step-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-boxed-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-boxed-step-test>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-boxed-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-boxed-step-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-boxed-steps.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-boxed-public-data")
    set_tests_properties(native-runner-boxed-step-public PROPERTIES TIMEOUT 60)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js")
  add_test(NAME native-runner-test-parallel-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-test-parallel.py"
    --executable "$<TARGET_FILE:suitecut-runner>"
    --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-test-parallel-public-data")
  set_tests_properties(native-runner-test-parallel-public PROPERTIES TIMEOUT 90)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-step-failure-source-test step_failure_source_test.cc)
  target_link_libraries(suitecut-runner-step-failure-source-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-step-failure-source-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-step-failure-source COMMAND suitecut-runner-step-failure-source-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-step-failure-source-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-source-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-step-failure-source-test>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-step-source-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-step-failure-source-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-step-sources.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-step-source-public-data")
    set_tests_properties(native-runner-step-failure-source-public PROPERTIES TIMEOUT 60)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  # Dedicated authored fault binary only. The production target has no fault
  # environment/CLI surface or compiled injection implementation.
  add_executable(suitecut-runner-coordinator-fault native_runner.cc)
  target_link_libraries(suitecut-runner-coordinator-fault PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-coordinator-fault PRIVATE -Wall -Wextra -Werror -fexceptions
    -include "${CMAKE_CURRENT_SOURCE_DIR}/coordinator_fault_test.h")
  add_library(suitecut-runner-coordinator-io-fault SHARED coordinator_io_fault.cc)
  target_link_libraries(suitecut-runner-coordinator-io-fault PRIVATE dl)
  target_compile_options(suitecut-runner-coordinator-io-fault PRIVATE -Wall -Wextra -Werror)
  add_test(NAME native-runner-coordinator-emergency-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-coordinator-faults.py"
    --executable "$<TARGET_FILE:suitecut-runner-coordinator-fault>"
    --library "$<TARGET_FILE:suitecut-runner-coordinator-io-fault>"
    --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-coordinator-emergency-public-data")
  set_tests_properties(native-runner-coordinator-emergency-public PROPERTIES TIMEOUT 90)
  add_test(NAME native-runner-file-coordinator-emergency-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-coordinator-faults.py"
    --anonymous-only
    --executable "$<TARGET_FILE:suitecut-runner-coordinator-fault>"
    --library "$<TARGET_FILE:suitecut-runner-coordinator-io-fault>"
    --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-file-coordinator-emergency-public-data")
  set_tests_properties(native-runner-file-coordinator-emergency-public PROPERTIES TIMEOUT 60)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js")
  add_test(NAME native-runner-anonymous-parallel-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-anonymous-parallel.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-anonymous-parallel-public-data")
  set_tests_properties(native-runner-anonymous-parallel-public PROPERTIES TIMEOUT 90)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-attachment-declarations-test attachment_declarations_test.cc)
  target_link_libraries(suitecut-runner-attachment-declarations-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-attachment-declarations-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-attachment-declarations COMMAND suitecut-runner-attachment-declarations-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-attachment-declaration-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-declaration-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-attachment-declarations-test>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachment-declaration-report-data")
  endif()
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-attachment-append-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-append.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachment-append-public-data")
    add_test(NAME native-runner-attachment-append-ownership COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-append-ownership.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --library "$<TARGET_FILE:suitecut-runner-attachment-io-fault>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attachment-append-ownership-data")
    set_tests_properties(native-runner-attachment-append-public native-runner-attachment-append-ownership PROPERTIES TIMEOUT 90)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js")
  add_test(NAME native-runner-worker-reuse-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-worker-reuse.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-worker-reuse-public-data")
  set_tests_properties(native-runner-worker-reuse-public PROPERTIES TIMEOUT 120)
  add_test(NAME native-runner-worker-reuse-soft-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-worker-reuse.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js" --soft-reuse-only
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-worker-reuse-soft-public-data")
  set_tests_properties(native-runner-worker-reuse-soft-public PROPERTIES TIMEOUT 60)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js")
  add_library(suitecut-runner-worker-reuse-idle-gate SHARED worker_reuse_idle_gate.cc)
  target_link_libraries(suitecut-runner-worker-reuse-idle-gate PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-worker-reuse-idle-gate PRIVATE -Wall -Wextra -Werror)
  add_test(NAME native-runner-worker-reuse-canceled-idle-ack-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-worker-reuse.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --cli-script "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-cli.js"
    --suite-api "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-suite.js"
    --idle-gate-library "$<TARGET_FILE:suitecut-runner-worker-reuse-idle-gate>" --held-idle-only
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-worker-reuse-canceled-idle-ack-public-data")
  set_tests_properties(native-runner-worker-reuse-canceled-idle-ack-public PROPERTIES TIMEOUT 45)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS soft_assertions runner_soft_assertions soft_phase soft_step_fence soft_assertion_report)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  endforeach()
  add_test(NAME native-runner-soft-policy COMMAND suitecut-runner-soft_assertions-test)
  add_test(NAME native-runner-soft-controller COMMAND suitecut-runner-runner_soft_assertions-test)
  add_test(NAME native-runner-soft-phases COMMAND suitecut-runner-soft_phase-test)
  add_test(NAME native-runner-soft-step-fence COMMAND suitecut-runner-soft_step_fence-test)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-soft-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-soft-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-soft_assertion_report-test>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-soft-report-data")
    if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_NODE
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
      add_test(NAME native-runner-soft-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-soft.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-soft-public-data")
      set_tests_properties(native-runner-soft-public PROPERTIES TIMEOUT 90)
    endif()
  endif()
endif()

# Staged native Source child registration/receipt policy. Eligibility unchanged.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS retained_source_leases retained_source_process)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-${component} COMMAND suitecut-runner-${component}-test)
  endforeach()
  set_tests_properties(native-runner-retained_source_process PROPERTIES TIMEOUT 20)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_program(SUITECUT_FIXTURE_LAYER_PYTHON NAMES python3)
  find_program(SUITECUT_FIXTURE_LAYER_NODE NAMES node)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_FIXTURE_LAYER_PYTHON AND SUITECUT_FIXTURE_LAYER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-fixture-bindings.js")
    add_test(NAME native-runner-fixture-layers-public COMMAND "${SUITECUT_FIXTURE_LAYER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-fixture-layers.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_FIXTURE_LAYER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-fixture-layers-data")
    set_tests_properties(native-runner-fixture-layers-public PROPERTIES TIMEOUT 90)
  endif()
endif()

# Native per-process endpoint transport only; no CEF reuse activation.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-retained_source_endpoint-test retained_source_endpoint_test.cc)
  target_link_libraries(suitecut-runner-retained_source_endpoint-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-retained_source_endpoint-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-retained_source_endpoint COMMAND suitecut-runner-retained_source_endpoint-test)
  set_tests_properties(native-runner-retained_source_endpoint PROPERTIES TIMEOUT 20)
endif()

# Retained process registration receiver only: no browser idle/reuse activation.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-retained_source_receiver-test retained_source_receiver_test.cc)
  target_link_libraries(suitecut-runner-retained_source_receiver-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-retained_source_receiver-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-retained_source_receiver COMMAND suitecut-runner-retained_source_receiver-test)
  set_tests_properties(native-runner-retained_source_receiver PROPERTIES TIMEOUT 20)
endif()

# A stopped test still owns genuine afterEach cleanup under its original clock.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-soft-cleanup-test soft_cleanup_test.cc)
  target_link_libraries(suitecut-runner-soft-cleanup-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-soft-cleanup-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-soft-cleanup-policy COMMAND suitecut-runner-soft-cleanup-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-soft-cleanup-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-soft-cleanup.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-soft-cleanup-data")
    set_tests_properties(native-runner-soft-cleanup-public PROPERTIES TIMEOUT 30)
  endif()
endif()

# Standalone checker supervision shares the existing adopted-child owner.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../tools" "${CMAKE_CURRENT_BINARY_DIR}/proof-tools")
endif()

include(report_merge.cmake)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-fixture-bindings.js")
  add_test(NAME native-runner-fixture-options-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-fixture-options.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-fixture-options-data")
  set_tests_properties(native-runner-fixture-options-public PROPERTIES TIMEOUT 60)
endif()

# Actual native owned-host/child registration transport; CEF reuse stays off.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-retained-source-host-test runner_retained_sources_test.cc)
  target_link_libraries(suitecut-runner-retained-source-host-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-retained-source-host-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-retained-source-host COMMAND suitecut-runner-retained-source-host-test)
  set_tests_properties(native-runner-retained-source-host PROPERTIES TIMEOUT 30)
  add_executable(suitecut-runner-source-callback-iteration-fence-test source_callback_iteration_fence_test.cc)
  target_link_libraries(suitecut-runner-source-callback-iteration-fence-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-source-callback-iteration-fence-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-source-callback-iteration-fence COMMAND suitecut-runner-source-callback-iteration-fence-test)
  set_tests_properties(native-runner-source-callback-iteration-fence PROPERTIES TIMEOUT 30)
  add_executable(suitecut-runner-retained-source-drain-test runner_retained_source_drain_test.cc)
  target_link_libraries(suitecut-runner-retained-source-drain-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-retained-source-drain-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-retained-source-drain COMMAND suitecut-runner-retained-source-drain-test)
  set_tests_properties(native-runner-retained-source-drain PROPERTIES TIMEOUT 30)
endif()

suitecut_register_mutable_attachment_files()
suitecut_register_output_retention_public()
# Distinct assertion deadlines are accepted failures without invented observations.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-soft-deadline-test soft_deadline_test.cc)
  target_link_libraries(suitecut-runner-soft-deadline-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-soft-deadline-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-soft-deadline-reports COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-soft-deadline-reports.py"
      --executable "$<TARGET_FILE:suitecut-runner-soft-deadline-test>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-soft-deadline-data")
  endif()
endif()

include(test_listing.cmake)
include(fixture_timeouts.cmake)
include(grep.cmake)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-attempt-error-attribution-test attempt_error_attribution_test.cc)
  target_link_libraries(suitecut-runner-attempt-error-attribution-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-attempt-error-attribution-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-attempt-error-attribution COMMAND suitecut-runner-attempt-error-attribution-test
    "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attempt-error-attribution-data")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND TARGET suitecut-proof-supervisor AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-attempt-error-attribution-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attempt-error-attribution.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-attempt-error-attribution-public-data")
  set_tests_properties(native-runner-attempt-error-attribution-public PROPERTIES TIMEOUT 180)
endif()

include(directory_execution.cmake)
include(project_output_roots.cmake)
include(terminal_reporter.cmake)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(work_check retained_source_work_wire retained_source_work_report)
    string(REPLACE "_" "-" work_name "${work_check}")
    add_executable(suitecut-runner-${work_name}-test ${work_check}_test.cc)
    target_link_libraries(suitecut-runner-${work_name}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${work_name}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-${work_name} COMMAND suitecut-runner-${work_name}-test)
  endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(snapshot_check IN ITEMS snapshot_paths snapshot_path_binding)
    string(REPLACE "_" "-" snapshot_name "${snapshot_check}")
    add_executable(suitecut-runner-${snapshot_name}-test ${snapshot_check}_test.cc)
    target_link_libraries(suitecut-runner-${snapshot_name}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${snapshot_name}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-${snapshot_name} COMMAND suitecut-runner-${snapshot_name}-test)
  endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND TARGET suitecut-proof-supervisor AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-snapshot-paths-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-snapshot-paths.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-snapshot-paths-public-data")
  set_tests_properties(native-runner-snapshot-paths-public PROPERTIES TIMEOUT 90)
  add_test(NAME native-runner-snapshot-suffix-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-snapshot-suffix.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-snapshot-suffix-public-data")
  set_tests_properties(native-runner-snapshot-suffix-public PROPERTIES TIMEOUT 90)
endif()

# Genuine repeated callbacks and shared native physical retirement.
include(repeat_execution_proofs.cmake)

# Actual repeated-group callbacks, global clocks and physical retirement.
include(repeat_group_proofs.cmake)

# Genuine final flaky-run outcomes; native retries and scheduling stay unchanged.
include(fail_on_flaky_proofs.cmake)

# Actual callback retries and physical retirement through the public SDK.
include(to_pass_proofs.cmake)
include(hook_callback_retry_cleanup.cmake)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-callback-retry-schedule-test callback_retry_schedule_test.cc)
  target_link_libraries(suitecut-runner-callback-retry-schedule-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-callback-retry-schedule-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-callback-retry-schedule COMMAND suitecut-runner-callback-retry-schedule-test)
  add_executable(suitecut-runner-callback-retry-steps-test callback_retry_steps_test.cc)
  target_link_libraries(suitecut-runner-callback-retry-steps-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-callback-retry-steps-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-callback-retry-steps COMMAND suitecut-runner-callback-retry-steps-test)
  add_executable(suitecut-runner-callback-retry-admission-test callback_retry_admission_test.cc)
  target_link_libraries(suitecut-runner-callback-retry-admission-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-callback-retry-admission-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-callback-retry-admission COMMAND suitecut-runner-callback-retry-admission-test)
  add_executable(suitecut-runner-callback-retry-controller-test runner_callback_retries_test.cc)
  target_link_libraries(suitecut-runner-callback-retry-controller-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-callback-retry-controller-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-callback-retry-controller COMMAND suitecut-runner-callback-retry-controller-test)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-callback-retry-report-test callback_retry_report_test.cc)
  target_link_libraries(suitecut-runner-callback-retry-report-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-callback-retry-report-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/callback-retry-report")
  add_test(NAME native-runner-callback-retry-report COMMAND suitecut-runner-callback-retry-report-test "${CMAKE_CURRENT_BINARY_DIR}/callback-retry-report")
endif()
