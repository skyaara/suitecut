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
   pnpm test:stream
   pnpm test:browsers
   pnpm test:retry-parallel
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

## Website deployment

Build and test the custom-domain site before deploying to the existing Cloudflare Pages project:

```sh
pnpm site:check
pnpm site:build
SUITECUT_SITE_PORT=4388 pnpm site:test
wrangler pages deploy site/dist --project-name suitecut --branch main --profile aakash
```

Pushing `main` also deploys GitHub Pages through `.github/workflows/pages.yml`. That workflow sets
`SUITECUT_SITE_URL=https://skyaara.github.io` and `SUITECUT_SITE_BASE=/suitecut`. Cloudflare builds
use the default `https://suitecut.aakashreddy.com` origin and `/` base. Verify `/docs/streaming`
on both deployments after publishing.

## 2.0 release artifact check

After building, pack the exact release tarball into `.suitecut/release/` and write
`latest.json` containing its `version` and absolute `tarball` path. Run:

```sh
SUITECUT_NATIVE_EXECUTABLE=/path/to/suitecut-browser node scripts/verify-beta-package.mjs --release
```

This installs the actual tarball in a temporary consumer, verifies root/native and
legacy Playwright exports, records with CEF page audio, and checks browser cleanup.
Publish this verified tarball. The native executable is built separately; do not
label the npm artifact as containing a signed or notarized CEF binary.

If registry authentication is unavailable, the verified tarballs can be published as
GitHub release assets first. Use those explicit asset URLs in installation docs,
including the matching shared runtime for audio families. Report registry publication
as pending and leave the npm dist-tag unchanged until authentication is restored.
