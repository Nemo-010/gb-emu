/*
 * Cartridge header and memory bank controllers.
 *
 * Only the mappers that cover real commercial software are here: none (ROM
 * only), MBC1, MBC2, MBC3 (with its real-time clock registers) and MBC5. Those
 * cover the overwhelming majority of the library, including the games people
 * actually write homebrew for.
 */

#ifndef GB_MBC_H
#define GB_MBC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
	MBC_NONE = 0,
	MBC_1,
	MBC_2,
	MBC_3,
	MBC_5,
} MBCType;

/* Header fields worth surfacing in the loader menu. */
typedef struct {
	char title[17];
	uint8_t cgb_flag;
	uint8_t cart_type;
	uint8_t rom_size_code;
	uint8_t ram_size_code;
	MBCType mbc;
	uint32_t rom_banks;
	uint32_t ram_bytes;
	bool has_battery;
	bool has_rtc;
} GBCartInfo;

typedef struct GBMBC {
	const uint8_t *rom;
	size_t rom_size;

	uint8_t *ram;
	size_t ram_size;

	MBCType type;

	/* Bank registers. */
	uint16_t rom_bank;
	uint8_t ram_bank;
	bool ram_enabled;
	bool bank_mode_high;

	/* MBC3 real-time clock. */
	uint8_t rtc[5];
	uint8_t rtc_latch[5];
	bool rtc_latched;

	/* MBC2 has 512x4 bits of built-in RAM. */
	uint8_t mbc2_ram[512];
} GBMBC;

/* Reads the header, picks the mapper and allocates cartridge RAM. Returns false
 * if the cartridge is not usable. */
bool mbc_init(GBMBC *mbc, const uint8_t *rom, size_t rom_size);

void mbc_free(GBMBC *mbc);

void mbc_read_info(const uint8_t *rom, size_t rom_size, GBCartInfo *info);

/* The mapper answers for the whole 0x0000-0x7FFF ROM window and the
 * 0xA000-0xBFFF RAM window. */
uint8_t mbc_read_rom(GBMBC *mbc, uint16_t addr);
void mbc_write_rom(GBMBC *mbc, uint16_t addr, uint8_t val);
uint8_t mbc_read_ram(GBMBC *mbc, uint16_t addr);
void mbc_write_ram(GBMBC *mbc, uint16_t addr, uint8_t val);

/* Advances the real-time clock by the given number of seconds. */
void mbc_tick_rtc(GBMBC *mbc, unsigned seconds);

#endif /* GB_MBC_H */
