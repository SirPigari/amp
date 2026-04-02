#ifndef CONFIG_H
#define CONFIG_H

/* Build config */
#define AMP_VERSION 0x010300 /* 1.3.0 */

#define THEMES_DIR "assets/themes/"
#define USE_THEMES 0
#define THEME "DEFAULT_THEME"

#define CC "gcc"
#define CFLAGS         "-Wno-cast-function-type", "-Wall", "-Wextra", "-D"THEME
#define RELEASE_CFLAGS "-Wno-cast-function-type", "-Wall", "-Wextra", "-D"THEME, "-O3"
#define SAVE_FILE_MAGIC 0x414D5056 /* 'AMPV' */
#define OUT_EXE_NAME "main"

#define AMP_FLASH_DEBUG_DEFAULT 0
#define AMP_FLASH_DEBUG_LEVEL_DEFAULT NOB_INFO

#define MAX_HISTORY 100
#define MAX_RECENT 5
#define CHECK_FILE_SIGNATURE 1
#define INITIAL_WINDOW_WIDTH 960
#define INITIAL_WINDOW_HEIGHT 540
#define SAVE_FILE 1
#define SAVE_FILE_PATH "./amp_save.dat"
#define HW_CACHE_SIZE 16
#define HASH_SIZE 256
#define USE_SAVE_IN_SAVE_FILE 0
#define MAX_BOOKMARKS_PER_FILE 64
#define BOOKMARK_NAME_MAX 64
#define TEXT_INPUT_MAX_LEN 256
#define BM_OVERRIDE_DEFAULT_COLOR 0

/* Demuxer settings */
#define DEMUX_MAX_READS_PER_TICK 64
#define DEMUX_MIN_READS_PER_TICK 4
#define DEMUX_TIME_BUDGET_MS 2.0
#define AMP_FF_PROBE_SIZE "4M"
#define AMP_FF_ANALYZE_DURATION_US "1500000"

/* Theme, this is a template and the default theme
   each theme MUST define all of these, even if they are the same as the default */
#if defined(DEFAULT_THEME) || !USE_THEMES
#define THEME_NAME "AMP"
#define THEME_FILE "config.h"

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
#define TIMELINE_THUMB_SIZE 12
#define TIMELINE_CHAPTER_MARKER_WIDTH 4
#define TIMELINE_CHAPTER_MARKER_HITBOX_WIDTH 6
#define TIMELINE_BOOKMARK_MARKER_WIDTH 4
#define TIMELINE_BOOKMARK_MARKER_HITBOX_WIDTH 6

/* Hamburger dimensions */
#define HAMBURGER_LINE_HEIGHT 2
#define HAMBURGER_LINE_MARGIN 6

#define SHADOW_OFFSET 2
#define DEFAULT_BOOKMARK_COLOR 0x44A0FF

/* Colors */
/*      Format is                 R,   G,   B */
#define PANEL_COLOR               20,  20,  24
#define TEXT_COLOR                230, 230, 235
#define ACCENT_COLOR              68,  160, 255
#define MUTED_COLOR               120, 120, 130
#define SHADOW_COLOR              0,   0,   0
#define LETTERBOX_COLOR           0,   0,   0
#define OVERLAY_COLOR             70,  70,  80
#define CHAPTER_MARKER_COLOR      200, 200, 210
#define TIMELINE_THUMB_COLOR      220, 220, 230
#define MEDIA_TITLE_COLOR         220, 220, 230
#define CHAPTER_HOVER_BG_COLOR    22,  22,  28
#define BOOKMARK_HOVER_BG_COLOR   22,  22,  28
#define CONTEXT_MENU_BG_COLOR     26,  26,  34
#define CONTEXT_MENU_ITEM_HL      40,  80,  120
#define TEXT_INPUT_BG_COLOR       28,  28,  36
#define TEXT_INPUT_PROMPT_COLOR   180, 180, 190
#define HAMBURGER_BG_COLOR        35,  35,  45
#define MENU_PANEL_BG_COLOR       28,  28,  36
#define MENU_PANEL_ITEM_BG_COLOR  35,  35,  45
#define LIST_BG_COLOR             26,  26,  34
#define LIST_ITEM_BG_COLOR        40,  80,  120
#define SCROLLBAR_BG_COLOR        35,  35,  45
#define SCROLLBAR_THUMB_COLOR     80,  80,  100
#define SUBTITLE_VLIST_BG_COLOR   22,  22,  30
#define PAUSED_TEXT_COLOR         240, 240, 245
#define FLASH_TEXT_COLOR          240, 240, 245
#define ACOL_TEXT_COLOR           255, 180, 100
#define ACOL_TEXT_ALPHA           200

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

/* for arch linux */
#define PLATFORM_FONTS \
    { "DejaVu Sans", "/usr/share/fonts/TTF/DejaVuSans.ttf" }, \
    { "DejaVu Mono", "/usr/share/fonts/TTF/DejaVuSansMono.ttf" }

#endif

/* Default fonts map */
#define DEFAULT_FONTS_MAP { \
    { "Iosevka (bundled)", "assets/Iosevka-Regular.ttc" }, \
    PLATFORM_FONTS \
}

#endif /* DEFAULT_THEME */

#endif /* CONFIG_H */
