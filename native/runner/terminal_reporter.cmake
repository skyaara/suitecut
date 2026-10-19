# SPDX-License-Identifier: MIT
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-terminal-reporter-test terminal_reporter_test.cc)
  target_link_libraries(suitecut-runner-terminal-reporter-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-terminal-reporter-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-terminal-reporter COMMAND suitecut-runner-terminal-reporter-test)
  set_tests_properties(native-runner-terminal-reporter PROPERTIES TIMEOUT 30)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND TARGET suitecut-proof-supervisor AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-terminal-reporters-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-terminal-reporters.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-terminal-reporters-public-data")
  set_tests_properties(native-runner-terminal-reporters-public PROPERTIES TIMEOUT 180)
endif()
