if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-project-output-roots-test project_output_roots_test.cc)
  target_link_libraries(suitecut-runner-project-output-roots-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-project-output-roots-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-project-output-roots COMMAND suitecut-runner-project-output-roots-test)
  if(SUITECUT_RUNNER_HOST_PROOFS AND SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE AND TARGET suitecut-proof-supervisor
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-host.js")
    add_test(NAME native-runner-project-output-roots-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-project-output.py"
      --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
      --supervisor "$<TARGET_FILE:suitecut-proof-supervisor>"
      --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
      --directory "${SUITECUT_RUNNER_ATTACHMENT_PROOF_DIRECTORY}/native-runner-project-output-data")
    set_tests_properties(native-runner-project-output-roots-public PROPERTIES TIMEOUT 240)
  endif()
endif()
