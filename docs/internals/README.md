# Engineering notes

Design contracts, verification records, performance reviews and upstream CEF
reports kept for maintainers. They describe how features were built and checked,
may cite local evidence under the gitignored `.suitecut/` directory or commits
from private integration builds, and are not user documentation. Start with the
[documentation index](../README.md) instead.

These notes were written while a Node SDK also drove the engine. Where a note
says "the SDK", "the host" or "the adapter" without naming the C++ client, it
means that Node package, which was removed on 2026-11-22. The engine contracts
the notes describe are still in [`native/browser`](../../native/browser/).

| Note                                                                                          |
| --------------------------------------------------------------------------------------------- |
| [Native context route admission (first slice)](native-context-route-contract.md)              |
| [Private context worker owner closure](native-context-worker-ownership.md)                    |
| [Native credential support and backend dependencies](native-credential-backend-boundaries.md) |
| [Native HAR route registration and archive ownership](native-har-route-order-contract.md)     |
| [Native init snapshot transport components](native-init-snapshot-transport.md)                |
| [Native startup cache preparation](native-init-startup-cache.md)                              |
| [Page route callback removal](native-page-route-removal-contract.md)                          |
| [Native engine and browser-control performance review](native-performance-review.md)          |
| [Native worker debugger ownership contract](native-worker-debugger-ownership.md)              |
| [Upstream CEF reports and feature ownership](cef-issues/README.md)                            |
