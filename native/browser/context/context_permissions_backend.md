# Pinned CEF156 MIDI permission restriction

Ordinary `midi` permission overrides are unavailable in this browser backend.
The public context options and `grantPermissions` native ingress reject that
name before mutating context state. This is not an intrinsic/no-op grant.
`midi-sysex` retains its real CEF prompt bit and MIDI_SYSEX content setting.

The actual browser diagnostic on CEF 156.0.7 trapped in Chromium's
`HostContentSettingsMap::SetContentSettingDefaultScope` because its registry
had no ordinary MIDI entry. A C API forwarding logger observed the successful
preceding calls and the fatal `CEF_CONTENT_SETTING_TYPE_MIDI` (27), BLOCK (2).
The enum's presence in CEF headers therefore does not establish setter support.
No ordinary MIDI setting is emitted for unrelated grants, denial or reset.

Original production smoke and isolated trace failures remain preserved. A
separate diagnostic shim skipped only the known unsupported setter and let
all remaining settings, idle override/IdleDetector/reset and close complete;
that shim is not production acceptance. The repaired unshimmed binary still
requires the separately authorized matching browser smoke.

The stock native permission callback also rejects zero-bit prompts, opaque or
invalid requesting origins, and pages/contexts already irreversibly closing.
Wildcard grants apply only to actual HTTP(S) requesting origins. An inherited
about:blank frame can still use the real HTTP(S) origin reported by CEF.

A separate pinned source repair and unapplied native adapter are in
`cef-patches/chromium156-separate-midi-permissions.md`. They cover the underlying
registry, Blink descriptor and actual ordinary MIDI transport authority. They do
not activate ordinary MIDI in the stock binary. Client certificate key import
remains outside public CEF's context-local API: `OnSelectClientCertificate` can
select only an existing Chromium-owned certificate; it cannot import a supplied
PEM/PFX/private key into an isolated request context. No certificate success is
inferred from `ignoreHTTPSErrors` or from server-certificate callbacks.
