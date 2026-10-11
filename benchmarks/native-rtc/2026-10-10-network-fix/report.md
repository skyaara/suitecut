# Remote Jitsi WebRTC diagnosis and proxy policy

Saved October 10, 2026. Human-readable times use Asia/Calcutta (IST); original RTC logs retain UTC timestamps. The native proxy trial began 2026-10-10T18:06:59.386000+05:30; standalone Chromium began 2026-10-10T18:09:30.306000+05:30.

## Finding and required fix

**The room joined, but remote media remains blocked/unavailable on this host.** Native SuiteCut CEF and a separate standalone Chromium browser both reproduced ICE error **701: “Failed to establish connection”** for Jitsi's supplied TLS TURN endpoint `meet-jit-si-turnrelay.jitsi.net:443`. Neither acquired a local relay candidate, connected a peer transport, received transport bytes or decoded video in the sampled interval. A participant's screen-share announcement was visible in both browsers; receiving its signaling is not receiving its media.

This strongly points to the shared host/network path as the immediate blocker. It is not proof that every SuiteCut remote RTC flow is correct. The comparison uses different Chromium versions and does not replace a successful cross-host call or matched network test.

The network policy configures HTTP proxy access, no TCP destination grants and no VPN. The documented TCP CONNECT proxy endpoint (`proxy:8088`) returned connection refused in the supported-path probe. Ordinary HTTPS/WebSocket room signaling worked through the HTTP proxy, but TURN is a separate media transport. Allowing HTTPS web pages on port 443 does not establish that a proxy can tunnel arbitrary TURN over TLS on that port.

To finish the fix in this environment:

1. Configure a usable raw TCP/TLS path to the supplied TURN relay on port 443 using the supported environment TCP access/VPN configuration. Restore/start the platform's configured TCP transport service if needed. The relay hostname can change; use the current room's advertised ICE servers.
2. Ensure Chromium's TURN TCP/TLS connection uses that permitted path and a tunnel that can carry TURN traffic. Keep the existing HTTPS proxy for page/API traffic. A generic HTTP application proxy may not carry raw TURN protocol.
3. Use the implemented `webrtcPolicy: 'disable-non-proxied-udp'` setting when intentionally operating through a proxy without UDP. Keep default behavior on ordinary networks where UDP is available.
4. Rejoin and require a selected successful ICE pair, a connected DTLS transport, increasing received bytes and decoded video/audio counters. Then inspect native recording for continuity. Do not declare remote RTC verified merely because the room joined.

An ordinary host/network with permitted WebRTC UDP and TCP/TLS fallback is another viable test environment, but that route has not been tested here. No network policy/grants were edited or bypassed, no VPN provisioned and no TURN credentials saved. The currently available tools expose environment status but cannot change TCP/VPN access. **The remaining host access step is still required; the media connection has not been fixed end to end.**

## Implemented SDK/native setting

```ts
const engine = await launchEngine({
  executablePath: '/absolute/path/to/suitecut-browser',
  webrtcPolicy: 'disable-non-proxied-udp',
})
const context = await engine.browser.newContext({
  proxy: { server: 'http://your-working-proxy:port' },
})
```

`NativeWebRTCPolicy` is exported through the root/native SDKs. The strict launcher accepts only `default` and `disable-non-proxied-udp`, forwarding `--suitecut-webrtc-policy`. The C++ browser applies Chromium's `webrtc.ip_handling_policy = disable_non_proxied_udp` to its global request context and every new isolated/proxied context. Unknown native modes are rejected, and CEF preference errors fail explicitly. Default leaves Chromium's preferences unchanged. No sandbox bypass or security flag was added for RTC.

The setting suppresses unproxied UDP; depending on proxy support, Chromium can use proxied UDP or TCP. It does not manufacture relay servers/credentials, grant network access, guarantee relay connectivity or fix absent camera/microphone devices. Both remote trials joined muted with no camera. Existing explicit SwiftShader software graphics were used for these trusted test workloads and retain their documented graphics-protection limitation.

## Tests and evidence

| Check                                                        | Result                                                                                                              |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------- |
| Native CEF 152.0.6 / Chromium 152.0.7977.83, proxy RTC trial | Room joined; 4 TURN 701 errors; zero received media bytes and decoded video; no local relay candidate               |
| Standalone Chromium 151.0.7922.34 reference                  | Room joined; same 4 TURN 701 errors; zero received media bytes and decoded video; no local relay candidate          |
| Native network-policy regression                             | 6/6 passed: default produces UDP candidates; explicit policy suppresses them in root, isolated and proxied contexts |
| Focused SDK tests                                            | 30/30 passed, including enum validation and argument forwarding                                                     |
| Native CTest                                                 | 7/7 passed                                                                                                          |
| Core browser functionality                                   | 17 checks passed; WebGPU optional unavailable; local RTC data/video still works in default mode                     |
| Curated browser integration                                  | 16/16 passed                                                                                                        |
| Build/typecheck/lint/format/whitespace                       | Passed                                                                                                              |

`comparison.json` summarizes every sampled interval; original `rtc-*.json` files retain sanitized counters/candidate types and configured ICE URLs. No ICE passwords, username credentials, raw SDP, browser profile or peer IP addresses are exported. `tcp-relay-access.json` records the configured TCP service's refused connection. Screenshots show actual room membership. The standalone browser used a fresh temporary profile with the matching WebRTC preference and explicit proxy, with Chromium sandboxing enabled. Its profile was removed on closure.

Initial attempts in the sibling `2026-10-10-jitsi` and `2026-10-10-jitsi-retry` directories are also preserved in the snapshot: one selector was wrong; the later joined session eventually encountered a document navigation during statistics sampling. Neither failure is represented as a media pass. The reference collector initially wrote a static display name into its action metadata; its screenshot shows the actual “SuiteCut Chromium reference” display name. The collector was corrected for subsequent runs.

Reproduce the local policy test with `pnpm test:native:rtc-policy` after building the native executable and providing display/audio plus `SUITECUT_NATIVE_EXECUTABLE`. The owned headless harness used here is:

```sh
MEMORY_DIRECTORY=$PWD/.suitecut/rtc-policy-check \
SUITECUT_RUNTIME_EXECUTABLE=$PWD/.suitecut/native-build/suitecut-browser \
python3 benchmarks/native-memory/runtime.py benchmarks/native-rtc/network-policy.mjs
```

The live-room collector accepts `SUITECUT_WEBRTC_POLICY`, `SUITECUT_RTC_DURATION_MS` and an explicit `SUITECUT_RTC_BACKEND=chromium-reference` comparison mode. Its room participation is time-bounded and automatically closes; it does not post chat messages or publish media.

## Saved package

The logs, failed/passing attempts, current source/SDK/executable snapshots, exact dependency digests, network policy and diagnostics are sealed with `checksums.sha256`. The archive is `/workspace/shared/suitecut-rtc-network-diagnosis-2026-10-10.tar.gz`. The prior pinned CEF distribution remains in the codec archive and is referenced by digest; the installed standalone Chromium is identified by version and binary digest rather than embedded here.

Earlier sealed benchmark payloads were verified unchanged. This work is functional network diagnosis, not an OBS performance comparison, a long-duration audio continuity test or a memory-leak guarantee.
