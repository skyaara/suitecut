suitecut_sdk_module(core json.cpp credentials.cpp image.cpp)
target_link_libraries(suitecut_sdk_core PRIVATE OpenSSL::Crypto ZLIB::ZLIB Threads::Threads)
