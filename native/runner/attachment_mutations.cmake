# SPDX-License-Identifier: MIT
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(component IN ITEMS attachment_callback_report attachment_callback_retry attachment_append_source attachment_mutations attachment_mutations_allocation attachment_view_receipts attachment_mutation_ingress attachment_projection_report)
    add_executable(suitecut-runner-${component}-test "${CMAKE_CURRENT_LIST_DIR}/${component}_test.cc")
    target_link_libraries(suitecut-runner-${component}-test PRIVATE suitecut-runner-core)
    target_compile_options(suitecut-runner-${component}-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
    add_test(NAME native-runner-${component} COMMAND suitecut-runner-${component}-test)
  endforeach()
  add_executable(suitecut-runner-attachment_mutation_files-test "${CMAKE_CURRENT_LIST_DIR}/attachment_mutation_files_test.cc")
  target_link_libraries(suitecut-runner-attachment_mutation_files-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-attachment_mutation_files-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-attachment_projection_xml-test "${CMAKE_CURRENT_LIST_DIR}/attachment_projection_xml_test.cc")
  target_link_libraries(suitecut-runner-attachment_projection_xml-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-attachment_projection_xml-test PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
endif()
function(suitecut_register_mutable_attachment_files)
  if(TARGET suitecut-runner-attachment_projection_xml-test AND SUITECUT_RUNNER_PYTHON)
    add_test(NAME native-runner-attachment-projection-xml COMMAND "${SUITECUT_RUNNER_PYTHON}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-projection-xml.py"
      --executable "$<TARGET_FILE:suitecut-runner-attachment_projection_xml-test>"
      --directory "${CMAKE_CURRENT_BINARY_DIR}/native-attachment-projection-xml-data")
    set_tests_properties(native-runner-attachment-projection-xml PROPERTIES TIMEOUT 30)
  endif()
  if(TARGET suitecut-runner-attachment-io-fault)
    add_test(NAME native-runner-attachment-mutable-files COMMAND suitecut-runner-attachment_mutation_files-test
      "${CMAKE_CURRENT_BINARY_DIR}/native-attachment-mutable-files-data" "$<TARGET_FILE:suitecut-runner-attachment-io-fault>")
    set_tests_properties(native-runner-attachment-mutable-files PROPERTIES TIMEOUT 45)
    if(SUITECUT_RUNNER_PYTHON AND SUITECUT_RUNNER_NODE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/native-runner-attachment-view.js")
      add_test(NAME native-runner-attachment-mutations-public COMMAND "${SUITECUT_RUNNER_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/check-native-runner-attachment-mutations.py"
        --executable "$<TARGET_FILE:suitecut-runner>" --node "${SUITECUT_RUNNER_NODE}"
        --sdk-directory "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        --library "$<TARGET_FILE:suitecut-runner-attachment-io-fault>"
        --directory "${CMAKE_CURRENT_BINARY_DIR}/native-attachment-mutations-public-data")
      set_tests_properties(native-runner-attachment-mutations-public PROPERTIES TIMEOUT 90)
    endif()
  endif()
endfunction()
