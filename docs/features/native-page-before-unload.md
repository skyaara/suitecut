# Native page close and beforeunload

`page.close({ runBeforeUnload: true })` requests a real CEF
`CloseBrowser(false)` and resolves when that dispatch is acknowledged. It does
not wait for the page to close. Subscribe to the page's dialog event before
requesting close if the application needs to accept or dismiss its real
`beforeunload` dialog.

Dismissal, the existing unanswered-dialog timeout, or a real CEF dialog reset
revokes only that close attempt. The page and context stay usable, and a later
close can be requested. A dispatch acknowledgement never emits a close event
or removes the page from its context. Default close and context/browser shutdown
still force close and await their existing physical lifecycle.

SuiteCut's windowless CEF browsers use Alloy. Genuine `DoClose` approval starts
the existing HAR, recording, streaming and download cleanup before final browser
destruction. The close's original native deadline is retained through that
cleanup; dialog decisions do not renew it. Actual `OnBeforeClose` alone supplies
the page-closed receipt. Pending attempts and dialog callbacks remain part of
native accepted-work fencing. No authored JavaScript substitutes for unload
handlers or a CEF close receipt.
