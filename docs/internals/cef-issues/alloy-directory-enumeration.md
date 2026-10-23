# Report title

alloy: DOM.setFileInputFiles cancels webkitdirectory enumeration

# Exact proposed issue body

**Describe the bug**

With an Alloy offscreen browser, `DOM.setFileInputFiles` accepts a real directory
for an `<input type="file" webkitdirectory>`, returns an empty success result,
but leaves the original input's native `FileList` empty. A regular-file input
with the same existing file works.

The browser-side debugger trace reaches the inherited default
`content::WebContentsDelegate::EnumerateDirectory`, which cancels the original
listener. `AlloyBrowserHostImpl` has no `EnumerateDirectory` override at the tested
CEF revision. This report concerns direct native directory enumeration, separate
from opening a native picker or its upload-confirmation dialog.

**To Reproduce**

1. Use an Alloy-style offscreen browser with the CEF version below. Create a
   directory `/absolute/path/cef-folder` containing `first.txt` and
   `nested/second.txt` with distinct contents.
2. Load `<input id="upload" type="file" webkitdirectory>`.
3. Through the browser's DevTools interface, run:

   ```js
   DOM.getDocument({})
   DOM.querySelector({nodeId: /* root.nodeId */, selector: '#upload'})
   DOM.setFileInputFiles({nodeId: /* query result */, files: ['/absolute/path/cef-folder']})
   ```

4. After waiting for native enumeration, evaluate:

   ```js
   await Promise.all(Array.from(document.querySelector('#upload').files,
     async f => ({name: f.name, path: f.webkitRelativePath, text: await f.text()})))
   ```

5. Observe `[]`. As a control, remove `webkitdirectory` and call
   `DOM.setFileInputFiles` with the absolute path to `first.txt`; the real file
   and its expected contents are available.

The direct protocol experiment also reproduced with a trailing directory slash
and with the input's `multiple` attribute. There is no synthetic `FileList`,
JavaScript `DataTransfer`, or replacement input in this reproduction.

**Expected behavior**

The original input receives both native files, their contents, and relative paths
`cef-folder/first.txt` and `cef-folder/nested/second.txt`. Alternatively, an
explicit unsupported-operation error would be preferable to an empty protocol
success followed by silent native cancellation.

**Versions**

- OS: Debian GNU/Linux 13.6 (trixie), Linux x86-64.
- Embedder compiler: GCC (Debian 14.2.0-19) 14.2.0.
- CEF: `156.0.7+g47dbc1d+chromium-156.0.8078.17`.
- CEF commit: `47dbc1df023e99f006fb9e850a8d83cc19b4fdd9`.
- Chromium: `156.0.8078.17`.
- Stock `libcef.so` SHA-256:
  `d6ae34b71b65685b9fc84f24b53214cdb71a17ceecca0034c3a95a9d4358d86f`.
- Offscreen rendering; Alloy style; sandbox enabled.

**Additional context**

Observed native call chain, with the stock library:

```text
content::FileChooserImpl::EnumerateChosenDirectory
content::WebContentsImpl::EnumerateDirectory
content::WebContentsDelegate::EnumerateDirectory
content::FileChooserImpl::FileSelectionCanceled
```

`FileSelectHelper::EnumerateDirectoryImpl` was not reached. The relevant sources
are [CEF's Alloy host](https://github.com/chromiumembedded/cef/blob/47dbc1df023e99f006fb9e850a8d83cc19b4fdd9/libcef/browser/alloy/alloy_browser_host_impl.h)
and [Chromium's default delegate](https://chromium.googlesource.com/chromium/src/+/refs/tags/156.0.8078.17/content/public/browser/web_contents_delegate.cc).

There is a small candidate forwarding the real listener and directory to
`FileSelectHelper::EnumerateDirectory`. It has only passed source applicability
checks; it has not been compiled or runtime verified, so this report does not
claim that candidate is a confirmed fix.

Reproduced in a native CEF embedder. The stock `cefclient`/`cefsimple` applications
and same-version Google Chrome have not been run for this report. The reproduction
does not establish behavior on newer CEF versions or other platforms.

Related issues checked: #958 (historical directory-picker support), #3314
(file-dialog unification), and #3316 (JS dialogs and directory confirmation).
This report isolates direct DevTools directory enumeration without a picker.
An issue search for `EnumerateDirectory` found no existing matching report on
2026-10-17.

This report and reproduction were prepared with assistance from OpenAI Codex.
