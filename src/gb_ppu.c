/*
 * Picture processing unit.
 *
 * The LCD is scanned one row at a time. Each row takes 456 dots (cycles):
 * 80 dots of mode 2 (searching OAM for sprites), 172 of mode 3 (drawing) and
 * 204 of mode 0 (horizontal blank). After 144 rows come 10 rows of mode 1
 * (vertical blank). Getting those numbers right is what makes games that use
 * STAT interrupts for effects like parallax and split screens behave.
 */

#include <string.h>

#include "gb_ppu.h"
#include "gb.h"

/* LCDC bits. */
#define LCDC_ENABLE      0x80
#define LCDC_WIN_MAP     0x40
#define LCDC_WIN_ENABLE  0x20
#define LCDC_TILE_DATA   0x10
#define LCDC_BG_MAP      0x08
#define LCDC_OBJ_SIZE    0x04
#define LCDC_OBJ_ENABLE  0x02
#define LCDC_BG_ENABLE   0x01

/* STAT interrupt sources. */
#define STAT_LYC  0x40
#define STAT_MODE2 0x20
#define STAT_MODE1 0x10
#define STAT_MODE0 0x08

/* The DMG's palette: four shades of olive green. */
static uint8_t shade_rgb[4][3] = {
	{ 0x9b, 0xbc, 0x0f },
	{ 0x8b, 0xac, 0x0f },
	{ 0x30, 0x62, 0x30 },
	{ 0x0f, 0x38, 0x0f },
};

void ppu_set_palette_shade(GBPPU *ppu, int shade, uint8_t r, uint8_t g, uint8_t b)
{
	(void)ppu;
	if (shade < 0 || shade > 3) {
		return;
	}
	shade_rgb[shade][0] = r;
	shade_rgb[shade][1] = g;
	shade_rgb[shade][2] = b;
}

void ppu_reset(GBPPU *ppu)
{
	memset(ppu, 0, sizeof(*ppu));
	ppu->lcdc = 0x91;
	ppu->stat = 0x85;
	ppu->bgp = 0xfc;
	ppu->obp0 = 0xff;
	ppu->obp1 = 0xff;
	ppu->mode = 1;
	ppu->ly = 0;
}

/* --- interrupt plumbing ----------------------------------------------- */

static void request_stat(GB *gb)
{
	gb->iflag |= 0x02; /* bit 1 = LCD STAT */
}

static void update_stat_line(GBPPU *ppu, GB *gb)
{
	bool coincidence = (ppu->ly == ppu->lyc);

	/* Bit 2 of STAT is the LYC=LY flag, bit 7 is unused and reads as 1. */
	ppu->stat = (ppu->stat & 0x78) | 0x80 | (coincidence ? 0x04 : 0x00);

	if (coincidence && (ppu->stat & STAT_LYC)) {
		request_stat(gb);
	}
}

/* --- palette ---------------------------------------------------------- */

static inline uint8_t apply_palette(uint8_t palette, uint8_t color_num)
{
	return (palette >> (color_num * 2)) & 0x03;
}

/* --- scanline rendering ----------------------------------------------- */

static void render_scanline(GBPPU *ppu, int y)
{
	int x;
	uint8_t line[160];
	bool bg_enabled = (ppu->lcdc & LCDC_BG_ENABLE) != 0;

	/* Background and window. */
	for (x = 0; x < 160; x++) {
		line[x] = 0;
	}

	if (bg_enabled) {
		uint16_t map_base = (ppu->lcdc & LCDC_BG_MAP) ? 0x1c00 : 0x1800;
		uint8_t py = ppu->scy + y;
		uint8_t px = ppu->scx;

		for (x = 0; x < 160; x++) {
			uint8_t tile_x = (px + x) >> 3;
			uint8_t tile_y = py >> 3;
			uint16_t map_addr = map_base + tile_y * 32 + tile_x;
			uint8_t tile_num = ppu->vram[map_addr & 0x1fff];
			uint16_t tile_addr;
			uint8_t bit;
			uint8_t lo, hi, color_num;

			if (ppu->lcdc & LCDC_TILE_DATA) {
				tile_addr = tile_num * 16;
			} else {
				/* Signed tile numbers index from 0x9000. */
				tile_addr = 0x1000 + ((int8_t)tile_num) * 16;
			}

			bit = 7 - ((px + x) & 7);
			lo = ppu->vram[(tile_addr + (py & 7) * 2) & 0x1fff];
			hi = ppu->vram[(tile_addr + (py & 7) * 2 + 1) & 0x1fff];

			color_num = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
			line[x] = color_num;
		}
	}

	/* The window is drawn on top of the background where it is enabled. */
	if ((ppu->lcdc & LCDC_WIN_ENABLE) && (ppu->lcdc & LCDC_BG_ENABLE) &&
	    y >= ppu->wy && ppu->wx <= 166) {
		uint16_t map_base = (ppu->lcdc & LCDC_WIN_MAP) ? 0x1c00 : 0x1800;
		int win_y = y - ppu->wy;

		for (x = ppu->wx - 7; x < 160; x++) {
			int wx = x - (ppu->wx - 7);
			uint8_t tile_x, tile_y;
			uint16_t map_addr, tile_addr;
			uint8_t bit, lo, hi, color_num;

			if (x < 0) {
				continue;
			}

			tile_x = wx >> 3;
			tile_y = win_y >> 3;
			map_addr = map_base + tile_y * 32 + tile_x;

			uint8_t tile_num = ppu->vram[map_addr & 0x1fff];

			if (ppu->lcdc & LCDC_TILE_DATA) {
				tile_addr = tile_num * 16;
			} else {
				tile_addr = 0x1000 + ((int8_t)tile_num) * 16;
			}

			bit = 7 - (wx & 7);
			lo = ppu->vram[(tile_addr + (win_y & 7) * 2) & 0x1fff];
			hi = ppu->vram[(tile_addr + (win_y & 7) * 2 + 1) & 0x1fff];

			color_num = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
			line[x] = color_num;
		}
	}

	/* Remember the background indices: sprites with priority behind
	 * background are hidden where the background is not colour 0. */
	memcpy(ppu->bg_color_num, line, sizeof(line));

	/* Write the background into the framebuffer. */
	for (x = 0; x < 160; x++) {
		uint8_t shade = apply_palette(ppu->bgp, line[x]);
		uint8_t *p = &ppu->fb[(y * 160 + x) * 3];

		p[0] = shade_rgb[shade][0];
		p[1] = shade_rgb[shade][1];
		p[2] = shade_rgb[shade][2];
	}

	/* Sprites. */
	if (ppu->lcdc & LCDC_OBJ_ENABLE) {
		int height = (ppu->lcdc & LCDC_OBJ_SIZE) ? 16 : 8;
		int sprite;

		/*
		 * Lower OAM indices win, so walk the list in order and only paint
		 * where nothing has been painted yet. A per-pixel mask is the
		 * simplest correct way to express that.
		 */
		uint8_t painted[160];

		memset(painted, 0, sizeof(painted));

		for (sprite = 0; sprite < 40; sprite++) {
			int sy = ppu->oam[sprite * 4 + 0] - 16;
			int sx = ppu->oam[sprite * 4 + 1] - 8;
			uint8_t tile = ppu->oam[sprite * 4 + 2];
			uint8_t flags = ppu->oam[sprite * 4 + 3];
			bool behind = (flags & 0x80) != 0;
			bool flip_y = (flags & 0x40) != 0;
			bool flip_x = (flags & 0x20) != 0;
			uint8_t palette = (flags & 0x10) ? ppu->obp1 : ppu->obp0;
			int row, sx_off;
			uint16_t tile_addr;
			uint8_t lo, hi, bit, color_num;

			if (y < sy || y >= sy + height) {
				continue;
			}

			row = y - sy;
			if (flip_y) {
				row = height - 1 - row;
			}

			/* 8x16 sprites use an even tile number and two tiles. */
			if (height == 16) {
				tile_addr = (tile & 0xfe) * 16 + (row >= 8 ? 16 : 0) +
				            (row & 7) * 2;
			} else {
				tile_addr = tile * 16 + row * 2;
			}

			lo = ppu->vram[tile_addr & 0x1fff];
			hi = ppu->vram[(tile_addr + 1) & 0x1fff];

			for (sx_off = 0; sx_off < 8; sx_off++) {
				int px = sx + sx_off;

				if (px < 0 || px >= 160) {
					continue;
				}
				if (painted[px]) {
					continue;
				}

				bit = flip_x ? sx_off : 7 - sx_off;
				color_num = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);

				if (color_num == 0) {
					continue; /* transparent */
				}
				if (behind && ppu->bg_color_num[px] != 0) {
					continue;
				}

				uint8_t shade = apply_palette(palette, color_num);
				uint8_t *p = &ppu->fb[(y * 160 + px) * 3];

				p[0] = shade_rgb[shade][0];
				p[1] = shade_rgb[shade][1];
				p[2] = shade_rgb[shade][2];
				painted[px] = 1;
			}
		}
	}
}

/* --- stepping --------------------------------------------------------- */

static void enter_mode(GBPPU *ppu, GB *gb, int mode)
{
	ppu->mode = mode;

	if (mode == 1) {
		/* Vertical blank: raise VBlank and, if enabled, the STAT mode 1
		 * interrupt. */
		gb->iflag |= 0x01;
		if (ppu->stat & STAT_MODE1) {
			request_stat(gb);
		}
	} else if (mode == 2) {
		if (ppu->stat & STAT_MODE2) {
			request_stat(gb);
		}
	} else if (mode == 0) {
		if (ppu->stat & STAT_MODE0) {
			request_stat(gb);
		}
	}
}

void ppu_step(GBPPU *ppu, GB *gb, int cycles)
{
	if (!(ppu->lcdc & LCDC_ENABLE)) {
		/* With the LCD off the PPU sits at line 0, mode 0. */
		ppu->lcd_off = true;
		ppu->ly = 0;
		ppu->dot = 0;
		ppu->mode = 0;
		ppu->frame_ready = false;
		return;
	}

	if (ppu->lcd_off) {
		/* Turning the LCD back on starts a fresh frame. */
		ppu->lcd_off = false;
		ppu->ly = 0;
		ppu->dot = 0;
		ppu->mode = 2;
	}

	ppu->dot += cycles;

	while (ppu->dot >= 456) {
		ppu->dot -= 456;
		ppu->ly++;

		if (ppu->ly == 144) {
			enter_mode(ppu, gb, 1);
			ppu->frame_ready = true;
		} else if (ppu->ly > 153) {
			ppu->ly = 0;
			enter_mode(ppu, gb, 2);
		} else if (ppu->ly < 144) {
			enter_mode(ppu, gb, 2);
		}

		update_stat_line(ppu, gb);
	}

	/* Mode transitions within the line. */
	if (ppu->ly < 144) {
		if (ppu->dot < 80 && ppu->mode != 2) {
			enter_mode(ppu, gb, 2);
		} else if (ppu->dot >= 80 && ppu->dot < 252 && ppu->mode != 3) {
			ppu->mode = 3;
		} else if (ppu->dot >= 252 && ppu->mode != 0) {
			enter_mode(ppu, gb, 0);
			render_scanline(ppu, ppu->ly);
		}
	}
}

/* --- register access -------------------------------------------------- */

uint8_t ppu_read_reg(GBPPU *ppu, uint16_t addr)
{
	switch (addr) {
	case 0xff40: return ppu->lcdc;
	case 0xff41: return ppu->stat | 0x80;
	case 0xff42: return ppu->scy;
	case 0xff43: return ppu->scx;
	case 0xff44: return ppu->ly;
	case 0xff45: return ppu->lyc;
	case 0xff47: return ppu->bgp;
	case 0xff48: return ppu->obp0;
	case 0xff49: return ppu->obp1;
	case 0xff4a: return ppu->wy;
	case 0xff4b: return ppu->wx;
	default: return 0xff;
	}
}

void ppu_write_reg(GBPPU *ppu, uint16_t addr, uint8_t val)
{
	switch (addr) {
	case 0xff40:
		/* Turning the LCD on from off restarts the scan at line 0. */
		if (!(ppu->lcdc & LCDC_ENABLE) && (val & LCDC_ENABLE)) {
			ppu->ly = 0;
			ppu->dot = 0;
			ppu->mode = 2;
			ppu->lcd_off = false;
		}
		if ((ppu->lcdc & LCDC_ENABLE) && !(val & LCDC_ENABLE)) {
			ppu->ly = 0;
			ppu->lcd_off = true;
		}
		ppu->lcdc = val;
		break;
	case 0xff41:
		ppu->stat = (ppu->stat & 0x07) | (val & 0x78);
		break;
	case 0xff42: ppu->scy = val; break;
	case 0xff43: ppu->scx = val; break;
	case 0xff44: break; /* LY is read-only */
	case 0xff45: ppu->lyc = val; break;
	case 0xff47: ppu->bgp = val; break;
	case 0xff48: ppu->obp0 = val; break;
	case 0xff49: ppu->obp1 = val; break;
	case 0xff4a: ppu->wy = val; break;
	case 0xff4b: ppu->wx = val; break;
	default: break;
	}
}
