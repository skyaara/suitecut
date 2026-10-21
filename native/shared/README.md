# Shared native helpers

These authored DOM/ARIA script literals are used by both the CEF runtime and
external CDP/BiDi backends. They contain no CEF headers, platform implementation,
browser process state, or public SDK ABI. Keep includes within this directory.

ARIA formatters come from `src/native-aria.ts`. From the repository root:

```sh
node scripts/generate-native-aria-formatter.mjs src/native-aria.ts native/shared/native_aria_formatter.h
node scripts/generate-native-aria-formatter.mjs src/native-aria.ts native/shared/native_aria_json.h formatNativeAriaJSON
```

The native ARIA/reference tests compare the script literals with the authored
TypeScript. Regenerate and run those tests together whenever formatting changes.
The SDK source distribution includes these generated headers so an SDK consumer
does not need Node, TypeScript, or the CEF source tree.
