# Native context permissions

`grantPermissions` and the context `permissions` option use the native CEF permission table. Mapped permissions accumulate within the selected origin or wildcard scope. Within an explicitly overridden scope, unlisted mapped settings are blocked. `clearPermissions` restores the actual applied content settings to CEF defaults.

`captured-surface-control` maps to CEF's captured-surface permission prompt flag and content setting. It controls permission for an application capturing a tab to scroll or zoom that captured tab; granting it does not create a capture or implement capture actions.

Two accepted names describe browser-intrinsic behavior rather than controllable native overrides:

- `clipboard-write`: CEF documents sanitized clipboard writes as always allowed, without associated preference data. This name does not grant full clipboard read/write access. The separate `clipboard-read` mapping uses CEF's broader clipboard permission.
- `screen-wake-lock`: CEF documents screen locks as needing no additional permission. Accepting this name does not override other browser requirements for a wake lock.

These two intrinsic names produce no native permission flag or content-setting write. Granting other permissions does not natively deny them, and `clearPermissions` does not reset their browser-intrinsic behavior. Their acceptance is not a guarantee that every clipboard or wake-lock operation succeeds.
