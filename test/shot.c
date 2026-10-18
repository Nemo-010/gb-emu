/*
 * Renders a cartridge to a raw RGB file after a number of frames.
 *
 * Used by the wasm comparison: the same ROM, the same frame count, and the two
 * pictures have to match.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gb.h"

int main(int argc, char **argv)
{
	FILE *f, *out;
	long size;
	uint8_t *rom;
	GB *gb;
	int frames = 120;

	if (argc < 3) {
		fprintf(stderr, "usage: %s rom.gb out.rgb [frames]\n", argv[0]);
		return 2;
	}
	if (argc > 3) {
		frames = atoi(argv[3]);
	}

	f = fopen(argv[1], "rb");
	if (!f) {
		return 2;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	rom = malloc(size);
	if (fread(rom, 1, size, f) != (size_t)size) {
		return 2;
	}
	fclose(f);

	gb = gb_new(rom, size);
	if (!gb) {
		return 2;
	}

	for (int i = 0; i < frames; i++) {
		gb_run_frame(gb);
	}

	out = fopen(argv[2], "wb");
	if (!out) {
		return 2;
	}
	fwrite(gb_framebuffer(gb), 1, GB_SCREEN_W * GB_SCREEN_H * 3, out);
	fclose(out);

	return 0;
}
