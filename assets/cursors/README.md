# macOS cursor artwork

Source: https://github.com/ful1e5/apple_cursor/tree/fe044a42b4e9cea16ea762d2ed9004f6d7b35543
Author: Abdulkaiz Khatri and apple_cursor contributors.
License: GPL-3.0, reproduced in LICENSE.

These are the original editable SVG sources for the arrow, pointing hand, and
text cursor embedded in src/presentation.ts. The embedded versions replace
#00FF00 with #000000 and #0000FF with #FFFFFF, as specified by upstream's
macOS render.json theme. Their canvas is displayed at 28px, with CSS classes
and aria-hidden added. Paths and built-in shadows are unchanged.

Hotspots from configs/x.build.toml, in source coordinates:
- left_ptr: 80, 38 on a 256 by 256 canvas
- hand2: 92, 53 on a 257 by 257 canvas
- xterm: 129, 136 on a 257 by 256 canvas
