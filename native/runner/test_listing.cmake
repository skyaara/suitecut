# Real module collection, selectors and list/run identity parity; no CEF helper.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-list-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-list.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-list-data")
  set_tests_properties(native-runner-list-public PROPERTIES TIMEOUT 90)
endif()
