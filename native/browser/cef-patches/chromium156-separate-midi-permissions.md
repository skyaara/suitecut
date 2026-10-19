# Separate ordinary MIDI and SysEx in pinned CEF156

The two accompanying patches are isolated candidate source changes. Neither is
applied to the stock CEF library or activated in SuiteCut. Ordinary `midi` still
returns an unsupported error; the stock binary never sends content-setting 27.

Pinned Chromium `156.0.8078.17` registers MIDI_SYSEX, but not MIDI. In addition,
`MidiPermissionContext` reads MIDI_SYSEX under `BlockMidiByDefault`, and Blink's
`MIDIAccessInitializer` changes an ordinary access request into a SysEx permission
request under the same feature. Merely registering MIDI, mapping it to SysEx,
or overriding CDP's ordinary MIDI permission does not complete ordinary access.

`chromium156-separate-midi-permissions.patch` changes four actual Chromium files:

- Registers ordinary MIDI as an independent secure-origin ALLOW/BLOCK setting.
- Reads that setting from the actual ordinary MIDI context.
- Grants actual ordinary transport rights with `GrantSendMidiMessage`, using
  Chromium's existing process ownership; it does not grant SysEx transport rights.
- Preserves the requested `sysex` boolean in Blink's actual permission request.

Ordinary MIDI has no upstream permission-prompt RequestType. This candidate
therefore deliberately defaults ordinary MIDI to BLOCK and permits no ASK
setting. This matches SuiteCut's deny-unlisted automation policy; it is an
explicit browser-default policy difference from upstream's shared SysEx prompt.
The existing secure-origin and permissions-policy checks remain authoritative.
Existing MIDI handles retain Chromium's upstream process-scoped transport rights;
this patch does not promise instantaneous revocation of an already open handle.

`chromium156-separate-midi-permissions.json` records exact upstream file hashes,
output hashes, URLs, and the patch digest. The patch passed `git apply --check`
and actual application against those four exact downloaded files. No Chromium
compile, rebuilt `libcef.so`, device access or permission runtime was performed.
Apply it at the Chromium source root using the pinned CEF build recipe in
`README.md`; keep all other CEF patches and provenance explicit.

`suitecut-verified-midi-adapter.patch` is a separate, unapplied SuiteCut header
patch. After the vendor repair is genuinely rebuilt and validated, a candidate
SuiteCut build may apply it and define
`SUITECUT_VERIFIED_CEF156_SEPARATE_MIDI_PERMISSIONS`. It then maps `midi` to the
new real setting and `midi-sysex` to both actual settings, because SysEx includes
ordinary MIDI capability. No ordinary CEF prompt bit is invented. Grants,
deny-unlisted updates, clear/reset and new-context permissions use the existing
native context scopes. Never enable this candidate against the stock SDK.

Required candidate acceptance includes ordinary query/grant/deny/request,
SysEx-only denial after ordinary grant, ordinary access after SysEx grant,
clear/reset, separate contexts, inherited/cross-origin frames, popups, native
MIDI send/receive, and original deadline/close behavior. These runs are deferred
with the other integration/E2E work and are not implied by the source apply check.
