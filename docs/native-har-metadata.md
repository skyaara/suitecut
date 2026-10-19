# Native HAR metadata

HAR request `postData` includes `params`. Non-form-urlencoded bodies currently have
an empty parameter list; multipart parameter decoding is not implemented. Actual `application/x-www-form-urlencoded` bodies produce
ordered name/value pairs, preserving repeated names and empty values, decoding
percent escapes and `+` spaces. The original captured body remains unchanged.
The projection admits at most 256 nonempty form fields per entry before creating
CEF parameter objects. A larger form retains its body, omits the unavailable
parameter list and records `_paramsError`; it is neither truncated nor reported
as an empty form. Existing capture body, metadata and entry limits also apply.

For a captured response body, `content.compression` is zero only when native
matched response metadata confirms an authoritative header observation is
ready and all observed content codings are empty or `identity`. The private
native `_nativeHeadersObserved` flag distinguishes genuine Chromium headers
from an empty dictionary supplied by a missing-metadata DTO default. A genuine
response ExtraInfo receipt can establish that provenance later. The bounded
SDK response schema strips this internal key.

Compressed, unread, failed and header-unknown bodies keep compression unknown.
The projection does not use a response's transfer total as payload length or
subtract estimated headers. Encoded `response.bodySize` remains unknown without
a genuine byte observation; zero identity compression does not supply one.

The ten original HAR locations selected for the historical ca7 baseline were
run unchanged on sealed native `b074f3e2`: five passed and five failed, without
skips, retries or native stderr. All 2002 inputs, 230 runtime assets and the
sealed SDK stayed unchanged. Failures established missing POST parameters and
compression fields. Two failures also observed genuine favicon requests that
changed an entry count or index; no request filtering or reordering was added.
The actual binary POST remained in the archive with its captured bytes, after
the favicon entry. These baseline failures are preserved independently of this
metadata change. Matching sealed `9cffe672` collected the same ten originals:
six passed and four failed, without skips, retries, native stderr or changed
2046 inputs/230 assets/SDK. All four current assertions selected a genuine
favicon entry before the actual POST/PNG. The saved actual text POST contains
`params: []` and `Hello`; PNG entries contain correct identity compression and
body/attachment data. Entries were not filtered or reordered. The binary POST
also retains a separate underlying projection gap: its missing wire Content-Type
exports an empty MIME field and its sixteen captured bytes remain intact, while
the stock oracle expects application/octet-stream and empty text. The original
failure remains; no wire header or empty body was fabricated.

`native/browser/har_metadata_test.cc` exercises the existing page/TLS/worker
contracts and the POST, coding, missing-provenance, header-readiness, failed-body
and parameter-capacity boundaries using actual CEF value objects. It does not
initialize CEF or substitute for a real browser/cross-process proof.

`native/browser/har_response_provenance_test.cc` separately checks real empty
header observations against absent-header defaults and detached captured-header
ownership using the exact native response projector.

HAR response cookies expose `httpOnly` and `secure` only when those attributes
actually occur in the received Set-Cookie header. Their presence is a flag even
when a header appends `=false` or `=0`; absent attributes are not exported as
invented false values. Attribute names are case insensitive. Observed supported
SameSite values become `Strict`, `Lax` or `None`; an unsupported final SameSite
attribute leaves that normalized field absent. Quoted values, equals signs,
commas and the separate order of cookie header lines remain intact. This is
metadata projection, not a browser cookie-jar mutation or default-policy guess.

The six unchanged pinned HAR cookie cases (214, 233, 251, 263, 277 and 293)
on sealed `b074f3e2` produced one pass and five failures, with zero native stderr
and unchanged 2002 inputs, 230 assets and SDK. The actual archives established
unobserved false flags and missing SameSite; they also preserve a separate
missing Max-Age expiry. Optional attribute projection has controlled CEF-valued
coverage in `native/browser/har_cookies_test.cc`. Matching sealed `4ba1c9d9`
collected all six original cookie cases: all six passed, with no skips, retries
or native stderr and unchanged 2075 inputs, 230 assets and the sealed SDK. The
expiry implementation is documented separately below.

The following expiry projector uses a separate native wall-clock seam. Its
default is `std::chrono::system_clock`; tests can supply an explicit controlled
clock. A record samples it once when actual matched headers are both observed
and authoritative (`headersReady`), including a later genuine ExtraInfo update.
Provisional headers do not start it. Duplicate updates, archive export and
context close never renew it. Redirect hops have separate logical records and
receipts. The clock callback is nonthrowing and reports unavailability with a nonfinite
value. An unavailable or unsupported reading still consumes the receipt and
relative expiry remains unknown.

A syntactically valid Max-Age directive takes precedence over Expires, including
zero, negative and numerically overflowing values. An unavailable or overflowing
relative calculation stays unknown; it does not fall back to Expires. Invalid
Max-Age syntax permits a supported absolute Expires date. UTC formatting handles
pre-epoch milliseconds and the supported year range 1601–9999.

Absolute normalization currently supports canonical RFC1123 GMT only, with
calendar and weekday agreement. Legacy RFC850/asctime formats, other timezone
tokens, leap seconds, two-digit years and weekday mismatches are known projection
gaps. Omitting their normalized `expires` field does **not** label the raw cookie
invalid or reject it from Chromium. All raw headers remain in the archive and
the browser's actual cookie jar is unchanged. Pure UTC/precedence and CEF-valued
receipt, duplicate-update, redirect and unknown-clock controls pass optimized
and ASan/UBSan/LSan builds, alongside the existing cookie and metadata suites.
They use the project's no-exceptions/no-RTTI flags and do not initialize CEF.
The matching `4ba1c9d9` cookie run includes the actual received Max-Age future
expiry assertion; calendar/overflow/clock-unavailable boundaries remain
controlled component coverage. An initial authored reader
failure is preserved separately: it borrowed a nested CEF value beyond the
parent graph's lifetime. The corrected reader copies while its parent is alive;
the expiry assertions are unchanged.

A following encoded-body counter admits page-owned requests on the exact native
primary observation channel only after the existing root `Network.enable`
command has an actual successful ACK. It starts at that request's real raw
start and pins its owner, frame, loader and native request identity. Private
mirrors, workers, session migration, late enable, redirects and unproven cache
metadata remain unknown. The dedicated consumer does not call the generic
network event handler or change request migration, cursors, waiters or callbacks.

It checks nonnegative integral chunk counts, at most 4096 observations and
16 MiB for each decoded/encoded total. Known payload length requires a genuine
successful terminal, at least one chunk, positive encoded total, explicit
uncached response observations and an exact decoded-total match to the actual
captured body. Missing/failed/overflowing or conflicting observations stay
unknown. Observed authoritative coding must remain unchanged; the first slice
normalizes identity and gzip only. Compression is decoded body bytes minus
actual encoded chunk bytes. Terminal transfer length is never used or adjusted
by estimated headers. Existing identity-only zero compression is an independent
header/body fact; it does not invent an encoded payload size.

Actual prior sealed `9cffe672` raw observations demonstrate a 175000-byte decoded
gzip response with 581 encoded chunk bytes, exactly matching the locally served
581-byte compressed payload. Root and private channels independently agree;
they were never summed together. The first live checker failed because it read
only the ToolEvent log prefix, while the existing lossless path had retained the
packets under the raw receive prefix. Its failure and raw log remain intact;
a separately hashed offline analysis establishes this fixture-specific result.
The actual terminal transfer total was 795, which was not used for payload
length. Core and included HAR controls, plus the Source helper seam, pass strict
optimized and ASan/UBSan/LSan builds. The final one-shot admission and retained
original-pin capacity/retirement controls also pass both builds. Original pin
string capacities have a separate 1 MiB retained metadata bound, included in
native retained-byte accounting; transferred record pin/coding capacities enter
the existing metadata budget. Existing fixed record/original count bounds remain.
The previous expiry/cookie/metadata suites pass before the final one-shot guard;
that final guard is exercised by the final included HAR controls. No component
initializes CEF or replaces matching full Source compilation/browser proof.
Matching sealed `4ba1c9d9` passes the separately declared corrected gzip-v3
checker. Both actual decoded bodies contain 175000 bytes. Plain encoded chunks,
served payload and public HAR bodySize all equal 175000 (compression zero).
Gzip encoded chunks, served payload and public HAR bodySize all equal 581;
public HAR compression is 174419. These are payload bytes, independently of the
actual terminal transfer totals. All 2075 inputs and 230 assets are unchanged,
the actual helper exits 0/null and post-run native process inventory is empty.
Intentional raw protocol stderr (137351 bytes) is retained and is **not** called
strict-clean. All 330 diagnostic fields remain in the proof; live default-page
and factory owners are qualified rather than relabeled zero.

The same epoch's ten unchanged original HAR cases collect ten and produce six
passes/four failures, with no skips, retries or native stderr. All four exact
current assertions select genuine favicon entries at their original indexes
(POST cases index 1; image cases index 2). Saved request order and all bodies
remain intact. The cookie, aggregate and original-HAR evidence index is
`/tmp/suitecut-har-4ba1c9d9-wave-index-v1/index.json`, SHA256
`1ad8825f7bee36d2717be3397a1fd92bc01f150161f8cc4833708ce292ecc0b6`.
MIME and ZIP member-name changes below are explicitly absent from this epoch.

The following POST MIME slice defaults the HAR entity's `postData.mimeType` to
`application/octet-stream` when no nonempty MIME type is available. It never
adds or rewrites a received request header. Explicit Content-Type values remain
unchanged. Actual captured body bytes, text/base64 representation, resource
publication and byte-exact replay matching stay intact. This does not implement
the stock binary oracle's empty-text expectation or hide the real favicon index
failure. Seven focused actual CEF-valued MIME/replay controls and the existing
metadata suite pass strict optimized and ASan/UBSan/LSan builds without CEF
initialization. Matching compiled browser proof remains pending for this
separate slice.

A separate following ZIP attachment slice writes real root-level
`<SHA1>.html`, `.css`, `.js`, `.json`, `.txt` or `.png` members from the bounded,
case-insensitive observed MIME type (parameters ignored for suffix selection).
Missing, malformed, oversized or other MIME values keep `.dat`. Names contain
only the actual body digest and a fixed suffix, never a URL or author path.
Each `_file` points to the actual written member; identical bytes/types dedupe,
while the same bytes under different MIME suffixes can have separate members.
Non-ZIP attachments retain `resources/<SHA1>.dat`. Existing ZIP/relative-file
readers still accept old archives. HAR entry order and body contents do not change.

The saved actual `9cffe672` attachment ZIP independently establishes the format
gap: its correct HTML/CSS/PNG bytes exist under `resources/<SHA1>.dat`, but the
pinned original predicates' root MIME names do not. This is a downstream
comparison of retained output, not another executed original or a repaired
favicon assertion. Fourteen MIME cases, raw ZIP member bytes/hashes, unchanged
entry order, deduplication, replay, legacy and non-ZIP compatibility, original
cancellation/deadline behavior and cleanup after actual temporary-file creation
pass optimized and ASan/UBSan/LSan controls, alongside the existing HAR file suite.
Atomic destination replacement and existing path/count/byte bounds remain in
the same native writer. Matching compiled browser proof remains pending.
