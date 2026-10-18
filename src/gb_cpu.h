/*
 * SM83 CPU core (the Game Boy's processor).
 *
 * This is a plain C implementation of the instruction set, written to be read
 * and checked rather than to be clever. Flags live in F exactly as the hardware
 * keeps them, so DAA and the carry instructions behave the way the hardware
 * does instead of being approximated.
 *
 * Interrupts, timers and the memory bus live in gb.c; this file only knows about
 * the CPU and calls back into the bus for every memory access.
 */

#ifndef GB_CPU_H
#define GB_CPU_H

#include <stdint.h>
#include <stdbool.h>

/* Flag bits in F. The low nibble is always zero on hardware. */
enum {
	FLAG_Z = 0x80,
	FLAG_N = 0x40,
	FLAG_H = 0x20,
	FLAG_C = 0x10,
};

typedef struct {
	uint8_t a, f, b, c, d, e, h, l;

	uint16_t sp;
	uint16_t pc;

	/* Interrupt master enable, and the two-cycle delay on EI. */
	bool ime;
	bool ime_pending;

	/* Set by the HALT instruction; cleared when an interrupt is serviced. */
	bool halted;

	/* Set when the CPU is stopped (STOP instruction). */
	bool stopped;

	/* Total cycles executed, used by the timers and the frame pacing. */
	uint64_t cycles;

	/* Back-pointer to the machine, for bus access. */
	struct GB *gb;
} SM83;

void sm83_reset(SM83 *cpu, struct GB *gb);

/* Runs one instruction and returns the cycles it took. */
int sm83_step(SM83 *cpu);

/* Called by the machine when an interrupt is pending. */
void sm83_interrupt(SM83 *cpu, uint8_t vector);

#endif /* GB_CPU_H */
