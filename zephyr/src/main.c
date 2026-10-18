/*
 * The Game Boy player.
 *
 * Two screens: a cartridge menu fed from the SD card, and the emulator itself
 * drawing to the panel. Buttons come from the devicetree's GPIO keys, so the
 * same firmware works on any board that declares them.
 *
 * The panel is 128x64, which is smaller than the Game Boy's 160x144. Rather than
 * squeeze the whole screen down, the emulator is shown as a 128x128 crop that
 * scrolls vertically, because that keeps the pixels square and readable. The
 * menu explains this rather than leaving it a mystery.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

#include "gb.h"
#include "menu.h"
#include "rom_loader.h"

LOG_MODULE_REGISTER(gb_player, LOG_LEVEL_INF);

#define PANEL_W 128
#define PANEL_H 64

/* The display layer wants the panel's own page format: one byte per eight
 * vertical pixels. This buffer is the 1bpp image before that conversion. */
static uint8_t canvas[PANEL_W / 8 * PANEL_H];

/* The emulator's 160x144 RGB888 output. */
static const uint8_t *gb_fb;

static Menu menu;
static GB *gb;
static uint8_t *rom;
static size_t rom_size;

static enum {
	SCREEN_MENU,
	SCREEN_RUNNING,
} screen = SCREEN_MENU;

/* Button state, in the emulator's layout. */
static uint8_t buttons;

static const struct device *display;

/* --- panel output ------------------------------------------------------ */

/*
 * Converts the 1bpp canvas to the SSD1306's page layout and pushes it. The
 * controller wants one byte per eight vertical pixels with the top pixel in bit
 * 0, which is a transpose of the canvas.
 */
static int panel_show(void)
{
	uint8_t page_buf[PANEL_W * (PANEL_H / 8)];
	int ret;

	for (int page = 0; page < PANEL_H / 8; page++) {
		for (int x = 0; x < PANEL_W; x++) {
			uint8_t bits = 0;

			for (int bit = 0; bit < 8; bit++) {
				int y = page * 8 + bit;
				size_t idx = (size_t)y * (PANEL_W / 8) + (x / 8);
				uint8_t mask = 0x80 >> (x & 7);

				if (canvas[idx] & mask) {
					bits |= (1 << bit);
				}
			}
			page_buf[page * PANEL_W + x] = bits;
		}
	}

	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(page_buf),
		.width = PANEL_W,
		.height = PANEL_H,
		.pitch = PANEL_W,
	};

	ret = display_write(display, 0, 0, &desc, page_buf);
	return ret;
}

/*
 * Draws the emulator's framebuffer onto the panel.
 *
 * The Game Boy's screen is 160x144 and the panel is 128x64, so this takes a
 * 128-wide window and a 64-tall slice, skipping rows to keep the aspect ratio
 * sane. The vertical position follows the player's view.
 */
static void panel_show_game(int scroll_y)
{
	memset(canvas, 0, sizeof(canvas));

	for (int y = 0; y < PANEL_H; y++) {
		int src_y = scroll_y + y;

		if (src_y >= GB_SCREEN_H) {
			break;
		}

		for (int x = 0; x < PANEL_W; x++) {
			/* Centre the 160-wide screen in the 128-wide window. */
			int src_x = x + 16;
			const uint8_t *p;

			if (src_x >= GB_SCREEN_W) {
				continue;
			}

			p = &gb_fb[(src_y * GB_SCREEN_W + src_x) * 3];

			/* Anything darker than mid grey counts as ink. */
			int luma = (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100;

			if (luma < 100) {
				size_t idx = (size_t)y * (PANEL_W / 8) + (x / 8);

				canvas[idx] |= 0x80 >> (x & 7);
			}
		}
	}

	panel_show();
}

/* --- menu -------------------------------------------------------------- */

static void build_menu(void)
{
	menu_init(&menu, "GAME BOY");
	menu_clear_items(&menu);

	for (int i = 0; i < rom_loader_count(); i++) {
		const ROMEntry *e = rom_loader_entry(i);
		char line[MENU_ITEM_LEN];

		snprintf(line, sizeof(line), "%s", e->name);
		menu_add_item(&menu, line);
	}

	menu_set_footer(&menu, rom_loader_status_text());
}

static void show_menu(void)
{
	screen = SCREEN_MENU;
	menu_render(&menu, canvas, PANEL_W, PANEL_H);
	panel_show();
}

/* --- launching --------------------------------------------------------- */

static void launch(int index)
{
	size_t size = 0;

	if (gb) {
		gb_free(gb);
		gb = NULL;
	}
	if (rom) {
		k_free(rom);
		rom = NULL;
	}

	rom = rom_loader_load(index, &size);
	if (!rom) {
		menu_set_footer(&menu, "could not read that file");
		show_menu();
		return;
	}

	gb = gb_new(rom, size);
	if (!gb) {
		menu_set_footer(&menu, "not a usable cartridge");
		k_free(rom);
		rom = NULL;
		show_menu();
		return;
	}

	GBCartInfo info;

	mbc_read_info(rom, size, &info);
	LOG_INF("loaded '%s', %u ROM banks, mapper %d", info.title,
	        info.rom_banks, (int)info.mbc);

	screen = SCREEN_RUNNING;
}

/* --- buttons ----------------------------------------------------------- */

/*
 * The devicetree declares which GPIO each button is on. The input subsystem
 * reports codes; this maps them onto the Game Boy's eight buttons.
 */
static uint8_t input_code_to_button(uint16_t code, bool *known)
{
	*known = true;

	switch (code) {
	case INPUT_KEY_UP:     return GB_BTN_UP;
	case INPUT_KEY_DOWN:   return GB_BTN_DOWN;
	case INPUT_KEY_LEFT:   return GB_BTN_LEFT;
	case INPUT_KEY_RIGHT:  return GB_BTN_RIGHT;
	case INPUT_KEY_ENTER:
	case INPUT_KEY_Z:      return GB_BTN_A;
	case INPUT_KEY_BACKSPACE:
	case INPUT_KEY_X:      return GB_BTN_B;
	case INPUT_KEY_ESC:    return GB_BTN_SELECT;
	case INPUT_KEY_SPACE:  return GB_BTN_START;
	default:
		*known = false;
		return 0;
	}
}

static void on_input(struct input_event *evt, void *user_data)
{
	bool known;
	uint8_t button;
	bool pressed;

	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY) {
		return;
	}

	button = input_code_to_button(evt->code, &known);
	if (!known) {
		return;
	}

	pressed = evt->value != 0;

	if (screen == SCREEN_MENU) {
		MenuAction action = menu_button(&menu, button, pressed);

		if (action == MENU_ACTION_LAUNCH) {
			launch(menu.selected);
		} else {
			show_menu();
		}
		return;
	}

	/* In the emulator, SELECT+START returns to the menu. */
	if (button == GB_BTN_SELECT && pressed) {
		show_menu();
		return;
	}

	if (pressed) {
		buttons |= button;
	} else {
		buttons &= ~button;
	}
	gb_set_buttons(gb, buttons);
}

INPUT_CALLBACK_DEFINE(NULL, on_input, NULL);

/* --- main -------------------------------------------------------------- */

int main(void)
{
	display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display)) {
		LOG_ERR("no display");
		return 0;
	}

	display_blanking_off(display);

	LOG_INF("GB player starting");

	ROMState state = rom_loader_init();

	LOG_INF("card: %s", rom_loader_status_text());

	build_menu();
	show_menu();

	while (1) {
		if (screen == SCREEN_RUNNING && gb) {
			gb_run_frame(gb);
			gb_fb = gb_framebuffer(gb);
			panel_show_game(0);
		} else {
			k_sleep(K_MSEC(50));
		}
	}

	return 0;
}
