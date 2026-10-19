# Native visibility/action policy with controlled backend ingress. Live CEF
# acceptance remains a separate matching-browser gate.
add_executable(suitecut-locator-handler-source-test locator_handler_source_test.cc)
SET_EXECUTABLE_TARGET_PROPERTIES(suitecut-locator-handler-source-test)
target_compile_features(suitecut-locator-handler-source-test PRIVATE cxx_std_20)
target_compile_options(suitecut-locator-handler-source-test PRIVATE -UNDEBUG -fexceptions -Wall -Wextra -Werror)
target_include_directories(suitecut-locator-handler-source-test SYSTEM PRIVATE "${CEF_ROOT}")
target_link_libraries(suitecut-locator-handler-source-test PRIVATE libcef_lib libcef_dll_wrapper)
add_test(NAME native-locator-handler-source COMMAND suitecut-locator-handler-source-test)

add_executable(suitecut-locator-handler-callback-commands-test locator_handler_callback_commands_test.cc)
target_compile_features(suitecut-locator-handler-callback-commands-test PRIVATE cxx_std_20)
target_compile_options(suitecut-locator-handler-callback-commands-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME native-locator-handler-callback-commands COMMAND suitecut-locator-handler-callback-commands-test)
