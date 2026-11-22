# SPDX-License-Identifier: MIT
# embed.h: apps that show and drive a shared session's tabs (protocol 1).
suitecut_sdk_module(embed embed.cpp embed_apple.cpp)
target_link_libraries(suitecut_sdk_embed PRIVATE Threads::Threads)
