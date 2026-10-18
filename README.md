# gb-emu

A Game Boy emulator that runs in a browser and on an NXP FRDM-MCXA156 with a
128×64 OLED.

**Live:** https://nemo-010.github.io/gb-emu/

There is no copyrighted ROM anywhere in this repository. The demo cartridge is
assembled from source here, and the two external test suites are downloaded on
demand rather than redistributed — one of them carries no licence that would
allow it.

---

## What it is

| Part | What it does |
| --- | --- |
| `src/gb_cpu.c` | SM83 CPU: the whole instruction set, exact flags, the HALT bug |
| `src/gb_ppu.c` | PPU: background, window, sprites with priority, four shades |
| `src/gb_mbc.c` | Cartridges: header parsing, MBC1/2/3/5, RTC registers |
| `src/gb.c` | Memory map, timers, interrupts, joypad, frame pacing |
| `src/gb_wasm.c` | The WebAssembly boundary: load, run, read the picture |
| `web/` | The browser player |
| `zephyr/` | The MCXA156 build: menu, buttons, SD card loader |
| `roms/demo/` | A demo cartridge written in RGBDS assembly, in this repo |

## How far it gets, measured

The CPU passes **all eleven of Blargg's instruction test suites**:

```
01-special  02-interrupts  03-op sp,hl  04-op r,imm  05-op rp
06-ld r,r   07-jr,jp,call,ret,rst  08-misc instrs
09-op r,r   10-bit ops  11-op a,(hl)
```

Those report through the console at real hardware timing, so passing them is a
check against the documented behaviour of the chip rather than against my idea of
it. `dmg-acid2`, the picture-accuracy test, renders correctly — the face, both
eyes with their highlight rings, the nose, the mouth and the "HELLO WORLD"
heading all come out right.

## What does not work

- **Instruction timing is not cycle-exact.** `instr_timing` and `mem_timing`
  fail. The cycle counts are right for the common cases but memory-access timing
  is approximate, so a game that counts cycles for an effect will drift. This is
  the honest limit of this implementation, and it is why `dmg-acid2` passing
  means the picture is right, not that the machine is.
- **No sound.** The four channels and their envelope/sweep units are not
  implemented.
- **No link cable.** The serial port captures bytes but does not talk to
  anything.
- **No CGB mode.** Game Boy Color cartridges run in DMG compatibility mode, so
  CGB-only games will not work.
- **Save files** are read and written in memory but not persisted to the SD card.

## The browser player

`web/index.html` loads a `.gb` file, runs it at the Game Boy's own 59.7275 Hz,
and shows the picture on a canvas. The keyboard maps to the d-pad and buttons,
and there are on-screen buttons for touch.

The core is 24 KB of wasm. It is checked against the native build by
`test/wasm.js`, which runs the same cartridge through both and requires the
framebuffers to be **byte-identical**.

## The MCXA156 player

`zephyr/` builds for `frdm_mcxa156` with an SSD1306 on LPI2C2 and eight buttons
on the Arduino header. It boots into a cartridge menu, lists the `.gb` files on
an SD card, and runs the one you pick.

```
FLASH: 78080 B / 1 MB      RAM: 94540 B / 120 KB
```

Two things about the panel are worth knowing:

- **A 128×64 panel cannot show a 160×144 screen.** Rather than squeeze it, the
  player shows a 128×64 window centred horizontally, which keeps pixels square.
  Full-screen scrolling is the obvious next step and is not done.
- **The board has no card slot**, so the overlay declares one on LPSPI0. This is
  not cosmetic: without a disk node in the devicetree, `FF_VOLUMES` resolves to
  zero and the FAT filesystem refuses to compile. The error says "Wrong
  FF_VOLUMES setting" and points nowhere useful.

## Building

### Host (tests)

```sh
cc -std=c11 -O2 -Isrc -o test/runner test/runner.c src/gb_cpu.c src/gb_mbc.c src/gb_ppu.c src/gb.c
./test/fetch-test-roms.sh
./test/smoke.sh
```

### WebAssembly

```sh
emcc -std=c11 -O3 -Isrc \
  src/gb_wasm.c src/gb.c src/gb_cpu.c src/gb_mbc.c src/gb_ppu.c \
  -o web/gb.js -sMODULARIZE=1 -sEXPORT_NAME=createGB -sINVOKE_RUN=0 \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=32MB \
  -sEXPORTED_FUNCTIONS=_malloc,_free,_gb_wasm_load,_gb_wasm_unload,_gb_wasm_frame,_gb_wasm_framebuffer,_gb_wasm_width,_gb_wasm_height,_gb_wasm_buttons,_gb_wasm_title,_gb_wasm_mapper,_gb_wasm_rom_banks,_gb_wasm_invalid_opcodes,_gb_wasm_save_state,_gb_wasm_load_state,_gb_wasm_state_size \
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,ccall,cwrap --no-entry
```

If emscripten's bundled binaryen is older than the compiler expects, every
optimised link fails with `Unknown option` — the message blames the optimiser
rather than the version mismatch. `binaryen-shim/bin/wasm-opt` drops the flags
binaryen 117 does not know; point `EM_BINARYEN_ROOT` at it.

### The Zephyr build

```sh
west build -b frdm_mcxa156 zephyr
```

### The demo cartridge

```sh
cd roms/demo
rgbasm -i include -o demo.o src/main.asm
rgblink -o ../../roms/demo.gb demo.o
rgbfix -v -p 0xFF -m 0x00 -r 0x00 -C ../../roms/demo.gb
```

## Testing

```sh
./test/smoke.sh      # the CPU and picture suites
node test/wasm.js    # wasm output must equal native output, byte for byte
node test/demo.js    # the self-authored cartridge boots and takes input
node test/page.js    # the page itself, in a real browser
```

`test/page.js` needs `npm install playwright` and drives the page the way a
person does: it loads a cartridge through the file input, reads the stats, checks
the canvas has been painted, and presses a key. It exists because the page once
threw on the first stat it updated and nothing noticed — the emulator was fine,
the page was broken, and only a browser could tell the difference.

## A note on ROMs

The demo cartridge is written here and is free to use. Blargg's test suite is
fetched rather than committed because it has no licence; `dmg-acid2` is MIT and
could be vendored but is fetched for consistency.

No commercial ROM belongs in this repository, and none is used. If you want to
run a game you own, load it from the page — the file never leaves your browser.
