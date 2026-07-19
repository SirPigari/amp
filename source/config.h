#ifndef CONFIG_H
#define CONFIG_H

/* Build config */
#define AMP_VERSION 0x010907 /* 1.9.7 */

#define THEMES_DIR "assets/themes/"

#define CC "gcc"
#define CFLAGS         "-Wno-cast-function-type", "-Wall", "-Wextra"
#define RELEASE_CFLAGS "-Wno-cast-function-type", "-Wall", "-Wextra", "-O3"
#define SAVE_FILE_MAGIC 0x414D5056 /* 'AMPV' */
#define OUT_EXE_NAME   "main"
#define TIMESTAMP      __DATE__ " " __TIME__

#define AMP_FLASH_DEBUG_DEFAULT 0
#define AMP_FLASH_DEBUG_LEVEL_DEFAULT NOB_INFO

#define MAX_HISTORY 100
#define MAX_RECENT 5
#define CHECK_FILE_SIGNATURE 1
#define INITIAL_WINDOW_WIDTH 960
#define INITIAL_WINDOW_HEIGHT 540
#define SAVE_FILE 1
#define DEBUG_SAVE_STATE 1
#ifndef DIST
#define SAVE_FILE_PATH "./amp_save.dat"
#else
#define SAVE_FILE_PATH "../amp_save.dat"
#endif
#define HW_CACHE_SIZE 16
#define HASH_SIZE 256
#define USE_PAUSE_IN_SAVE_FILE 0
#define MAX_BOOKMARKS_PER_FILE 64
#define BOOKMARK_NAME_MAX 64
#define TEXT_INPUT_MAX_LEN 256
#define BM_OVERRIDE_DEFAULT_COLOR 0
#define ALLOW_CTRL_Q_ON_WINDOWS 0

/* Drawing mode settings */
#define MAX_DRAW_STROKES 1024
#define MAX_DRAW_UNDO_STACK 32
#define DRAW_BRUSH_SIZE_MIN 1
#define DRAW_BRUSH_SIZE_MAX 69 /* it lags at big sizes */
#define DRAW_BRUSH_SIZE_DEFAULT 5
#define DRAW_SMOOTHING_ALPHA 0.25f

/* Demuxer settings */
#define DEMUX_MAX_READS_PER_TICK 64
#define DEMUX_MIN_READS_PER_TICK 4
#define DEMUX_TIME_BUDGET_MS 2.0
#define AMP_FF_PROBE_SIZE "4M"
#define AMP_FF_ANALYZE_DURATION_US "1500000"

/* Subtitle marquee settings */
#define MARQUEE_DELAY_MS 800
#define MARQUEE_SPEED    10.0f
#define MARQUEE_ELLIPSIS "..."

#define MAX_BLACKOUT_WINDOWS 16
#define BLACKOUT_MODE_TURN_OFF_MONITOR 0
#define AMBIENT_FADE_ROWS 8
#define AMBIENT_ZONES 8

#define DEFAULT_THEME "default"

/* System default font */
#ifdef _WIN32

#define SYSTEM_DEFAULT_FONT { "Segoe UI", "C:/Windows/Fonts/segoeui.ttf" }

#elif defined(__APPLE__)

#define SYSTEM_DEFAULT_FONT { "Arial", "/Library/Fonts/Arial.ttf" }

#else

#define SYSTEM_DEFAULT_FONT { "DejaVu Sans", "/usr/share/fonts/TTF/DejaVuSans.ttf" } /* on arch linux */

#endif

#endif /* CONFIG_H */
