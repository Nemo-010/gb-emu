#!/usr/bin/env node
/*
 * Checks the self-authored demo cartridge through the wasm core.
 *
 * This is the one cartridge in the repository that nobody else has rights to, so
 * it is also the one that must work: it is what a visitor sees when they open
 * the page. The checks are behavioural — a title, a picture, and input that
 * changes the picture — rather than assertions about internal state.
 */

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const W = 160, H = 144;

let failures = 0;
function check(name, ok, detail) {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail === undefined ? '' : ` — ${detail}`}`);
  if (!ok) failures++;
}

async function main() {
  const createGB = require(path.join(ROOT, 'web', 'gb.js'));
  const module = await createGB();
  const rom = fs.readFileSync(path.join(ROOT, 'roms', 'demo.gb'));

  const ptr = module._malloc(rom.length);
  module.HEAPU8.set(rom, ptr);
  const loaded = module._gb_wasm_load(ptr, rom.length);
  module._free(ptr);
  check('demo cartridge loads', loaded === 1);

  const titlePtr = module._gb_wasm_title();
  let title = '';
  for (let i = 0; i < 16; i++) {
    const c = module.HEAPU8[titlePtr + i];
    if (!c) break;
    title += String.fromCharCode(c);
  }
  check('header carries its title', title.trim() === 'GBDEMO', JSON.stringify(title.trim()));

  for (let i = 0; i < 40; i++) module._gb_wasm_frame();

  const grab = () => {
    const fb = module._gb_wasm_framebuffer();
    return Buffer.from(module.HEAPU8.slice(fb, fb + W * H * 3));
  };

  const before = grab();
  const lit = before.reduce((n, b, i) => n + (i % 3 === 0 && b < 100 ? 1 : 0), 0);
  check('it draws something', lit > 100, `${lit} dark pixels`);
  check('no unrecognised opcodes', module._gb_wasm_invalid_opcodes() === 0);

  // Hold right for forty frames; the sprite should move, changing the picture.
  module._gb_wasm_buttons(0x01);
  for (let i = 0; i < 40; i++) module._gb_wasm_frame();
  module._gb_wasm_buttons(0);

  const after = grab();
  let diff = 0;
  for (let i = 0; i < before.length; i++) {
    if (before[i] !== after[i]) diff++;
  }
  check('the d-pad moves the sprite', diff > 0, `${diff} framebuffer bytes changed`);

  console.log(failures === 0 ? '\nPASS' : `\nFAIL (${failures})`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((e) => { console.error(e); process.exit(1); });
