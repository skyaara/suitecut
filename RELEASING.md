# Releasing SuiteCut

Only maintainers with npm and GitHub release access should use this checklist.

1. Start from a clean `main` branch and review every change since the previous tag.
2. Confirm the package name is still available or owned by the maintainer with `npm view suitecut`
   and `npm whoami`.
3. Replace `Unreleased` in `CHANGELOG.md` with the release date. Set the same version in
   `package.json` and `pnpm-lock.yaml`.
4. Run the release checks:

   ```sh
   pnpm install --frozen-lockfile
   pnpm check
   pnpm site:check
   pnpm site:build
   pnpm test:native:cadence
   pnpm test:audio-plugin
   pnpm test:kokoro
   SUITECUT_SHERPA_VITS_MODEL_DIR=/path/to/vits-model pnpm test:sherpa
   pnpm test:package
   SUITECUT_NATIVE_EXECUTABLE=/path/to/suitecut-browser pnpm test:native
   pnpm audit
   ```

5. Inspect `npm pack --dry-run --json`. The SuiteCut tarball should contain only `dist`, the README,
   changelog, licenses, package metadata, cursor/Kokoro assets, and native browser source/build pins.
   Compiled CEF binaries and SDKs must stay out of the npm tarball. Also inspect the shared
   Sherpa runtime and each family package:

   ```sh
   npm pack --dry-run --json --workspace @suitecut/audio-sherpa-core
   npm pack --dry-run --json --workspace @suitecut/audio-vits
   npm pack --dry-run --json --workspace @suitecut/audio-matcha
   npm pack --dry-run --json --workspace @suitecut/audio-kokoro-sherpa
   npm pack --dry-run --json --workspace @suitecut/audio-kitten
   npm pack --dry-run --json --workspace @suitecut/audio-zipvoice
   npm pack --dry-run --json --workspace @suitecut/audio-pocket
   npm pack --dry-run --json --workspace @suitecut/audio-supertonic
   ```

   Each tarball should contain compiled output, its README, and package metadata. It must not contain
   model files or TypeScript source.
6. Publish SuiteCut, then the shared runtime, then the matching family package versions. Use npm
   provenance when the npm account and repository are configured for it:

   ```sh
   npm publish --provenance --access public
   npm publish --provenance --access public --workspace @suitecut/audio-sherpa-core
   npm publish --provenance --access public --workspace @suitecut/audio-vits
   npm publish --provenance --access public --workspace @suitecut/audio-matcha
   npm publish --provenance --access public --workspace @suitecut/audio-kokoro-sherpa
   npm publish --provenance --access public --workspace @suitecut/audio-kitten
   npm publish --provenance --access public --workspace @suitecut/audio-zipvoice
   npm publish --provenance --access public --workspace @suitecut/audio-pocket
   npm publish --provenance --access public --workspace @suitecut/audio-supertonic
   ```

7. Verify the published package in a new temporary project before creating and pushing the matching
   Git tag and GitHub release.

Never test a release by publishing another version. Use `npm pack` and the package smoke test first.

## Browser-control and native SDK release gates

The release includes the `suitecut` and `@suitecut/browser-control` npm packages,
the eight audio packages, and separate Linux C++20/C ABI/CLI distributions.
Keep CEF and other browser runtimes outside the small SDK-only archives. The
optional full-runtime archive is built separately with
`scripts/package-native-runtime.py`; see [native full runtime](docs/native-full-runtime.md).
Verify native recording from a freshly extracted and relocated installation,
including CEF page audio and external Chromium, using the bundled media tools.
Do not publish the GPL media bundle without its complete corresponding source.
Native SDK versioning
and ABI versioning are independent of npm package versions. Current native
support is Linux/POSIX; a Linux proof does not qualify Windows or macOS binaries.

Build `pnpm build:browser-control` as well as the root package. Ignore generated
package `dist` directories during linting, then check the authored code with
`pnpm lint`, `pnpm format:check`, `pnpm typecheck`, and `pnpm test -- --run`.
Regenerate `native_aria_formatter.h` and `native_aria_json.h` with
`scripts/generate-native-aria-formatter.mjs` after changing their authored
functions; the JSON generator requires its `formatNativeAriaJSON` argument.
The matching regression tests enforce the canonical source contract.

Run the standalone native client tests in Release and with
`SUITECUT_CLIENT_SANITIZERS=ON`. Set `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`
and `UBSAN_OPTIONS=halt_on_error=1`. Run the heavy native JSON/packet and
observation campaigns directly with `SUITECUT_CLIENT_FUZZ_ITERATIONS` and
`SUITECUT_AGENT_FUZZ_ITERATIONS`; ordinary CTest uses smaller finite defaults.
Exercise the session worker with `scripts/fuzz-native-control.py`, passing an
absolute CLI path, the built `simple_agent_test` engine, and a new report path.
Run process-owning checks under `suitecut-proof-supervisor` and retain its
descendant-cleanup receipt. Sanitizer CLI builds explicitly request leak checks
before a worker's `_exit`.

Run the emitted JavaScript lifecycle, routes, protocol and value fuzz scripts
against both `dist` and `packages/browser-control/dist`. Use fixed seeds, finite
iteration counts, exposed GC, and a bounded heap. These are controlled transport
proofs; additionally run real CEF, Chrome and Firefox browser workflows. Do not
label acknowledgement-only results as verified application outcomes.

Before distribution, install both static and shared native builds into fresh
prefixes and compile independent C and C++ consumers using only installed
headers/CMake exports. Verify the installed CLI, skill files, native browser
cleanup, and runtime library requirements. Test npm tarballs in fresh consumers,
including declarations, exports, CLI entry points and the browser viewer.
Run `pnpm audit` after dependency updates and verify a frozen lockfile install.

Publish only the exact tested archives. Record versions, source/binary hashes,
archive SHA-256, dependency audit, fuzz seeds/counts, platform requirements and
remaining backend limits in the release manifest. Passing finite fuzz tests
does not establish whole-browser leak freedom; prebuilt CEF is not sanitizer
instrumented by native-client sanitizer builds.

## Website deployment

Build and test the custom-domain site before deploying to the existing Cloudflare Pages project:

```sh
pnpm site:check
pnpm site:build
wrangler pages deploy site/dist --project-name suitecut --branch main --profile aakash
```

Cloudflare builds use the default `https://suitecut.aakashreddy.com` origin and `/` base.
Verify `/docs/streaming` after publishing.

## 2.0 release artifact check

After building, pack the exact release tarball into `.suitecut/release/` and write
`latest.json` containing its `version` and absolute `tarball` path. Run:

```sh
SUITECUT_NATIVE_EXECUTABLE=/path/to/suitecut-browser node scripts/verify-beta-package.mjs --release
```

This installs the actual tarball in a temporary consumer, verifies root/native and
native subpath exports, records with CEF page audio, and checks browser cleanup.
Publish this verified tarball. The native executable is built separately; do not
label the npm artifact as containing a signed or notarized CEF binary.

If registry authentication is unavailable, the verified tarballs can be published as
GitHub release assets first. Use those explicit asset URLs in installation docs,
including the matching shared runtime for audio families. Report registry publication
as pending and leave the npm dist-tag unchanged until authentication is restored.
