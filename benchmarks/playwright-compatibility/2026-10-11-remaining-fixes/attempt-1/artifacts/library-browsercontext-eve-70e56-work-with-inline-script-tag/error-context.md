# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: library/browsercontext-events.spec.ts >> dialog event should work with inline script tag
- Location: ../../tmp/suitecut-playwright-upstream/playwright-1.62.1/tests/library/browsercontext-events.spec.ts:154:5

# Error details

```
Error: dialog.accept: Protocol error (Page.handleJavaScriptDialog): No dialog is showing
```

# Page snapshot

```yaml
- link "Click me" [active] [ref=e2] [cursor=pointer]:
  - /url: popup.html
```

# Test source

```ts
  71  | 
  72  | test('console event should work in immediately closed popup', async ({ page, browserName, isBidi }) => {
  73  |   test.fixme(browserName === 'firefox' && !isBidi, 'console message is not reported at all');
  74  | 
  75  |   const [, message, popup] = await Promise.all([
  76  |     page.evaluate(async () => {
  77  |       const win = window.open()!;
  78  |       (win as any).console.log('hello');
  79  |       win.close();
  80  |     }),
  81  |     page.context().waitForEvent('console'),
  82  |     page.waitForEvent('popup'),
  83  |   ]);
  84  | 
  85  |   expect(message.text()).toBe('hello');
  86  |   expect(message.page()).toBe(popup);
  87  | });
  88  | 
  89  | test('dialog event should work @smoke', async ({ page }) => {
  90  |   const promise = page.evaluate(() => prompt('hey?'));
  91  |   const [dialog1, dialog2] = await Promise.all([
  92  |     page.context().waitForEvent('dialog'),
  93  |     page.waitForEvent('dialog'),
  94  |   ]);
  95  | 
  96  |   expect(dialog1).toBe(dialog2);
  97  |   expect(dialog1.message()).toBe('hey?');
  98  |   expect(dialog1.page()).toBe(page);
  99  |   await dialog1.accept('hello');
  100 |   expect(await promise).toBe('hello');
  101 | });
  102 | 
  103 | test('dialog event should work in popup', async ({ page }) => {
  104 |   const promise = page.evaluate(() => {
  105 |     const win = window.open('');
  106 |     return (win as any).prompt('hey?');
  107 |   });
  108 | 
  109 |   const [dialog, popup] = await Promise.all([
  110 |     page.context().waitForEvent('dialog'),
  111 |     page.waitForEvent('popup'),
  112 |   ]);
  113 | 
  114 |   expect(dialog.message()).toBe('hey?');
  115 |   expect(dialog.page()).toBe(popup);
  116 |   await dialog.accept('hello');
  117 |   expect(await promise).toBe('hello');
  118 | });
  119 | 
  120 | test('dialog event should work in popup 2', async ({ page, browserName }) => {
  121 |   test.fixme(browserName === 'firefox', 'dialog from javascript: url is not reported at all');
  122 | 
  123 |   const promise = page.evaluate(async () => {
  124 |     window.open('javascript:prompt("hey?")');
  125 |   });
  126 | 
  127 |   const dialog = await page.context().waitForEvent('dialog');
  128 | 
  129 |   expect(dialog.message()).toBe('hey?');
  130 |   expect(dialog.page()).toBe(null);
  131 |   await dialog.accept('hello');
  132 |   await promise;
  133 | });
  134 | 
  135 | test('dialog event should work in immediately closed popup', async ({ page }) => {
  136 |   const promise = page.evaluate(async () => {
  137 |     const win = window.open()!;
  138 |     const result = (win as any).prompt('hey?');
  139 |     win.close();
  140 |     return result;
  141 |   });
  142 | 
  143 |   const [dialog, popup] = await Promise.all([
  144 |     page.context().waitForEvent('dialog'),
  145 |     page.waitForEvent('popup'),
  146 |   ]);
  147 | 
  148 |   expect(dialog.message()).toBe('hey?');
  149 |   expect(dialog.page()).toBe(popup);
  150 |   await dialog.accept('hello');
  151 |   expect(await promise).toBe('hello');
  152 | });
  153 | 
  154 | test('dialog event should work with inline script tag', async ({ page, server }) => {
  155 |   server.setRoute('/popup.html', (req, res) => {
  156 |     res.setHeader('content-type', 'text/html');
  157 |     res.end(`<script>window.result = prompt('hey?')</script>`);
  158 |   });
  159 | 
  160 |   await page.goto(server.EMPTY_PAGE);
  161 |   await page.setContent(`<a href='popup.html' target=_blank>Click me</a>`);
  162 | 
  163 |   const promise = page.click('a');
  164 |   const [dialog, popup] = await Promise.all([
  165 |     page.context().waitForEvent('dialog'),
  166 |     page.context().waitForEvent('page'),
  167 |   ]);
  168 | 
  169 |   expect(dialog.message()).toBe('hey?');
  170 |   expect(dialog.page()).toBe(popup);
> 171 |   await dialog.accept('hello');
      |                ^ Error: dialog.accept: Protocol error (Page.handleJavaScriptDialog): No dialog is showing
  172 |   await promise;
  173 |   await expect.poll(() => popup.evaluate('window.result')).toBe('hello');
  174 | });
  175 | 
  176 | test('weberror event should work', async ({ page }) => {
  177 |   const [webError] = await Promise.all([
  178 |     page.context().waitForEvent('weberror'),
  179 |     page.setContent('<script>throw new Error("boom")</script>'),
  180 |   ]);
  181 |   expect(webError.page()).toBe(page);
  182 |   expect(webError.error().stack).toContain('boom');
  183 | });
  184 | 
  185 | test('weberror event should include location', async ({ page, server }) => {
  186 |   server.setRoute('/error.js', (req, res) => {
  187 |     res.setHeader('content-type', 'application/javascript');
  188 |     res.end(`
  189 |       function foo() {
  190 |         throw new Error('boom');
  191 |       }
  192 |       foo();
  193 |     `);
  194 |   });
  195 | 
  196 |   server.setRoute('/error.html', (req, res) => {
  197 |     res.setHeader('content-type', 'text/html');
  198 |     res.end('<script src="/error.js"></script>');
  199 |   });
  200 | 
  201 |   const [webError] = await Promise.all([
  202 |     page.context().waitForEvent('weberror'),
  203 |     page.goto(server.PREFIX + '/error.html'),
  204 |   ]);
  205 | 
  206 |   const location = webError.location();
  207 |   expect(location.url).toBe(`${server.PREFIX}/error.js`);
  208 |   expect(location.line).toBe(2);
  209 |   expect(location.column).toBeGreaterThan(0); // column is not consistent across browsers
  210 | });
  211 | 
  212 | test('pageload event should work @smoke', async ({ page, server }) => {
  213 |   const [eventPage] = await Promise.all([
  214 |     page.context().waitForEvent('pageload'),
  215 |     page.goto(server.EMPTY_PAGE),
  216 |   ]);
  217 |   expect(eventPage).toBe(page);
  218 | });
  219 | 
  220 | test('framenavigated event should work @smoke', async ({ page, server }) => {
  221 |   const [frame] = await Promise.all([
  222 |     page.context().waitForEvent('framenavigated'),
  223 |     page.goto(server.EMPTY_PAGE),
  224 |   ]);
  225 |   expect(frame).toBe(page.mainFrame());
  226 |   expect(frame.url()).toBe(server.EMPTY_PAGE);
  227 | });
  228 | 
  229 | test('pageclose event should work @smoke', async ({ context }) => {
  230 |   const page = await context.newPage();
  231 |   const [closed] = await Promise.all([
  232 |     context.waitForEvent('pageclose'),
  233 |     page.close(),
  234 |   ]);
  235 |   expect(closed).toBe(page);
  236 | });
  237 | 
  238 | test('frameattached event should work @smoke', async ({ page, server }) => {
  239 |   await page.goto(server.EMPTY_PAGE);
  240 |   const [frame] = await Promise.all([
  241 |     page.context().waitForEvent('frameattached'),
  242 |     page.evaluate(() => {
  243 |       const iframe = document.createElement('iframe');
  244 |       iframe.src = 'about:blank';
  245 |       document.body.appendChild(iframe);
  246 |     }),
  247 |   ]);
  248 |   expect(frame.parentFrame()).toBe(page.mainFrame());
  249 | });
  250 | 
  251 | test('framedetached event should work @smoke', async ({ page, server }) => {
  252 |   await page.goto(server.EMPTY_PAGE);
  253 |   await page.evaluate(() => {
  254 |     const iframe = document.createElement('iframe');
  255 |     iframe.id = 'x';
  256 |     iframe.src = 'about:blank';
  257 |     document.body.appendChild(iframe);
  258 |   });
  259 |   await page.waitForSelector('iframe');
  260 |   const [frame] = await Promise.all([
  261 |     page.context().waitForEvent('framedetached'),
  262 |     page.evaluate(() => document.getElementById('x')!.remove()),
  263 |   ]);
  264 |   expect(frame.parentFrame()).toBe(page.mainFrame());
  265 | });
  266 | 
  267 | test('download event should work @smoke', async ({ page, server }) => {
  268 |   server.setRoute('/download', (req, res) => {
  269 |     res.setHeader('Content-Type', 'application/octet-stream');
  270 |     res.setHeader('Content-Disposition', 'attachment; filename=file.txt');
  271 |     res.end('Hello world');
```