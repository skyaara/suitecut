# Native report merging is a separate tool; it never starts workers or loads JS.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-callback-retry-merge-test callback_retry_merge_test.cc repeat_report_fixture.cc)
  target_link_libraries(suitecut-runner-callback-retry-merge-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-callback-retry-merge-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-callback-retry-merge COMMAND suitecut-runner-callback-retry-merge-test "${CMAKE_CURRENT_BINARY_DIR}/native-callback-retry-merge-data")
  set_tests_properties(native-runner-callback-retry-merge PROPERTIES TIMEOUT 30)
  add_executable(suitecut-runner-fail-on-flaky-report-test fail_on_flaky_report_test.cc fail_on_flaky_report_fixture.cc)
  target_link_libraries(suitecut-runner-fail-on-flaky-report-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-fail-on-flaky-report-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-fail-on-flaky-report COMMAND suitecut-runner-fail-on-flaky-report-test "${CMAKE_CURRENT_BINARY_DIR}/native-fail-on-flaky-report-data")
  set_tests_properties(native-runner-fail-on-flaky-report PROPERTIES TIMEOUT 30)
  add_executable(suitecut-runner-repeat-configuration-report-test repeat_configuration_report_test.cc)
  target_link_libraries(suitecut-runner-repeat-configuration-report-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-repeat-configuration-report-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-repeat-configuration-report COMMAND suitecut-runner-repeat-configuration-report-test)
  set_tests_properties(native-runner-repeat-configuration-report PROPERTIES TIMEOUT 30)
  add_executable(suitecut-runner-repeat-report-merge-test repeat_report_merge_test.cc repeat_report_fixture.cc)
  target_link_libraries(suitecut-runner-repeat-report-merge-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-repeat-report-merge-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-repeat-report-merge COMMAND suitecut-runner-repeat-report-merge-test
    "${CMAKE_CURRENT_BINARY_DIR}/native-repeat-report-merge-data")
  set_tests_properties(native-runner-repeat-report-merge PROPERTIES TIMEOUT 30)
  add_executable(suitecut-merge-reports merge_reports.cc)
  target_link_libraries(suitecut-merge-reports PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-merge-reports PRIVATE -Wall -Wextra -Werror -fexceptions)
  if(SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-report-merge COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-report-merge.py"
      --executable "$<TARGET_FILE:suitecut-merge-reports>"
      --report-fixture "$<TARGET_FILE:suitecut-runner-runner_report-test>"
      --attachment-fixture "$<TARGET_FILE:suitecut-runner-attachment_report-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-report-merge-data")
    set_tests_properties(native-runner-report-merge PROPERTIES TIMEOUT 40)
  endif()
endif()
