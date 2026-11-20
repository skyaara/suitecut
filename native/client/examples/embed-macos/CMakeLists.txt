# SPDX-License-Identifier: MIT
# suitecut-embed-sample: a small AppKit host that shows one tab from the
# engine's IOSurface frames and forwards input, using only suitecut/embed.h.
enable_language(OBJCXX)
add_executable(suitecut-embed-sample main.mm)
target_compile_options(suitecut-embed-sample PRIVATE -fobjc-arc)
target_link_libraries(suitecut-embed-sample PRIVATE SuiteCut::Client
  "-framework AppKit" "-framework QuartzCore" "-framework CoreImage" "-framework ImageIO" "-framework IOSurface")
set_target_properties(suitecut-embed-sample PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
