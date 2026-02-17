#ifndef CONFIG_H
#define CONFIG_H

/* Build config */
#define AMP_VERSION 0x010000 /* 1.0.0 */
#define CC "gcc"
#define CFLAGS "-std=c99", "-Wno-cast-function-type", "-Wall", "-Wextra"
#define SAVE_FILE_MAGIC 0x41504D56 /* 'APMV' */

#define AMP_FLASH_DEBUG_DEFAULT 0
#define AMP_FLASH_DEBUG_LEVEL_DEFAULT NOB_INFO

#define MAX_HISTORY 100
#define MAX_RECENT 5
#define CHECK_FILE_SIGNATURE 1
#define INITIAL_WINDOW_WIDTH 960
#define INITIAL_WINDOW_HEIGHT 540
#define SAVE_FILE 1
#define SAVE_FILE_PATH "amp_save.dat"
#define HASH_SIZE 256

/* Menu dimensions */
#define MENU_DROPDOWN_ITEM_HEIGHT 28
#define MENU_DROPDOWN_WIDTH 230
#define MENU_DROPDOWN_SCROLLBAR_WIDTH 12
#define MENU_DROPDOWN_TEXT_PADDING_X 8
#define MENU_DROPDOWN_TEXT_PADDING_Y 2
#define MENU_MAX_VISIBLE_ITEMS 10

/* Timeline dimensions */
#define TIMELINE_HEIGHT 6
#define TIMELINE_HITBOX_PADDING 12

/* Platform-specific fonts */
#ifdef _WIN32

#define PLATFORM_FONTS \
    { "Segoe UI", "C:/Windows/Fonts/segoeui.ttf" }, \
    { "Consolas", "C:/Windows/Fonts/consola.ttf" }

#elif defined(__APPLE__)

#define PLATFORM_FONTS \
    { "Arial", "/Library/Fonts/Arial.ttf" }, \
    { "Monaco", "/Library/Fonts/Monaco.ttf" }

#else

#define PLATFORM_FONTS \
    { "DejaVu Sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" }, \
    { "DejaVu Mono", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf" }

#endif

/* Default fonts map */
#define DEFAULT_FONTS_MAP { \
    { "Iosevka (bundled)", "assets/Iosevka-Regular.ttc" }, \
    PLATFORM_FONTS \
}

#endif /* CONFIG_H */