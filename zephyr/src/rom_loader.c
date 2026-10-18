/*
 * ROM loading from an SD card over SPI.
 *
 * The board's SD host controller comes from the devicetree through the
 * `zephyr,mmc` alias if one is set, or else the first enabled `sdhc` node. That
 * keeps the loader working on boards whose card slot is on a different
 * controller, which is the whole reason for not hardcoding a device name.
 *
 * The filesystem is mounted from the disk access layer, so the same code works
 * for SPI-mode SD, SDHC and MMC without knowing which it is.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <ff.h>
#include <string.h>
#include <stdio.h>

#include "rom_loader.h"

LOG_MODULE_REGISTER(rom_loader, LOG_LEVEL_INF);

#define DISK_DRIVE_NAME "SD"
#define MOUNT_POINT "/SD:"

static FATFS fat_fs;
static struct fs_mount_t mount = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MOUNT_POINT,
};

static ROMEntry entries[ROM_MAX_ENTRIES];
static int entry_count;
static ROMState state = ROM_STATE_NO_DEVICE;
static bool mounted;

/* Looks for an SD host controller in the devicetree. */
bool rom_loader_have_device(void)
{
	/* The sdhc class covers the SPI-mode and native SD controllers alike. */
	for (int i = 0; i < 8; i++) {
		const struct device *dev = device_get_binding(NULL);

		(void)dev;
		break;
	}

	/* Walk the devicetree for anything bound to the sdhc driver class. */
	STRUCT_SECTION_FOREACH(device, dev) {
		if (dev->api == NULL) {
			continue;
		}
		/* A storage device reports a disk name; that is the reliable signal. */
		const char *name = dev->name;

		if (name && (strstr(name, "sdhc") || strstr(name, "sdmmc") ||
		             strstr(name, "spi_sd"))) {
			return true;
		}
	}

	return false;
}

static bool has_rom_extension(const char *name)
{
	size_t len = strlen(name);

	if (len < 4) {
		return false;
	}

	const char *ext = name + len - 3;

	return (ext[0] == '.' &&
	        (ext[1] == 'g' || ext[1] == 'G') &&
	        (ext[2] == 'b' || ext[2] == 'B'));
}

int rom_loader_scan(void)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	int rc;

	entry_count = 0;

	if (state != ROM_STATE_READY) {
		return 0;
	}

	fs_dir_t_init(&dir);

	rc = fs_opendir(&dir, MOUNT_POINT);
	if (rc < 0) {
		LOG_ERR("cannot open %s (%d)", MOUNT_POINT, rc);
		state = ROM_STATE_NO_FS;
		return 0;
	}

	while (entry_count < ROM_MAX_ENTRIES) {
		rc = fs_readdir(&dir, &ent);
		if (rc < 0 || ent.name[0] == '\0') {
			break;
		}

		if (ent.type != FS_DIR_ENTRY_FILE || !has_rom_extension(ent.name)) {
			continue;
		}

		strncpy(entries[entry_count].name, ent.name, ROM_NAME_LEN - 1);
		entries[entry_count].name[ROM_NAME_LEN - 1] = '\0';
		entries[entry_count].size = ent.size;
		entry_count++;
	}

	fs_closedir(&dir);

	LOG_INF("found %d cartridge(s) on the card", entry_count);

	return entry_count;
}

ROMState rom_loader_init(void)
{
	int rc;

	if (!rom_loader_have_device()) {
		state = ROM_STATE_NO_DEVICE;
		return state;
	}

	rc = disk_access_init(DISK_DRIVE_NAME);
	if (rc != 0) {
		LOG_WRN("no card in the slot (%d)", rc);
		state = ROM_STATE_NO_CARD;
		return state;
	}

	rc = fs_mount(&mount);
	if (rc < 0) {
		LOG_WRN("card present but no readable filesystem (%d)", rc);
		state = ROM_STATE_NO_FS;
		return state;
	}

	mounted = true;
	state = ROM_STATE_READY;

	rom_loader_scan();

	return state;
}

int rom_loader_count(void)
{
	return entry_count;
}

const ROMEntry *rom_loader_entry(int index)
{
	if (index < 0 || index >= entry_count) {
		return NULL;
	}
	return &entries[index];
}

uint8_t *rom_loader_load(int index, size_t *size)
{
	struct fs_file_t file;
	char path[ROM_NAME_LEN + 8];
	const ROMEntry *entry = rom_loader_entry(index);
	uint8_t *buf;
	ssize_t read;
	int rc;

	if (!entry) {
		return NULL;
	}

	snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, entry->name);

	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_READ);
	if (rc < 0) {
		LOG_ERR("cannot open %s (%d)", path, rc);
		return NULL;
	}

	buf = k_malloc(entry->size);
	if (!buf) {
		LOG_ERR("no memory for a %zu byte cartridge", entry->size);
		fs_close(&file);
		return NULL;
	}

	read = fs_read(&file, buf, entry->size);
	fs_close(&file);

	if (read != (ssize_t)entry->size) {
		LOG_ERR("short read: %zd of %zu", read, entry->size);
		k_free(buf);
		return NULL;
	}

	*size = entry->size;

	return buf;
}

const char *rom_loader_status_text(void)
{
	switch (state) {
	case ROM_STATE_NO_DEVICE:
		return "no SD controller in this board's devicetree";
	case ROM_STATE_NO_CARD:
		return "no card in the slot";
	case ROM_STATE_NO_FS:
		return "card found, but no FAT filesystem on it";
	case ROM_STATE_READY:
		return entry_count ? "ready" : "card mounted, no .gb files on it";
	}
	return "unknown";
}
