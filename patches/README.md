# Playwright 1.62.1 authentication compatibility patch

`playwright-core@1.62.1.patch` separates Chromium proxy credentials from website
HTTP credentials. Unpatched Playwright replaces website credentials with proxy
credentials during context initialization. The patch selects credentials from
`Fetch.authRequired.authChallenge.source`, preserves website origin restrictions,
and tracks retries per challenge so a proxy challenge and website challenge can
both complete on one request. Retry tracking is bounded and request completion
releases tracked entries. Chromium pages, process frames, dedicated workers and
service-worker network managers use the corrected configuration; Firefox and
WebKit authentication implementations are unchanged and are not certified here.

The SuiteCut repository configures the patch through `pnpm-workspace.yaml` and
records it in `pnpm-lock.yaml`. A fresh `pnpm install --frozen-lockfile` applies it.
The SDK package includes the patch for downstream projects, but importing
SuiteCut **does not modify your Playwright installation**.

To use both credential sets in another pnpm project, pin `playwright` and, if
used, `@playwright/test` to `1.62.1`, copy this patch into that project's `patches/`
directory, and merge this entry into its `pnpm-workspace.yaml`:

```yaml
patchedDependencies:
  playwright-core@1.62.1: patches/playwright-core@1.62.1.patch
```

Run `pnpm install` and commit the resulting lockfile. Other package managers
require their own explicit dependency-patching mechanism. Do not apply this
patch to another Playwright version without reviewing and testing it.

This workaround applies to the Node-side Playwright browser driver, including
CEF and stock Chromium connections driven by that installation. For a remote
Playwright browser server, the server's driver must carry the fix too. Native
SuiteCut bindings implementing authentication themselves do not need this
Playwright patch. It does not add DRM, remote network access, persistent profiles
or complete Playwright compatibility.

The saved follow-up report lists real HTTP/HTTPS success and negative tests,
including any remaining stock-Chromium failure. Test credentials are local dummy
values, and proxy credentials must never be reused for an origin challenge.
