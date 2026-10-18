/*
 * Game Boy machine: memory map, cartridge, timers, interrupts.
 *
 * The display (PPU) and sound live in their own files. This one owns the
 * address space and everything that answers on it.
 */

#ifndef GB_H
#define GB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "gb_cpu.h"
#include "gb_mbc.h"
#include "gb_ppu.h"

#define GB_SCREEN_W 160
#define GB_SCREEN_H 144
#define GB_FRAMEBUFFER_BYTES (GB_SCREEN_W * GB_SCREEN_H * 3)

/* 4 MiB of addressable cartridge space is the practical maximum for the MBCs
 * implemented here; ROM-only and MBC1/2/3/5 fit well inside it. */
#define GB_MAX_ROM (4 * 1024 * 1024)

typedef struct GB {
	SM83 cpu;
	GBMBC mbc;
	GBPPU ppu;

	uint8_t wram[0x2000];
	uint8_t hram[0x7F];

	/* I/O registers, indexed 0x00-0x7F for 0xFF00-0xFF7F. */
	uint8_t io[0x80];

	uint8_t ie;             /* 0xFFFF */
	uint8_t iflag;          /* 0xFF0F */

	/* Timer state. */
	uint16_t div_counter;   /* internal 16-bit divider, DIV is the high byte */
	uint16_t tima_counter;

	/* Which CPU interrupt is being serviced, for the HALT bug. */
	uint8_t pending_irq;

	/* Boot ROM, if one is supplied. */
	const uint8_t *bootrom;
	size_t bootrom_size;
	bool bootrom_enabled;

	/* Button state, bit set = pressed. Layout matches the JOYP register. */
	uint8_t buttons;

	/* Diagnostics. */
	uint64_t invalid_opcodes;

	/* Set when the cartridge asks to be saved (not implemented beyond this). */
	bool dirty;

	/* Called for every byte written to the link port (0xFF01). Test ROMs and
	 * some games report through it. */
	void (*serial_out)(void *ctx, uint8_t byte);
	void *serial_ctx;
} GB;

/* Allocates and resets a machine. Returns NULL if the ROM is too large. */
GB *gb_new(const uint8_t *rom, size_t rom_size);

/* Optional boot ROM (256 bytes). Without one the machine starts at 0x0100 with
 * the register values a real console has after its own boot ROM has run. */
void gb_set_bootrom(GB *gb, const uint8_t *bootrom, size_t size);

void gb_free(GB *gb);

/* Runs one frame (about 70224 cycles) and returns the cycles actually run. */
int gb_run_frame(GB *gb);

uint8_t gb_read(GB *gb, uint16_t addr);
void gb_write(GB *gb, uint16_t addr, uint8_t val);

/* Interrupt lines that are asserted. */
uint8_t gb_pending_interrupts(GB *gb);

/* The framebuffer is RGB888, 160x144. */
const uint8_t *gb_framebuffer(GB *gb);

/*
 * Buttons, as two nibbles:
 *   bits 0-3: Right, Left, Up, Down
 *   bits 4-7: A, B, Select, Start
 * A set bit means pressed.
 */
enum {
	GB_BTN_RIGHT  = 0x01,
	GB_BTN_LEFT   = 0x02,
	GB_BTN_UP     = 0x04,
	GB_BTN_DOWN   = 0x08,
	GB_BTN_A      = 0x10,
	GB_BTN_B      = 0x20,
	GB_BTN_SELECT = 0x40,
	GB_BTN_START  = 0x80,
};

void gb_set_buttons(GB *gb, uint8_t buttons);

/* Cartridge RAM, for save files. */
uint8_t *gb_sram(GB *gb, size_t *size);

/* Reads the joypad register from the current button state. */
uint8_t joypad_read(GB *gb);

#endif /* GB_H */
