#ifndef SPRUCE_IGM_ITEMS_H
#define SPRUCE_IGM_ITEMS_H

#include <stddef.h>
#include <boolean.h>

/* Every entry the menu can show. Which ones appear, and in what order, comes
 * from igm_items.json; this order is only the canonical one. */
enum spruce_igm_item_id
{
   IGM_ID_RESUME = 0,
   IGM_ID_SAVE,
   IGM_ID_LOAD,
   IGM_ID_FF_SPEED,
   IGM_ID_RESET,
   IGM_ID_RETROARCH,
   IGM_ID_EXIT,
   IGM_ID_DISC,
   IGM_ID_SCREENSHOT,
   IGM_ID_UNDO_SAVE,
   IGM_ID_UNDO_LOAD,
   IGM_ID_SHOW_FPS,
   IGM_ID_MUTE,
   IGM_ID_REWIND,
   IGM_ID_SHADER,
   IGM_ID_CHEATS,
   IGM_ID_COUNT
};

/* Rows from IGM_CHEAT_ROW up are cheat (id - IGM_CHEAT_ROW) of the cheat list. */
#define IGM_CHEAT_ROW 1000
#define IGM_MAX_ROWS  256

enum spruce_igm_press
{
   IGM_PRESS_NONE = 0,
   IGM_PRESS_SAVE,    /* run in place by the caller, menu stays open */
   IGM_PRESS_STAY,    /* queue for spruce_igm_item_run, menu stays open */
   IGM_PRESS_CLOSE,   /* close the menu, then spruce_igm_item_run */
   IGM_PRESS_CHEATS   /* show the cheat list in place of the menu */
};

struct spruce_igm_theme;

const char *spruce_igm_item_key(int id);
const char *spruce_igm_item_default_label(int id);

/* Runloop context. Fills rows with the configured items that apply to the
 * running content and returns how many; never fewer than one. */
int  spruce_igm_items_open(int *rows, int max);
/* Runloop context. Writes back only the settings the menu changed, and saves
 * the cheats if any were toggled. */
void spruce_igm_items_close(void);
/* Any context: spruce_igm_items_open already loaded the cheats. Fills rows with
 * one row per cheat and returns how many. */
int  spruce_igm_cheats_rows(int *rows, int max);

bool spruce_igm_item_has_arrows(int id);
/* True when the step must be queued for spruce_igm_item_run instead. */
bool spruce_igm_item_step(int id, int dir);
enum spruce_igm_press spruce_igm_item_press(int id);
void spruce_igm_item_run(int id);
void spruce_igm_item_label(int id, const struct spruce_igm_theme *t,
      char *buf, size_t len);

/* First row to draw so that selected stays inside a window of shown rows. */
int  spruce_igm_items_scroll(int selected, int count, int shown, int top);

#endif /* SPRUCE_IGM_ITEMS_H */
