/*
 * SM83 CPU core.
 *
 * The Game Boy's processor is a 4.19 MHz 8-bit chip with 16-bit address pairs.
 * Every opcode is implemented here explicitly rather than through a table of
 * generated functions, because a switch is what a reader can check against a
 * reference table line by line.
 *
 * Cycle counts follow the hardware, since the timers and the frame pacing both
 * depend on them being right.
 */

#include <string.h>

#include "gb_cpu.h"
#include "gb.h"

/* Reads and writes go through the bus, which owns the memory map and the I/O
 * registers. The CPU has no direct access to memory. */

static inline uint8_t rd(SM83 *cpu, uint16_t addr)
{
	return gb_read(cpu->gb, addr);
}

static inline void wr(SM83 *cpu, uint16_t addr, uint8_t val)
{
	gb_write(cpu->gb, addr, val);
}

static inline uint8_t fetch8(SM83 *cpu)
{
	return rd(cpu, cpu->pc++);
}

static inline uint16_t fetch16(SM83 *cpu)
{
	uint16_t lo = fetch8(cpu);
	uint16_t hi = fetch8(cpu);

	return lo | (hi << 8);
}

/* --- register pair helpers ------------------------------------------- */

static inline uint16_t get_bc(SM83 *c) { return (c->b << 8) | c->c; }
static inline uint16_t get_de(SM83 *c) { return (c->d << 8) | c->e; }
static inline uint16_t get_hl(SM83 *c) { return (c->h << 8) | c->l; }
static inline uint16_t get_af(SM83 *c) { return (c->a << 8) | c->f; }

static inline void set_bc(SM83 *c, uint16_t v) { c->b = v >> 8; c->c = v & 0xff; }
static inline void set_de(SM83 *c, uint16_t v) { c->d = v >> 8; c->e = v & 0xff; }
static inline void set_hl(SM83 *c, uint16_t v) { c->h = v >> 8; c->l = v & 0xff; }
static inline void set_af(SM83 *c, uint16_t v)
{
	c->a = v >> 8;
	/* The low nibble of F is not real storage on hardware. */
	c->f = v & 0xf0;
}

static inline uint16_t get_r16(SM83 *c, int idx)
{
	switch (idx) {
	case 0: return get_bc(c);
	case 1: return get_de(c);
	case 2: return get_hl(c);
	default: return c->sp;
	}
}

static inline void set_r16(SM83 *c, int idx, uint16_t v)
{
	switch (idx) {
	case 0: set_bc(c, v); break;
	case 1: set_de(c, v); break;
	case 2: set_hl(c, v); break;
	default: c->sp = v; break;
	}
}

/* r8 index: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A */

static uint8_t rd_r8(SM83 *c, int idx)
{
	switch (idx) {
	case 0: return c->b;
	case 1: return c->c;
	case 2: return c->d;
	case 3: return c->e;
	case 4: return c->h;
	case 5: return c->l;
	case 6: return rd(c, get_hl(c));
	default: return c->a;
	}
}

static void wr_r8(SM83 *c, int idx, uint8_t v)
{
	switch (idx) {
	case 0: c->b = v; break;
	case 1: c->c = v; break;
	case 2: c->d = v; break;
	case 3: c->e = v; break;
	case 4: c->h = v; break;
	case 5: c->l = v; break;
	case 6: wr(c, get_hl(c), v); break;
	default: c->a = v; break;
	}
}

/* --- flag helpers ----------------------------------------------------- */

static inline void set_flag(SM83 *c, uint8_t flag, bool on)
{
	if (on) {
		c->f |= flag;
	} else {
		c->f &= ~flag;
	}
}

static inline bool get_flag(SM83 *c, uint8_t flag)
{
	return (c->f & flag) != 0;
}

/* --- stack ------------------------------------------------------------ */

static void push16(SM83 *c, uint16_t v)
{
	c->sp--;
	wr(c, c->sp, v >> 8);
	c->sp--;
	wr(c, c->sp, v & 0xff);
}

static uint16_t pop16(SM83 *c)
{
	uint16_t lo = rd(c, c->sp++);
	uint16_t hi = rd(c, c->sp++);

	return lo | (hi << 8);
}

/* --- ALU -------------------------------------------------------------- */

static void alu_add(SM83 *c, uint8_t v, bool with_carry)
{
	unsigned carry = with_carry && get_flag(c, FLAG_C) ? 1 : 0;
	unsigned r = c->a + v + carry;

	set_flag(c, FLAG_Z, (r & 0xff) == 0);
	set_flag(c, FLAG_N, false);
	set_flag(c, FLAG_H, ((c->a & 0xf) + (v & 0xf) + carry) > 0xf);
	set_flag(c, FLAG_C, r > 0xff);
	c->a = r & 0xff;
}

static void alu_sub(SM83 *c, uint8_t v, bool with_carry, bool store)
{
	unsigned carry = with_carry && get_flag(c, FLAG_C) ? 1 : 0;
	int r = c->a - v - carry;

	set_flag(c, FLAG_Z, (r & 0xff) == 0);
	set_flag(c, FLAG_N, true);
	set_flag(c, FLAG_H, ((int)(c->a & 0xf) - (int)(v & 0xf) - (int)carry) < 0);
	set_flag(c, FLAG_C, r < 0);
	if (store) {
		c->a = r & 0xff;
	}
}

static void alu_and(SM83 *c, uint8_t v)
{
	c->a &= v;
	c->f = FLAG_H | (c->a == 0 ? FLAG_Z : 0);
}

static void alu_xor(SM83 *c, uint8_t v)
{
	c->a ^= v;
	c->f = c->a == 0 ? FLAG_Z : 0;
}

static void alu_or(SM83 *c, uint8_t v)
{
	c->a |= v;
	c->f = c->a == 0 ? FLAG_Z : 0;
}

static void alu_cp(SM83 *c, uint8_t v)
{
	alu_sub(c, v, false, false);
}

static void alu_inc(SM83 *c, uint8_t *reg)
{
	uint8_t before = *reg;

	*reg = before + 1;
	set_flag(c, FLAG_Z, *reg == 0);
	set_flag(c, FLAG_N, false);
	set_flag(c, FLAG_H, (before & 0xf) == 0xf);
	/* INC does not touch the carry flag. */
}

static void alu_dec(SM83 *c, uint8_t *reg)
{
	uint8_t before = *reg;

	*reg = before - 1;
	set_flag(c, FLAG_Z, *reg == 0);
	set_flag(c, FLAG_N, true);
	set_flag(c, FLAG_H, (before & 0xf) == 0);
}

/* 16-bit ADD HL,rr: only N, H and C are affected, and H/C come from bit 11
 * and bit 15 of the 16-bit sum. */
static void alu_add_hl(SM83 *c, uint16_t v)
{
	uint16_t hl = get_hl(c);
	uint32_t r = hl + v;

	set_flag(c, FLAG_N, false);
	set_flag(c, FLAG_H, ((hl & 0xfff) + (v & 0xfff)) > 0xfff);
	set_flag(c, FLAG_C, r > 0xffff);
	set_hl(c, r & 0xffff);
}

/* ADD SP,e8 and LD HL,SP+e8 share these flag rules. */
static uint16_t add_sp_e8(SM83 *c, int8_t e)
{
	uint16_t sp = c->sp;

	set_flag(c, FLAG_Z, false);
	set_flag(c, FLAG_N, false);
	set_flag(c, FLAG_H, ((sp & 0xf) + (e & 0xf)) > 0xf);
	set_flag(c, FLAG_C, ((sp & 0xff) + ((uint8_t)e)) > 0xff);

	return sp + e;
}

/* --- rotate / shift helpers (used by both A-register and CB forms) ----- */

static uint8_t rot_rlc(SM83 *c, uint8_t v)
{
	uint8_t carry = v >> 7;

	v = (v << 1) | carry;
	c->f = carry ? FLAG_C : 0;
	if (v == 0) {
		c->f |= FLAG_Z;
	}
	return v;
}

static uint8_t rot_rrc(SM83 *c, uint8_t v)
{
	uint8_t carry = v & 1;

	v = (v >> 1) | (carry << 7);
	c->f = carry ? FLAG_C : 0;
	if (v == 0) {
		c->f |= FLAG_Z;
	}
	return v;
}

static uint8_t rot_rl(SM83 *c, uint8_t v)
{
	uint8_t carry = v >> 7;
	uint8_t in = get_flag(c, FLAG_C) ? 1 : 0;

	v = (v << 1) | in;
	c->f = carry ? FLAG_C : 0;
	if (v == 0) {
		c->f |= FLAG_Z;
	}
	return v;
}

static uint8_t rot_rr(SM83 *c, uint8_t v)
{
	uint8_t carry = v & 1;
	uint8_t in = get_flag(c, FLAG_C) ? 0x80 : 0;

	v = (v >> 1) | in;
	c->f = carry ? FLAG_C : 0;
	if (v == 0) {
		c->f |= FLAG_Z;
	}
	return v;
}

static uint8_t shift_sla(SM83 *c, uint8_t v)
{
	uint8_t carry = v >> 7;

	v <<= 1;
	c->f = carry ? FLAG_C : 0;
	if (v == 0) {
		c->f |= FLAG_Z;
	}
	return v;
}

static uint8_t shift_sra(SM83 *c, uint8_t v)
{
	uint8_t carry = v & 1;

	/* Arithmetic shift keeps the sign bit. */
	v = (v >> 1) | (v & 0x80);
	c->f = carry ? FLAG_C : 0;
	if (v == 0) {
		c->f |= FLAG_Z;
	}
	return v;
}

static uint8_t shift_swap(SM83 *c, uint8_t v)
{
	v = (v << 4) | (v >> 4);
	c->f = v == 0 ? FLAG_Z : 0;
	return v;
}

static uint8_t shift_srl(SM83 *c, uint8_t v)
{
	uint8_t carry = v & 1;

	v >>= 1;
	c->f = carry ? FLAG_C : 0;
	if (v == 0) {
		c->f |= FLAG_Z;
	}
	return v;
}

/* --- DAA -------------------------------------------------------------- */

/*
 * Decimal adjust after arithmetic. The result depends on N, H and C, which is
 * why the flags have to be exact: an approximate DAA makes BCD scores in games
 * drift.
 */
static void daa(SM83 *c)
{
	uint8_t correction = 0;
	bool carry = get_flag(c, FLAG_C);

	if (!get_flag(c, FLAG_N)) {
		if (get_flag(c, FLAG_H) || (c->a & 0x0f) > 9) {
			correction |= 0x06;
		}
		if (carry || c->a > 0x99) {
			correction |= 0x60;
			carry = true;
		}
		c->a += correction;
	} else {
		if (get_flag(c, FLAG_H)) {
			correction |= 0x06;
		}
		if (carry) {
			correction |= 0x60;
		}
		c->a -= correction;
	}

	set_flag(c, FLAG_Z, c->a == 0);
	set_flag(c, FLAG_H, false);
	set_flag(c, FLAG_C, carry);
}

/* --- CB prefix -------------------------------------------------------- */

static int op_cb(SM83 *c)
{
	uint8_t op = fetch8(c);
	int reg = op & 7;
	int bit = (op >> 3) & 7;
	uint8_t v;
	int cycles = 8;

	switch (op >> 6) {
	case 0: /* rotate / shift group */
		v = rd_r8(c, reg);
		if (reg == 6) {
			cycles = 16;
		}
		switch (bit) {
		case 0: v = rot_rlc(c, v); break;
		case 1: v = rot_rrc(c, v); break;
		case 2: v = rot_rl(c, v); break;
		case 3: v = rot_rr(c, v); break;
		case 4: v = shift_sla(c, v); break;
		case 5: v = shift_sra(c, v); break;
		case 6: v = shift_swap(c, v); break;
		default: v = shift_srl(c, v); break;
		}
		wr_r8(c, reg, v);
		break;

	case 1: /* BIT */
		v = rd_r8(c, reg);
		if (reg == 6) {
			cycles = 12;
		}
		set_flag(c, FLAG_Z, (v & (1 << bit)) == 0);
		set_flag(c, FLAG_N, false);
		set_flag(c, FLAG_H, true);
		break;

	case 2: /* RES */
		v = rd_r8(c, reg);
		if (reg == 6) {
			cycles = 16;
		}
		v &= ~(1 << bit);
		wr_r8(c, reg, v);
		break;

	default: /* SET */
		v = rd_r8(c, reg);
		if (reg == 6) {
			cycles = 16;
		}
		v |= (1 << bit);
		wr_r8(c, reg, v);
		break;
	}

	return cycles;
}

/* --- reset ------------------------------------------------------------ */

void sm83_reset(SM83 *c, struct GB *gb)
{
	memset(c, 0, sizeof(*c));
	c->gb = gb;

	/* The post-boot register values a real console presents to the cartridge. */
	c->a = 0x01;
	c->f = 0xb0;
	c->b = 0x00;
	c->c = 0x13;
	c->d = 0x00;
	c->e = 0xd8;
	c->h = 0x01;
	c->l = 0x4d;
	c->sp = 0xfffe;
	c->pc = 0x0100;

	c->ime = false;
}

void sm83_interrupt(SM83 *c, uint8_t vector)
{
	c->ime = false;
	c->halted = false;
	c->stopped = false;

	/* Push the return address and jump. */
	push16(c, c->pc);
	c->pc = vector;
	c->cycles += 20;
}

/* --- the instruction switch ------------------------------------------- */

int sm83_step(SM83 *c)
{
	uint8_t op = fetch8(c);
	uint8_t v;
	uint16_t nn;
	int8_t e;
	int cycles = 4;

	switch (op) {
	case 0x00: /* NOP */
		break;

	/* LD rr,nn */
	case 0x01: set_bc(c, fetch16(c)); cycles = 12; break;
	case 0x11: set_de(c, fetch16(c)); cycles = 12; break;
	case 0x21: set_hl(c, fetch16(c)); cycles = 12; break;
	case 0x31: c->sp = fetch16(c); cycles = 12; break;

	/* LD (rr),A */
	case 0x02: wr(c, get_bc(c), c->a); cycles = 8; break;
	case 0x12: wr(c, get_de(c), c->a); cycles = 8; break;
	case 0x22: wr(c, get_hl(c), c->a); set_hl(c, get_hl(c) + 1); cycles = 8; break;
	case 0x32: wr(c, get_hl(c), c->a); set_hl(c, get_hl(c) - 1); cycles = 8; break;

	/* LD A,(rr) */
	case 0x0a: c->a = rd(c, get_bc(c)); cycles = 8; break;
	case 0x1a: c->a = rd(c, get_de(c)); cycles = 8; break;
	case 0x2a: c->a = rd(c, get_hl(c)); set_hl(c, get_hl(c) + 1); cycles = 8; break;
	case 0x3a: c->a = rd(c, get_hl(c)); set_hl(c, get_hl(c) - 1); cycles = 8; break;

	/* INC / DEC rr */
	case 0x03: set_bc(c, get_bc(c) + 1); cycles = 8; break;
	case 0x13: set_de(c, get_de(c) + 1); cycles = 8; break;
	case 0x23: set_hl(c, get_hl(c) + 1); cycles = 8; break;
	case 0x33: c->sp++; cycles = 8; break;
	case 0x0b: set_bc(c, get_bc(c) - 1); cycles = 8; break;
	case 0x1b: set_de(c, get_de(c) - 1); cycles = 8; break;
	case 0x2b: set_hl(c, get_hl(c) - 1); cycles = 8; break;
	case 0x3b: c->sp--; cycles = 8; break;

	/* INC r8 */
	case 0x04: case 0x0c: case 0x14: case 0x1c:
	case 0x24: case 0x2c: case 0x34: case 0x3c: {
		int r = (op >> 3) & 7;

		if (r == 6) {
			v = rd(c, get_hl(c));
			alu_inc(c, &v);
			wr(c, get_hl(c), v);
			cycles = 12;
		} else {
			v = rd_r8(c, r);
			alu_inc(c, &v);
			wr_r8(c, r, v);
		}
		break;
	}

	/* DEC r8 */
	case 0x05: case 0x0d: case 0x15: case 0x1d:
	case 0x25: case 0x2d: case 0x35: case 0x3d: {
		int r = (op >> 3) & 7;

		if (r == 6) {
			v = rd(c, get_hl(c));
			alu_dec(c, &v);
			wr(c, get_hl(c), v);
			cycles = 12;
		} else {
			v = rd_r8(c, r);
			alu_dec(c, &v);
			wr_r8(c, r, v);
		}
		break;
	}

	/* LD r8,n */
	case 0x06: case 0x0e: case 0x16: case 0x1e:
	case 0x26: case 0x2e: case 0x36: case 0x3e: {
		int r = (op >> 3) & 7;

		v = fetch8(c);
		wr_r8(c, r, v);
		cycles = r == 6 ? 12 : 8;
		break;
	}

	/* rotate A shorthand: these clear Z unconditionally */
	case 0x07: /* RLCA */
		c->a = rot_rlc(c, c->a);
		c->f &= ~FLAG_Z;
		break;
	case 0x0f: /* RRCA */
		c->a = rot_rrc(c, c->a);
		c->f &= ~FLAG_Z;
		break;
	case 0x17: /* RLA */
		c->a = rot_rl(c, c->a);
		c->f &= ~FLAG_Z;
		break;
	case 0x1f: /* RRA */
		c->a = rot_rr(c, c->a);
		c->f &= ~FLAG_Z;
		break;

	case 0x27: /* DAA */
		daa(c);
		break;

	case 0x2f: /* CPL */
		c->a = ~c->a;
		set_flag(c, FLAG_N, true);
		set_flag(c, FLAG_H, true);
		break;

	case 0x37: /* SCF */
		set_flag(c, FLAG_N, false);
		set_flag(c, FLAG_H, false);
		set_flag(c, FLAG_C, true);
		break;

	case 0x3f: /* CCF */
		set_flag(c, FLAG_N, false);
		set_flag(c, FLAG_H, false);
		set_flag(c, FLAG_C, !get_flag(c, FLAG_C));
		break;

	case 0x08: /* LD (nn),SP */
		nn = fetch16(c);
		wr(c, nn, c->sp & 0xff);
		wr(c, nn + 1, c->sp >> 8);
		cycles = 20;
		break;

	case 0x09: case 0x19: case 0x29: case 0x39: /* ADD HL,rr */
		alu_add_hl(c, get_r16(c, (op >> 4) & 3));
		cycles = 8;
		break;

	case 0x10: /* STOP */
		fetch8(c); /* the padding byte is consumed */
		c->stopped = true;
		break;

	case 0x18: /* JR e */
		e = (int8_t)fetch8(c);
		c->pc += e;
		cycles = 12;
		break;

	case 0x20: case 0x28: case 0x30: case 0x38: { /* JR cc,e */
		bool take;

		e = (int8_t)fetch8(c);
		switch ((op >> 3) & 3) {
		case 0: take = !get_flag(c, FLAG_Z); break; /* NZ */
		case 1: take = get_flag(c, FLAG_Z); break;  /* Z  */
		case 2: take = !get_flag(c, FLAG_C); break; /* NC */
		default: take = get_flag(c, FLAG_C); break; /* C  */
		}
		if (take) {
			c->pc += e;
			cycles = 12;
		} else {
			cycles = 8;
		}
		break;
	}

	case 0xf3: /* DI */
		c->ime = false;
		c->ime_pending = false;
		break;

	case 0xfb: /* EI */
		/* EI takes effect after the following instruction. */
		c->ime_pending = true;
		break;

	/* LD r8,r8 and HALT live in 0x40-0x7f */
	case 0x40 ... 0x7f: {
		int dst = (op >> 3) & 7;
		int src = op & 7;

		if (op == 0x76) {
			/*
			 * HALT, with the hardware's HALT bug: if interrupts are disabled
			 * and one is already pending, the CPU halts but the halt does not
			 * take effect, so the byte after HALT executes twice. Getting this
			 * wrong makes some games skip a byte of code.
			 */
			if (!c->ime && gb_pending_interrupts(c->gb) != 0) {
				c->pc--;
			} else {
				c->halted = true;
			}
			break;
		}
		v = rd_r8(c, src);
		wr_r8(c, dst, v);
		if (dst == 6 || src == 6) {
			cycles = 8;
		}
		break;
	}

	/* ALU A,r8 */
	case 0x80 ... 0xbf: {
		int kind = (op >> 3) & 7;
		int src = op & 7;

		v = rd_r8(c, src);
		if (src == 6) {
			cycles = 8;
		}
		switch (kind) {
		case 0: alu_add(c, v, false); break;
		case 1: alu_add(c, v, true); break;
		case 2: alu_sub(c, v, false, true); break;
		case 3: alu_sub(c, v, true, true); break;
		case 4: alu_and(c, v); break;
		case 5: alu_xor(c, v); break;
		case 6: alu_or(c, v); break;
		default: alu_cp(c, v); break;
		}
		break;
	}

	/* ALU A,n */
	case 0xc6: alu_add(c, fetch8(c), false); cycles = 8; break;
	case 0xce: alu_add(c, fetch8(c), true); cycles = 8; break;
	case 0xd6: alu_sub(c, fetch8(c), false, true); cycles = 8; break;
	case 0xde: alu_sub(c, fetch8(c), true, true); cycles = 8; break;
	case 0xe6: alu_and(c, fetch8(c)); cycles = 8; break;
	case 0xee: alu_xor(c, fetch8(c)); cycles = 8; break;
	case 0xf6: alu_or(c, fetch8(c)); cycles = 8; break;
	case 0xfe: alu_cp(c, fetch8(c)); cycles = 8; break;

	/* returns */
	case 0xc9: /* RET */
		c->pc = pop16(c);
		cycles = 16;
		break;

	case 0xd9: /* RETI */
		c->pc = pop16(c);
		c->ime = true;
		cycles = 16;
		break;

	case 0xc0: case 0xc8: case 0xd0: case 0xd8: { /* RET cc */
		bool take;

		switch ((op >> 3) & 3) {
		case 0: take = !get_flag(c, FLAG_Z); break;
		case 1: take = get_flag(c, FLAG_Z); break;
		case 2: take = !get_flag(c, FLAG_C); break;
		default: take = get_flag(c, FLAG_C); break;
		}
		if (take) {
			c->pc = pop16(c);
			cycles = 20;
		} else {
			cycles = 8;
		}
		break;
	}

	/* PUSH / POP */
	case 0xc1: set_bc(c, pop16(c)); cycles = 12; break;
	case 0xd1: set_de(c, pop16(c)); cycles = 12; break;
	case 0xe1: set_hl(c, pop16(c)); cycles = 12; break;
	case 0xf1: set_af(c, pop16(c)); cycles = 12; break;
	case 0xc5: push16(c, get_bc(c)); cycles = 16; break;
	case 0xd5: push16(c, get_de(c)); cycles = 16; break;
	case 0xe5: push16(c, get_hl(c)); cycles = 16; break;
	case 0xf5: push16(c, get_af(c)); cycles = 16; break;

	/* JP nn */
	case 0xc3:
		c->pc = fetch16(c);
		cycles = 16;
		break;

	/* JP HL */
	case 0xe9:
		c->pc = get_hl(c);
		break;

	/* JP cc,nn */
	case 0xc2: case 0xca: case 0xd2: case 0xda: {
		bool take;

		nn = fetch16(c);
		switch ((op >> 3) & 3) {
		case 0: take = !get_flag(c, FLAG_Z); break;
		case 1: take = get_flag(c, FLAG_Z); break;
		case 2: take = !get_flag(c, FLAG_C); break;
		default: take = get_flag(c, FLAG_C); break;
		}
		if (take) {
			c->pc = nn;
			cycles = 16;
		} else {
			cycles = 12;
		}
		break;
	}

	/* CALL nn */
	case 0xcd:
		nn = fetch16(c);
		push16(c, c->pc);
		c->pc = nn;
		cycles = 24;
		break;

	/* CALL cc,nn */
	case 0xc4: case 0xcc: case 0xd4: case 0xdc: {
		bool take;

		nn = fetch16(c);
		switch ((op >> 3) & 3) {
		case 0: take = !get_flag(c, FLAG_Z); break;
		case 1: take = get_flag(c, FLAG_Z); break;
		case 2: take = !get_flag(c, FLAG_C); break;
		default: take = get_flag(c, FLAG_C); break;
		}
		if (take) {
			push16(c, c->pc);
			c->pc = nn;
			cycles = 24;
		} else {
			cycles = 12;
		}
		break;
	}

	/* RST */
	case 0xc7: case 0xcf: case 0xd7: case 0xdf:
	case 0xe7: case 0xef: case 0xf7: case 0xff:
		push16(c, c->pc);
		c->pc = op & 0x38;
		cycles = 16;
		break;

	/* LDH (n),A / LDH A,(n) */
	case 0xe0: wr(c, 0xff00 + fetch8(c), c->a); cycles = 12; break;
	case 0xf0: c->a = rd(c, 0xff00 + fetch8(c)); cycles = 12; break;

	/* LD (C),A / LD A,(C) */
	case 0xe2: wr(c, 0xff00 + c->c, c->a); cycles = 8; break;
	case 0xf2: c->a = rd(c, 0xff00 + c->c); cycles = 8; break;

	/* LD (nn),A / LD A,(nn) */
	case 0xea: wr(c, fetch16(c), c->a); cycles = 16; break;
	case 0xfa: c->a = rd(c, fetch16(c)); cycles = 16; break;

	/* ADD SP,e8 / LD HL,SP+e8 */
	case 0xe8:
		e = (int8_t)fetch8(c);
		c->sp = add_sp_e8(c, e);
		cycles = 16;
		break;

	case 0xf8:
		e = (int8_t)fetch8(c);
		set_hl(c, add_sp_e8(c, e));
		cycles = 12;
		break;

	/* LD SP,HL */
	case 0xf9:
		c->sp = get_hl(c);
		cycles = 8;
		break;

	case 0xcb:
		cycles = op_cb(c);
		break;

	case 0xd3: case 0xdb: case 0xdd: case 0xe3: case 0xe4:
	case 0xeb: case 0xec: case 0xed: case 0xf4: case 0xfc: case 0xfd:
		/*
		 * These opcodes do not exist on the SM83. Real hardware locks up;
		 * treating them as a NOP keeps a bad ROM from taking the emulator
		 * with it, and the counter lets a caller notice.
		 */
		c->gb->invalid_opcodes++;
		break;

	default:
		c->gb->invalid_opcodes++;
		break;
	}

	c->cycles += cycles;

	/* EI is delayed by one instruction. */
	if (c->ime_pending) {
		c->ime_pending = false;
		/* Only commit if no interrupt was serviced this step. */
		if (!c->ime) {
			c->ime = true;
		}
	}

	return cycles;
}
