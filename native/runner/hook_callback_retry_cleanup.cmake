# Joint managed callback/parent physical fences before native cleanup admission.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(suitecut-runner-hook-callback-retry-cleanup-test
    hook_callback_retry_cleanup_test.cc)
  target_link_libraries(suitecut-runner-hook-callback-retry-cleanup-test PRIVATE suitecut-runner-core)
  target_compile_options(suitecut-runner-hook-callback-retry-cleanup-test
    PRIVATE -Wall -Wextra -Werror -UNDEBUG -fexceptions)
  add_test(NAME native-runner-hook-callback-retry-cleanup
    COMMAND suitecut-runner-hook-callback-retry-cleanup-test)
endif()
