/*
 * Host-side test runner.
 *
 * Boots a ROM, runs it, and prints whatever the ROM writes to the serial port
 * (0xFF01/0xFF02), which is how the standard Game Boy test ROMs report results.
 * That makes this a check against the hardware's documented behaviour rather
 * than against my own idea of it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gb.h"

/* Blargg's ROMs write a status byte to 0xA000 (or 0x6000 for older ones) and
 * print text on the serial port. */
static char serial_buf[65536];
static size_t serial_len;

static void serial_hook(void *ctx, uint8_t val)
{
	(void)ctx;
	if (serial_len < sizeof(serial_buf) - 1) {
		serial_buf[serial_len++] = (char)val;
		serial_buf[serial_len] = '\0';
	}
}

int main(int argc, char **argv)
{
	FILE *f;
	long size;
	uint8_t *rom;
	GB *gb;
	int frames = 0;
	int max_frames = 3000;

	if (argc < 2) {
		fprintf(stderr, "usage: %s rom.gb [max_frames]\n", argv[0]);
		return 2;
	}
	if (argc > 2) {
		max_frames = atoi(argv[2]);
	}

	f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	rom = malloc(size);
	if (fread(rom, 1, size, f) != (size_t)size) {
		fprintf(stderr, "short read\n");
		return 2;
	}
	fclose(f);

	gb = gb_new(rom, size);
	if (!gb) {
		fprintf(stderr, "could not load %s (%ld bytes)\n", argv[1], size);
		return 2;
	}

	gb->serial_out = serial_hook;
	gb->serial_ctx = NULL;

	/*
	 * Blargg's multi-test ROM prints one result per line and waits for a button
	 * press before continuing. Pressing START every 30 frames is what the
	 * instructions on screen tell a human to do, and without it the ROM sits
	 * still after the first line.
	 */
	while (frames < max_frames) {
		if (frames % 30 == 0) {
			gb_set_buttons(gb, GB_BTN_START);
		} else if (frames % 30 == 1) {
			gb_set_buttons(gb, 0x00);
		}

		gb_run_frame(gb);
		frames++;

		/* Blargg's ROMs park the result in 0xA000: 0 = pass, else fail. */
		uint8_t status = gb->mbc.ram ? gb->mbc.ram[0] : 0xff;
		if (serial_len > 0 && strstr(serial_buf, "Passed")) {
			break;
		}
		if (serial_len > 0 && strstr(serial_buf, "Failed")) {
			break;
		}
		(void)status;
	}

	printf("frames: %d\n", frames);
	if (serial_len) {
		printf("--- serial ---\n%s\n-------------\n", serial_buf);
	} else {
		printf("(no serial output)\n");
	}

	int ok = strstr(serial_buf, "Passed") != NULL;
	printf("%s\n", ok ? "PASS" : "FAIL");

	gb_free(gb);
	free(rom);
	return ok ? 0 : 1;
}
