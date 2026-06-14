/* 
   This is a theme template with the default theme values
   This file is IGNORED by amp
   To create a new theme, copy this file and change the values as needed
   It is NOT a perfect C header parser, so some stuff aren't supported (for example callable macros and // comments)
*/

#define THEME_NAME "Template"
#define THEME_FILE "template.h"

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
#define AMBIENT_GLOW_ALPHA 255

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
#define DRAW_PALETTE_BG_COLOR     30,  30,  38
#define DRAW_PALETTE_BORDER_COLOR 60,  60,  70
#define DRAW_CANVAS_CLEAR_COLOR   0,   0,   0
#define PAUSED_TEXT_COLOR         240, 240, 245
#define FLASH_TEXT_COLOR          240, 240, 245
#define ACOL_TEXT_COLOR           255, 180, 100
#define ACOL_TEXT_ALPHA           200

/* Drawing palette colors (7 colors) */
#define DRAW_PALETTE_COLOR_1     255, 255, 255  /* White */
#define DRAW_PALETTE_COLOR_2     0,   0,   0    /* Black */
#define DRAW_PALETTE_COLOR_3     255, 0,   0    /* Red */
#define DRAW_PALETTE_COLOR_4     0,   255, 0    /* Green */
#define DRAW_PALETTE_COLOR_5     0,   0,   255  /* Blue */
#define DRAW_PALETTE_COLOR_6     255, 255, 0    /* Yellow */
#define DRAW_PALETTE_COLOR_7     255, 128, 0    /* Orange */
#define DRAW_PALETTE_COLOR_ALPHA 255

/* Font */
#define FONT { "Iosevka (bundled)", "./assets/Iosevka-Regular.ttf" }
