/*
 * Picture processing unit.
 *
 * The PPU draws 160x144 pixels by walking the background, window and sprite
 * layers in step with the CPU. It reports the current scanline through the
 * LY register and raises the STAT and VBlank interrupts, which is how games
 * pace themselves.
 *
 * Output is RGB888. The palette registers hold 2-bit colour numbers, so the
 * four shades are looked up here rather than being fixed, which is what makes
 * games that fade to white or invert the screen work.
 */

#ifndef GB_PPU_H
#define GB_PPU_H

#include <stdint.h>
#include <stdbool.h>

struct GB;

typedef struct GBPPU {
	uint8_t vram[0x2000];
	uint8_t oam[0xA0];

	/* Registers. */
	uint8_t lcdc;   /* 0xFF40 */
	uint8_t stat;   /* 0xFF41 */
	uint8_t scy;    /* 0xFF42 */
	uint8_t scx;    /* 0xFF43 */
	uint8_t ly;     /* 0xFF44 */
	uint8_t lyc;    /* 0xFF45 */
	uint8_t bgp;    /* 0xFF47 */
	uint8_t obp0;   /* 0xFF48 */
	uint8_t obp1;   /* 0xFF49 */
	uint8_t wy;     /* 0xFF4A */
	uint8_t wx;     /* 0xFF4B */

	/* Per-scanline background colour indices, used by the sprite priority
	 * rules and by the "background over sprites" behaviour. */
	uint8_t bg_color_num[160];

	/* RGB888 output. */
	uint8_t fb[160 * 144 * 3];

	/* Cycle accumulator within the current scanline. */
	int dot;
	int mode;

	/* Set when the frame has been drawn and is ready to present. */
	bool frame_ready;

	/* Set when the LCD is off, so the caller knows to show white. */
	bool lcd_off;
} GBPPU;

void ppu_reset(GBPPU *ppu);

/* Steps the PPU by the given number of cycles, calling back into the machine for
 * interrupts. */
void ppu_step(GBPPU *ppu, struct GB *gb, int cycles);

uint8_t ppu_read_reg(GBPPU *ppu, uint16_t addr);
void ppu_write_reg(GBPPU *ppu, uint16_t addr, uint8_t val);

/* The four shades, as the original DMG's green-ish LCD. */
void ppu_set_palette_shade(GBPPU *ppu, int shade, uint8_t r, uint8_t g, uint8_t b);

#endif /* GB_PPU_H */
