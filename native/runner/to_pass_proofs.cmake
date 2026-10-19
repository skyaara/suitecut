# SPDX-License-Identifier: MIT
# Actual callbacks only; native policy units own numeric/capacity boundaries.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND TARGET suitecut-proof-supervisor AND SUITECUT_RUNNER_PYTHON
   AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-to-pass-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-to-pass.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-to-pass-public-data")
  # Seven independent45s supervisor guards +1s cleanup+10s physical each,
  # plus52s orchestration. No author, assertion or cleanup budget is shortened.
  set_tests_properties(native-runner-to-pass-public PROPERTIES TIMEOUT 444)
  # The browser target is declared later by the parent project. Its generator
  # expression resolves after configuration; standalone runner builds omit it.
  if(CMAKE_PROJECT_NAME STREQUAL "suitecut_browser")
    add_test(NAME native-runner-to-pass-source COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-to-pass-source.py"
      --executable "$<TARGET_FILE:suitecut-runner>"
      --browser-executable "$<TARGET_FILE:suitecut-browser>"
      --node "${SUITECUT_RUNNER_NODE}"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-to-pass-source-data")
    # One 45s supervised scope, 1s cleanup and 10s physical retirement,
    # with orchestration allowance outside the original author budgets.
    set_tests_properties(native-runner-to-pass-source PROPERTIES TIMEOUT 75)
  endif()
endif()
