#ifndef SPRUCE_IGM_PLATFORM_H
#define SPRUCE_IGM_PLATFORM_H

/*
 * Everything that differs between the GPU IGM builds.
 *
 * spruce_igm.c is identical across the common, pixel2 and rgds patches; the
 * build script selects a variant with -DIGM_VARIANT_PIXEL2 or -DIGM_VARIANT_RGDS
 * and common is the default.
 *
 * IGM_LAYOUT_SCALE is a double and IGM_FONT_SCALE a float on purpose - that is
 * what the pixel2/rgds sources used, and the two round differently either side
 * of a truncation: (1280*2/100) * 1.2 is 29, the same expression with 1.2f is 30.
 */

#if defined(IGM_VARIANT_RGDS)
#  define IGM_BASE_DIR      "/storage/RetroArch"
#  define IGM_FONT_PRIMARY  "/storage/RetroArch/nunwen.ttf"
#  define IGM_FONT_FALLBACK "/storage/RetroArch/nunwen.ttf"
#  define IGM_LAYOUT_SCALE  1.2
#  define IGM_FONT_SCALE    1.2f
#  define IGM_BATTERY_SMALL 0
#elif defined(IGM_VARIANT_PIXEL2)
#  define IGM_BASE_DIR      "/mnt/SDCARD/RetroArch"
#  define IGM_FONT_PRIMARY  "/mnt/SDCARD/Themes/SPRUCE/nunwen.ttf"
#  define IGM_FONT_FALLBACK "/mnt/SDCARD/App/PixelReader/resources/fonts/nunwen.ttf"
#  define IGM_LAYOUT_SCALE  1.2
#  define IGM_FONT_SCALE    1.2f
#  define IGM_BATTERY_SMALL 0
#else
#  define IGM_BASE_DIR      "/mnt/SDCARD/RetroArch"
#  define IGM_FONT_PRIMARY  "/mnt/SDCARD/Themes/SPRUCE/nunwen.ttf"
#  define IGM_FONT_FALLBACK "/mnt/SDCARD/App/PixelReader/resources/fonts/nunwen.ttf"
#  define IGM_LAYOUT_SCALE  1.0
#  define IGM_FONT_SCALE    1.0f
#  define IGM_BATTERY_SMALL 1
#endif

#define IGM_FLAG_PATH IGM_BASE_DIR "/IGM.txt"

#endif /* SPRUCE_IGM_PLATFORM_H */
