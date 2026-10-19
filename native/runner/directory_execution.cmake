# SPDX-License-Identifier: MIT
# Native collection/planning/report policy plus actual serial file activation.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component suite_discovery multi_file_catalog multi_file_plan directory_report directory_parallel_policy repeat_execution repeat_selection repeat_projection repeat_multi_file_plan repeat_group_partition)
    add_executable(suitecut-runner-${component}-test "${component}_test.cc")
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-${component} COMMAND suitecut-runner-${component}-test)
    set_tests_properties(native-runner-${component} PROPERTIES TIMEOUT 30)
  endforeach()
  if(SUITECUT_RUNNER_HOST_PROOFS AND TARGET suitecut-proof-supervisor
     AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-directory-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-directory.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-directory-public-data")
    set_tests_properties(native-runner-directory-public PROPERTIES TIMEOUT 240)
    add_test(NAME native-runner-directory-parallel-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-directory-parallel.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-directory-parallel-public-data")
    set_tests_properties(native-runner-directory-parallel-public PROPERTIES TIMEOUT 180)
    add_test(NAME native-runner-directory-project-output-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-directory-project-output.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-directory-project-output-data")
    set_tests_properties(native-runner-directory-project-output-public PROPERTIES TIMEOUT 240)
    if(TARGET suitecut-runner-coordinator-fault AND TARGET suitecut-runner-coordinator-io-fault)
      add_test(NAME native-runner-directory-driver-fault COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-directory-fault.py"
        --executable "$<TARGET_FILE:suitecut-runner-coordinator-fault>"
        --library "$<TARGET_FILE:suitecut-runner-coordinator-io-fault>"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>" --node "${SUITECUT_RUNNER_NODE}"
        --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-directory-driver-fault-data")
      set_tests_properties(native-runner-directory-driver-fault PROPERTIES TIMEOUT 40)
    endif()
  endif()
endif()
