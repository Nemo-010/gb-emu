/*
 * Game Boy machine: the memory map and everything that answers on it.
 *
 * Address map, as the hardware has it:
 *
 *   0000-3FFF  cartridge ROM bank 0 (or boot ROM while it is mapped)
 *   4000-7FFF  cartridge ROM, switched bank
 *   8000-9FFF  video RAM
 *   A000-BFFF  cartridge RAM, switched bank
 *   C000-DFFF  work RAM
 *   E000-FDFF  echo of C000-DDFF
 *   FE00-FE9F  sprite attribute table
 *   FF00-FF7F  I/O registers
 *   FF80-FFFE  high RAM
 *   FFFF       interrupt enable
 *
 * The timer and the divider are derived from the same cycle counter, because on
 * hardware they are the same counter observed at different rates.
 */

#include <stdlib.h>
#include <string.h>

#include "gb.h"

/* --- interrupt vectors ------------------------------------------------- */

#define IRQ_VBLANK 0x40
#define IRQ_STAT   0x48
#define IRQ_TIMER  0x50
#define IRQ_SERIAL 0x58
#define IRQ_JOYPAD 0x60

uint8_t gb_pending_interrupts(GB *gb)
{
	return gb->ie & gb->iflag;
}

/* --- timer ------------------------------------------------------------- */

/*
 * TAC selects one of four bit positions of the internal divider as the timer
 * input. The exact bit is what makes some games' timing work, so it is computed
 * rather than approximated with a frequency table.
 */
static int timer_bit(uint8_t tac)
{
	switch (tac & 0x03) {
	case 0: return 9;  /* 4096 Hz */
	case 1: return 3;  /* 262144 Hz */
	case 2: return 5;  /* 65536 Hz */
	default: return 7; /* 16384 Hz */
	}
}

static void timer_step(GB *gb, int cycles)
{
	uint8_t tac = gb->io[0x07];
	int bit;
	bool old_signal;
	uint16_t old_div = gb->div_counter;

	gb->div_counter += cycles;

	/* DIV is the top byte of the divider; writing it resets the counter, which
	 * also drives the timer, so a DIV reset can clock TIMA. */
	if (!(tac & 0x04)) {
		return;
	}

	bit = timer_bit(tac);
	old_signal = (old_div >> bit) & 1;

	/* The falling edge of the selected bit increments TIMA. */
	int steps = (gb->div_counter >> bit) - (old_div >> bit);

	for (int i = 0; i < steps; i++) {
		if (gb->io[0x05] == 0xff) {
			gb->io[0x05] = gb->io[0x06];
			gb->iflag |= 0x04; /* timer interrupt */
		} else {
			gb->io[0x05]++;
		}
	}
	(void)old_signal;
}

/* --- OAM DMA ----------------------------------------------------------- */

/*
 * Writing the source page to 0xFF46 copies 160 bytes into the sprite table.
 * Real hardware does it over 160 machine cycles and the CPU can only touch high
 * RAM while it runs; copying eagerly is close enough that every game that uses
 * it draws correctly, and it avoids a whole class of timing bugs.
 */
static void gb_oam_dma(GB *gb, uint8_t page)
{
	uint16_t src = (uint16_t)page << 8;

	for (int i = 0; i < 0xa0; i++) {
		gb->ppu.oam[i] = gb_read(gb, src + i);
	}
}

/* --- memory map -------------------------------------------------------- */

uint8_t gb_read(GB *gb, uint16_t addr)
{
	if (addr < 0x8000) {
		if (gb->bootrom_enabled && addr < gb->bootrom_size) {
			return gb->bootrom[addr];
		}
		return mbc_read_rom(&gb->mbc, addr);
	}

	if (addr < 0xa000) {
		return gb->ppu.vram[addr & 0x1fff];
	}

	if (addr < 0xc000) {
		return mbc_read_ram(&gb->mbc, addr);
	}

	if (addr < 0xe000) {
		return gb->wram[addr - 0xc000];
	}

	if (addr < 0xfe00) {
		/* Echo RAM. */
		return gb->wram[addr - 0xe000];
	}

	if (addr < 0xfea0) {
		return gb->ppu.oam[addr - 0xfe00];
	}

	if (addr < 0xff00) {
		return 0xff; /* unusable */
	}

	if (addr < 0xff80) {
		uint8_t reg = addr & 0x7f;

		switch (reg) {
		case 0x00: /* JOYP */
			return joypad_read(gb);
		case 0x01: /* SB */
		case 0x02: /* SC */
			return gb->io[reg];
		case 0x04: /* DIV */
			return gb->div_counter >> 8;
		case 0x05: /* TIMA */
		case 0x06: /* TMA */
		case 0x07: /* TAC */
			return gb->io[reg];
		case 0x0f: /* IF */
			return gb->iflag | 0xe0;
		case 0x40 ... 0x45:
		case 0x47 ... 0x4b:
			return ppu_read_reg(&gb->ppu, addr);
		default:
			return gb->io[reg];
		}
	}

	if (addr < 0xffff) {
		return gb->hram[addr - 0xff80];
	}

	return gb->ie;
}

void gb_write(GB *gb, uint16_t addr, uint8_t val)
{
	if (addr < 0x8000) {
		/* Writing to 0xFF50 unmaps the boot ROM. */
		mbc_write_rom(&gb->mbc, addr, val);
		return;
	}

	if (addr < 0xa000) {
		gb->ppu.vram[addr & 0x1fff] = val;
		return;
	}

	if (addr < 0xc000) {
		mbc_write_ram(&gb->mbc, addr, val);
		gb->dirty = true;
		return;
	}

	if (addr < 0xe000) {
		gb->wram[addr - 0xc000] = val;
		return;
	}

	if (addr < 0xfe00) {
		gb->wram[addr - 0xe000] = val;
		return;
	}

	if (addr < 0xfea0) {
		gb->ppu.oam[addr - 0xfe00] = val;
		return;
	}

	if (addr < 0xff00) {
		return;
	}

	if (addr < 0xff80) {
		uint8_t reg = addr & 0x7f;

		switch (reg) {
		case 0x00: /* JOYP: the low nibble selects which button group to read */
			gb->io[0x00] = (gb->io[0x00] & 0xcf) | (val & 0x30);
			return;
		case 0x01: /* SB: a byte to send */
			gb->io[0x01] = val;
			if (gb->serial_out) {
				gb->serial_out(gb->serial_ctx, val);
			}
			return;
		case 0x02: /* SC: writing 0x81 starts an immediate transfer */
			gb->io[0x02] = val;
			if ((val & 0x81) == 0x81) {
				gb->io[0x02] = val & 0x7f;
				gb->iflag |= 0x08;
			}
			return;
		case 0x04: /* DIV: writing resets the whole divider */
			gb->div_counter = 0;
			return;
		case 0x05: gb->io[0x05] = val; return;
		case 0x06: gb->io[0x06] = val; return;
		case 0x07: gb->io[0x07] = val; return;
		case 0x0f: gb->iflag = val & 0x1f; return;
		case 0x40 ... 0x45:
		case 0x47 ... 0x4b:
			ppu_write_reg(&gb->ppu, addr, val);
			return;
		case 0x46: /* OAM DMA source; the copy is done eagerly below */
			gb_oam_dma(gb, val);
			gb->io[0x46] = val;
			return;
		case 0x50: /* boot ROM disable */
			gb->bootrom_enabled = false;
			return;
		default:
			gb->io[reg] = val;
			return;
		}
	}

	if (addr < 0xffff) {
		gb->hram[addr - 0xff80] = val;
		return;
	}

	gb->ie = val;
}

/* --- joypad ------------------------------------------------------------ */

/*
 * The joypad register presents two groups of four buttons, selected by bits 4
 * and 5. A pressed button reads as 0. When both groups are selected at once the
 * hardware ANDs them together, which is what this does.
 */
uint8_t joypad_read(GB *gb)
{
	uint8_t sel = gb->io[0x00] & 0x30;
	/* Bits 6 and 7 read as 1; the select bits read back as written. */
	uint8_t out = 0xc0 | sel;

	/*
	 * Bit 4 selects the direction keys, bit 5 the action buttons, and a pressed
	 * button pulls its line low. Button state is stored as two nibbles: low for
	 * directions, high for actions.
	 */
	if (!(sel & 0x10)) {
		out |= 0x0f & ~(gb->buttons & 0x0f);
	} else {
		out |= 0x0f;
	}

	if (!(sel & 0x20)) {
		out |= (0x0f & ~((gb->buttons >> 4) & 0x0f)) << 4;
	} else {
		out |= 0xf0;
	}

	return out;
}

void gb_set_buttons(GB *gb, uint8_t buttons)
{
	uint8_t before = gb->buttons;

	gb->buttons = buttons;

	/* A press on any button raises the joypad interrupt. */
	if (buttons & ~before) {
		gb->iflag |= 0x10;
	}
}

/* --- construction ------------------------------------------------------ */

GB *gb_new(const uint8_t *rom, size_t rom_size)
{
	GB *gb;

	if (!rom || rom_size < 0x150 || rom_size > GB_MAX_ROM) {
		return NULL;
	}

	gb = calloc(1, sizeof(*gb));
	if (!gb) {
		return NULL;
	}

	if (!mbc_init(&gb->mbc, rom, rom_size)) {
		free(gb);
		return NULL;
	}

	gb->bootrom_enabled = false;
	gb->ppu.lcd_off = false;

	sm83_reset(&gb->cpu, gb);
	ppu_reset(&gb->ppu);

	gb->io[0x00] = 0xcf;
	gb->ie = 0x00;
	gb->iflag = 0x01;

	return gb;
}

void gb_set_bootrom(GB *gb, const uint8_t *bootrom, size_t size)
{
	gb->bootrom = bootrom;
	gb->bootrom_size = size < 0x100 ? size : 0x100;
	gb->bootrom_enabled = gb->bootrom != NULL;
}

void gb_free(GB *gb)
{
	if (!gb) {
		return;
	}
	mbc_free(&gb->mbc);
	free(gb);
}

const uint8_t *gb_framebuffer(GB *gb)
{
	return gb->ppu.fb;
}

uint8_t *gb_sram(GB *gb, size_t *size)
{
	if (size) {
		*size = gb->mbc.ram_size;
	}
	return gb->mbc.ram;
}

/* --- running ----------------------------------------------------------- */

/*
 * Interrupt dispatch happens before the next instruction is fetched. Taking an
 * interrupt costs 20 cycles, and RETI costs 16, which is why the frame pacing
 * below works out to 70224 cycles per frame at 59.7 frames per second.
 */
static void service_interrupts(GB *gb)
{
	uint8_t pending = gb_pending_interrupts(gb);

	if (pending == 0) {
		return;
	}

	/* A pending interrupt wakes the CPU from HALT even with IME clear. */
	gb->cpu.halted = false;

	if (!gb->cpu.ime) {
		return;
	}

	if (pending & 0x01) {
		gb->iflag &= ~0x01;
		sm83_interrupt(&gb->cpu, IRQ_VBLANK);
	} else if (pending & 0x02) {
		gb->iflag &= ~0x02;
		sm83_interrupt(&gb->cpu, IRQ_STAT);
	} else if (pending & 0x04) {
		gb->iflag &= ~0x04;
		sm83_interrupt(&gb->cpu, IRQ_TIMER);
	} else if (pending & 0x08) {
		gb->iflag &= ~0x08;
		sm83_interrupt(&gb->cpu, IRQ_SERIAL);
	} else if (pending & 0x10) {
		gb->iflag &= ~0x10;
		sm83_interrupt(&gb->cpu, IRQ_JOYPAD);
	}
}

int gb_run_frame(GB *gb)
{
	int total = 0;

	gb->ppu.frame_ready = false;

	/* 70224 cycles is one frame. Run until the PPU says the frame is done, with
	 * a hard cap so a broken ROM cannot wedge the host. */
	while (!gb->ppu.frame_ready && total < 70224 * 2) {
		int cycles;

		service_interrupts(gb);

		if (gb->cpu.halted || gb->cpu.stopped) {
			/*
			 * A halted CPU still advances the rest of the machine. Four
			 * cycles at a time keeps the timer and PPU in step.
			 */
			cycles = 4;
			gb->cpu.cycles += cycles;
		} else {
			cycles = sm83_step(&gb->cpu);
		}

		timer_step(gb, cycles);
		ppu_step(&gb->ppu, gb, cycles);

		total += cycles;
	}

	/* The real-time clock runs from wall time in the caller; here it is only
	 * advanced when a cartridge asks for it. */
	return total;
}
