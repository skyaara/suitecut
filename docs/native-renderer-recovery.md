# Native child renderer recovery

A physically open child page can call `page.reload()` after an actual renderer crash. The C++ engine calls the supported `CefBrowser::Reload` API and admits the new renderer only after a genuine render-view callback, a new committed main document, and both main and utility Runtime contexts. DOM access stays failed until these facts exist. Generic helper/protocol failure and failure of the initial root page remain fatal; recovery currently supports child-page reload, not arbitrary navigation of a failed page.

The Page and source objects keep their identities. `source.signal` and `source.failure` describe the current render epoch; previously captured signals and failure promises stay failed permanently. The optional `source.pageSignal` describes actual page/helper closure, allowing durable event listeners, routes and exposed binding definitions to survive a renderer crash. Existing DOM calls, handle creation, temporary evaluation groups and host binding calls belong to their original epoch and cannot resume after recovery. Unresolved host callback promises retain their bounded physical admission slots until they settle.

Recordings and publishers belonging to the old renderer fail. The engine retains their child processes and files until actual physical settlement. Recovery can admit fresh recording and publishing jobs; it does not revive failed media. A closing page/context is never reopened by recovery.

Reload observers keep their original native deadline and cancellation. If an observer times out or cancels, the actual CEF navigation remains owned by the page. Another reload observer joins that outstanding navigation without a second HTTP request. A real load error or another renderer crash ends that recovery attempt. Context/page close cancels observers and waits for the real CEF close boundary.

Native diagnostics expose actual renderer generation, recovery state, active handles, pending handles and evaluation groups. `failedEpoch*Metadata` counters refer to exact logical metadata entries observed at the last genuine crash and retired thereafter; they do not claim a remote Runtime release acknowledgement. Current-document console handles remain separately owned by the console journal. Clearing that journal through the public native API is separate from retiring old-epoch handles.

Run the three authored proof groups against a built helper:

```sh
SUITECUT_NATIVE_EXECUTABLE=/absolute/path/to/suitecut-browser pnpm test:native:recovery
```

They trigger real renderer crashes and HTTP reloads, retain bindings/routes and console/frame/error/dialog/chooser/download/worker/WebSocket events, compare actual uploaded/downloaded bytes, preserve failed old calls/handles, inspect native ownership before page close, and exercise original deadlines/cancellation/physical context close. The media group creates real native publishers and recordings before and after the crash, checks old encoder death and TCP EOF, and decodes both completed new files with FFmpeg. It does not claim a canceled partial clip is decodable.
