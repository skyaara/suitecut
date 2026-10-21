suitecut_sdk_module(c_api client.cpp)
target_link_libraries(suitecut_sdk_c_api PRIVATE suitecut_sdk_transport Threads::Threads)
