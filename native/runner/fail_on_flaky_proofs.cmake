# SPDX-License-Identifier: MIT
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND TARGET suitecut-proof-supervisor AND TARGET suitecut-merge-reports
   AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-fail-on-flaky-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-fail-flaky.py"
    --executable "$<TARGET_FILE:suitecut-runner>"
    --merge-executable "$<TARGET_FILE:suitecut-merge-reports>"
    --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-fail-on-flaky-public-data")
  # Five45s native-run guards and two15s merge guards, each with1s cleanup
  # and10s physical retirement, plus40s Python/SDK orchestration.
  set_tests_properties(native-runner-fail-on-flaky-public PROPERTIES TIMEOUT 372)
endif()
