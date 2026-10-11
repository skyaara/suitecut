# Candidate persistent CEF audio capture

This patch is a candidate against CEF revision `14c5a08`, used by the pinned
154.0.34 stock distribution (the previous Linux default).
`0001-persistent-audio-capture-cef152.patch` is a separate candidate rebased onto
revision `708dc14`, the codec-enabled Linux 152 distribution. It applies with
zero patch fuzz to the retained pinned source; its dry-application log and
checksums are under `benchmarks/native-sync/2026-10-11-shared-clock/`. Neither
candidate has been compiled or runtime verified. They start Alloy loopback capture after browser creation,
when an audio handler is provided, and keeps that capture alive during silence.
Browser destruction retains the existing capture shutdown. It checks for a
browser destroyed synchronously during its lifespan callback.

The stock binary starts capture only when the page becomes audible and stops
it after two seconds of silence. That behavior prevents reliably initializing
capture while a page is silent and can miss the beginning of the first sound.
No container or SuiteCut encoder setting can recover those uncaptured samples.
Persistent capture addresses this lifecycle boundary; it does not recover
interior Chromium glitch samples or prove continuous capture.

**Status: patch application checked; not compiled or runtime verified.** It is
not enabled in the pinned stock CEF binary. Do not describe it as a deployed fix.
A Chromium/CEF source build requires a dedicated build environment with enough
space for the full checkout and build; this host has a 32 GiB total filesystem.

After building a separate patched distribution, verify silent first-packet
capture, first-sound preservation, long quiet intervals, source error/restart,
popups/context shutdown, and a long simultaneous recording/streaming run. SuiteCut
must then require native source initialization at its launch/recording handshake,
including zero PCM, and pin the verified custom archive with its own checksum.
The current engine-ready handshake confirms the output encoders/muxer; it does
not claim that stock CEF has initialized its audibility-gated source capture.
