# SPDX-License-Identifier: MIT
# The session socket: startup lock, readiness, framing and peer checks. It is
# shared with suitecut-native-control's named sessions.
add_library(suitecut_session_socket STATIC control_session.cpp)
target_include_directories(suitecut_session_socket PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(suitecut_session_socket PUBLIC SuiteCut::Client)
if(SUITECUT_CLIENT_SANITIZERS)
  target_compile_definitions(suitecut_session_socket PRIVATE SUITECUT_CLI_SANITIZERS=1)
endif()

if(NOT SUITECUT_CLIENT_BUILD_SESSION)
  return()
endif()

# suitecut-session, the product service, and suitecut-shared-session, the same
# program under the name embedding apps already run (Napdock's helper checks
# that exact path in its bundle). Both are built from one set of objects.
add_library(suitecut_session_objects OBJECT host.cpp protocol.cpp main.cpp)
target_compile_definitions(suitecut_session_objects PRIVATE SUITECUT_SESSION_VERSION="${PROJECT_VERSION}")
target_link_libraries(suitecut_session_objects PRIVATE suitecut_session_socket OpenSSL::Crypto)
set(session_programs suitecut-session suitecut-shared-session)
foreach(program IN LISTS session_programs)
  add_executable(${program} $<TARGET_OBJECTS:suitecut_session_objects>)
  target_link_libraries(${program} PRIVATE suitecut_session_socket SuiteCut::Client OpenSSL::Crypto)
  set_target_properties(${program} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
  if(APPLE)
    set_target_properties(${program} PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
  elseif(UNIX)
    set_target_properties(${program} PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  endif()
endforeach()
install(TARGETS ${session_programs} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
