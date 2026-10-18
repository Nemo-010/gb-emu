/*
 * The menu.
 *
 * A 128x64 monochrome panel has room for a title, six entries and a footer, so
 * that is exactly what this draws. Everything is one pixel wide so it stays
 * legible on an OLED.
 *
 * The menu is drawn into an 8-bit framebuffer that the display layer converts to
 * the panel's page format, so the same code works on any monochrome panel the
 * board has.
 */

#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include <stdbool.h>

#define MENU_MAX_ITEMS 24
#define MENU_TITLE_LEN 24
#define MENU_ITEM_LEN 24
#define MENU_VISIBLE 6

typedef enum {
	MENU_ACTION_NONE = 0,
	MENU_ACTION_LAUNCH,   /* the selected entry was chosen */
	MENU_ACTION_RESET,    /* the emulator asked to reset */
} MenuAction;

typedef struct {
	char title[MENU_TITLE_LEN];
	char items[MENU_MAX_ITEMS][MENU_ITEM_LEN];
	int item_count;
	int selected;
	int scroll;
	/* Shown under the list: how the card is doing. */
	char footer[MENU_ITEM_LEN * 2];
} Menu;

void menu_init(Menu *menu, const char *title);
void menu_clear_items(Menu *menu);
void menu_add_item(Menu *menu, const char *text);
void menu_set_footer(Menu *menu, const char *text);

/* Button handling. Returns the action the press implies, if any. */
MenuAction menu_button(Menu *menu, uint8_t button, bool pressed);

/* Draws into a 1-bit-per-pixel framebuffer, 128x64. */
void menu_render(const Menu *menu, uint8_t *fb, int width, int height);

#endif /* MENU_H */
