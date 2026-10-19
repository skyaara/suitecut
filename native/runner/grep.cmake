# Native matching is bounded by actual NFA work, independent of author hosts.
foreach(component pattern selection file vm configuration)
  add_executable(suitecut-runner-grep-${component}-test grep_${component}_test.cc)
  target_link_libraries(suitecut-runner-grep-${component}-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-grep-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-grep-${component} COMMAND suitecut-runner-grep-${component}-test)
  set_tests_properties(native-runner-grep-${component} PROPERTIES TIMEOUT 20)
endforeach()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SUITECUT_RUNNER_HOST_PROOFS
   AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
  add_test(NAME native-runner-grep-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-grep.py"
    --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
    --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
    --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    --directory "${CMAKE_CURRENT_BINARY_DIR}/native-runner-grep-data")
  set_tests_properties(native-runner-grep-public PROPERTIES TIMEOUT 120)
endif()
add_executable(suitecut-runner-grep-oracle-test grep_oracle_test.cc)
target_link_libraries(suitecut-runner-grep-oracle-test PRIVATE suitecut-runner-core)
target_compile_options(suitecut-runner-grep-oracle-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
add_test(NAME native-runner-grep-stock-contract COMMAND suitecut-runner-grep-oracle-test
  "${CMAKE_CURRENT_SOURCE_DIR}/contracts/native-grep-stock-contract.json")
set_tests_properties(native-runner-grep-stock-contract PROPERTIES TIMEOUT 20)
add_executable(suitecut-runner-grep-config-oracle-test grep_config_oracle_test.cc)
target_link_libraries(suitecut-runner-grep-config-oracle-test PRIVATE suitecut-runner-core)
target_compile_options(suitecut-runner-grep-config-oracle-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
add_test(NAME native-runner-grep-config-stock-contract COMMAND suitecut-runner-grep-config-oracle-test
  "${CMAKE_CURRENT_SOURCE_DIR}/contracts/native-grep-config-stock-contract.json")
set_tests_properties(native-runner-grep-config-stock-contract PROPERTIES TIMEOUT 20)
add_executable(suitecut-runner-grep-node-oracle-test grep_node_oracle_test.cc)
target_link_libraries(suitecut-runner-grep-node-oracle-test PRIVATE suitecut-runner-core)
target_compile_options(suitecut-runner-grep-node-oracle-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
add_test(NAME native-runner-grep-node-regexp-contract COMMAND suitecut-runner-grep-node-oracle-test
  "${CMAKE_CURRENT_SOURCE_DIR}/contracts/native-grep-node-regexp-contract.json")
set_tests_properties(native-runner-grep-node-regexp-contract PROPERTIES TIMEOUT 30)
add_test(NAME native-runner-grep-node-vsets-contract COMMAND suitecut-runner-grep-node-oracle-test
  "${CMAKE_CURRENT_SOURCE_DIR}/contracts/native-grep-node-vsets-contract.json")
set_tests_properties(native-runner-grep-node-vsets-contract PROPERTIES TIMEOUT 30)
