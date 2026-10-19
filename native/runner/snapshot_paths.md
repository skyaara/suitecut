# Native snapshot paths

`NativeTestInfo.snapshotPath(...name)` synchronously resolves explicit snapshot names from a C++ token plan admitted for the actual test declaration. `snapshotDir` is the native per-file directory and ignores path templates. Both APIs create no files. They do not select a baseline for screenshot or ARIA matchers.

Root and project declarations accept `testDir`, `snapshotDir`, `snapshotPathTemplate`, and `expect.toHaveScreenshot.pathTemplate` / `expect.toMatchAriaSnapshot.pathTemplate`. Project fields override root fields independently. Relative configuration paths and relative template results resolve against the actual canonical suite module's directory; SuiteCut has no separate configuration-file directory in this API.

```ts
export default defineNativeSuite({
  snapshotDir: './baselines',
  snapshotPathTemplate: '{snapshotDir}/{testFilePath}/{arg}{ext}',
  tests: [{
    title: 'header',
    run: (_, info) => {
      const png = info.snapshotPath('header.png', { kind: 'screenshot' })
      const text = info.snapshotPath('nested', 'header.txt')
      // These are paths only; no files have been read or written.
    },
  }],
})
```

C++ binds `{configDir}`, `{testDir}`, `{snapshotDir}`, `{testFileDir}`, `{testFilePath}`, `{testFileName}`, `{testFileBaseName}`, `{testName}`, `{projectName}`, `{platform}` and `{snapshotSuffix}`. It retains only `{arg}` and `{ext}` as argument-insertion opcodes. A single-character token prefix is emitted only when the token is nonempty. Unknown tokens remain literal. `snapshotPath(name, { kind })` accepts `snapshot`, `screenshot` or `aria` and requires one name; the default form accepts up to 32 relative name segments. Normalized names cannot escape their relative argument.

The generic and screenshot default templates include the native sanitized project name and initial platform suffix. The ARIA default omits both; an explicit global template still applies to every kind unless that kind has its own override. These defaults were observed through the pinned stock public API with unnamed and named projects. Implicit matcher names, baseline discovery, update modes and matcher integration remain separate work.

An admitted test can synchronously read or assign `info.snapshotSuffix`. C++ issues its initial platform, finite filename-component policy and suffix insertion opcodes; the SDK retains only that test's local author value. The same beforeEach/body/afterEach admission shares the cell, while another test, a new native retry or a fresh host starts from the native initial value. Retry selection uses the existing validated native BODY/CALL retry field. Old Info objects retain their old cells and cannot mutate a later retry's cell. Empty removes the optional prefix; a safe custom value replaces it. `{platform}` remains fixed, `{snapshotSuffix}` is mutable, and default ARIA has neither opcode nor project suffix. No file operation, native request or timer occurs. This bounded slice accepts at most 1024 UTF8 bytes, rejects NUL/unpaired UTF16/separators and dot navigation components, and does not claim arbitrary legacy suffix/path quirks or ownerless hook support.

The native producer wraps its path plan in a version-3 context containing the initial value and policy. Legacy version-1/2 path DTOs still format paths, but cannot grant suffix mutation without a native policy. An unsupported path/title retains an independently usable native-issued suffix cell. Context and packet budgets remain enforced; each admitted cell retains at most 1024 author bytes.

Plans are bounded and installed before the real native callback admission, then reused by subsequent callbacks in that same worker. A title with isolated UTF16 units remains intact for ordinary callbacks. If a `{testName}` token requires an unrepresentable filesystem name, only `snapshotPath` rejects; a valid independent `snapshotDir` remains readable. Genuinely invalid module/testDir relationships do not produce a fabricated directory.

When all three native compiled kind plans are exactly equal, C++ sends one version-2 part definition and native local IDs for the three kinds. Identical effective template text is compiled once; different text is coalesced only after C++ compares complete part kind/value vectors. Distinct plans, including the corrected defaults, retain the version-1 payload. The SDK resolves only the IDs in that actual DTO and does not infer equality or share plans across tests/projects. The same callback, byte, part, metadata and filesystem bounds apply; there is no extra message or acknowledgement.
