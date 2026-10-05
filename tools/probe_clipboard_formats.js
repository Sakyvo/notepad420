// K:\Projects\dev\notepad420\tools\probe_clipboard_formats.js
// Launch a headful(headless=false) Chromium, write a web custom format via
// Async Clipboard API, then dump ALL registered clipboard format names so we
// can see the OS-side name mapping. Run: node .\tools\probe_clipboard_formats.js
// Serves the page from in-memory blob URL so no dev server is needed.

const { execSync } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');

const chrome = process.env.CHROME_PATH
  || 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const outDir = path.join(os.tmpdir(), 'np420-clip-probe');
fs.mkdirSync(outDir, { recursive: true });
const outFile = path.join(outDir, 'formats.txt');

const html = `<!doctype html><html><body><pre id="log"></pre><script>
(async () => {
  const log = (s) => document.getElementById('log').textContent += s + "\\n";
  const bridge = 'web application/x-notepad420-paste';
  log('supports=' + (window.ClipboardItem && ClipboardItem.supports ? ClipboardItem.supports(bridge) : 'n/a'));
  try {
    await navigator.clipboard.write([new ClipboardItem({
      'text/plain': new Blob(['hello from chrome'], { type: 'text/plain' }),
      [bridge]: new Blob(['{"v":1,"text":"hello from chrome","images":[]}'], { type: bridge }),
    })]);
    log('write=ok');
  } catch (e) {
    log('write=ERR ' + e.message);
  }
})();
<\/script></body></html>`;

const htmlPath = path.join(outDir, 'p.html');
fs.writeFileSync(htmlPath, html);

// PowerShell one-liner: after the browser writes, dump every registered
// clipboard format name into the file.
const dump = path.join(outDir, 'dump.ps1');
fs.writeFileSync(dump, `
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class Clip {
  [DllImport("user32.dll")] public static extern bool OpenClipboard(IntPtr h);
  [DllImport("user32.dll")] public static extern bool CloseClipboard();
  [DllImport("user32.dll")] public static extern uint EnumClipboardFormats(uint fmt);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClipboardFormatNameW(uint fmt, StringBuilder buf, int cap);
  public static IEnumerable<string> All() {
    if (!OpenClipboard(IntPtr.Zero)) yield break;
    uint f = 0;
    while ((f = EnumClipboardFormats(f)) != 0) {
      var sb = new StringBuilder(255);
      int n = GetClipboardFormatNameW(f, sb, sb.Capacity);
      if (n > 0) yield return f.ToString("x4") + " " + sb.ToString();
    }
    CloseClipboard();
  }
}
'@
[Clip]::All() | Out-File -Encoding utf8 -FilePath '${outFile.replace(/\\/g, '\\\\')}'
`);

(async () => {
  // Spawn chrome headful to allow clipboard write (headless doesn't have perms).
  const cp = require('child_process').spawn(chrome, [
    '--user-data-dir=' + path.join(outDir, 'profile'),
    '--allow-insecure-localhost',
    '--no-first-run',
    '--remote-debugging-port=9333',
    'file:///' + htmlPath.replace(/\\/g, '/'),
  ], { detached: true, stdio: 'ignore' });
  // Give it time to start and write.
  await new Promise(r => setTimeout(r, 4000));
  execSync(`powershell -NoProfile -ExecutionPolicy Bypass -File "${dump}"`);
  cp.unref();
  // Kill chrome instance we spawned (by unique user-data-dir marker).
  try {
    execSync(`
      Get-CimInstance Win32_Process -Filter "Name='chrome.exe'" |
        Where-Object CommandLine -Match 'np420-clip-probe' |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
    `, { shell: 'cmd.exe' });
  } catch (_) {}
  const out = fs.readFileSync(outFile, 'utf8');
  console.log(out);
})();
