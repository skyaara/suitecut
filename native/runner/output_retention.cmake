# SPDX-License-Identifier: MIT
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS output_retention_policy output_retention_files output_retention_finalize)
    add_executable(suitecut-runner-${component}-test "${CMAKE_CURRENT_LIST_DIR}/${component}_test.cc")
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-${component} COMMAND suitecut-runner-${component}-test)
    set_tests_properties(native-runner-${component} PROPERTIES TIMEOUT 30)
  endforeach()
endif()
function(suitecut_register_output_retention_public)
  if(TARGET suitecut-proof-supervisor AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner.js")
    add_test(NAME native-runner-output-retention-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-output-retention.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-output-retention-public-data")
    set_tests_properties(native-runner-output-retention-public PROPERTIES TIMEOUT 180)
  endif()
endfunction()
