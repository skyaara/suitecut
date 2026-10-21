# SPDX-License-Identifier: MIT
install(TARGETS suitecut_client EXPORT SuiteCutClientTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Explicit public inventory: private backend/transport headers cannot leak.
set(client_public_headers client.h browser.hpp transport.hpp json.hpp credentials.hpp
  image.hpp observation.hpp visual.hpp agent.hpp agent_driver.hpp agent_tools.hpp simple_agent.hpp)
foreach(header IN LISTS client_public_headers)
  install(FILES "${PROJECT_SOURCE_DIR}/include/suitecut/${header}" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/suitecut)
endforeach()
install(FILES README.md VALIDATION.md ARCHITECTURE.md ../../LICENSE ../../THIRD_PARTY_NOTICES.md
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/SuiteCutClient)
install(DIRECTORY skills/ DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/SuiteCutClient/skills)
install(EXPORT SuiteCutClientTargets NAMESPACE SuiteCut:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SuiteCutClient)
configure_package_config_file(cmake/SuiteCutClientConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/SuiteCutClientConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SuiteCutClient)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/SuiteCutClientConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SuiteCutClientConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/SuiteCutClientConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SuiteCutClient)
