// SPDX-License-Identifier: MIT
/** Dependency-free assets for the authenticated native browser viewer. */
export function nativeViewerHTML(): string {
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="color-scheme" content="dark">
  <meta name="referrer" content="no-referrer">
  <title>SuiteCut · Browser viewer</title>
  <link rel="stylesheet" href="/viewer.css">
  <script src="/viewer.js" defer></script>
</head>
<body>
  <main class="viewer">
    <header class="header">
      <div class="brand"><span class="brand-mark" aria-hidden="true">S</span><div><strong>SuiteCut</strong><span class="brand-caption">Browser viewer</span></div></div>
      <div class="session-status" role="status" aria-live="polite"><span id="status-dot" class="status-dot"></span><span id="status">Connecting…</span></div>
      <div class="session-actions"><button id="help-toggle" class="button button-quiet" type="button" aria-expanded="false" aria-controls="help">Help</button><button id="control" class="button button-primary" type="button" disabled>Take control</button></div>
    </header>
    <nav id="tabs" class="tabs" aria-label="Browser pages"><span class="tabs-empty">Waiting for browser pages…</span></nav>
    <form id="navigation" class="navigation" aria-label="Browser navigation">
      <button id="back" class="icon-button" type="button" aria-label="Go back" title="Go back" disabled>←</button>
      <button id="forward" class="icon-button" type="button" aria-label="Go forward" title="Go forward" disabled>→</button>
      <button id="reload" class="icon-button" type="button" aria-label="Reload page" title="Reload page" disabled>↻</button>
      <label class="address-wrap"><span class="sr-only">Page address</span><input id="address" type="text" inputmode="url" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="Page address" disabled></label>
      <button id="go" class="button button-small" type="submit" disabled>Go</button>
    </form>
    <form id="resolution-form" class="resolution" aria-label="Browser resolution">
      <label class="resolution-choice">Resolution <select id="resolution" disabled><option value="fixed">Native / current</option><option value="auto">Auto fit viewer</option><option value="1280x720">1280 × 720</option><option value="1920x1080">1920 × 1080</option><option value="2560x1440">2560 × 1440</option><option value="3840x2160">3840 × 2160</option><option value="390x844">390 × 844 · portrait</option><option value="768x1024">768 × 1024 · portrait</option></select></label>
      <div class="resolution-custom"><label><span class="sr-only">Custom width</span><input id="resolution-width" type="number" min="2" step="1" inputmode="numeric" aria-label="Custom width" disabled></label><span aria-hidden="true">×</span><label><span class="sr-only">Custom height</span><input id="resolution-height" type="number" min="2" step="1" inputmode="numeric" aria-label="Custom height" disabled></label><button id="resolution-apply" class="button button-small" type="submit" disabled>Apply</button></div>
    </form>
    <div id="notice" class="notice" role="status" aria-live="polite" hidden></div>
    <aside id="help" class="help" hidden>
      <strong>Use the same browser session</strong>
      <p>Choose <b>Take control</b> to pause agent input. Click the page to type, scroll or drag. Choose <b>Resume AI</b> when finished. Another viewer can watch while you are in control.</p>
      <p>Paste text using your usual paste shortcut. Your browser may reserve some shortcuts. Press <kbd>Shift</kbd> + <kbd>Esc</kbd> to leave page keyboard focus and release held input, or click the toolbar. The viewer requires its private session link.</p>
    </aside>
    <section id="stage" class="stage" aria-label="Remote browser viewport">
      <canvas id="screen" width="1280" height="720" tabindex="0" role="img" aria-label="Live browser page. Take control, then click here to interact."></canvas>
      <textarea id="keyboard" class="keyboard-sink" aria-label="Remote browser keyboard input" tabindex="-1" autocomplete="off" autocapitalize="off" spellcheck="false"></textarea>
      <div id="empty" class="empty"><div class="empty-symbol" aria-hidden="true">▣</div><strong id="empty-title">Connecting to your browser</strong><p id="empty-description">The live page will appear here.</p><button id="retry" class="button" type="button" hidden>Reconnect</button></div>
    </section>
    <footer class="footer"><span id="ownership">Read-only until you take control</span><span id="frame-status">Waiting for first frame</span></footer>
  </main>
</body>
</html>`
}

export function nativeViewerStyles(): string {
  return `:root{color-scheme:dark;font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#101318;color:#e9edf5;font-synthesis:none}*{box-sizing:border-box}body{margin:0;min-width:320px}button,input,textarea,select{font:inherit}button{cursor:pointer}button:disabled{cursor:default;opacity:.42}button,input,select{outline-offset:3px}button:focus-visible,input:focus-visible,select:focus-visible,canvas:focus-visible{outline:2px solid #b3c5ff}button{touch-action:manipulation}[hidden]{display:none!important}.viewer{display:flex;flex-direction:column;height:100vh;height:100dvh;min-height:0}.viewer>header,.viewer>nav,.viewer>form,.viewer>aside,.viewer>.notice,.viewer>footer{flex-shrink:0}.header{display:flex;align-items:center;gap:24px;padding:17px 24px;border-bottom:1px solid #2c323d;background:#181d25}.brand{display:flex;align-items:center;gap:11px;min-width:180px}.brand-mark{width:33px;height:33px;border:1px solid #7a94e8;border-radius:10px;background:#26334f;color:#d2ddff;display:grid;place-items:center;font-weight:750;font-size:21px}.brand strong{display:block;font-size:15px;letter-spacing:.02em}.brand-caption{display:block;color:#929eaf;font-size:11px;margin-top:2px}.session-status{display:flex;align-items:center;gap:8px;font-size:13px;flex:1;min-width:0}.status-dot{width:7px;height:7px;flex-shrink:0;background:#8290a4;border-radius:50%;box-shadow:0 0 0 4px #8290a412}.status-dot.agent{background:#82a9ff;box-shadow:0 0 0 4px #82a9ff12}.status-dot.human{background:#7de0b1;box-shadow:0 0 0 4px #7de0b112}.status-dot.error{background:#f19b91;box-shadow:0 0 0 4px #f19b9112}.session-actions{display:flex;gap:8px;align-items:center}.button,.icon-button{border:1px solid #384252;border-radius:7px;background:#242c37;color:#e9edf5;min-height:35px;padding:7px 13px;font-size:12px;font-weight:600;white-space:nowrap}.button:hover:not(:disabled),.icon-button:hover:not(:disabled){background:#303b4b;border-color:#586981}.button-primary{background:#b6caff;border-color:#b6caff;color:#142341;min-width:118px}.button-primary:hover:not(:disabled){background:#cedbff;border-color:#cedbff}.button-primary.is-human{background:#9cdec2;border-color:#9cdec2;color:#132f24}.button-quiet{border-color:transparent;background:transparent;color:#aab6c8}.button-small{min-height:31px;padding:5px 13px}.tabs{display:flex;gap:5px;overflow-x:auto;min-height:43px;padding:8px 24px 0;background:#151a21;scrollbar-width:thin;align-items:stretch}.tab{display:flex;align-items:center;gap:7px;max-width:230px;min-width:80px;padding:8px 12px;border:1px solid transparent;border-radius:7px 7px 0 0;background:transparent;color:#8e9aad;font-size:12px;white-space:nowrap}.tab span{overflow:hidden;text-overflow:ellipsis}.tab[aria-pressed="true"]{background:#202733;border-color:#30394a;border-bottom-color:#202733;color:#e7edf9}.tab:disabled{opacity:.8}.tab-indicator{width:6px;height:6px;border-radius:50%;background:#7293dc;flex-shrink:0}.tabs-empty{font-size:12px;padding:7px 0;color:#7f8da1}.navigation{display:flex;align-items:center;gap:7px;background:#202733;padding:10px 24px;border-bottom:1px solid #323b49}.icon-button{min-width:32px;min-height:31px;padding:3px 7px;font-size:20px;line-height:1;background:transparent;border-color:transparent;font-weight:400}.address-wrap{display:flex;flex:1;min-width:0}.address-wrap input{width:100%;min-width:0;border:1px solid #354155;border-radius:7px;background:#151b24;color:#d8e1f0;font-size:12px;height:33px;padding:7px 11px}.address-wrap input:disabled{opacity:.8}.resolution{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;padding:8px 24px;background:#1a202a;border-bottom:1px solid #323b49;color:#aab6c8;font-size:12px}.resolution-choice,.resolution-custom{display:flex;align-items:center;gap:8px}.resolution select,.resolution input{height:31px;border:1px solid #354155;border-radius:6px;background:#151b24;color:#d8e1f0;padding:4px 8px;min-width:0}.resolution select{max-width:210px}.resolution input{width:80px}.resolution select:disabled,.resolution input:disabled{opacity:.6}.notice{padding:10px 24px;background:#392820;border-bottom:1px solid #664736;color:#f2c4aa;font-size:12px;overflow-wrap:anywhere}.help{max-height:30vh;overflow:auto;padding:17px 24px;background:#1a2639;border-bottom:1px solid #35486a;font-size:12px;line-height:1.65;color:#bfcce0}.help strong{font-size:13px;color:#e0eaff}.help p{margin:6px 0 0;max-width:900px}.help kbd{font-family:inherit;border:1px solid #54627b;border-radius:3px;padding:0 4px}.stage{position:relative;flex:1;min-height:0;display:flex;align-items:center;justify-content:center;overflow:hidden;padding:22px;background:radial-gradient(ellipse at 50% 20%,#1b2330 0,#11151c 75%)}#screen{display:block;max-width:100%;max-height:100%;width:auto;height:auto;background:#fff;border-radius:3px;box-shadow:0 8px 40px #0005;touch-action:none;outline-offset:5px;opacity:0;pointer-events:none}#screen.has-frame{opacity:1}#screen.interactive{pointer-events:auto;cursor:default}.stage:focus-within #screen.interactive{outline:2px solid #a9bfff}.keyboard-sink{position:absolute;left:50%;top:50%;width:1px;height:1px;margin:0;padding:0;opacity:0;overflow:hidden;resize:none;pointer-events:none;border:0;font-size:16px}.empty{position:absolute;inset:20px;display:flex;align-items:center;justify-content:center;flex-direction:column;text-align:center;gap:10px;pointer-events:none}.empty strong{font-size:16px;font-weight:600}.empty p{font-size:12px;line-height:1.5;color:#9ca9bb;max-width:400px;margin:0}.empty .button{pointer-events:auto;margin-top:7px}.empty-symbol{font-size:36px;color:#7587a6;margin-bottom:4px}.footer{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:11px 24px;border-top:1px solid #282f3b;background:#151a21;color:#8493a8;font-size:11px;min-height:37px}.sr-only{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}@media(min-width:1800px){.stage{padding:30px}}@media(max-width:680px){.header{padding:12px;gap:12px;flex-wrap:wrap}.brand{min-width:0;flex:1}.brand-caption{display:none}.session-status{order:3;flex-basis:100%;font-size:12px}.session-actions{gap:3px}.button-primary{min-width:105px}.tabs{padding-left:12px;padding-right:12px}.navigation{padding:8px 9px;gap:2px}.icon-button{min-width:28px}.button-small{padding:5px 9px}.stage{padding:12px}.resolution{padding:8px 12px}.resolution-choice{flex-wrap:wrap;gap:6px}.resolution select{max-width:190px}.footer{padding:10px 12px;align-items:flex-start;font-size:10px}.footer span:last-child{text-align:right}.notice,.help{padding:11px 12px}.tab{max-width:170px}}@media(prefers-reduced-motion:no-preference){.button,.icon-button{transition:background .12s,border-color .12s}.status-dot{transition:background .15s}}`
}

export function nativeViewerScript(): string {
  return String.raw`(() => {
  'use strict';
  const tokenKey = 'suitecut.native-viewer.token.' + location.host;
  let token = '';
  try {
    const fragment = location.hash.slice(1);
    const params = new URLSearchParams(fragment);
    token = params.get('token') || (fragment && !fragment.includes('=') ? decodeURIComponent(fragment) : '') || sessionStorage.getItem(tokenKey) || '';
    if (token) sessionStorage.setItem(tokenKey, token);
  } catch (_) {}
  // A bearer capability must not remain in the address bar or browser history.
  if (location.hash) history.replaceState(null, '', location.pathname + location.search);

  const $ = (id) => document.getElementById(id);
  const ui = { screen: $('screen'), stage: $('stage'), keyboard: $('keyboard'), tabs: $('tabs'), address: $('address'), status: $('status'), dot: $('status-dot'), control: $('control'), notice: $('notice'), empty: $('empty'), title: $('empty-title'), description: $('empty-description'), retry: $('retry'), ownership: $('ownership'), frame: $('frame-status'), resolution: $('resolution'), width: $('resolution-width'), height: $('resolution-height'), apply: $('resolution-apply') };
  const context = ui.screen.getContext('2d', { alpha: false });
  const MAX_QUEUE = 96;
  let state = null;
  let clientId = '';
  let connection = 'connecting';
  let stopped = false;
  let connecting = false;
  let frameReady = false;
  let frameSequence = 0;
  let frameAt = 0;
  let frameFetch = null;
  let frameTimer = 0;
  let stateTimer = 0;
  let tabSignature = '';
  let resolutionSignature = '';
  let desiredResize = null;
  let resizeInFlight = null;
  let resizeTimer = 0;
  let pumping = false;
  let transitionPending = false;
  let resetting = false;
  let needsReset = false;
  let composing = false;
  let compositionCommit = null;
  const queue = [];
  const heldKeys = new Map();
  const heldButtons = new Set();
  const capturedPointers = new Set();
  let lastPointer = null;

  function isOwner() { return !!state && state.mode === 'human' && state.owner === clientId && !!clientId; }
  function canInput() { return isOwner() && connection === 'connected' && !transitionPending && !resetting && !needsReset && !resizeInFlight && frameReady; }
  function describe(error) { return error instanceof Error ? error.message : String(error); }
  function showError(message) { ui.notice.textContent = message; ui.notice.hidden = !message; }
  function empty(title, description, retry) {
    ui.title.textContent = title;
    ui.description.textContent = description;
    ui.empty.hidden = false;
    ui.retry.hidden = !retry;
  }
  function labelForPage(page) {
    if (!page.url || page.url === 'about:blank') return 'New page';
    try { const url = new URL(page.url); return url.hostname || page.url; } catch (_) { return page.url; }
  }
  function render() {
    const owned = isOwner();
    const connected = connection === 'connected';
    const human = state && state.mode === 'human';
    let status = connection === 'connecting' ? 'Connecting…' : 'Disconnected';
    let dot = '';
    if (connected && state) {
      if (state.mode === 'agent') { status = 'Agent has control'; dot = 'agent'; }
      else if (state.mode === 'taking-over') status = 'Pausing agent input…';
      else if (state.mode === 'releasing') status = 'Returning control to agent…';
      else if (state.mode === 'closed') { status = 'Browser session closed'; dot = 'error'; }
      else if (owned) { status = 'You have control'; dot = 'human'; }
      else if (state.owner === null) status = 'Agent paused · take control to continue';
      else { status = 'Another viewer has control'; dot = 'human'; }
    } else if (connection === 'disconnected') dot = 'error';
    ui.status.textContent = status;
    ui.dot.className = 'status-dot' + (dot ? ' ' + dot : '');
    ui.control.textContent = owned ? 'Resume AI' : 'Take control';
    ui.control.classList.toggle('is-human', owned);
    ui.control.disabled = !connected || !state || transitionPending || !(owned || state.mode === 'agent' || (state.mode === 'human' && state.owner === null));
    const controls = connected && owned && !transitionPending;
    for (const id of ['address', 'back', 'forward', 'reload', 'go']) $(id).disabled = !controls || !state || !state.pageId;
    for (const field of [ui.resolution, ui.width, ui.height, ui.apply]) field.disabled = !controls || !state || !state.pageId || !!resizeInFlight;
    if (state) {
      const signature = JSON.stringify([state.pageId, state.width, state.height, state.resolutionMode, state.maxWidth, state.maxHeight]);
      if (signature !== resolutionSignature) {
        resolutionSignature = signature;
        const preset = state.width + 'x' + state.height;
        ui.resolution.value = state.resolutionMode === 'auto' ? 'auto' : Array.from(ui.resolution.options).some((option) => option.value === preset) ? preset : 'fixed';
        ui.width.value = state.width; ui.height.value = state.height;
        ui.width.max = maxDimension('maxWidth'); ui.height.max = maxDimension('maxHeight');
        for (const option of ui.resolution.options) {
          const size = option.value.split('x').map(Number);
          option.disabled = size.length === 2 && (size[0] > maxDimension('maxWidth') || size[1] > maxDimension('maxHeight'));
        }
      }
    }
    ui.screen.classList.toggle('interactive', canInput());
    ui.screen.setAttribute('aria-label', owned ? 'Live browser page. Click to interact. Use the toolbar to return control to the agent.' : 'Live browser page, read only. Choose Take control to interact.');
    ui.ownership.textContent = owned ? 'Human input · agent paused' : human ? 'Watching · another viewer is in control' : 'Watching · take control to interact';
    if (state) {
      const pages = Array.isArray(state.pages) ? state.pages : [];
      const selected = pages.find((page) => page.id === state.pageId);
      if (document.activeElement !== ui.address) ui.address.value = selected ? selected.url : '';
      const signature = JSON.stringify([state.pageId, pages.map((page) => [page.id, page.url]), controls]);
      if (signature !== tabSignature) {
        tabSignature = signature;
        ui.tabs.replaceChildren();
        if (!pages.length) {
          const hint = document.createElement('span'); hint.className = 'tabs-empty'; hint.textContent = 'No browser pages'; ui.tabs.append(hint);
        }
        for (const page of pages) {
          const button = document.createElement('button'); button.type = 'button'; button.className = 'tab'; button.disabled = !controls;
          button.setAttribute('aria-pressed', String(page.id === state.pageId)); button.title = page.url || 'New page';
          const indicator = document.createElement('span'); indicator.className = 'tab-indicator'; indicator.setAttribute('aria-hidden', 'true');
          const text = document.createElement('span'); text.textContent = labelForPage(page);
          button.append(indicator, text); button.addEventListener('click', () => enqueue('selectPage', { pageId: page.id })); ui.tabs.append(button);
        }
      }
    }
    if (state && state.mode === 'closed') empty('Browser session closed', 'This viewer is no longer attached to a running browser.', false);
    else if (!frameReady && connected && state && !state.pageId) empty('No browser page', 'Create or open a page in the connected browser session.', false);
    else if (frameReady && connected) ui.empty.hidden = true;
    updateFrameStatus();
  }
  function updateFrameStatus() {
    if (!frameReady || !state) { ui.frame.textContent = 'Waiting for first frame'; return; }
    const age = Date.now() - frameAt;
    ui.frame.textContent = state.width + ' × ' + state.height + (connection !== 'connected' ? ' · connection lost' : age > 3000 ? ' · last frame ' + Math.floor(age / 1000) + 's ago' : ' · live');
  }
  function clearLocalInput() {
    heldKeys.clear(); heldButtons.clear(); composing = false; compositionCommit = null; ui.keyboard.value = ''; lastPointer = null;
    for (const id of capturedPointers) {
      try { if (ui.screen.hasPointerCapture(id)) ui.screen.releasePointerCapture(id); } catch (_) {}
    }
    capturedPointers.clear();
  }
  function acceptState(next) {
    if (next && next.state) next = next.state;
    if (!next || !Number.isSafeInteger(next.revision) || !['agent', 'taking-over', 'human', 'releasing', 'closed'].includes(next.mode) || !Array.isArray(next.pages)) throw new Error('The viewer received an invalid session state.');
    if (state && next.revision < state.revision) return;
    const changed = !state || next.revision !== state.revision || next.pageId !== state.pageId || next.width !== state.width || next.height !== state.height;
    const ownerChanged = !state || next.owner !== state.owner || next.mode !== state.mode;
    const pageChanged = !state || next.pageId !== state.pageId;
    state = next;
    connection = 'connected';
    if (changed || ownerChanged) {
      if (!isOwner() || ownerChanged || pageChanged) desiredResize = null;
      queue.length = 0; clearLocalInput(); resetting = false; needsReset = false;
      if (changed) {
        frameReady = false; frameSequence = 0; ui.screen.classList.remove('has-frame');
        if (frameFetch) frameFetch.abort();
        empty('Updating browser view', 'Waiting for a frame from the current page.', false);
      }
    }
    render();
    if (changed || ownerChanged) scheduleAutoResize();
  }
  function maxDimension(name) { return state && Number.isSafeInteger(state[name]) && state[name] >= 2 ? state[name] : 16384; }
  function fitSize() {
    const style = getComputedStyle(ui.stage);
    const width = Math.floor(ui.stage.clientWidth - (parseFloat(style.paddingLeft) || 0) - (parseFloat(style.paddingRight) || 0));
    const height = Math.floor(ui.stage.clientHeight - (parseFloat(style.paddingTop) || 0) - (parseFloat(style.paddingBottom) || 0));
    return { width: Math.max(2, Math.min(maxDimension('maxWidth'), width)), height: Math.max(2, Math.min(maxDimension('maxHeight'), height)) };
  }
  function requestResize(width, height, mode) {
    if (!isOwner() || connection !== 'connected' || transitionPending || stopped) return;
    if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width < 2 || height < 2 || width > maxDimension('maxWidth') || height > maxDimension('maxHeight')) {
      showError('Choose a whole-number resolution from 2 × 2 to ' + maxDimension('maxWidth') + ' × ' + maxDimension('maxHeight') + '.'); return;
    }
    desiredResize = { width, height, mode, pageId: state.pageId };
    flushResize();
  }
  function flushResize() {
    // Keep resize outside the input FIFO: newer dimensions can replace an unsent
    // request, while every existing key/button edge completes in its original order.
    if (!desiredResize || !canInput() || pumping || queue.length || heldKeys.size || heldButtons.size || composing) return;
    const desired = desiredResize;
    desiredResize = null;
    if (desired.pageId !== state.pageId) return;
    if (desired.width === state.width && desired.height === state.height && desired.mode === state.resolutionMode) return;
    resizeInFlight = desired;
    clearLocalInput(); render();
    if (!enqueue('resize', { width: desired.width, height: desired.height, mode: desired.mode })) { resizeInFlight = null; render(); }
  }
  function scheduleAutoResize() {
    clearTimeout(resizeTimer);
    if (!isOwner() || !state || (desiredResize ? desiredResize.mode : resizeInFlight ? resizeInFlight.mode : state.resolutionMode) !== 'auto') return;
    resizeTimer = window.setTimeout(() => {
      if (!isOwner() || !state || (desiredResize ? desiredResize.mode : resizeInFlight ? resizeInFlight.mode : state.resolutionMode) !== 'auto') return;
      const size = fitSize(); requestResize(size.width, size.height, 'auto');
    }, 200);
  }
  async function request(path, init) {
    const response = await fetch(path, Object.assign({ cache: 'no-store', credentials: 'omit', redirect: 'error' }, init || {}, { headers: Object.assign({ Authorization: 'Bearer ' + token }, init && init.headers) }));
    if (!response.ok) {
      let message = 'Viewer request failed (' + response.status + ').';
      try { const body = await response.json(); if (typeof body.error === 'string') message = body.error; } catch (_) {}
      const error = new Error(message); error.status = response.status; throw error;
    }
    return response;
  }
  function disconnect(error) {
    if (stopped) return;
    needsReset = needsReset || isOwner();
    connection = 'disconnected'; desiredResize = null; clearTimeout(resizeTimer); queue.length = 0; clearLocalInput(); resetting = needsReset;
    showError(describe(error));
    if (!frameReady) empty('Unable to connect', 'Use the private viewer link from the running SuiteCut session, or reconnect after restoring the connection.', true);
    render();
  }
  function admitted(type) {
    if (!state || connection !== 'connected' || !clientId || stopped) return false;
    if (type === 'heartbeat') return state.mode !== 'closed';
    if (type === 'takeover') return state.mode === 'agent' || (state.mode === 'human' && state.owner === null);
    return isOwner();
  }
  function enqueue(type, details) {
    if (!admitted(type)) return false;
    const item = Object.assign({ clientId, revision: state.revision, type }, details || {});
    const previous = queue[queue.length - 1];
    if (previous && previous.revision === item.revision && type === 'pointer' && item.action === 'move' && previous.type === 'pointer' && previous.action === 'move') {
      queue[queue.length - 1] = item;
    } else if (previous && previous.revision === item.revision && type === 'wheel' && previous.type === 'wheel') {
      previous.deltaX = Math.max(-100000, Math.min(100000, previous.deltaX + item.deltaX));
      previous.deltaY = Math.max(-100000, Math.min(100000, previous.deltaY + item.deltaY));
    } else {
      if (queue.length >= MAX_QUEUE) {
        queue.length = 0; clearLocalInput(); resetting = true;
        queue.push({ clientId, revision: state.revision, type: 'resetInput' });
        showError('Input arrived faster than the browser could process it. Held input was reset; try again.'); render(); pump(); return false;
      }
      queue.push(item);
    }
    pump(); return true;
  }
  async function pump() {
    if (pumping || stopped) return;
    pumping = true;
    try {
      while (queue.length && !stopped) {
        const item = queue.shift();
        if (!state || item.revision !== state.revision || !admitted(item.type)) continue;
        try {
          const response = await request('/action', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(item) });
          acceptState(await response.json());
          if (item.type === 'resize') { resizeInFlight = null; showError(''); }
          if (item.type === 'resetInput') { resetting = false; needsReset = false; render(); }
        } catch (error) {
          queue.length = 0; clearLocalInput();
          if (item.type === 'resize') { resizeInFlight = null; desiredResize = null; }
          needsReset = isOwner() && item.type !== 'resetInput' && item.type !== 'heartbeat'; resetting = needsReset;
          showError(describe(error));
          if (error.status === 401 && /^Viewer session expired/.test(error.message)) {
            try { await openClientSession(); await refreshState(); } catch (sessionError) { disconnect(sessionError); }
            break;
          }
          // A failed cleanup stays fail-closed and retries on the bounded state poll.
          if (item.type === 'resetInput') { needsReset = true; resetting = true; disconnect(error); break; }
          if (!error.status) disconnect(error);
          await refreshState();
          break;
        }
      }
    } finally { pumping = false; transitionPending = false; scheduleReset(); render(); if (queue.length) pump(); else flushResize(); }
  }
  function scheduleReset() {
    if (needsReset && isOwner() && connection === 'connected' && !stopped) {
      needsReset = false; resetting = true; enqueue('resetInput');
    }
  }
  function resetInput() {
    if (isOwner() && connection === 'connected') {
      queue.length = 0; clearLocalInput(); needsReset = false; resetting = true; enqueue('resetInput'); render();
    } else clearLocalInput();
  }
  async function refreshState() {
    if (!clientId || stopped) return;
    try { acceptState(await (await request('/state')).json()); scheduleReset(); } catch (error) { disconnect(error); }
  }
  async function pollState() {
    await refreshState();
    if (!stopped) stateTimer = window.setTimeout(pollState, document.hidden ? 3000 : 1000);
  }
  async function decodeFrame(blob) {
    if (typeof createImageBitmap === 'function') return createImageBitmap(blob);
    const url = URL.createObjectURL(blob);
    try {
      const image = new Image();
      await new Promise((resolve, reject) => { image.onload = resolve; image.onerror = () => reject(new Error('Unable to decode the browser frame.')); image.src = url; });
      return image;
    } finally { URL.revokeObjectURL(url); }
  }
  async function pollFrame() {
    const started = performance.now();
    let quiet = false;
    if (!stopped && !document.hidden && state && state.pageId && state.mode !== 'closed' && connection === 'connected') {
      const revision = state.revision;
      const controller = new AbortController(); frameFetch = controller;
      const timeout = window.setTimeout(() => controller.abort(), 15000);
      try {
        const response = await request('/frame?after=' + frameSequence + '&revision=' + revision + '&wait=1', { signal: controller.signal });
        if (response.status === 204) quiet = true;
        else {
          const sequence = Number(response.headers.get('x-frame-sequence'));
          const receivedRevision = Number(response.headers.get('x-viewer-revision'));
          if (!Number.isSafeInteger(sequence) || sequence < 0 || !Number.isSafeInteger(receivedRevision)) throw new Error('The frame has invalid synchronization metadata.');
          const blob = await response.blob();
          if (state && revision === state.revision && receivedRevision === revision && sequence > frameSequence) {
            const image = await decodeFrame(blob);
            try {
              if (state && revision === state.revision && receivedRevision === revision && sequence > frameSequence) {
                const width = Math.max(1, Math.min(16384, state.width));
                const height = Math.max(1, Math.min(16384, state.height));
                if (ui.screen.width !== width) ui.screen.width = width;
                if (ui.screen.height !== height) ui.screen.height = height;
                context.drawImage(image, 0, 0, width, height);
                const firstFrame = !frameReady;
                frameSequence = sequence; frameAt = Date.now(); frameReady = true; ui.screen.dataset.frameSequence = String(sequence); ui.screen.dataset.revision = String(revision); ui.screen.classList.add('has-frame'); if (firstFrame) render(); flushResize();
              }
            } finally { if (typeof image.close === 'function') image.close(); }
          }
        }
      } catch (error) {
        if (error.name !== 'AbortError') {
          if (error.status === 409) await refreshState();
          else disconnect(error);
        }
      } finally { clearTimeout(timeout); if (frameFetch === controller) frameFetch = null; }
    } else quiet = true;
    if (!stopped) frameTimer = window.setTimeout(pollFrame, Math.max(0, (document.hidden ? 500 : quiet ? 80 : 34) - (performance.now() - started)));
  }
  async function connect() {
    if (connecting || stopped) return;
    connecting = true;
    if (!token) {
      connection = 'disconnected'; empty('Open your private viewer link', 'The link from SuiteCut contains the access token needed to view this browser.', false); render(); connecting = false; return;
    }
    connection = 'connecting'; showError(''); render();
    try {
      await openClientSession();
      await refreshState();
      clearTimeout(stateTimer); clearTimeout(frameTimer); pollState(); pollFrame();
    } catch (error) { disconnect(error); } finally { connecting = false; }
  }
  async function openClientSession() {
    const session = await (await request('/session', { method: 'POST' })).json();
    if (!session || typeof session.clientId !== 'string' || !session.clientId) throw new Error('The viewer received an invalid client session.');
    clientId = session.clientId;
  }

  ui.control.addEventListener('click', () => {
    if (!state || transitionPending) return;
    showError(''); queue.length = 0; clearLocalInput();
    const type = isOwner() ? 'release' : 'takeover';
    if (enqueue(type)) { transitionPending = true; render(); }
  });
  ui.resolution.addEventListener('change', () => {
    clearTimeout(resizeTimer);
    const value = ui.resolution.value;
    if (value === 'auto') { const size = fitSize(); requestResize(size.width, size.height, 'auto'); }
    else if (value === 'fixed') requestResize(state.width, state.height, 'fixed');
    else { const size = value.split('x').map(Number); requestResize(size[0], size[1], 'fixed'); }
  });
  $('resolution-form').addEventListener('submit', (event) => {
    event.preventDefault(); clearTimeout(resizeTimer);
    requestResize(Number(ui.width.value), Number(ui.height.value), 'fixed');
  });
  if (typeof ResizeObserver === 'function') new ResizeObserver(scheduleAutoResize).observe(ui.stage);
  window.addEventListener('resize', scheduleAutoResize);
  $('navigation').addEventListener('submit', (event) => {
    event.preventDefault();
    const value = ui.address.value.trim();
    if (!value) return;
    const url = /^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(value) ? value : 'https://' + value;
    enqueue('navigate', { url });
  });
  for (const type of ['back', 'forward', 'reload']) $(type).addEventListener('click', () => enqueue(type));
  $('help-toggle').addEventListener('click', () => {
    const expanded = $('help-toggle').getAttribute('aria-expanded') !== 'true';
    $('help-toggle').setAttribute('aria-expanded', String(expanded)); $('help').hidden = !expanded;
  });
  ui.retry.addEventListener('click', () => { if (clientId) { showError(''); refreshState(); } else connect(); });
  function coordinates(event) {
    const rect = ui.screen.getBoundingClientRect();
    return { x: Math.max(0, Math.min(state.width - 1, (event.clientX - rect.left) * state.width / rect.width)), y: Math.max(0, Math.min(state.height - 1, (event.clientY - rect.top) * state.height / rect.height)) };
  }
  function focusKeyboard() { if (canInput()) ui.keyboard.focus({ preventScroll: true }); }
  function pointerButton(button) { return ['left', 'middle', 'right'][button]; }
  function pointer(action, button, event) {
    const point = coordinates(event);
    lastPointer = point;
    enqueue('pointer', Object.assign({ action, button }, point));
  }
  ui.screen.addEventListener('pointerdown', (event) => {
    if (!canInput() || !pointerButton(event.button)) return;
    event.preventDefault(); focusKeyboard();
    try { ui.screen.setPointerCapture(event.pointerId); capturedPointers.add(event.pointerId); } catch (_) {}
    heldButtons.add(event.button);
    pointer('down', pointerButton(event.button), event);
  });
  ui.screen.addEventListener('pointermove', (event) => {
    if (!canInput()) return;
    event.preventDefault(); pointer('move', 'left', event);
  });
  ui.screen.addEventListener('pointerup', (event) => {
    if (!canInput() || !heldButtons.has(event.button)) return;
    event.preventDefault(); heldButtons.delete(event.button);
    pointer('up', pointerButton(event.button), event);
    if (!event.buttons) { capturedPointers.delete(event.pointerId); try { ui.screen.releasePointerCapture(event.pointerId); } catch (_) {} }
  });
  ui.screen.addEventListener('pointercancel', resetInput);
  ui.screen.addEventListener('lostpointercapture', () => { if (heldButtons.size) resetInput(); });
  ui.screen.addEventListener('contextmenu', (event) => { if (isOwner()) event.preventDefault(); });
  ui.screen.addEventListener('wheel', (event) => {
    if (!canInput()) return;
    event.preventDefault();
    const factor = event.deltaMode === WheelEvent.DOM_DELTA_LINE ? 16 : event.deltaMode === WheelEvent.DOM_DELTA_PAGE ? state.height : 1;
    const point = coordinates(event);
    if (!lastPointer || lastPointer.x !== point.x || lastPointer.y !== point.y) pointer('move', 'left', event);
    enqueue('wheel', { deltaX: Math.max(-100000, Math.min(100000, event.deltaX * factor)), deltaY: Math.max(-100000, Math.min(100000, event.deltaY * factor)) });
  }, { passive: false });
  ui.screen.addEventListener('focus', focusKeyboard);

  function physicalKey(event) {
    if (/^Key[A-Z]$/.test(event.code)) return event.code.slice(3).toLowerCase();
    if (/^Digit[0-9]$/.test(event.code)) return event.code.slice(5);
    const punctuation = { Space: 'Space', Backquote: String.fromCharCode(96), Minus: '-', Equal: '=', BracketLeft: '[', BracketRight: ']', Backslash: '\\', Semicolon: ';', Quote: "'", Comma: ',', Period: '.', Slash: '/' };
    return punctuation[event.code] || (event.key === ' ' ? 'Space' : event.key);
  }
  function printable(event) { return Array.from(event.key).length === 1; }
  function insertText(text) {
    if (!canInput() || !text) return;
    if (text.length > 65536) { showError('Paste up to 65,536 characters at a time.'); return; }
    for (let start = 0; start < text.length;) {
      let end = Math.min(text.length, start + 4096);
      const last = text.charCodeAt(end - 1);
      if (end < text.length && last >= 0xd800 && last <= 0xdbff) end--;
      if (!enqueue('text', { text: text.slice(start, end) })) break;
      start = end;
    }
  }
  ui.stage.addEventListener('keydown', (event) => {
    if (!canInput()) return;
    if (!event.isComposing && !composing) compositionCommit = null;
    if (event.isComposing || composing || event.key === 'Process' || event.key === 'Dead' || event.key === 'Unidentified') return;
    if (event.key === 'Escape' && event.shiftKey) { event.preventDefault(); resetInput(); ui.control.focus(); return; }
    const altGraph = event.getModifierState && event.getModifierState('AltGraph');
    const shortcut = !altGraph && (event.ctrlKey || event.metaKey || event.altKey);
    // Leave normal text insertion to beforeinput so Unicode and input methods work.
    if (printable(event) && !shortcut) return;
    // The paste event supplies the clipboard text without reading the OS clipboard.
    if ((event.ctrlKey || event.metaKey) && !event.altKey && event.code === 'KeyV') return;
    event.preventDefault();
    const identity = event.code || event.key;
    const key = physicalKey(event);
    if (!key || key.length > 128) return;
    if (!heldKeys.has(identity)) heldKeys.set(identity, key);
    enqueue('key', { action: 'down', key: heldKeys.get(identity) });
  });
  ui.stage.addEventListener('keyup', (event) => {
    const identity = event.code || event.key;
    const key = heldKeys.get(identity);
    if (!key) return;
    event.preventDefault(); heldKeys.delete(identity);
    if (canInput()) enqueue('key', { action: 'up', key });
  });
  ui.keyboard.addEventListener('beforeinput', (event) => {
    if (!canInput()) { event.preventDefault(); return; }
    if (event.isComposing || composing || event.inputType === 'insertCompositionText') return;
    if (event.inputType.startsWith('insert') && event.data) {
      event.preventDefault();
      if (compositionCommit && compositionCommit.text === event.data && Date.now() - compositionCommit.at < 150) { compositionCommit = null; return; }
      compositionCommit = null; insertText(event.data); ui.keyboard.value = '';
    } else if (event.inputType === 'insertLineBreak' || event.inputType === 'insertParagraph') {
      event.preventDefault(); enqueue('key', { action: 'down', key: 'Enter' }); enqueue('key', { action: 'up', key: 'Enter' });
    } else if (event.inputType === 'deleteContentBackward' || event.inputType === 'deleteContentForward') {
      // Mobile input methods can issue edits without a physical key event.
      event.preventDefault(); const key = event.inputType === 'deleteContentBackward' ? 'Backspace' : 'Delete';
      if (!heldKeys.size) { enqueue('key', { action: 'down', key }); enqueue('key', { action: 'up', key }); }
    }
  });
  ui.keyboard.addEventListener('input', () => { if (!composing) ui.keyboard.value = ''; });
  ui.keyboard.addEventListener('compositionstart', () => { composing = true; compositionCommit = null; });
  ui.keyboard.addEventListener('compositionend', (event) => {
    composing = false; ui.keyboard.value = '';
    if (event.data) { compositionCommit = { text: event.data, at: Date.now() }; insertText(event.data); }
  });
  ui.keyboard.addEventListener('paste', (event) => {
    if (!canInput()) return;
    event.preventDefault(); const text = event.clipboardData && event.clipboardData.getData('text/plain'); if (text) insertText(text);
  });
  ui.stage.addEventListener('focusout', (event) => { if (!ui.stage.contains(event.relatedTarget) && (heldKeys.size || heldButtons.size)) resetInput(); });
  window.addEventListener('blur', () => { if (heldKeys.size || heldButtons.size || composing) resetInput(); });
  document.addEventListener('visibilitychange', () => { if (document.hidden && (heldKeys.size || heldButtons.size || composing)) resetInput(); });
  window.addEventListener('pagehide', () => {
    if (stopped) return;
    if (isOwner()) {
      fetch('/action', { method: 'POST', cache: 'no-store', credentials: 'omit', redirect: 'error', keepalive: true, headers: { Authorization: 'Bearer ' + token, 'Content-Type': 'application/json' }, body: JSON.stringify({ clientId, revision: state.revision, type: 'release' }) }).catch(() => {});
    }
    stopped = true; clearTimeout(resizeTimer); desiredResize = null; queue.length = 0; clearLocalInput(); clearTimeout(frameTimer); clearTimeout(stateTimer); if (frameFetch) frameFetch.abort();
  });
  window.setInterval(() => { if (state && state.mode !== 'closed' && connection === 'connected' && !pumping && !queue.length) enqueue('heartbeat'); }, 5000);
  window.setInterval(updateFrameStatus, 1000);
  connect();
})();`
}
