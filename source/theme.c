#define HT_IMPLEMENTATION
#include "../thirdparty/ht.h"
#include "../thirdparty/nob.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "config.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef DIST
#define D_ASSETS "/assets/"
#else
#define D_ASSETS "/../assets/"
#endif

#undef THEME_NAME
#undef THEME_FILE

static char THEME_NAME[256];
static char THEME_FILE[256];

typedef struct {
    char name[256];
    char path[512];
} ThemeFont;

static ThemeFont THEME_FONT;
static ThemeFont THEME_SYSTEM_DEFAULT_FONT = SYSTEM_DEFAULT_FONT;

static int THEME_MENU_DROPDOWN_ITEM_HEIGHT;
static int THEME_MENU_DROPDOWN_WIDTH;
static int THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH;
static int THEME_MENU_DROPDOWN_TEXT_PADDING_X;
static int THEME_MENU_DROPDOWN_TEXT_PADDING_Y;
static int THEME_MENU_MAX_VISIBLE_ITEMS;
static int THEME_TIMELINE_HEIGHT;
static int THEME_TIMELINE_HITBOX_PADDING;
static int THEME_TIMELINE_THUMB_SIZE;
static int THEME_TIMELINE_CHAPTER_MARKER_WIDTH;
static int THEME_TIMELINE_CHAPTER_MARKER_HITBOX_WIDTH;
static int THEME_TIMELINE_BOOKMARK_MARKER_WIDTH;
static int THEME_TIMELINE_BOOKMARK_MARKER_HITBOX_WIDTH;
static int THEME_HAMBURGER_LINE_HEIGHT;
static int THEME_HAMBURGER_LINE_MARGIN;
static int THEME_SHADOW_OFFSET;
static int THEME_DEFAULT_BOOKMARK_COLOR;
static int THEME_AMBIENT_GLOW_ALPHA;
static float THEME_AMBIENT_GLOW_ASCALE;
static float THEME_AMBIENT_GLOW_AMAX;
static int THEME_PANEL_COLOR[3];
static int THEME_TEXT_COLOR[3];
static int THEME_ACCENT_COLOR[3];
static int THEME_MUTED_COLOR[3];
static int THEME_SHADOW_COLOR[3];
static int THEME_LETTERBOX_COLOR[3];
static int THEME_OVERLAY_COLOR[3];
static int THEME_CHAPTER_MARKER_COLOR[3];
static int THEME_TIMELINE_THUMB_COLOR[3];
static int THEME_MEDIA_TITLE_COLOR[3];
static int THEME_CHAPTER_HOVER_BG_COLOR[3];
static int THEME_BOOKMARK_HOVER_BG_COLOR[3];
static int THEME_CONTEXT_MENU_BG_COLOR[3];
static int THEME_CONTEXT_MENU_ITEM_HL[3];
static int THEME_TEXT_INPUT_BG_COLOR[3];
static int THEME_TEXT_INPUT_PROMPT_COLOR[3];
static int THEME_HAMBURGER_BG_COLOR[3];
static int THEME_MENU_PANEL_BG_COLOR[3];
static int THEME_MENU_PANEL_ITEM_BG_COLOR[3];
static int THEME_LIST_BG_COLOR[3];
static int THEME_LIST_ITEM_BG_COLOR[3];
static int THEME_SCROLLBAR_BG_COLOR[3];
static int THEME_SCROLLBAR_THUMB_COLOR[3];
static int THEME_SUBTITLE_VLIST_BG_COLOR[3];
static int THEME_DRAW_PALETTE_BG_COLOR[3];
static int THEME_DRAW_PALETTE_BORDER_COLOR[3];
static int THEME_DRAW_CANVAS_CLEAR_COLOR[3];
static int THEME_PAUSED_TEXT_COLOR[3];
static int THEME_FLASH_TEXT_COLOR[3];
static int THEME_ACOL_TEXT_COLOR[3];
static int THEME_ACOL_TEXT_ALPHA;
static int THEME_DRAW_PALETTE_COLOR_1[3];
static int THEME_DRAW_PALETTE_COLOR_2[3];
static int THEME_DRAW_PALETTE_COLOR_3[3];
static int THEME_DRAW_PALETTE_COLOR_4[3];
static int THEME_DRAW_PALETTE_COLOR_5[3];
static int THEME_DRAW_PALETTE_COLOR_6[3];
static int THEME_DRAW_PALETTE_COLOR_7[3];
static int THEME_DRAW_PALETTE_COLOR_ALPHA;

static void get_exe_dir(char* buf, size_t buf_size);
void fix_theme_font_path(void);

typedef Ht(const char*, char*) ThemeDefines;

typedef struct {
    const char* file_path;
    int line;
    int column;
} ThemeLocation;

static void theme_error(ThemeLocation loc, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s:%d:%d: error: ", loc.file_path, loc.line, loc.column);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void theme_skip_whitespace(const char** p) {
    while (isspace(**p)) (*p)++;
}

static bool theme_skip_comment(const char** p) {
    if ((*p)[0] == '/' && (*p)[1] == '*') {
        *p += 2;
        while ((*p)[0] && !((*p)[0] == '*' && (*p)[1] == '/')) {
            (*p)++;
        }
        if ((*p)[0] == '*' && (*p)[1] == '/') {
            *p += 2;
            return true;
        }
    }
    return false;
}

static void theme_skip_whitespace_and_comments(const char** p) {
    while (true) {
        theme_skip_whitespace(p);
        if (!theme_skip_comment(p)) break;
    }
}

static bool theme_parse_identifier(const char** p, char* out, size_t out_size) {
    theme_skip_whitespace_and_comments(p);
    const char* start = *p;
    if (!isalpha(**p) && **p != '_') return false;
    
    (*p)++;
    while (isalnum(**p) || **p == '_') (*p)++;
    
    size_t len = *p - start;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool theme_parse_value(const char** p, char* out, size_t out_size) {
    theme_skip_whitespace_and_comments(p);
    char* write = out;
    size_t remaining = out_size - 1;
    
    while (**p && remaining > 0) {
        if (**p == '\\') {
            const char* next = *p + 1;
            if (*next == '\r') next++;
            if (*next == '\n') {
                *p = next + 1;
                while (**p && (**p == ' ' || **p == '\t')) (*p)++;
                continue;
            }
        }
        
        if (**p == '\n' || **p == '\r' || ((*p)[0] == '/' && (*p)[1] == '*')) {
            break;
        }
        
        *write++ = **p;
        (*p)++;
        remaining--;
    }
    
    while (write > out && isspace(*(write - 1))) write--;
    
    *write = '\0';
    return write > out;
}

static bool theme_is_defined(ThemeDefines* defines, const char* name) {
    return ht_find(defines, name) != NULL;
}

static bool theme_eval_condition(ThemeDefines* defines, const char* condition) {
    char name[256];
    const char* p = condition;
    theme_skip_whitespace_and_comments(&p);
    
    if (strncmp(p, "defined", 7) == 0) {
        p += 7;
        theme_skip_whitespace_and_comments(&p);
        if (*p == '(') {
            p++;
            if (!theme_parse_identifier(&p, name, sizeof(name))) return false;
            theme_skip_whitespace_and_comments(&p);
            if (*p != ')') return false;
            return theme_is_defined(defines, name);
        }
    }
    
    if (theme_parse_identifier(&p, name, sizeof(name))) {
        return theme_is_defined(defines, name);
    }
    
    return false;
}

static char* theme_expand_macros(ThemeDefines* defines, const char* value) {
    static char expanded[4096];
    char temp[4096];
    strncpy(temp, value, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    bool changed = true;
    int iterations = 0;
    while (changed && iterations < 10) {
        changed = false;
        iterations++;
        expanded[0] = '\0';
        
        const char* p = temp;
        char* out = expanded;
        size_t remaining = sizeof(expanded) - 1;
        
        while (*p && remaining > 0) {
            if (isspace(*p)) {
                *out++ = *p++;
                remaining--;
                continue;
            }
            
            if (isalpha(*p) || *p == '_') {
                char ident[256];
                int ident_len = 0;
                
                while ((isalnum(*p) || *p == '_') && ident_len < 255) {
                    ident[ident_len++] = *p++;
                }
                ident[ident_len] = '\0';
                
                char** macro_value = ht_find(defines, ident);
                if (macro_value && *macro_value) {
                    const char* expansion = *macro_value;
                    while (*expansion && remaining > 0) {
                        *out++ = *expansion++;
                        remaining--;
                    }
                    changed = true;
                } else {
                    for (int i = 0; i < ident_len && remaining > 0; i++) {
                        *out++ = ident[i];
                        remaining--;
                    }
                }
            } else {
                *out++ = *p++;
                remaining--;
            }
        }
        *out = '\0';
        
        snprintf(temp, sizeof(temp), "%s", expanded);
        temp[sizeof(temp) - 1] = '\0';
    }
    
    return expanded;
}

static bool theme_load_file(const char* theme_path, ThemeDefines* defines) {
    nob_log(NOB_INFO, "Loading theme: %s", theme_path);
    
    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(theme_path, &sb)) {
        nob_log(NOB_ERROR, "Failed to read theme file: %s", theme_path);
        return false;
    }
    nob_sb_append_null(&sb);
    
    const char* content = sb.items;
    const char* p = content;
    const char* line_start = content;
    int line = 1;
    
    int cond_stack[32] = {1};
    int taken_stack[32] = {1};
    int cond_depth = 1;
    
    while (*p) {
        theme_skip_whitespace_and_comments(&p);
        if (*p == '\n') { line++; line_start = p + 1; p++; continue; }
        if (*p == '\0') break;
        
        ThemeLocation loc = { .file_path = theme_path, .line = line, .column = (int)(p - line_start) + 1 };
        
        if (*p == '#') {
            p++;
            char directive[32];
            if (!theme_parse_identifier(&p, directive, sizeof(directive))) {
                theme_error(loc, "Expected preprocessor directive");
                return false;
            }
            
            if (strcmp(directive, "define") == 0) {
                if (cond_stack[cond_depth - 1]) {
                    char name[256], value[512];
                    if (!theme_parse_identifier(&p, name, sizeof(name))) {
                        theme_error(loc, "Expected identifier after #define");
                        return false;
                    }
                    if (!theme_parse_value(&p, value, sizeof(value))) {
                        value[0] = '\0';
                    }
                    
                    char* name_copy = strdup(name);
                    char* value_copy = strdup(value);
                    *ht_put(defines, name_copy) = value_copy;
                }
            } else if (strcmp(directive, "ifdef") == 0) {
                char name[256];
                if (!theme_parse_identifier(&p, name, sizeof(name))) {
                    theme_error(loc, "Expected identifier after #ifdef");
                    return false;
                }
                if (cond_depth >= 32) {
                    theme_error(loc, "Conditional nesting too deep");
                    return false;
                }
                bool is_def = theme_is_defined(defines, name);
                bool branch = cond_stack[cond_depth - 1] && is_def;
                cond_stack[cond_depth] = branch;
                taken_stack[cond_depth] = branch;
                cond_depth++;
            } else if (strcmp(directive, "ifndef") == 0) {
                char name[256];
                if (!theme_parse_identifier(&p, name, sizeof(name))) {
                    theme_error(loc, "Expected identifier after #ifndef");
                    return false;
                }
                if (cond_depth >= 32) {
                    theme_error(loc, "Conditional nesting too deep");
                    return false;
                }
                bool is_def = theme_is_defined(defines, name);
                bool branch = cond_stack[cond_depth - 1] && !is_def;
                cond_stack[cond_depth] = branch;
                taken_stack[cond_depth] = branch;
                cond_depth++;
            } else if (strcmp(directive, "if") == 0) {
                char condition[512];
                if (!theme_parse_value(&p, condition, sizeof(condition))) {
                    theme_error(loc, "Expected condition after #if");
                    return false;
                }
                if (cond_depth >= 32) {
                    theme_error(loc, "Conditional nesting too deep");
                    return false;
                }
                bool result = theme_eval_condition(defines, condition);
                bool branch = cond_stack[cond_depth - 1] && result;
                cond_stack[cond_depth] = branch;
                taken_stack[cond_depth] = branch;
                cond_depth++;
            } else if (strcmp(directive, "elif") == 0) {
                if (cond_depth <= 1) {
                    theme_error(loc, "#elif without matching #if");
                    return false;
                }
                char condition[512];
                if (!theme_parse_value(&p, condition, sizeof(condition))) {
                    theme_error(loc, "Expected condition after #elif");
                    return false;
                }
                bool result = theme_eval_condition(defines, condition);
                bool branch = cond_stack[cond_depth - 2] && result && !taken_stack[cond_depth - 1];
                cond_stack[cond_depth - 1] = branch;
                if (branch) taken_stack[cond_depth - 1] = true;
            } else if (strcmp(directive, "else") == 0) {
                if (cond_depth <= 1) {
                    theme_error(loc, "#else without matching #if");
                    return false;
                }
                cond_stack[cond_depth - 1] = cond_stack[cond_depth - 2] && !taken_stack[cond_depth - 1];
            } else if (strcmp(directive, "endif") == 0) {
                if (cond_depth <= 1) {
                    theme_error(loc, "#endif without matching #if");
                    return false;
                }
                taken_stack[cond_depth - 1] = 0;
                cond_depth--;
            }
        }
        
        while (*p && *p != '\n') p++;
        if (*p == '\n') { line++; line_start = p + 1; p++; }
    }
    
    nob_sb_free(sb);
    return true;
}

static void theme_set_int(ThemeDefines* defines, const char* name, int* target) {
    char** value = ht_find(defines, name);
    if (value && *value) {
        char* endptr;
        long val = strtol(*value, &endptr, 0);
        if (endptr != *value) {
            *target = (int)val;
            nob_log(NOB_INFO, "  %s = %d", name, *target);
        }
    }
}

static void theme_set_float(ThemeDefines* defines, const char* name, float* target) {
    char** value = ht_find(defines, name);
    if (value && *value) {
        char* endptr;
        float val = strtof(*value, &endptr);
        if (endptr != *value) {
            *target = val;
            nob_log(NOB_INFO, "  %s = %f", name, *target);
        }
    }
}

static void theme_set_color(ThemeDefines* defines, const char* name, int* target) {
    char** value = ht_find(defines, name);
    if (value && *value) {
        int r, g, b;
        const char* str = *value;
        
        while (*str && (*str == '{' || isspace(*str))) str++;
        
        if (sscanf(str, "%d , %d , %d", &r, &g, &b) == 3 ||
            sscanf(str, "%d, %d, %d", &r, &g, &b) == 3) {
            target[0] = r;
            target[1] = g;
            target[2] = b;
            nob_log(NOB_INFO, "  %s = { %d, %d, %d }", name, r, g, b);
        }
    }
}

static void theme_set_string(ThemeDefines* defines, const char* name, char* target, size_t target_size) {
    char** value = ht_find(defines, name);
    if (value && *value) {
        const char* str = *value;
        if (*str == '"') {
            str++;
            const char* end = strchr(str, '"');
            if (end) {
                size_t len = end - str;
                if (len >= target_size) len = target_size - 1;
                memcpy(target, str, len);
                target[len] = '\0';
                nob_log(NOB_INFO, "  %s = \"%s\"", name, target);
                return;
            }
        }
        strncpy(target, *value, target_size - 1);
        target[target_size - 1] = '\0';
        nob_log(NOB_INFO, "  %s = \"%s\"", name, target);
    }
}

static bool theme_load(const char* theme_path) {
    ThemeDefines defines = { .hasheq = ht_cstr_hasheq };
    
#ifdef _WIN32
    *ht_put(&defines, strdup("_WIN32")) = strdup("1");
#elif defined(__APPLE__)
    *ht_put(&defines, strdup("__APPLE__")) = strdup("1");
#else
    *ht_put(&defines, strdup("__linux__")) = strdup("1");
#endif
    
    if (!theme_load_file(theme_path, &defines)) {
        ht_foreach(value, &defines) {
            free((void*)ht_key(&defines, value));
            free(*value);
        }
        ht_free(&defines);
        return false;
    }
    
    if (!theme_is_defined(&defines, "THEME_NAME")) {
        nob_log(NOB_ERROR, "%s: error: THEME_NAME is required but not defined", theme_path);
        ht_foreach(value, &defines) {
            free((void*)ht_key(&defines, value));
            free(*value);
        }
        ht_free(&defines);
        return false;
    }
    
    if (!theme_is_defined(&defines, "THEME_FILE")) {
        nob_log(NOB_ERROR, "%s: error: THEME_FILE is required but not defined", theme_path);
        ht_foreach(value, &defines) {
            free((void*)ht_key(&defines, value));
            free(*value);
        }
        ht_free(&defines);
        return false;
    }
    
    nob_log(NOB_INFO, "Applying theme values:");
    
    theme_set_string(&defines, "THEME_NAME", THEME_NAME, sizeof(THEME_NAME));
    theme_set_string(&defines, "THEME_FILE", THEME_FILE, sizeof(THEME_FILE));
    
    theme_set_int(&defines, "MENU_DROPDOWN_ITEM_HEIGHT", &THEME_MENU_DROPDOWN_ITEM_HEIGHT);
    theme_set_int(&defines, "MENU_DROPDOWN_WIDTH", &THEME_MENU_DROPDOWN_WIDTH);
    theme_set_int(&defines, "MENU_DROPDOWN_SCROLLBAR_WIDTH", &THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH);
    theme_set_int(&defines, "MENU_DROPDOWN_TEXT_PADDING_X", &THEME_MENU_DROPDOWN_TEXT_PADDING_X);
    theme_set_int(&defines, "MENU_DROPDOWN_TEXT_PADDING_Y", &THEME_MENU_DROPDOWN_TEXT_PADDING_Y);
    theme_set_int(&defines, "MENU_MAX_VISIBLE_ITEMS", &THEME_MENU_MAX_VISIBLE_ITEMS);
    
    theme_set_int(&defines, "TIMELINE_HEIGHT", &THEME_TIMELINE_HEIGHT);
    theme_set_int(&defines, "TIMELINE_HITBOX_PADDING", &THEME_TIMELINE_HITBOX_PADDING);
    theme_set_int(&defines, "TIMELINE_THUMB_SIZE", &THEME_TIMELINE_THUMB_SIZE);
    theme_set_int(&defines, "TIMELINE_CHAPTER_MARKER_WIDTH", &THEME_TIMELINE_CHAPTER_MARKER_WIDTH);
    theme_set_int(&defines, "TIMELINE_CHAPTER_MARKER_HITBOX_WIDTH", &THEME_TIMELINE_CHAPTER_MARKER_HITBOX_WIDTH);
    theme_set_int(&defines, "TIMELINE_BOOKMARK_MARKER_WIDTH", &THEME_TIMELINE_BOOKMARK_MARKER_WIDTH);
    theme_set_int(&defines, "TIMELINE_BOOKMARK_MARKER_HITBOX_WIDTH", &THEME_TIMELINE_BOOKMARK_MARKER_HITBOX_WIDTH);
    
    theme_set_int(&defines, "HAMBURGER_LINE_HEIGHT", &THEME_HAMBURGER_LINE_HEIGHT);
    theme_set_int(&defines, "HAMBURGER_LINE_MARGIN", &THEME_HAMBURGER_LINE_MARGIN);
    theme_set_int(&defines, "SHADOW_OFFSET", &THEME_SHADOW_OFFSET);
    theme_set_int(&defines, "DEFAULT_BOOKMARK_COLOR", &THEME_DEFAULT_BOOKMARK_COLOR);
    theme_set_int(&defines, "AMBIENT_GLOW_ALPHA", &THEME_AMBIENT_GLOW_ALPHA);
    theme_set_float(&defines, "AMBIENT_GLOW_ASCALE", &THEME_AMBIENT_GLOW_ASCALE);
    theme_set_float(&defines, "AMBIENT_GLOW_AMAX", &THEME_AMBIENT_GLOW_AMAX);
    
    theme_set_color(&defines, "PANEL_COLOR", THEME_PANEL_COLOR);
    theme_set_color(&defines, "TEXT_COLOR", THEME_TEXT_COLOR);
    theme_set_color(&defines, "ACCENT_COLOR", THEME_ACCENT_COLOR);
    theme_set_color(&defines, "MUTED_COLOR", THEME_MUTED_COLOR);
    theme_set_color(&defines, "SHADOW_COLOR", THEME_SHADOW_COLOR);
    theme_set_color(&defines, "LETTERBOX_COLOR", THEME_LETTERBOX_COLOR);
    theme_set_color(&defines, "OVERLAY_COLOR", THEME_OVERLAY_COLOR);
    theme_set_color(&defines, "CHAPTER_MARKER_COLOR", THEME_CHAPTER_MARKER_COLOR);
    theme_set_color(&defines, "TIMELINE_THUMB_COLOR", THEME_TIMELINE_THUMB_COLOR);
    theme_set_color(&defines, "MEDIA_TITLE_COLOR", THEME_MEDIA_TITLE_COLOR);
    theme_set_color(&defines, "CHAPTER_HOVER_BG_COLOR", THEME_CHAPTER_HOVER_BG_COLOR);
    theme_set_color(&defines, "BOOKMARK_HOVER_BG_COLOR", THEME_BOOKMARK_HOVER_BG_COLOR);
    theme_set_color(&defines, "CONTEXT_MENU_BG_COLOR", THEME_CONTEXT_MENU_BG_COLOR);
    theme_set_color(&defines, "CONTEXT_MENU_ITEM_HL", THEME_CONTEXT_MENU_ITEM_HL);
    theme_set_color(&defines, "TEXT_INPUT_BG_COLOR", THEME_TEXT_INPUT_BG_COLOR);
    theme_set_color(&defines, "TEXT_INPUT_PROMPT_COLOR", THEME_TEXT_INPUT_PROMPT_COLOR);
    theme_set_color(&defines, "HAMBURGER_BG_COLOR", THEME_HAMBURGER_BG_COLOR);
    theme_set_color(&defines, "MENU_PANEL_BG_COLOR", THEME_MENU_PANEL_BG_COLOR);
    theme_set_color(&defines, "MENU_PANEL_ITEM_BG_COLOR", THEME_MENU_PANEL_ITEM_BG_COLOR);
    theme_set_color(&defines, "LIST_BG_COLOR", THEME_LIST_BG_COLOR);
    theme_set_color(&defines, "LIST_ITEM_BG_COLOR", THEME_LIST_ITEM_BG_COLOR);
    theme_set_color(&defines, "SCROLLBAR_BG_COLOR", THEME_SCROLLBAR_BG_COLOR);
    theme_set_color(&defines, "SCROLLBAR_THUMB_COLOR", THEME_SCROLLBAR_THUMB_COLOR);
    theme_set_color(&defines, "SUBTITLE_VLIST_BG_COLOR", THEME_SUBTITLE_VLIST_BG_COLOR);
    theme_set_color(&defines, "DRAW_PALETTE_BG_COLOR", THEME_DRAW_PALETTE_BG_COLOR);
    theme_set_color(&defines, "DRAW_PALETTE_BORDER_COLOR", THEME_DRAW_PALETTE_BORDER_COLOR);
    theme_set_color(&defines, "DRAW_CANVAS_CLEAR_COLOR", THEME_DRAW_CANVAS_CLEAR_COLOR);
    theme_set_color(&defines, "PAUSED_TEXT_COLOR", THEME_PAUSED_TEXT_COLOR);
    theme_set_color(&defines, "FLASH_TEXT_COLOR", THEME_FLASH_TEXT_COLOR);
    theme_set_color(&defines, "ACOL_TEXT_COLOR", THEME_ACOL_TEXT_COLOR);
    theme_set_int(&defines, "ACOL_TEXT_ALPHA", &THEME_ACOL_TEXT_ALPHA);
    
    theme_set_color(&defines, "DRAW_PALETTE_COLOR_1", THEME_DRAW_PALETTE_COLOR_1);
    theme_set_color(&defines, "DRAW_PALETTE_COLOR_2", THEME_DRAW_PALETTE_COLOR_2);
    theme_set_color(&defines, "DRAW_PALETTE_COLOR_3", THEME_DRAW_PALETTE_COLOR_3);
    theme_set_color(&defines, "DRAW_PALETTE_COLOR_4", THEME_DRAW_PALETTE_COLOR_4);
    theme_set_color(&defines, "DRAW_PALETTE_COLOR_5", THEME_DRAW_PALETTE_COLOR_5);
    theme_set_color(&defines, "DRAW_PALETTE_COLOR_6", THEME_DRAW_PALETTE_COLOR_6);
    theme_set_color(&defines, "DRAW_PALETTE_COLOR_7", THEME_DRAW_PALETTE_COLOR_7);
    theme_set_int(&defines, "DRAW_PALETTE_COLOR_ALPHA", &THEME_DRAW_PALETTE_COLOR_ALPHA);
    
    char** font_value = ht_find(&defines, "FONT");
    if (font_value && *font_value) {
        THEME_FONT.name[0] = '\0';
        THEME_FONT.path[0] = '\0';
        const char* raw = theme_expand_macros(&defines, *font_value);
        const char* p = raw;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '{') p++;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '"') {
            p++;
            size_t i = 0;
            while (*p && *p != '"' && i < sizeof(THEME_FONT.name) - 1)
                THEME_FONT.name[i++] = *p++;
            THEME_FONT.name[i] = '\0';
            if (*p == '"') p++;
        }
        while (isspace((unsigned char)*p) || *p == ',') p++;
        if (*p == '"') {
            p++;
            size_t i = 0;
            while (*p && *p != '"' && i < sizeof(THEME_FONT.path) - 1)
                THEME_FONT.path[i++] = *p++;
            THEME_FONT.path[i] = '\0';
        }
        if (THEME_FONT.path[0])
            nob_log(NOB_INFO, "  Theme font: %s (%s)", THEME_FONT.name[0] ? THEME_FONT.name : "unnamed", THEME_FONT.path);
    } else {
        if (THEME_FONT.path[0])
            nob_log(NOB_INFO, "  Theme font: %s (%s)", THEME_FONT.name[0] ? THEME_FONT.name : "unnamed", THEME_FONT.path);
    }

    fix_theme_font_path();
    
    ht_foreach(value, &defines) {
        free((void*)ht_key(&defines, value));
        free(*value);
    }
    ht_free(&defines);
    
    nob_log(NOB_INFO, "Theme loaded successfully: %s", THEME_NAME);
    
    return true;
}

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static void get_exe_dir(char* buf, size_t buf_size) {
#ifdef _WIN32
    GetModuleFileNameA(NULL, buf, (DWORD)buf_size);
    char* last_slash = strrchr(buf, '\\');
    if (last_slash) *last_slash = '\0';
#else
    ssize_t len = readlink("/proc/self/exe", buf, buf_size - 1);
    if (len != -1) {
        buf[len] = '\0';
        char* last_slash = strrchr(buf, '/');
        if (last_slash) *last_slash = '\0';
    } else {
        buf[0] = '.';
        buf[1] = '\0';
    }
#endif
}

static bool is_path(const char* name) {
    return strchr(name, '/') != NULL || strchr(name, '\\') != NULL;
}

bool is_valid_theme_name(const char* name) {
    if (!name || !*name) return false;
    
    if (is_path(name)) {
        return file_exists(name);
    }
    
    char exe_dir[512];
    char theme_path[1024];
    get_exe_dir(exe_dir, sizeof(exe_dir));
    
    if (strstr(name, ".h") == NULL) {
        snprintf(theme_path, sizeof(theme_path), "%s" D_ASSETS "themes/%s.h", exe_dir, name);
    } else {
        snprintf(theme_path, sizeof(theme_path), "%s" D_ASSETS "themes/%s", exe_dir, name);
    }
    
    return file_exists(theme_path);
}

bool load_theme(const char* name) {
    if (!name || !*name) {
        nob_log(NOB_ERROR, "Theme name cannot be empty");
        return false;
    }

    char theme_path[1024];
    
    if (is_path(name)) {
        strncpy(theme_path, name, sizeof(theme_path) - 1);
        theme_path[sizeof(theme_path) - 1] = '\0';
    } else {
        char exe_dir[512];
        get_exe_dir(exe_dir, sizeof(exe_dir));

        if (strstr(name, ".h") == NULL) {
            snprintf(theme_path, sizeof(theme_path), "%s" D_ASSETS "themes/%s.h", exe_dir, name);
        } else {
            snprintf(theme_path, sizeof(theme_path), "%s" D_ASSETS "themes/%s", exe_dir, name);
        }
    }
    
    return theme_load(theme_path);
}

bool init_theme(void) {
    nob_minimal_log_level = NOB_ERROR;
    char theme_name_saved[256];
    memcpy(theme_name_saved, THEME_NAME, sizeof(theme_name_saved));
    char theme_file_saved[256];
    memcpy(theme_file_saved, THEME_FILE, sizeof(theme_file_saved));
    if (!load_theme(DEFAULT_THEME)) {
        nob_minimal_log_level = NOB_INFO;
        nob_log(NOB_ERROR, "Failed to load default theme: %s", DEFAULT_THEME);
        return false;
    }
    memcpy(THEME_NAME, theme_name_saved, sizeof(THEME_NAME));
    memcpy(THEME_FILE, theme_file_saved, sizeof(THEME_FILE));
    nob_minimal_log_level = NOB_INFO;
    return true;
}

void fix_theme_font_path(void) {
    #ifdef DIST
    if (strncmp(THEME_FONT.path, "assets/", 7) == 0 ||
        strncmp(THEME_FONT.path, "./assets/", 9) == 0) {
        char temp[sizeof(THEME_FONT.path) + 1024];
        char exe_dir[512];
        get_exe_dir(exe_dir, sizeof(exe_dir));
        snprintf(temp, sizeof(temp), "%s\\..\\%s", exe_dir, THEME_FONT.path);
        memcpy(THEME_FONT.path, temp, sizeof(THEME_FONT.path));
    }
    #endif
    return;
}

bool load_theme_probe(const char* path) {
    if (!path || !path[0]) return false;
    ThemeDefines defines = { .hasheq = ht_cstr_hasheq };
#ifdef _WIN32
    *ht_put(&defines, strdup("_WIN32")) = strdup("1");
#elif defined(__APPLE__)
    *ht_put(&defines, strdup("__APPLE__")) = strdup("1");
#else
    *ht_put(&defines, strdup("__linux__")) = strdup("1");
#endif
    bool ok = theme_load_file(path, &defines)
           && theme_is_defined(&defines, "THEME_NAME")
           && theme_is_defined(&defines, "THEME_FILE");
    ht_foreach(value, &defines) {
        free((void*)ht_key(&defines, value));
        free(*value);
    }
    ht_free(&defines);
    return ok;
}

bool load_theme_file(const char* path) {
    if (!path || !path[0]) return false;
    return theme_load(path);
}

int get_theme_list(char themes[][64], int max_count) {
    int count = 0;

    char exe_dir[512];
    char themes_dir[1024];
    get_exe_dir(exe_dir, sizeof(exe_dir));
    snprintf(themes_dir, sizeof(themes_dir), "%s" D_ASSETS "themes", exe_dir);

    Nob_File_Paths paths = {0};
    if (nob_read_entire_dir(themes_dir, &paths)) {
        for (size_t i = 0; i < paths.count && count < max_count; i++) {
            const char* name = paths.items[i];
            if (!name || !*name) continue;
            if (strcmp(name, "template.h") == 0) continue;

            size_t len = strlen(name);
            if (len <= 2) continue;
            if (strcmp(name + len - 2, ".h") != 0) continue;

            size_t copy_len = len - 2;
            if (copy_len >= 64) copy_len = 63;

            memcpy(themes[count], name, copy_len);
            themes[count][copy_len] = '\0';

            count++;
        }
        nob_da_free(paths);
    }

    return count;
}

void list_themes(FILE* out) {
    if (!out) out = stdout;

    char exe_dir[512];
    get_exe_dir(exe_dir, sizeof(exe_dir));

    char themes_dir[1024];
    snprintf(themes_dir, sizeof(themes_dir), "%s" D_ASSETS "themes", exe_dir);

    fprintf(out, "Available themes in %s:\n", themes_dir);

    char themes[32][64];
    int count = get_theme_list(themes, 32);

    if (count <= 0) {
        fprintf(out, "  (no themes found)\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        fprintf(out, "  - %s\n", themes[i]);
    }
}
