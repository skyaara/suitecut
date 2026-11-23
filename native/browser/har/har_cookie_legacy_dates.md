Native HAR legacy cookie expiry dates
====================================

HAR response cookies now project complete RFC850 dates such as
`Sunday, 06-Nov-94 08:49:37 GMT` and asctime dates such as
`Sun Nov  6 08:49:37 1994`. They use the existing native UTC calendar validation,
year range 1601–9999, exact weekday agreement and 0–59 seconds. English month and
weekday names are case insensitive. RFC850 cookie years 00–69 mean 2000–2069;
70–99 mean 1970–1999, following the fixed cookie-date pivot. No current-year,
locale or timezone lookup supplies missing fields. Asctime is interpreted as
UTC. Partial dates, other timezone tokens and leap seconds remain unsupported.
This strict projection does not claim a complete browser cookie-date parser.

Canonical RFC1123 dates retain their existing behavior. A valid Max-Age still
has precedence over Expires, including overflowing Max-Age values. Relative
expiry uses only the original authoritative native header receipt clock. An
unavailable clock or overflow keeps that relative expiry unknown; it does not
fall back to an absolute date. An invalid Max-Age permits a supported absolute
date. Absolute complete dates need no clock reading to supply their time.

Raw Set-Cookie fields remain unchanged, including unsupported date values. This
projection neither mutates Chromium's cookie jar nor reorders or filters HAR
entries. In particular, genuine favicon observations and missing genuine
Network terminal events are separate backend boundaries. Transparent routing
of original native browser WebSockets also requires an actual transport/vendor
seam; no JavaScript constructor or event facade is introduced here.

Component coverage uses the exact native parser and HAR capture projector,
including unavailable receipt clocks, duplicate export and raw-header ownership.
Genuine browser/HAR validation is a separate gate and must use a matching build.
