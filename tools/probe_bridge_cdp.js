// probe chromes clipboard bridge round-trip: headless chrome + CDP writes the
// exact kedit bridge payload, then the script (python ctypes side) dumps the
// OS clipboard state. Run: node probe_bridge_cdp.js ; then probe_dump.py
const { spawn } = require('child_process');
const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');

const chrome = 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const port = 9334;
const prof = path.join(os.tmpdir(), 'np420-bridge-cdp');
fs.rmSync(prof, { recursive: true, force: true });

const TEXT = '![输入图片说明](/imgs/2026-09-25/tdTHpo5zf2EfqjA7.png)\nmodels顺序和url顺序\n';
const PAYLOAD = JSON.stringify({ v: 1, text: TEXT, images: [{ uri: '/imgs/2026-09-25/tdTHpo5zf2EfqjA7.png', mime: 'image/png', dataBase64: 'iVBORw0KGgo=' }] });

function httpJson(pathStr) {
  return new Promise((resolve, reject) => {
    http.get({ host: '127.0.0.1', port, path: pathStr }, res => {
      let b = ''; res.on('data', c => b += c); res.on('end', () => resolve(JSON.parse(b)));
    }).on('error', reject);
  });
}

(async () => {
  // static file server for secure-context page
  const srv = http.createServer((req, res) => {
    res.writeHead(200, { 'content-type': 'text/html' });
    res.end('<!doctype html><html><body>probe</body></html>');
  });
  await new Promise(r => srv.listen(8975, '127.0.0.1', r));

  const cp = spawn(chrome, [
    '--remote-debugging-port=' + port,
    '--user-data-dir=' + prof, '--no-first-run', 'about:blank',
  ], { stdio: 'ignore' });
  await new Promise(r => setTimeout(r, 2500));
  const targets = await httpJson('/json/list');
  const page = targets.find(t => t.type === 'page');
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = new Map();
  ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); } };
  const send = (method, params) => new Promise(r => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });

  // grant clipboard permissions on the *browser* context via page's browser ws
  const ver = await httpJson('/json/version');
  const bws = new WebSocket(ver.webSocketDebuggerUrl);
  await new Promise(r => bws.onopen = r); let bid = 0; const bpend = new Map();
  bws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.id && bpend.has(m.id)) { bpend.get(m.id)(m); bpend.delete(m.id); } };
  const bsend = (m2, p2) => new Promise(r => { const i = ++bid; bpend.set(i, r); bws.send(JSON.stringify({ id: i, method: m2, params: p2 || {} })); });
  await bsend('Browser.grantPermissions', { permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'] });

  const expr = `(async () => {
    const T = ${JSON.stringify(TEXT)};
    const B = 'web application/x-notepad420-paste';
    const P = ${JSON.stringify(PAYLOAD)};
    let out;
    try {
      const cb = navigator.clipboard || (navigator.clipboard = undefined);
      if (!cb) return 'navigator.clipboard undefined (not secure ctx)';
      await cb.write([new ClipboardItem({
        'text/plain': new Blob([T], { type: 'text/plain' }),
        [B]: new Blob([P], { type: B }),
      })]);
      out = 'write-ok';
    } catch (e) { out = 'write-err: ' + e.message; }
    return out;
  })()`;
  // page must be secure context; use a localhost page
  await send('Page.navigate', { url: 'http://localhost:8975/probe.html' }).catch(() => {});
  await new Promise(r2 => setTimeout(r2, 1200));
  // focus the page so clipboard.write can proceed (CDP requirement)
  await send('Page.bringToFront', {});
  await send('Runtime.evaluate', { expression: 'window.focus()' });
  const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, userGesture: true });
  console.log('eval:', JSON.stringify(r.result));
  // dump twice: immediate + after delay (deal with deferred rendering)
  const { execSync } = require('child_process');
  console.log('==== DUMP1 (immediate) ====');
  console.log(execSync('python -X utf8 "' + path.join(__dirname, 'probe_dump.py') + '"', { encoding: 'utf-8' }));
  await new Promise(r2 => setTimeout(r2, 3000));
  console.log('==== DUMP2 (3s later) ====');
  console.log(execSync('python -X utf8 "' + path.join(__dirname, 'probe_dump.py') + '"', { encoding: 'utf-8' }));
  srv.close();
  cp.kill();
})();
