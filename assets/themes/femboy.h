#ifdef FEMBOY_THEME
#ifndef FEMBOY_THEME_DEFINED
#define FEMBOY_THEME_DEFINED

/* Theme metadata */
#define THEME_NAME "Femboy~"
#define THEME_FILE "femboy.h"

/* Cute Pink (Femboy) Theme */
#define PANEL_COLOR               24,  20,  26
#define TEXT_COLOR                245, 235, 240
#define ACCENT_COLOR              255, 170, 210
#define MUTED_COLOR               150, 130, 145
#define OVERLAY_COLOR             80,  65,  78
#define CHAPTER_MARKER_COLOR      255, 200, 225
#define TIMELINE_THUMB_COLOR      255, 210, 230
#define MEDIA_TITLE_COLOR         250, 220, 235
#define CHAPTER_HOVER_BG_COLOR    32,  24,  34
#define HAMBURGER_BG_COLOR        45,  34,  46
#define MENU_PANEL_BG_COLOR       36,  28,  38
#define MENU_PANEL_ITEM_BG_COLOR  48,  36,  50
#define LIST_BG_COLOR             34,  26,  36
#define LIST_ITEM_BG_COLOR        120, 70,  105
#define SCROLLBAR_BG_COLOR        44,  34,  46
#define SCROLLBAR_THUMB_COLOR     140, 110, 135
#define SUBTITLE_VLIST_BG_COLOR   28,  22,  30
#define PAUSED_TEXT_COLOR         255, 235, 245
#define FLASH_TEXT_COLOR          255, 235, 245
#define ACOL_TEXT_COLOR           255, 185, 215
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

// for arch linux
#define PLATFORM_FONTS \
    { "DejaVu Sans", "/usr/share/fonts/TTF/DejaVuSans.ttf" }, \
    { "DejaVu Mono", "/usr/share/fonts/TTF/DejaVuSansMono.ttf" }

#endif

/* Default fonts map */
#define DEFAULT_FONTS_MAP { \
    { "Iosevka (bundled)", "assets/Iosevka-Regular.ttc" }, \
    PLATFORM_FONTS \
}

#endif /* FEMBOY_THEME_DEFINED */
#endif /* FEMBOY_THEME */
