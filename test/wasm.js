#!/usr/bin/env node
/*
 * Checks the WebAssembly core against the native one.
 *
 * The wasm build is only useful if it produces the same picture as the code the
 * conformance suite tests, so this runs a cartridge in both and compares the
 * framebuffers byte for byte.
 */

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const W = 160, H = 144;
const FRAME_BYTES = W * H * 3;

let failures = 0;
function check(name, ok, detail) {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail === undefined ? '' : ` — ${detail}`}`);
  if (!ok) failures++;
}

async function main() {
  const romPath = path.join(ROOT, 'roms', 'dmg-acid2.gb');
  if (!fs.existsSync(romPath)) {
    console.log('skip: roms/dmg-acid2.gb not fetched');
    process.exit(0);
  }

  const createGB = require(path.join(ROOT, 'web', 'gb.js'));
  const module = await createGB();
  const rom = fs.readFileSync(romPath);

  const ptr = module._malloc(rom.length);
  module.HEAPU8.set(rom, ptr);
  const loaded = module._gb_wasm_load(ptr, rom.length);
  module._free(ptr);

  check('wasm core accepts the cartridge', loaded === 1);

  const w = module._gb_wasm_width();
  const h = module._gb_wasm_height();
  check('reports the Game Boy screen size', w === W && h === H, `${w}x${h}`);

  for (let i = 0; i < 120; i++) {
    module._gb_wasm_frame();
  }

  const fbPtr = module._gb_wasm_framebuffer();
  const wasmFb = Buffer.from(module.HEAPU8.slice(fbPtr, fbPtr + FRAME_BYTES));

  check('framebuffer is the right size', wasmFb.length === FRAME_BYTES, `${wasmFb.length} bytes`);

  // Four shades is what dmg-acid2 draws; anything else means the PPU is wrong.
  const shades = new Set();
  for (let i = 0; i < wasmFb.length; i += 3) {
    shades.add((wasmFb[i] << 16) | (wasmFb[i + 1] << 8) | wasmFb[i + 2]);
  }
  check('renders the expected four shades', shades.size === 4, `${shades.size} colours`);

  check('no unrecognised opcodes', module._gb_wasm_invalid_opcodes() === 0,
        String(module._gb_wasm_invalid_opcodes()));

  // Now the same cartridge through the native build.
  const nativeOut = path.join(require('os').tmpdir(), 'gb-native.rgb');
  execFileSync(path.join(ROOT, 'test', 'shot'),
               [romPath, nativeOut, '120'], { stdio: 'ignore' });
  const nativeFb = fs.readFileSync(nativeOut);

  check('wasm and native render identically', wasmFb.equals(nativeFb),
        wasmFb.equals(nativeFb) ? 'byte-identical' : 'differ');

  console.log(failures === 0 ? '\nPASS' : `\nFAIL (${failures})`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((e) => { console.error(e); process.exit(1); });
