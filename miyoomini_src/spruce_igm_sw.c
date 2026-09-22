/*
 * Spruce IGM (Software) - In-game menu for Miyoo Mini
 *
 * Software-rendered IGM for devices without a GPU.
 * Draws directly to an ARGB8888 pixel buffer using RetroArch's
 * built-in bitmap font.  Same menu structure as the GL-based IGM.
 */

#include "spruce_igm_sw.h"
#include "spruce_igm_theme.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "dingux/dingux_utils.h"
#include "retroarch.h"
#include "configuration.h"
#include "command.h"
#include "runloop.h"
#include "input/input_driver.h"
#include "menu/menu_driver.h"
#include <string/stdstring.h>
#include <formats/image.h>
#include <gfx/scaler/scaler.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ── Menu item definitions ─────────────────────────────────── */

enum spruce_igm_sw_item
{
   IGM_RESUME = 0,
   IGM_SAVE_STATE,
   IGM_LOAD_STATE,
   IGM_RESET,
   IGM_RETROARCH,
   IGM_EXIT,
   IGM_ITEM_COUNT
};

/* Colours come from the theme, as ARGB8888 for this blitter. The shadow
 * colour is not used here - this renderer has no shadow pass at all. */

typedef char igm_sw_item_count_matches[
   (IGM_ITEM_COUNT == SPRUCE_IGM_ITEM_COUNT) ? 1 : -1];

#define IGM_PCT(total, pct)  ((int)((total) * (pct) / 100.0f))

#define IGM_NO_PENDING   -1
#define IGM_FLAG_PATH    "/mnt/SDCARD/RetroArch/IGM.txt"

/* ── State ─────────────────────────────────────────────────── */

static struct
{
   bool        active;
   bool        was_paused;
   bool        needs_bg_capture;
   int         selected;
   int         pending_action;
   int         deferred_close;
   const spruce_igm_theme_t *theme;
   uint16_t    prev_buttons;
   uint32_t   *bg_capture;
   int         battery_level;
   /* Save state preview */
   uint32_t   *preview_pixels;
   unsigned    preview_w;
   unsigned    preview_h;
   int         preview_slot;
   char        state_path[8192];
} igm;

#define IGM_PREVIEW_NO_SLOT -999

/* ── Helpers ───────────────────────────────────────────────── */

static uint16_t igm_read_buttons(void)
{
   unsigned i;
   uint16_t bits = 0;
   for (i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++)
   {
      if (input_driver_state_raw_joypad(0, i))
         bits |= (1 << i);
   }
   return bits;
}

#define IGM_PRESSED(cur, prev, btn) \
   (((cur) & (1 << (btn))) && !((prev) & (1 << (btn))))

/* ── Pixel drawing helpers ─────────────────────────────────── */

static inline uint32_t blend_argb(uint32_t dst, uint32_t src)
{
   unsigned sa = (src >> 24) & 0xFF;
   unsigned sr = (src >> 16) & 0xFF;
   unsigned sg = (src >>  8) & 0xFF;
   unsigned sb =  src        & 0xFF;
   unsigned dr = (dst >> 16) & 0xFF;
   unsigned dg = (dst >>  8) & 0xFF;
   unsigned db =  dst        & 0xFF;
   unsigned ia = 255 - sa;
   unsigned or_ = (sr * sa + dr * ia) / 255;
   unsigned og  = (sg * sa + dg * ia) / 255;
   unsigned ob  = (sb * sa + db * ia) / 255;
   return 0xFF000000u | (or_ << 16) | (og << 8) | ob;
}

static void fill_rect_blend(uint32_t *buf, unsigned pitch,
      int x, int y, int w, int h,
      unsigned scr_w, unsigned scr_h, uint32_t color)
{
   int ix, iy;
   for (iy = y; iy < y + h && iy < (int)scr_h; iy++)
   {
      if (iy < 0) continue;
      for (ix = x; ix < x + w && ix < (int)scr_w; ix++)
      {
         if (ix < 0) continue;
         buf[iy * pitch + ix] = blend_argb(buf[iy * pitch + ix], color);
      }
   }
}

static void fill_rect_solid(uint32_t *buf, unsigned pitch,
      int x, int y, int w, int h,
      unsigned scr_w, unsigned scr_h, uint32_t color)
{
   int ix, iy;
   for (iy = y; iy < y + h && iy < (int)scr_h; iy++)
   {
      if (iy < 0) continue;
      for (ix = x; ix < x + w && ix < (int)scr_w; ix++)
      {
         if (ix < 0) continue;
         buf[iy * pitch + ix] = color;
      }
   }
}

/* Nearest-neighbour expansion of the bitmap font. The scale was a hardcoded 2
 * in four places; it now follows layout.scale - but only in whole steps, since
 * a bitmap glyph cannot be drawn at 2.4x. So the text tracks the panel coarsely:
 * scale 1.5 takes it from 2x to 3x, while scale 1.2 leaves it at 2x and only the
 * panel grows. There is no TTF here, so font.path and the font sizes do nothing. */
static int draw_char(uint32_t *buf, unsigned pitch,
      unsigned scr_w, unsigned scr_h,
      int x, int y, char ch, uint32_t color,
      bitmapfont_lut_t *font, int gs)
{
   int i, j, sx, sy;
   unsigned symbol = (unsigned char)ch;
   bool *lut;

   if (!font || symbol >= font->glyph_max)
      return FONT_WIDTH_STRIDE * gs;

   lut = font->lut[symbol];
   if (!lut)
      return FONT_WIDTH_STRIDE * gs;

   for (j = 0; j < FONT_HEIGHT; j++)
   {
      for (i = 0; i < FONT_WIDTH; i++)
      {
         if (!lut[i + j * FONT_WIDTH])
            continue;
         for (sy = 0; sy < gs; sy++)
         {
            int py = y + j * gs + sy;
            if (py < 0 || py >= (int)scr_h)
               continue;
            for (sx = 0; sx < gs; sx++)
            {
               int px = x + i * gs + sx;
               if (px >= 0 && px < (int)scr_w)
                  buf[py * pitch + px] = color;
            }
         }
      }
   }
   return FONT_WIDTH_STRIDE * gs;
}

static void draw_text(uint32_t *buf, unsigned pitch,
      unsigned scr_w, unsigned scr_h,
      int x, int y, const char *text, uint32_t color,
      bitmapfont_lut_t *font, int gs)
{
   while (*text)
   {
      x += draw_char(buf, pitch, scr_w, scr_h,
            x, y, *text, color, font, gs);
      text++;
   }
}

static int text_width(const char *text, int gs)
{
   return (int)strlen(text) * FONT_WIDTH_STRIDE * gs;
}

/* ── Preview texture management ────────────────────────────── */

static void igm_unload_preview(void)
{
   if (igm.preview_pixels)
   {
      free(igm.preview_pixels);
      igm.preview_pixels = NULL;
   }
   igm.preview_w    = 0;
   igm.preview_h    = 0;
   igm.preview_slot = IGM_PREVIEW_NO_SLOT;
}

static void igm_load_preview(int slot)
{
   igm.state_path[0] = '\0';
   size_t len;
   struct texture_image ti = {0};

   igm_unload_preview();

   if (!runloop_get_savestate_path(igm.state_path, sizeof(igm.state_path), slot))
      return;

   len = strlen(igm.state_path);
   snprintf(igm.state_path + len, sizeof(igm.state_path) - len, ".png");

   if (access(igm.state_path, F_OK) != 0)
      return;

   if (!image_texture_load(&ti, igm.state_path))
      return;

   igm.preview_pixels = ti.pixels;
   igm.preview_w      = ti.width;
   igm.preview_h      = ti.height;
   igm.preview_slot   = slot;
}

static time_t igm_preview_mtime = 0;
static off_t  igm_preview_size  = 0;

static void igm_update_preview(void)
{
   settings_t *settings = config_get_ptr();
   int slot = settings->ints.state_slot;
   struct stat st;
   bool reload = false;

   if (slot != igm.preview_slot || igm.state_path[0] == '\0')
      reload = true;
   else if (stat(igm.state_path, &st) == 0)
   {
      if (st.st_mtime != igm_preview_mtime || st.st_size != igm_preview_size)
         reload = true;
   }

   if (!reload)
      return;

   igm_load_preview(slot);

   if (stat(igm.state_path, &st) == 0 && st.st_size > 0)
   {
      igm_preview_mtime = st.st_mtime;
      igm_preview_size  = st.st_size;
   }
   else
   {
      igm_preview_mtime = 0;
      igm_preview_size  = 0;
   }
}

static void igm_draw_preview(uint32_t *buf, unsigned pitch,
      unsigned scr_w, unsigned scr_h,
      int area_x, int area_y, int area_w, int area_h)
{
   if (!igm.preview_pixels || igm.preview_w == 0 || igm.preview_h == 0)
      return;

   /* Calculate scaled size preserving aspect ratio */
   float scale_w = (float)area_w / (float)igm.preview_w;
   float scale_h = (float)area_h / (float)igm.preview_h;
   float scale   = (scale_w < scale_h) ? scale_w : scale_h;

   unsigned draw_w = (unsigned)(igm.preview_w * scale);
   unsigned draw_h = (unsigned)(igm.preview_h * scale);
   int draw_x = area_x + ((int)area_w - (int)draw_w) / 2;
   int draw_y = area_y + ((int)area_h - (int)draw_h) / 2;

   /* Scale and blit using scaler */
   struct scaler_ctx ctx = {0};
   ctx.in_width   = igm.preview_w;
   ctx.in_height  = igm.preview_h;
   ctx.in_stride  = igm.preview_w * sizeof(uint32_t);
   ctx.in_fmt     = SCALER_FMT_ARGB8888;
   ctx.out_width  = draw_w;
   ctx.out_height = draw_h;
   ctx.out_stride = draw_w * sizeof(uint32_t);
   ctx.out_fmt    = SCALER_FMT_ARGB8888;
   ctx.scaler_type = SCALER_TYPE_BILINEAR;

   if (!scaler_ctx_gen_filter(&ctx))
      return;

   /* Scale to temp buffer then copy to framebuffer */
   uint32_t *scaled = (uint32_t *)malloc(draw_w * draw_h * sizeof(uint32_t));
   if (!scaled)
   {
      scaler_ctx_gen_reset(&ctx);
      return;
   }

   scaler_ctx_scale(&ctx, scaled, igm.preview_pixels);
   scaler_ctx_gen_reset(&ctx);

   /* Blit scaled preview to framebuffer */
   unsigned ix, iy;
   for (iy = 0; iy < draw_h; iy++)
   {
      int dy = draw_y + (int)iy;
      if (dy < 0 || dy >= (int)scr_h) continue;
      for (ix = 0; ix < draw_w; ix++)
      {
         int dx = draw_x + (int)ix;
         if (dx < 0 || dx >= (int)scr_w) continue;
         buf[dy * pitch + dx] = scaled[iy * draw_w + ix];
      }
   }

   free(scaled);
}

/* ── Enable check ──────────────────────────────────────────── */

bool spruce_igm_sw_is_enabled(void)
{
   return access(IGM_FLAG_PATH, F_OK) == 0;
}

/* ── Toggle ────────────────────────────────────────────────── */

void spruce_igm_sw_toggle(void)
{
   if (igm.active)
   {
      igm.active         = false;
      igm.pending_action = IGM_NO_PENDING;

      if (igm.bg_capture)
      {
         free(igm.bg_capture);
         igm.bg_capture = NULL;
      }
      igm_unload_preview();

      if (!igm.was_paused)
         command_event(CMD_EVENT_UNPAUSE, NULL);

      menu_state_get_ptr()->input_driver_flushing_input = 2;
   }
   else
   {
      runloop_state_t *runloop_st = runloop_state_get_ptr();
      /* Runloop context - safe to touch the card here. Self-latching. */
      igm.theme            = spruce_igm_theme_get();
      igm.was_paused       = (runloop_st->flags & RUNLOOP_FLAG_PAUSED) != 0;
      igm.active           = true;
      igm.selected         = 0;
      igm.pending_action   = IGM_NO_PENDING;
      igm.deferred_close   = IGM_NO_PENDING;
      igm.prev_buttons     = 0xFFFF;
      igm.needs_bg_capture = true;
      igm.preview_slot     = IGM_PREVIEW_NO_SLOT;
      igm.state_path[0]    = '\0';
      igm.battery_level    = dingux_get_battery_level();

      if (!igm.was_paused)
         command_event(CMD_EVENT_PAUSE, NULL);
   }
}

bool spruce_igm_sw_is_active(void)
{
   return igm.active;
}

/* ── Deferred action processing ────────────────────────────── */

void spruce_igm_sw_process_pending(void)
{
   int action;

   if (igm.pending_action == IGM_NO_PENDING)
      return;

   action             = igm.pending_action;
   igm.pending_action = IGM_NO_PENDING;
   igm.active         = false;

   if (igm.bg_capture)
   {
      free(igm.bg_capture);
      igm.bg_capture = NULL;
   }
   igm_unload_preview();

   if (!igm.was_paused)
      command_event(CMD_EVENT_UNPAUSE, NULL);

   menu_state_get_ptr()->input_driver_flushing_input = 2;

   switch (action)
   {
      case IGM_RESUME:
         break;
      case IGM_LOAD_STATE:
         command_event(CMD_EVENT_LOAD_STATE, NULL);
         runloop_state_get_ptr()->run_frames_and_pause = 0;
         break;
      case IGM_RESET:
         command_event(CMD_EVENT_RESET, NULL);
         break;
      case IGM_RETROARCH:
         command_event(CMD_EVENT_MENU_TOGGLE, NULL);
         break;
      case IGM_EXIT:
         command_event(CMD_EVENT_QUIT, NULL);
         break;
   }
}

/* ── Input handling ────────────────────────────────────────── */

static bool igm_handle_input(uint32_t *draw_buf,
      unsigned width, unsigned height)
{
   uint16_t cur  = igm_read_buttons();
   uint16_t prev = igm.prev_buttons;
   igm.prev_buttons = cur;

   if (igm.deferred_close != IGM_NO_PENDING)
   {
      if (!(cur & ((1 << RETRO_DEVICE_ID_JOYPAD_A)
                  | (1 << RETRO_DEVICE_ID_JOYPAD_B))))
      {
         igm.pending_action = igm.deferred_close;
         igm.deferred_close = IGM_NO_PENDING;
      }
      for (unsigned px = 0; px < width * height; px++)
         draw_buf[px] = 0xFF000000u;
      return false;
   }

   if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_UP))
   {
      igm.selected--;
      if (igm.selected < 0)
         igm.selected = IGM_ITEM_COUNT - 1;
   }

   if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_DOWN))
   {
      igm.selected++;
      if (igm.selected >= IGM_ITEM_COUNT)
         igm.selected = 0;
   }

   if (igm.selected == IGM_SAVE_STATE || igm.selected == IGM_LOAD_STATE)
   {
      if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_LEFT))
         command_event(CMD_EVENT_SAVE_STATE_DECREMENT, NULL);
      if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_RIGHT))
         command_event(CMD_EVENT_SAVE_STATE_INCREMENT, NULL);
   }

   if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_B))
   {
      igm.deferred_close = IGM_RESUME;
      for (unsigned px = 0; px < width * height; px++)
         draw_buf[px] = 0xFF000000u;
      return false;
   }

   if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_A))
   {
      switch (igm.selected)
      {
         case IGM_SAVE_STATE:
            command_event(CMD_EVENT_SAVE_STATE, NULL);
            igm.preview_slot = IGM_PREVIEW_NO_SLOT;
            break;
         case IGM_RESUME:
            igm.deferred_close = igm.selected;
            for (unsigned px = 0; px < width * height; px++)
               draw_buf[px] = 0xFF000000u;
            return false;
         default:
            igm.deferred_close = igm.selected;
            break;
      }
   }

   return true;
}

/* ── Rendering ─────────────────────────────────────────────── */

void spruce_igm_sw_frame(uint32_t *draw_buf, const uint32_t *front_buf,
      unsigned width, unsigned height,
      unsigned pitch, bitmapfont_lut_t *font)
{
   int i;
   char slot_buf[64];
   settings_t *settings;
   const spruce_igm_theme_t *t;

   if (!igm.active)
      return;

   t = igm.theme ? igm.theme : spruce_igm_theme_get();

   /* Capture background on first frame */
   if (igm.needs_bg_capture)
   {
      size_t sz = width * height * sizeof(uint32_t);
      if (!igm.bg_capture)
         igm.bg_capture = (uint32_t *)malloc(sz);
      if (igm.bg_capture && front_buf)
         memcpy(igm.bg_capture, front_buf, sz);
      igm.needs_bg_capture = false;
   }

   /* Handle input */
   if (!igm_handle_input(draw_buf, width, height))
      return;

   /* Update preview if slot changed. Skipped when the theme hides it -
    * decoding a PNG and running the scaler for something nobody draws is pure
    * cost, and this is the weakest device we ship on. */
   if (t->show_preview)
      igm_update_preview();

   settings = config_get_ptr();

   /* ── Layout constants ────────────────────────────── */
   double s     = t->scale;
   int gs       = (int)(2.0f * t->scale);
   int margin   = IGM_PCT(width,  t->margin_pct)  * s;
   int panel_w  = IGM_PCT(width,  t->panel_w_pct) * s;
   int item_h   = IGM_PCT(height, t->item_h_pct)  * s;
   int title_h  = IGM_PCT(height, t->title_h_pct) * s;
   int panel_h  = item_h * IGM_ITEM_COUNT + title_h;
   /* Sizes are scaled by s; explicit positions deliberately are not - see the
    * GPU copy for why. */
   int panel_x  = t->panel_x_auto ? margin : IGM_PCT(width, t->panel_x_pct);
   int panel_y  = t->panel_y_auto ? (height - panel_h) / 2
                  : IGM_PCT(height, t->panel_y_pct);
   int text_cx  = panel_x + panel_w / 2;
   int sep_in   = IGM_PCT(panel_w, t->separator_inset_pct);
   int sep_w    = panel_w - IGM_PCT(panel_w, t->separator_inset_pct * 2.0f);
   int arrow_in = IGM_PCT(panel_w, t->arrow_inset_pct);
   int accent_w = IGM_PCT(panel_w, t->accent_w_pct);
   uint32_t col_text  = spruce_igm_theme_color_argb(t, IGM_COL_TEXT);
   uint32_t col_sel   = spruce_igm_theme_color_argb(t, IGM_COL_TEXT_SELECTED);
   uint32_t col_title = spruce_igm_theme_color_argb(t, IGM_COL_TEXT_TITLE);
   uint32_t col_batt  = spruce_igm_theme_color_argb(t, IGM_COL_BATTERY);
   uint32_t dim       = spruce_igm_theme_color_rgba(t, IGM_COL_DIM_BG);
   int glyph_h;
   if (gs < 1) gs = 1;
   glyph_h = FONT_HEIGHT * gs;
   if (accent_w < 2) accent_w = 2;

   /* ── Draw dimmed background ──────────────────────── */
   if (igm.bg_capture)
   {
      /* Blend the theme's dim colour over the captured frame, rather than the
       * fixed 52/256 darkening this used to do. That also brings the Mini into
       * line with every other device, which has always dimmed by alpha - the
       * two were never quite the same brightness before. */
      unsigned px;
      unsigned da = dim & 0xFF;
      unsigned dr = (dim >> 24) & 0xFF;
      unsigned dg = (dim >> 16) & 0xFF;
      unsigned db = (dim >>  8) & 0xFF;
      unsigned inv = 255 - da;
      uint32_t total_pixels = width * height;
      for (px = 0; px < total_pixels; px++)
      {
         uint32_t c = igm.bg_capture[px];
         unsigned r = (((c >> 16) & 0xFF) * inv + dr * da) / 255;
         unsigned g = (((c >>  8) & 0xFF) * inv + dg * da) / 255;
         unsigned b = (( c        & 0xFF) * inv + db * da) / 255;
         uint32_t target_idx = total_pixels - 1 - px;
         draw_buf[target_idx] = 0xFF000000u | (r << 16) | (g << 8) | b;
      }
   }
   else
      memset(draw_buf, 0, width * height * sizeof(uint32_t));

   /* ── Panel background ────────────────────────────── */
   if (spruce_igm_theme_color_rgba(t, IGM_COL_PANEL_BG) & 0xFF)
      fill_rect_blend(draw_buf, pitch, panel_x, panel_y, panel_w, panel_h,
            width, height,
            spruce_igm_theme_color_argb(t, IGM_COL_PANEL_BG));

   /* ── Battery percentage (top-right) ─────────────── */
   if (t->show_battery && igm.battery_level >= 0)
   {
      char batt_buf[8];
      int batt_tw, batt_x, batt_y;
      snprintf(batt_buf, sizeof(batt_buf), "%d%%", igm.battery_level);
      batt_tw = text_width(batt_buf, gs);
      batt_x  = (int)width - margin - batt_tw;
      batt_y  = margin;
      draw_text(draw_buf, pitch, width, height,
            batt_x, batt_y, batt_buf, col_batt, font, gs);
   }

   /* ── Title ───────────────────────────────────────── */
   if (!string_is_empty(t->title))
   {
      int tw = text_width(t->title, gs);
      int tx = text_cx - tw / 2;
      int ty = panel_y + (title_h - glyph_h) / 2;
      draw_text(draw_buf, pitch, width, height,
            tx, ty, t->title, col_title, font, gs);
   }

   /* Thin line under title */
   if (t->separator_h_px > 0)
   {
      int ly = panel_y + title_h - t->separator_h_px;
      fill_rect_blend(draw_buf, pitch, panel_x + sep_in, ly,
            sep_w, t->separator_h_px, width, height,
            spruce_igm_theme_color_argb(t, IGM_COL_TITLE_LINE));
   }

   /* ── Menu items ──────────────────────────────────── */
   for (i = 0; i < IGM_ITEM_COUNT; i++)
   {
      int iy        = panel_y + title_h + i * item_h;
      bool selected = (i == igm.selected);
      const char *label;
      uint32_t text_col;
      int tw, tx, ty;

      /* Selection: row fill, plus the accent bar if the theme wants it */
      if (selected)
      {
         fill_rect_blend(draw_buf, pitch,
               panel_x, iy, panel_w, item_h,
               width, height,
               spruce_igm_theme_color_argb(t, IGM_COL_SELECTION));
         if (t->accent_bar)
            fill_rect_blend(draw_buf, pitch,
                  panel_x, iy, accent_w, item_h,
                  width, height,
                  spruce_igm_theme_color_argb(t, IGM_COL_ACCENT));
      }

      /* Label text */
      if (i == IGM_SAVE_STATE || i == IGM_LOAD_STATE)
      {
         int slot = settings->ints.state_slot;
         if (slot < 0)
            snprintf(slot_buf, sizeof(slot_buf), "%s %s",
                  t->labels[i], t->auto_label);
         else
            snprintf(slot_buf, sizeof(slot_buf), "%s %s %d",
                  t->labels[i], t->slot_label, slot);
         label = slot_buf;
      }
      else
         label = t->labels[i];

      text_col = selected ? col_sel : col_text;
      tw = text_width(label, gs);
      tx = text_cx - tw / 2;
      ty = iy + (item_h - glyph_h) / 2;

      draw_text(draw_buf, pitch, width, height,
            tx, ty, label, text_col, font, gs);

      /* Arrows for Save/Load rows */
      if (i == IGM_SAVE_STATE || i == IGM_LOAD_STATE)
      {
         int arrow_y = iy + (item_h - glyph_h) / 2;
         draw_text(draw_buf, pitch, width, height,
               panel_x + arrow_in, arrow_y, t->arrow_left,
               selected ? col_sel : col_text, font, gs);
         draw_text(draw_buf, pitch, width, height,
               panel_x + panel_w - arrow_in - text_width(t->arrow_right, gs),
               arrow_y, t->arrow_right,
               selected ? col_sel : col_text, font, gs);
      }
   }

   /* ── Save state preview (right of panel) ───────────── */
   if (t->show_preview && igm.preview_pixels)
   {
      int preview_area_x = panel_x + panel_w + margin;
      int preview_area_w = (int)width - preview_area_x - margin;
      int preview_area_h = panel_h;

      if (preview_area_w > 0)
         igm_draw_preview(draw_buf, pitch, width, height,
               preview_area_x, panel_y, preview_area_w, preview_area_h);
   }
}
