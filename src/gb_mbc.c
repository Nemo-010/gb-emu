/*
 * Cartridge header parsing and memory bank controllers.
 */

#include <string.h>
#include <stdlib.h>

#include "gb_mbc.h"

void mbc_read_info(const uint8_t *rom, size_t rom_size, GBCartInfo *info)
{
	memset(info, 0, sizeof(*info));

	if (rom_size < 0x150) {
		return;
	}

	/* The title is 16 bytes at 0x0134; CGB cartridges use the last byte as a
	 * flag, so copy and terminate. */
	memcpy(info->title, rom + 0x0134, 16);
	info->title[16] = '\0';

	info->cgb_flag = rom[0x0143];
	info->cart_type = rom[0x0147];
	info->rom_size_code = rom[0x0148];
	info->ram_size_code = rom[0x0149];

	info->rom_banks = (rom_size + 0x3fff) / 0x4000;
	if (info->rom_banks == 0) {
		info->rom_banks = 1;
	}

	switch (info->cart_type) {
	case 0x00: case 0x08: case 0x09:
		info->mbc = MBC_NONE;
		break;
	case 0x01: case 0x02: case 0x03:
		info->mbc = MBC_1;
		break;
	case 0x05: case 0x06:
		info->mbc = MBC_2;
		break;
	case 0x0f: case 0x10: case 0x11: case 0x12: case 0x13:
		info->mbc = MBC_3;
		break;
	case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e:
		info->mbc = MBC_5;
		break;
	default:
		/* Anything else falls back to MBC1, which is what a real console
		 * effectively does when it does not recognise the type. */
		info->mbc = MBC_1;
		break;
	}

	info->has_battery = (info->cart_type == 0x03 || info->cart_type == 0x06 ||
	                     info->cart_type == 0x09 || info->cart_type == 0x0f ||
	                     info->cart_type == 0x10 || info->cart_type == 0x13 ||
	                     info->cart_type == 0x1b || info->cart_type == 0x1e);
	info->has_rtc = (info->cart_type == 0x0f || info->cart_type == 0x10);

	switch (info->ram_size_code) {
	case 0x00: info->ram_bytes = 0; break;
	case 0x01: info->ram_bytes = 0x800; break;
	case 0x02: info->ram_bytes = 0x2000; break;
	case 0x03: info->ram_bytes = 0x8000; break;
	case 0x04: info->ram_bytes = 0x20000; break;
	case 0x05: info->ram_bytes = 0x10000; break;
	default: info->ram_bytes = 0; break;
	}

	/* MBC2 always has its own 512x4-bit RAM regardless of the header. */
	if (info->mbc == MBC_2) {
		info->ram_bytes = 512;
	}
}

bool mbc_init(GBMBC *mbc, const uint8_t *rom, size_t rom_size)
{
	GBCartInfo info;

	memset(mbc, 0, sizeof(*mbc));

	if (rom_size < 0x150) {
		return false;
	}

	mbc_read_info(rom, rom_size, &info);

	mbc->rom = rom;
	mbc->rom_size = rom_size;
	mbc->type = info.mbc;
	mbc->rom_bank = 1;
	mbc->ram_enabled = false;

	if (info.mbc == MBC_2) {
		mbc->ram = mbc->mbc2_ram;
		mbc->ram_size = sizeof(mbc->mbc2_ram);
	} else if (info.ram_bytes > 0) {
		mbc->ram = calloc(1, info.ram_bytes);
		if (!mbc->ram) {
			return false;
		}
		mbc->ram_size = info.ram_bytes;
	} else {
		mbc->ram = NULL;
		mbc->ram_size = 0;
	}

	return true;
}

void mbc_free(GBMBC *mbc)
{
	if (mbc->ram && mbc->ram != mbc->mbc2_ram) {
		free(mbc->ram);
	}
	mbc->ram = NULL;
	mbc->ram_size = 0;
}

static uint32_t rom_bank_count(const GBMBC *mbc)
{
	uint32_t n = (mbc->rom_size + 0x3fff) / 0x4000;

	return n ? n : 1;
}

uint8_t mbc_read_rom(GBMBC *mbc, uint16_t addr)
{
	uint32_t bank;
	uint32_t offset;

	if (addr < 0x4000) {
		bank = 0;
		/* MBC1 mode 1 can remap the low bank. */
		if (mbc->type == MBC_1 && mbc->bank_mode_high) {
			bank = (uint32_t)(mbc->ram_bank & 0x03) << 5;
		}
		offset = bank * 0x4000 + addr;
	} else {
		bank = mbc->rom_bank;
		if (mbc->type == MBC_1 && mbc->bank_mode_high) {
			bank = (bank & 0x1f) | ((uint32_t)(mbc->ram_bank & 0x03) << 5);
		}
		offset = bank * 0x4000 + (addr - 0x4000);
	}

	/* Unmapped banks read as open bus, which in practice means 0xFF. */
	if (offset >= mbc->rom_size) {
		return 0xff;
	}
	return mbc->rom[offset];
}

void mbc_write_rom(GBMBC *mbc, uint16_t addr, uint8_t val)
{
	switch (mbc->type) {
	case MBC_NONE:
		/* Writes to ROM are ignored. */
		break;

	case MBC_1:
		if (addr < 0x2000) {
			mbc->ram_enabled = (val & 0x0f) == 0x0a;
		} else if (addr < 0x4000) {
			uint16_t bank = val & 0x1f;

			if (bank == 0) {
				bank = 1;
			}
			mbc->rom_bank = (mbc->rom_bank & 0x60) | bank;
		} else if (addr < 0x6000) {
			mbc->ram_bank = val & 0x03;
		} else {
			mbc->bank_mode_high = (val & 0x01) != 0;
		}
		break;

	case MBC_2:
		/* The low address bit selects RAM enable or ROM bank. */
		if (addr < 0x4000) {
			if (addr & 0x0100) {
				uint16_t bank = val & 0x0f;

				if (bank == 0) {
					bank = 1;
				}
				mbc->rom_bank = bank;
			} else {
				mbc->ram_enabled = (val & 0x0f) == 0x0a;
			}
		}
		break;

	case MBC_3:
		if (addr < 0x2000) {
			mbc->ram_enabled = (val & 0x0f) == 0x0a;
		} else if (addr < 0x4000) {
			uint16_t bank = val & 0x7f;

			if (bank == 0) {
				bank = 1;
			}
			mbc->rom_bank = bank;
		} else if (addr < 0x6000) {
			/* 0x00-0x07 selects RAM, 0x08-0x0C selects an RTC register. */
			mbc->ram_bank = val;
		} else {
			/* A write of 0x00 then 0x01 latches the clock. */
			if (val == 0x00) {
				mbc->rtc_latched = false;
			} else if (val == 0x01 && !mbc->rtc_latched) {
				memcpy(mbc->rtc_latch, mbc->rtc, sizeof(mbc->rtc));
				mbc->rtc_latched = true;
			}
		}
		break;

	case MBC_5:
		if (addr < 0x2000) {
			mbc->ram_enabled = (val & 0x0f) == 0x0a;
		} else if (addr < 0x3000) {
			mbc->rom_bank = (mbc->rom_bank & 0x100) | val;
		} else if (addr < 0x4000) {
			mbc->rom_bank = (mbc->rom_bank & 0xff) | ((val & 0x01) << 8);
		} else if (addr < 0x6000) {
			mbc->ram_bank = val & 0x0f;
		}
		break;
	}

	/* Keep the bank inside the cartridge. */
	uint32_t banks = rom_bank_count(mbc);

	if (mbc->rom_bank >= banks) {
		mbc->rom_bank %= banks;
		if (mbc->rom_bank == 0) {
			mbc->rom_bank = 1;
		}
	}
}

uint8_t mbc_read_ram(GBMBC *mbc, uint16_t addr)
{
	if (!mbc->ram_enabled || !mbc->ram) {
		return 0xff;
	}

	if (mbc->type == MBC_3 && mbc->ram_bank >= 0x08 && mbc->ram_bank <= 0x0c) {
		return mbc->rtc_latched ? mbc->rtc_latch[mbc->ram_bank - 0x08]
		                        : mbc->rtc[mbc->ram_bank - 0x08];
	}

	size_t offset = (size_t)mbc->ram_bank * 0x2000 + (addr - 0xa000);

	if (offset >= mbc->ram_size) {
		return 0xff;
	}

	if (mbc->type == MBC_2) {
		/* MBC2 stores four bits per address and the upper nibble reads as 1. */
		return 0xf0 | (mbc->ram[offset & 0x1ff] & 0x0f);
	}

	return mbc->ram[offset];
}

void mbc_write_ram(GBMBC *mbc, uint16_t addr, uint8_t val)
{
	if (!mbc->ram_enabled || !mbc->ram) {
		return;
	}

	if (mbc->type == MBC_3 && mbc->ram_bank >= 0x08 && mbc->ram_bank <= 0x0c) {
		mbc->rtc[mbc->ram_bank - 0x08] = val;
		return;
	}

	size_t offset = (size_t)mbc->ram_bank * 0x2000 + (addr - 0xa000);

	if (offset >= mbc->ram_size) {
		return;
	}

	if (mbc->type == MBC_2) {
		mbc->ram[offset & 0x1ff] = val & 0x0f;
		return;
	}

	mbc->ram[offset] = val;
}

void mbc_tick_rtc(GBMBC *mbc, unsigned seconds)
{
	if (mbc->type != MBC_3 || seconds == 0) {
		return;
	}

	unsigned s = mbc->rtc[0] + seconds;

	mbc->rtc[0] = s % 60;
	unsigned m = mbc->rtc[1] + s / 60;

	mbc->rtc[1] = m % 60;
	unsigned h = mbc->rtc[2] + m / 60;

	mbc->rtc[2] = h % 24;
	unsigned days = mbc->rtc[3] | ((mbc->rtc[4] & 1) << 8);

	days += h / 24;
	mbc->rtc[3] = days & 0xff;
	mbc->rtc[4] = (mbc->rtc[4] & 0xfe) | ((days >> 8) & 1);

	/* Bit 7 is the day counter carry. */
	if (days > 0x1ff) {
		mbc->rtc[4] |= 0x80;
	}
}
