# SPDX-License-Identifier: MIT
# Actual process controls; numeric/projection bounds remain native unit controls.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND TARGET suitecut-proof-supervisor AND SUITECUT_RUNNER_PYTHON
   AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-repeat-groups-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-repeat-groups.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
    --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-repeat-groups-public-data")
  # Nine independent45s guards +1s cleanup +10s physical each, plus52s
  # Python/SDK orchestration. Authored native callback budgets are unchanged.
  set_tests_properties(native-runner-repeat-groups-public PROPERTIES TIMEOUT 556)
endif()
