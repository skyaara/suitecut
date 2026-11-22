# Shared native helpers

These authored DOM/ARIA script literals are used by both the CEF runtime and
external CDP/BiDi backends. They contain no CEF headers, platform implementation,
browser process state, or public SDK ABI. Keep includes within this directory.

`native_aria_formatter.h` and `native_aria_json.h` were generated from the
TypeScript formatter in `src/native-aria.ts`, which was removed with the Node
product (it is on the `archive/node` branch). Edit the script literals in these
headers directly now, and keep the native ARIA and reference tests passing.
