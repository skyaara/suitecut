# Raw Inspector accepted-work lane

This bounded C++ component is one prerequisite of a future process-wide Engine
barrier. It is not wired into production Source in this commit and it supplies
no global idle, close acknowledgement, or browser worker reuse capability.

Reserve before actual enqueue/send; dispatch before entering the real CEF send
stack. Record accepted/refused only from its real return. A native throw retains
the unresolved receipt and poisons idle authority. A genuine reply must match
channel incarnation, command and session. Logical completion, renderer failure,
context clear and session detach cannot release raw work. Only the actual exact
CEF OnBeforeClose can end that channel. Keep reentrant send and genuine CEF
callback stacks charged until their real C++ return. Capacity or unsupported
tracking poisons authority while original backend behavior may continue.

The standalone controls verify policy and native owner reentrancy, including
reply/close during send, false return, throwing send/callback, foreign/stale
channel/session, queue discard and bounded admission. They execute no CEF and
do not prove dispatch hooks or actual browser retirement.

The separate Source binding proposal must cover every actual page, broker and
utility send and callback. A global barrier additionally needs native admission
watermarks, accepted physical HTTP/file/media/controller lanes, posted callback
ownership, and original output stream publication/drain receipts. Neither empty
logical operation maps nor startup/shutdown labels nor pidfd readiness substitute
for those receipts. Existing CEF-child reuse remains disabled.
