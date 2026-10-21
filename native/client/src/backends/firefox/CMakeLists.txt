suitecut_sdk_module(firefox bidi_backend.cpp bidi_dom.cpp bidi_features.cpp bidi_har.cpp
  bidi_privileged.cpp bidi_permissions.cpp)
target_link_libraries(suitecut_sdk_firefox PRIVATE suitecut_sdk_wire Threads::Threads OpenSSL::Crypto)
target_include_directories(suitecut_sdk_firefox PRIVATE "${PROJECT_SOURCE_DIR}/../shared")
