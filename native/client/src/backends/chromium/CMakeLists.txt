suitecut_sdk_module(chromium cdp_backend.cpp cdp_dom.cpp cdp_input.cpp cdp_page.cpp
  cdp_frame.cpp cdp_network.cpp cdp_media.cpp)
target_link_libraries(suitecut_sdk_chromium PRIVATE suitecut_sdk_wire Threads::Threads)
target_include_directories(suitecut_sdk_chromium PRIVATE "${PROJECT_SOURCE_DIR}/../shared")
