/*
 * WebAssembly bindings for the Game Boy core.
 *
 * The page needs to do three things: load a ROM, run frames, and read the
 * picture. Everything else stays inside the core. Keeping the boundary this
 * small means the browser side has no opinion about how the emulator works.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

#include "gb.h"

/* One machine at a time is all a page needs. */
static GB *machine;

/* The ROM is copied here because the core keeps a pointer to it. */
static uint8_t *rom_copy;
static size_t rom_copy_size;

EXPORT int gb_wasm_load(const uint8_t *rom, size_t size)
{
	if (machine) {
		gb_free(machine);
		machine = NULL;
	}
	if (rom_copy) {
		free(rom_copy);
		rom_copy = NULL;
		rom_copy_size = 0;
	}

	if (!rom || size < 0x150 || size > GB_MAX_ROM) {
		return 0;
	}

	rom_copy = malloc(size);
	if (!rom_copy) {
		return 0;
	}
	memcpy(rom_copy, rom, size);
	rom_copy_size = size;

	machine = gb_new(rom_copy, size);
	if (!machine) {
		free(rom_copy);
		rom_copy = NULL;
		rom_copy_size = 0;
		return 0;
	}

	return 1;
}

EXPORT void gb_wasm_unload(void)
{
	if (machine) {
		gb_free(machine);
		machine = NULL;
	}
	if (rom_copy) {
		free(rom_copy);
		rom_copy = NULL;
		rom_copy_size = 0;
	}
}

/* Runs one frame. Returns the cycles executed, or 0 if nothing is loaded. */
EXPORT int gb_wasm_frame(void)
{
	if (!machine) {
		return 0;
	}
	return gb_run_frame(machine);
}

/* RGB888, 160x144, owned by the core. */
EXPORT const uint8_t *gb_wasm_framebuffer(void)
{
	return machine ? gb_framebuffer(machine) : NULL;
}

EXPORT int gb_wasm_width(void)  { return GB_SCREEN_W; }
EXPORT int gb_wasm_height(void) { return GB_SCREEN_H; }

EXPORT void gb_wasm_buttons(int buttons)
{
	if (machine) {
		gb_set_buttons(machine, (uint8_t)buttons);
	}
}

/* Header details for the page: title, mapper, ROM/RAM size. */
static char title_buf[17];

EXPORT const char *gb_wasm_title(void)
{
	GBCartInfo info;

	if (!machine) {
		return "";
	}
	mbc_read_info(rom_copy, rom_copy_size, &info);

	/* Trim trailing spaces, which cartridges pad their titles with. */
	memcpy(title_buf, info.title, sizeof(title_buf));
	for (int i = 15; i >= 0 && title_buf[i] == ' '; i--) {
		title_buf[i] = '\0';
	}
	return title_buf;
}

EXPORT int gb_wasm_mapper(void)
{
	GBCartInfo info;

	if (!machine) {
		return -1;
	}
	mbc_read_info(rom_copy, rom_copy_size, &info);
	return (int)info.mbc;
}

EXPORT int gb_wasm_rom_banks(void)
{
	GBCartInfo info;

	if (!machine) {
		return 0;
	}
	mbc_read_info(rom_copy, rom_copy_size, &info);
	return (int)info.rom_banks;
}

/* Number of opcodes the core did not recognise. Anything above zero means the
 * cartridge is doing something this core does not implement, which the page
 * shows rather than hiding. */
EXPORT unsigned gb_wasm_invalid_opcodes(void)
{
	return machine ? (unsigned)machine->invalid_opcodes : 0;
}

/* Saves and restores the whole machine, so the page can offer save states. */
EXPORT size_t gb_wasm_state_size(void)
{
	return sizeof(GB);
}

EXPORT int gb_wasm_save_state(uint8_t *dst, size_t len)
{
	if (!machine || len < sizeof(GB)) {
		return 0;
	}
	memcpy(dst, machine, sizeof(GB));

	/*
	 * The snapshot holds pointers to the ROM and to the bus, which are not
	 * valid in another process. Restoring re-links them, so they can be
	 * zeroed here to make that explicit.
	 */
	GB *copy = (GB *)dst;

	copy->cpu.gb = NULL;
	copy->mbc.rom = NULL;
	copy->mbc.ram = NULL;

	return 1;
}

EXPORT int gb_wasm_load_state(const uint8_t *src, size_t len)
{
	if (!machine || len < sizeof(GB)) {
		return 0;
	}

	uint8_t *sram = machine->mbc.ram;
	uint8_t *mbc2 = machine->mbc.mbc2_ram;

	memcpy(machine, src, sizeof(GB));

	/* Re-link the pointers the snapshot could not carry. */
	machine->cpu.gb = machine;
	machine->mbc.rom = rom_copy;
	machine->mbc.ram = (sram == NULL) ? NULL : sram;
	if (mbc2) {
		/* MBC2's RAM lives inside the struct, so it moved with the copy. */
		machine->mbc.ram = machine->mbc.mbc2_ram;
	}

	return 1;
}
