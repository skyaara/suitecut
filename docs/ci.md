# Continuous integration and releases

Three GitHub Actions workflows live in `.github/workflows/`.

| Workflow      | Runs on                                       | What it does                                                                                                                         |
| ------------- | --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `ci.yml`      | every push to `main` or `native`, every pull request | Site and tooling checks; the client SDK in Release and ASan/UBSan on Linux x64 and macOS arm64; the CEF engine and its native checks on both |
| `release.yml` | a pushed `native-v*` tag, or by hand          | Builds, signs, notarizes, verifies and packages every archive; a tag also creates a **draft** pre-release                           |
| `site.yml`    | a push to `main` that changes the site        | Builds the site and deploys it to the Cloudflare Pages project `suitecut`                                                            |

Runners are GitHub's `ubuntu-24.04` (x64) and `macos-26` (arm64).

## CI

- **Site and tooling**: `pnpm format:check`, `lint`, `typecheck`,
  `site:check`, `site:build` and the CEF regression harness unit tests.
- **Client SDK**, on each OS in Release and with
  `SUITECUT_CLIENT_SANITIZERS=ON` (gate 2). Release also installs the SDK and
  builds the outside consumer against it (gate 6), and runs 2,000 iterations of
  the control CLI fuzzer (gate 3). Linux uses the system OpenSSL; macOS uses
  Homebrew's.
- **CEF engine** with `SUITECUT_BUILD_BROWSER_CHECKS=ON` and its CTest checks
  (gate 4). Linux uses the official `linux64` SDK, Clang 18, system libraries and Xvfb;
  macOS uses the official `macosarm64Official` SDK and the static libraries from
  `scripts/build-embed-kit-deps-macos.py`, cached by their pin.
  `scripts/fetch-cef-sdk.py` downloads the SDK and checks it against
  `native/browser/cef-build.json`.

The CEF regression suite (gate 5) and recording checks stay local: they need
more time and steadier timing than hosted runners give.

## Releasing

1. Set the version in `native/client/CMakeLists.txt`, update `CHANGELOG.md`
   and the release record, and merge.
2. Push an annotated tag `native-v<version>` on that commit. The workflow
   refuses a tag that does not match the version.
3. The macOS job repeats the candidate recipe: static libraries and the LGPL
   FFmpeg from scratch, the engine with the official CEF SDK, the static and
   shared clients with their tests, the runtime, SDK and embed kit archives
   (gates 7 and 8), the build-path check and strict signature checks after
   extraction. The Linux job builds, tests and archives the static and shared
   SDKs and builds a consumer against each.
4. The draft job checks the hashes and creates a draft pre-release with every
   archive and one `SHA256SUMS`. Review the draft, replace the generated notes
   with the release notes, and publish it by hand.

Running the workflow by hand (Actions > Release > Run workflow) builds the same
archives as workflow artifacts, kept for 30 days, without creating a release.
Without the signing certificate a hand run signs ad hoc, which is fine for
testing but not for publishing; a tag push without it fails.

## Secrets

Add these under Settings > Secrets and variables > Actions.

| Secret                        | Used by       | Value                                                                                                   |
| ----------------------------- | ------------- | ------------------------------------------------------------------------------------------------------- |
| `MACOS_CERTIFICATE_P12`       | `release.yml` | The Developer ID Application certificate and private key exported as `.p12`, base64 encoded           |
| `MACOS_CERTIFICATE_PASSWORD`  | `release.yml` | The password chosen when exporting the `.p12`                                                           |
| `APPLE_API_KEY_P8`            | `release.yml` | An App Store Connect API key (`.p8`, Developer access is enough), base64 encoded; enables notarization |
| `APPLE_API_KEY_ID`            | `release.yml` | That key's ID                                                                                           |
| `APPLE_API_ISSUER_ID`         | `release.yml` | The issuer ID shown above the keys list                                                                 |
| `CLOUDFLARE_API_TOKEN`        | `site.yml`    | A token with Cloudflare Pages: Edit for the account                                                     |
| `CLOUDFLARE_ACCOUNT_ID`       | `site.yml`    | The Cloudflare account ID                                                                               |

Base64 encode a file for a secret with `base64 -i certificate.p12 | pbcopy`.
The release job imports the certificate into a temporary keychain, signs with
its fingerprint through `SUITECUT_MACOS_SIGN_IDENTITY`, stores the API key as
the notarytool profile `suitecut-ci` in that keychain, and deletes the keychain
at the end. Without the API key the archives are signed but not notarized.
