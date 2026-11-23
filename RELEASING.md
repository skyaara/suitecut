# Releasing suitecut

Only maintainers with release access should use this checklist. Nothing here
publishes on its own; publishing, pushing and deployment need explicit
authorization.

## What a release contains

suitecut ships native distributions only:

- the C++20 client SDK, C ABI and `suitecut-native-control` CLI, as SDK-only
  archives per platform (no CEF or browser binaries);
- the optional full-runtime archive built with `scripts/package-native-runtime.py`
  (Linux x64) or `scripts/package-native-runtime-macos.py` (macOS arm64); see
  [native full runtime](docs/native-full-runtime.md) and the
  [macOS recipe](docs/native-full-runtime-macos.md);
- the signed macOS embed kit built with `scripts/package-embed-kit-macos.py`; see
  [the embed kit](docs/embedding/embed-kit-macos.md).

Native SDK versioning and C ABI versioning are independent. Each platform needs
its own dependency and runtime qualification. Windows runtime packaging is not
implemented. Do not publish the GPL media bundle without its complete
corresponding source.

## Gates

1. Build the engine and client from a clean checkout against the pinned CEF SDK
   (`native/browser/cef-build.json`) and libyuv commit. Record the commit.
2. Run the client tests in Release and with `SUITECUT_CLIENT_SANITIZERS=ON`. Set
   `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` on Linux and
   `ASAN_OPTIONS=halt_on_error=1` on macOS, where ASan has no leak detector
   (`detect_leaks=1` aborts every test there), and
   `UBSAN_OPTIONS=halt_on_error=1`. Run the heavy JSON/packet and observation
   campaigns with `SUITECUT_CLIENT_FUZZ_ITERATIONS` and
   `SUITECUT_AGENT_FUZZ_ITERATIONS`; ordinary CTest uses smaller finite defaults.
3. Exercise the session worker with `scripts/fuzz-native-control.py`, passing an
   absolute CLI path, the built `simple_agent_test` engine and a new report path.
4. Build the engine with `-DSUITECUT_BUILD_BROWSER_CHECKS=ON` and run its CTest
   checks.
5. Run the [full CEF regression suite](docs/cef-regressions.md) with bounded
   workers against the exact candidate app and freshly built client. Retain the
   source and runtime fingerprints, failures and cleanup receipts.
6. Install static and shared client builds into fresh prefixes and compile
   independent C and C++ consumers using only installed headers and CMake
   exports, as described in the [SDK architecture](native/client/ARCHITECTURE.md).
   Verify the installed CLI, skill files, browser cleanup and runtime library
   requirements.
7. For the full runtime, verify native recording from a freshly extracted and
   relocated installation, including CEF page audio and external Chromium, using
   the bundled media tools.
8. For the embed kit, run `scripts/verify-embed-kit-macos.py` and
   `scripts/verify-embed-sample-macos.py` against the exact kit folder.

`native/shared/native_aria_formatter.h` and `native/shared/native_aria_json.h`
were generated from the removed TypeScript formatter; edit them by hand now and
keep their regression tests passing.

Publish only the exact tested archives. Record versions, source and binary
hashes, archive SHA-256, fuzz seeds and counts, platform requirements and
remaining backend limits in the release manifest. Passing finite fuzz tests
does not establish whole-browser leak freedom; prebuilt CEF is not sanitizer
instrumented by the client's sanitizer builds.

## Website deployment

Build and check the site before deploying to the existing Cloudflare Pages
project:

```sh
pnpm site:check
pnpm site:build
wrangler pages deploy site/dist --project-name suitecut --branch main
```

Cloudflare builds use the default `https://suitecut.aakashreddy.com` origin and
`/` base. Check the home page and `/docs` after publishing.
