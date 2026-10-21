suitecut_sdk_module(wire connection.cpp)
target_link_libraries(suitecut_sdk_wire PRIVATE suitecut_sdk_core OpenSSL::SSL Threads::Threads)
