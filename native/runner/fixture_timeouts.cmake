# Native fixture clock policy, source provenance and actual public adapter controls.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS clocks catalog)
    add_executable(suitecut-runner-fixture-timeout-${component}-test
      fixture_timeout_${component}_test.cc)
    target_link_libraries(suitecut-runner-fixture-timeout-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-fixture-timeout-${component}-test
      PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-fixture-timeout-${component}
      COMMAND suitecut-runner-fixture-timeout-${component}-test)
  endforeach()
  foreach(component IN ITEMS fixture_attempt_clocks hook_fixture_clocks
      flat_fixture_clocks scoped_fixture_clocks fixture_timeout_runtime fixture_default_timeouts fixture_declaration_sources fixture_source_setup_clock fixture_transition_time fixture_result_reporting)
    add_executable(suitecut-runner-${component}-test ${component}_test.cc)
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test
      PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    string(REPLACE "_" "-" test_name "${component}")
    add_test(NAME native-runner-${test_name} COMMAND suitecut-runner-${component}-test)
  endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-fixture-timeouts.js")
  add_test(NAME native-runner-fixture-timeouts-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-fixture-timeouts.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-fixture-timeouts-data")
  set_tests_properties(native-runner-fixture-timeouts-public PROPERTIES TIMEOUT 90)
  add_test(NAME native-runner-beforeall-duration-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-beforeall-duration.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-beforeall-duration-data")
  set_tests_properties(native-runner-beforeall-duration-public PROPERTIES TIMEOUT 30)
endif()
