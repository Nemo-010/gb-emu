#!/usr/bin/env node
/*
 * Loads a cartridge in a real browser and checks the page actually runs it.
 *
 * The previous version of the page threw on the first stat it tried to update,
 * and nothing caught it because the tests only exercised the emulator core. This
 * drives the page the way a person does: open it, load a ROM, watch the screen
 * change, press a button.
 *
 * Requires playwright. Skips with a clear message when it is not installed, so a
 * checkout without it still passes the rest of the suite.
 */

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const WEB = path.join(ROOT, 'web');

let failures = 0;
function check(name, ok, detail) {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail === undefined ? '' : ` — ${detail}`}`);
  if (!ok) failures++;
}

let chromium;
try {
  ({ chromium } = require('playwright'));
} catch {
  console.log('skip: playwright is not installed (npm i -D playwright)');
  process.exit(0);
}

/*
 * Serves web/ through request interception rather than a real socket. Some
 * sandboxes cannot bind a port, and this needs no network either way.
 */
async function serve(page) {
  const types = {
    '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
    '.gb': 'application/octet-stream', '.bin': 'application/octet-stream',
  };

  await page.route('**/*', (route) => {
    const url = new URL(route.request().url());
    const rel = decodeURIComponent(url.pathname).replace(/^\/+/, '') || 'index.html';
    const file = path.join(WEB, rel);

    if (!file.startsWith(WEB) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
      route.fulfill({ status: 404, body: 'not found' });
      return;
    }

    route.fulfill({
      status: 200,
      body: fs.readFileSync(file),
      headers: { 'Content-Type': types[path.extname(file)] || 'application/octet-stream' },
    });
  });
}

async function main() {
  const browser = await chromium.launch({ args: ['--no-sandbox'] });
  const page = await browser.newPage();
  await serve(page);

  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });

  await page.goto('http://localhost/index.html', { waitUntil: 'load' });
  await page.waitForFunction(() => window.Module !== undefined || typeof createGB === 'function',
                             { timeout: 20000 });

  check('page loads without errors', errors.length === 0, errors[0]);

  // Load the demo cartridge through the file input, exactly as a user would.
  const rom = path.join(ROOT, 'roms', 'demo.gb');
  await page.setInputFiles('#file', rom);
  await page.waitForTimeout(2500);

  check('no errors after loading a cartridge', errors.length === 0, errors[0]);

  const title = await page.textContent('#s-title span:last-child');
  check('title is shown', title.trim() === 'GBDEMO', JSON.stringify(title.trim()));

  const mapper = await page.textContent('#s-mapper span:last-child');
  check('mapper is shown', mapper.trim() === 'ROM only', JSON.stringify(mapper.trim()));

  const running = await page.textContent('#s-run span:last-child');
  check('reports running', running.trim() === 'yes', JSON.stringify(running.trim()));

  /*
   * The canvas must have been drawn on. The demo cartridge is a still picture
   * until a button is pressed, so comparing two frames a moment apart proves
   * nothing; counting distinct colours does, because a canvas that was never
   * painted is a single flat colour.
   */
  const colours = () => page.evaluate(() => {
    const c = document.getElementById('screen');
    const ctx = c.getContext('2d');
    const d = ctx.getImageData(0, 0, c.width, c.height).data;
    const seen = new Set();
    for (let i = 0; i < d.length; i += 4) {
      seen.add((d[i] << 16) | (d[i + 1] << 8) | d[i + 2]);
    }
    return seen.size;
  });

  const before = await colours();
  check('the screen has been drawn on', before >= 3, `${before} distinct colours`);

  const shot = () => page.evaluate(() =>
    document.getElementById('screen').toDataURL().slice(-4096));

  // Pressing a button must reach the emulator and move the sprite.
  const a = await shot();
  await page.keyboard.down('ArrowRight');
  await page.waitForTimeout(600);
  await page.keyboard.up('ArrowRight');
  await page.waitForTimeout(200);
  const b = await shot();
  check('input changes the picture', a !== b, a === b ? 'nothing moved' : 'sprite moved');

  check('no errors at the end', errors.length === 0, errors[0]);

  await browser.close();

  console.log(failures === 0 ? '\nPASS' : `\nFAIL (${failures})`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((e) => { console.error(e); process.exit(1); });
