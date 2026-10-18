/*
 * ROM loading: an SD card over SPI, a FAT filesystem, and a directory listing.
 *
 * The point of this file is that the player never assumes anything about the
 * card. It finds the SD controller from the devicetree, mounts whatever volume
 * is there, and lists every .gb/.gbc file it can see. When there is no card, or
 * no controller in the devicetree, it says so instead of failing silently.
 */

#ifndef ROM_LOADER_H
#define ROM_LOADER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ROM_MAX_ENTRIES 32
#define ROM_NAME_LEN 48

typedef struct {
	char name[ROM_NAME_LEN];
	size_t size;
} ROMEntry;

typedef enum {
	ROM_STATE_NO_DEVICE,   /* no SD controller in the devicetree */
	ROM_STATE_NO_CARD,     /* controller present, no card */
	ROM_STATE_NO_FS,       /* card present, no mountable filesystem */
	ROM_STATE_READY,       /* mounted, entries available */
} ROMState;

/* Finds the SD controller and mounts it. Safe to call once at startup. */
ROMState rom_loader_init(void);

/* Re-scans the card for .gb and .gbc files. Returns the number found. */
int rom_loader_scan(void);

int rom_loader_count(void);
const ROMEntry *rom_loader_entry(int index);

/* Loads an entry into a freshly allocated buffer. The caller owns it. */
uint8_t *rom_loader_load(int index, size_t *size);

/* A human-readable reason for the current state. */
const char *rom_loader_status_text(void);

/* True when the devicetree has an SD host controller at all. */
bool rom_loader_have_device(void);

#endif /* ROM_LOADER_H */
