# Validate registered types before forwarding CEF settings

Status: isolated CEF robustness candidate, **not compiled or activated**.

The pinned CEF headers publicly declare `CEF_CONTENT_SETTING_TYPE_MIDI`.
Its conversion to Chromium `ContentSettingsType::MIDI` succeeds, but the pinned
Chromium registry only registers MIDI SysEx. A standalone CEF program calling
`SetContentSetting` with the named ordinary MIDI enum and `BLOCK` reproduces
SIGTRAP inside `HostContentSettingsMap`. The same program using the named SysEx
enum returns. The public API warns that incorrect content-setting usage can
cause instability; this candidate makes unregistered enum handling explicit.

The patch validates content-setting registration before forwarding the public
content-setting getter/setter. It similarly validates website-setting
registration for the website-setting getter/setter. Unregistered types return
the existing default/null sentinel from getters and do nothing in the void
setters. Registered types retain their existing Chromium validation and policy.
This neither registers ordinary MIDI nor implements its permission override.
The separate ordinary-MIDI extension remains a distinct candidate.

The adjacent manifest pins the CEF source, input/output hashes and patch hash.
Pinned-source applicability passes; CEF compilation and runtime verification of
the guard are pending. Stock SuiteCut already rejects ordinary MIDI overrides
before calling the unsafe setting path and remains unchanged by this patch.

The upstream report and standalone reproduction are recorded under
`docs/internals/cef-issues`. No custom library is built or installed by the ordinary SDK
build.
