suitecut_sdk_module(browser browser.cpp observation.cpp visual.cpp)
target_link_libraries(suitecut_sdk_browser PRIVATE suitecut_sdk_transport Threads::Threads)
