#ifdef FEMBOY_THEME
#ifndef FEMBOY_THEME_DEFINED
#define FEMBOY_THEME_DEFINED

/*
Femboy theme for amp
One of defaulty shipped themes with amp, 
colors were chosen by markofwitch.
*/

/* Theme metadata */
#define THEME_NAME "Femboy~"
#define THEME_FILE "femboy.h"

/* Menu dimensions (same as default) */
#define MENU_DROPDOWN_ITEM_HEIGHT 28
#define MENU_DROPDOWN_WIDTH 230
#define MENU_DROPDOWN_SCROLLBAR_WIDTH 12
#define MENU_DROPDOWN_TEXT_PADDING_X 8
#define MENU_DROPDOWN_TEXT_PADDING_Y 2
#define MENU_MAX_VISIBLE_ITEMS 10

/* Timeline dimensions (same as default) */
#define TIMELINE_HEIGHT 6
#define TIMELINE_HITBOX_PADDING 12
#define TIMELINE_THUMB_SIZE 12
#define TIMELINE_CHAPTER_MARKER_WIDTH 4
#define TIMELINE_CHAPTER_MARKER_HITBOX_WIDTH 6
#define TIMELINE_BOOKMARK_MARKER_WIDTH 4
#define TIMELINE_BOOKMARK_MARKER_HITBOX_WIDTH 6

/* Hamburger dimensions (same as default) */
#define HAMBURGER_LINE_HEIGHT 2
#define HAMBURGER_LINE_MARGIN 6

#define SHADOW_OFFSET 2
#define DEFAULT_BOOKMARK_COLOR 0xFF96C8

/* Cute Pink (Femboy) Theme */
#define PANEL_COLOR               24,  20,  26
#define TEXT_COLOR                245, 235, 240
#define ACCENT_COLOR              255, 170, 210
#define MUTED_COLOR               150, 130, 145
#define SHADOW_COLOR              20,  20,  20
#define LETTERBOX_COLOR           20,  20,  20
#define OVERLAY_COLOR             80,  65,  78
#define CHAPTER_MARKER_COLOR      255, 200, 225
#define BOOKMARK_HOVER_BG_COLOR   36,  26,  38
#define CONTEXT_MENU_BG_COLOR     36,  28,  38
#define CONTEXT_MENU_ITEM_HL      90,  55,  90
#define TEXT_INPUT_BG_COLOR       32,  24,  34
#define TEXT_INPUT_PROMPT_COLOR   200, 180, 200
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
#define DRAW_PALETTE_BG_COLOR     30,  30,  38
#define DRAW_PALETTE_BORDER_COLOR 60,  60,  70
#define DRAW_CANVAS_CLEAR_COLOR   0,   0,   0
#define PAUSED_TEXT_COLOR         255, 235, 245
#define FLASH_TEXT_COLOR          255, 235, 245
#define ACOL_TEXT_COLOR           255, 185, 215
#define ACOL_TEXT_ALPHA           200

/* Drawing palette colors (changed just for fun) */
#define DRAW_PALETTE_COLOR_1     255, 235, 245  /* soft white pink */
#define DRAW_PALETTE_COLOR_2     24,  20,  26   /* panel dark */
#define DRAW_PALETTE_COLOR_3     255, 170, 210  /* accent pink */
#define DRAW_PALETTE_COLOR_4     200, 150, 180  /* muted rose */
#define DRAW_PALETTE_COLOR_5     150, 130, 145  /* muted purple gray */
#define DRAW_PALETTE_COLOR_6     255, 200, 225  /* light highlight pink */
#define DRAW_PALETTE_COLOR_7     255, 185, 215  /* warm pink */
#define DRAW_PALETTE_COLOR_ALPHA 255

/* Platform-specific fonts (same as default) */
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

#endif /* FEMBOY_THEME_DEFINED */
#endif /* FEMBOY_THEME */
