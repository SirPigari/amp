#define NOB_IMPLEMENTATION
#define NOB_UNSTRIP_PREFIX
#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <objbase.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#endif
#include "../thirdparty/SDL2/SDL.h"
#include "../thirdparty/SDL2/SDL_ttf.h"
#ifdef _WIN32
#include "../thirdparty/SDL2/SDL_syswm.h"
#endif
#include "../thirdparty/nob.h"
#include "../thirdparty/tinyfd.c"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../thirdparty/stb_image_write.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifdef _WIN32
#include <limits.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#ifndef _WIN32
#include <strings.h>
#include <dirent.h>
#endif
#undef NOB_IMPLEMENTATION

#if !defined(PATH_MAX) && defined(MAX_PATH)
#define PATH_MAX MAX_PATH
#endif

#include "config.h"
#include "theme.c"
#include "text.c"
#include "renderer.c"
#include "../thirdparty/ascii.h"

#if SAVE_FILE
#include "save.c"
#endif

#include "drawing.c"

char* recent_files[MAX_RECENT] = {0};
int recent_count = 0;

char flash_text[256] = {0};
Uint32 flash_until = 0;
float flash_alpha = 0.0f;
static char hw_option[32] = "auto";
static FILE* log_file = NULL;

typedef struct {
    Bookmark bms[MAX_BOOKMARKS_PER_FILE];
    int count;
} BmSnap;

typedef enum { HIST_SEEK, HIST_BOOKMARK, HIST_VOLUME } HistKind;

typedef struct {
    HistKind kind;
    union {
        struct { char* before_path; double before_time; char* after_path; double after_time; } seek;
        struct { BmSnap before; BmSnap after; } bookmark;
        struct { float before; float after; } volume;
    };
    char desc[256];
} HistoryEntry;

static int flash_debug_enabled = AMP_FLASH_DEBUG_DEFAULT;
static int flash_debug_level = AMP_FLASH_DEBUG_LEVEL_DEFAULT;

static const uint32_t subtitle_override_colors[] = {
    0xFFFFFF,
    0xFFE066,
    0x66D9EF,
    0xFF9ECF
};
static const char* subtitle_override_color_labels[] = {
    "White",
    "Yellow",
    "Cyan",
    "Pink"
};
static const int subtitle_override_sizes[] = {
    24,
    30,
    36,
    44
};
static const char* subtitle_override_size_labels[] = {
    "Small",
    "Normal",
    "Large",
    "XL"
};
static const int subtitle_override_margins[] = {
    40,
    70,
    100,
    140
};
static const char* subtitle_override_move_labels[] = {
    "Very Low",
    "Low",
    "Mid",
    "High"
};
typedef struct {
    union {
        struct {
            SDL_Keycode key;
            SDL_Keymod mod;
        } keysym;
        const char* textinput;
    } key;
    enum {
        KBD_ENTRY_KEYSYM,
        KBD_ENTRY_TEXT_INPUT,
    } tag;
    const char* desc;
} KbdEntry;

#define KBD_KSM(m, k, d) \
    { .key.keysym = { SDLK_##k, m }, .tag = KBD_ENTRY_KEYSYM, .desc = d }

#define KBD_KS(m, k, d) \
    KBD_KSM(KMOD_##m, k, d)

#define KBD_K(k, d) KBD_KS(NONE, k, d)

#define KBD_TI(t, d) \
    { .key.textinput = t, .tag = KBD_ENTRY_TEXT_INPUT, .desc = d }

static const KbdEntry kbd_entries[] = {
    KBD_K(SPACE, "Play / Pause"),
    KBD_K(LEFT, "Seek -5s"),
    KBD_K(RIGHT, "Seek +5s"),
    KBD_K(UP, "Volume +5%"),
    KBD_K(DOWN, "Volume -5%"),
    KBD_KS(ALT, LEFT, "Previous Frame"),
    KBD_KS(ALT, RIGHT, "Next Frame"),
    KBD_KS(SHIFT, LEFT, "Previous Media"),
    KBD_KS(SHIFT, RIGHT, "Next Media"),
    KBD_K(HOME, "Seek to Beginning"),
    KBD_K(END, "Seek to End"),
    KBD_KS(ALT, m, "Minimize"),
    KBD_K(F10, "Toggle Maximize"),
    KBD_K(F11, "Toggle Fullscreen"),
    KBD_K(F2, "Screenshot"),
    KBD_K(c, "Toggle Subtitles"),
    KBD_KS(ALT, c, "Cycle Subtitles"),
    KBD_KS(CTRL, o, "Open File"),
    KBD_KS(CTRL, r, "Reload File"),
    KBD_KS(CTRL, z, "Undo"),
    KBD_KS(CTRL, y, "Redo"),
    KBD_K(b, "Add Bookmark"),
    KBD_KS(SHIFT, b, "Delete Closest Bookmark"),
    KBD_TI("[", "Previous Bookmark/Chapter"),
    KBD_TI("]", "Next Bookmark/Chapter"),
    KBD_K(m, "Mute / Unmute"),
    KBD_KS(CTRL, 1, "Volume 100%"),
    KBD_KS(CTRL, 2, "Volume 200%"),
    KBD_K(p, "Enter Draw Mode"),
    KBD_KS(CTRL, g, "Jump to Time"),
    KBD_KS(ALT, r, "Custom Resolution"),
    KBD_KSM(KMOD_SHIFT | KMOD_ALT, r, "Reset Resolution"),
    KBD_KSM(KMOD_ALT, a, "Custom Aspect Ratio"),
    KBD_KSM(KMOD_SHIFT | KMOD_ALT, a, "Reset Aspect Ratio"),
    KBD_KSM(KMOD_SHIFT | KMOD_ALT, s, "Stretch to Window"),
    KBD_KSM(KMOD_SHIFT | KMOD_ALT, z, "Custom Zoom"),
    KBD_KSM(KMOD_CTRL | KMOD_ALT, z, "Reset Zoom"),
    KBD_KSM(KMOD_ALT, s, "Custom Speed"),
    KBD_KS(CTRL, 0, "Reset view state"),
    KBD_KS(CTRL, KP_PLUS, "Zoom In"),
    KBD_KS(CTRL, KP_MINUS, "Zoom Out"),
    KBD_K(KP_PLUS, "Subtitle +1ms"),
    KBD_K(KP_MINUS, "Subtitle -1ms"),
    KBD_KS(SHIFT, KP_PLUS, "Subtitle +100ms"),
    KBD_KS(SHIFT, KP_MINUS, "Subtitle -100ms"),
    KBD_TI("?", "Keyboard Shortcuts"),
    #ifdef _WIN32
    KBD_KS(ALT, F4, "Exit"),
    #else
    KBD_KS(CTRL, q, "Exit"),
    #endif
    KBD_K(ESCAPE, "Close"),
};
static const int kbd_entry_count = (int)(sizeof(kbd_entries) / sizeof(kbd_entries[0]));
#undef KBD_KSM
#undef KBD_KS
#undef KBD_K
#undef KBD_TI

static char* display_kbd(KbdEntry* kbd) {
    static char key[64];
    key[0] = '\0';

    if (kbd->tag == KBD_ENTRY_KEYSYM) {
        SDL_Keymod mod = kbd->key.keysym.mod;

        if (mod & KMOD_CTRL)
            strncat(key, "Ctrl+", sizeof(key) - strlen(key) - 1);
        if (mod & KMOD_SHIFT)
            strncat(key, "Shift+", sizeof(key) - strlen(key) - 1);
        if (mod & KMOD_ALT)
            strncat(key, "Alt+", sizeof(key) - strlen(key) - 1);
        if (mod & KMOD_GUI)
            strncat(key, "Super+", sizeof(key) - strlen(key) - 1);

        const char* raw = SDL_GetKeyName(kbd->key.keysym.key);

        char pretty[32];
        snprintf(pretty, sizeof(pretty), "%s", raw);

        if (strcmp(raw, "-") == 0) strcpy(pretty, "Minus");
        else if (strcmp(raw, "Keypad -") == 0) strcpy(pretty, "Minus");
        else if (strcmp(raw, "=") == 0) strcpy(pretty, "Equals");
        else if (strcmp(raw, "+") == 0) strcpy(pretty, "Plus");
        else if (strcmp(raw, "Keypad +") == 0) strcpy(pretty, "Plus");
        else if (strcmp(raw, "Return") == 0) strcpy(pretty, "Enter");
        else if (strcmp(raw, "Escape") == 0) strcpy(pretty, "Esc");
        else if (strcmp(raw, "Left Shift") == 0 || strcmp(raw, "Right Shift") == 0) strcpy(pretty, "Shift");
        else if (strcmp(raw, "Left Ctrl") == 0 || strcmp(raw, "Right Ctrl") == 0) strcpy(pretty, "Ctrl");
        else if (strcmp(raw, "Left Alt") == 0 || strcmp(raw, "Right Alt") == 0) strcpy(pretty, "Alt");

        strncat(key, pretty, sizeof(key) - strlen(key) - 1);
    } else {
        snprintf(key, sizeof(key), "%s", kbd->key.textinput);
    }

    return key;
}

static void apply_subtitle_override(VideoRenderer* vr, uint32_t color_rgb, int size, int margin_bottom) {
    if (!vr) return;
    vr_set_subtitle_style_override(vr, color_rgb, size, margin_bottom);
}

static void apply_audio_feature_toggles(VideoRenderer* vr, bool os_passthrough, bool night_mode, bool ambient_glow) {
    if (!vr) return;
    vr_set_os_passthrough(vr, os_passthrough ? 1 : 0);
    vr_set_night_mode(vr, night_mode ? 1 : 0);
    vr_set_ambient_glow(vr, ambient_glow ? 1 : 0);
}

#if defined(_WIN32) && BLACKOUT_MODE_TURN_OFF_MONITOR
typedef struct {
    HANDLE hPhysicalMonitor;
    WCHAR  szPhysicalMonitorDescription[128];
} AmpPhysicalMonitor;
typedef BOOL (WINAPI *PFN_GetNumPhysMon)(HMONITOR, LPDWORD);
typedef BOOL (WINAPI *PFN_GetPhysMon)(HMONITOR, DWORD, AmpPhysicalMonitor*);
typedef BOOL (WINAPI *PFN_DestroyPhysMon)(DWORD, AmpPhysicalMonitor*);
typedef BOOL (WINAPI *PFN_SetVCPFeature)(HANDLE, BYTE, DWORD);

static HMONITOR s_blkout_standby_hmons[MAX_BLACKOUT_WINDOWS];
static int      s_blkout_standby_count = 0;

static void blkout_ddc_power(HMONITOR hmon, DWORD value) {
    HMODULE dll = LoadLibraryA("dxva2.dll");
    if (!dll) return;
    PFN_GetNumPhysMon  fn_num  = (PFN_GetNumPhysMon) GetProcAddress(dll, "GetNumberOfPhysicalMonitorsFromHMONITOR");
    PFN_GetPhysMon     fn_get  = (PFN_GetPhysMon)    GetProcAddress(dll, "GetPhysicalMonitorsFromHMONITOR");
    PFN_DestroyPhysMon fn_dest = (PFN_DestroyPhysMon)GetProcAddress(dll, "DestroyPhysicalMonitors");
    PFN_SetVCPFeature  fn_vcp  = (PFN_SetVCPFeature) GetProcAddress(dll, "SetVCPFeature");
    if (fn_num && fn_get && fn_dest && fn_vcp) {
        DWORD count = 0;
        if (fn_num(hmon, &count) && count > 0) {
            AmpPhysicalMonitor* phys = (AmpPhysicalMonitor*)calloc(count, sizeof(AmpPhysicalMonitor));
            if (phys) {
                if (fn_get(hmon, count, phys)) {
                    for (DWORD j = 0; j < count; j++)
                        fn_vcp(phys[j].hPhysicalMonitor, 0xD6, value);
                }
                fn_dest(count, phys);
                free(phys);
            }
        }
    }
    FreeLibrary(dll);
}

static void blkout_restore_monitors(void) {
    for (int i = 0; i < s_blkout_standby_count; i++)
        blkout_ddc_power(s_blkout_standby_hmons[i], 1);
    s_blkout_standby_count = 0;
}
#endif

static void blackout_destroy_windows(SDL_Window* windows[MAX_BLACKOUT_WINDOWS], int* count) {
    if (!windows || !count) return;
#if defined(_WIN32) && BLACKOUT_MODE_TURN_OFF_MONITOR
    blkout_restore_monitors();
#endif
    for (int i = 0; i < *count && i < MAX_BLACKOUT_WINDOWS; i++) {
        if (windows[i]) {
            SDL_DestroyWindow(windows[i]);
            windows[i] = NULL;
        }
    }
    *count = 0;
}

static void blackout_sync_windows(SDL_Window* main_win,
                                  bool fullscreen_active,
                                  bool blackout_mode,
                                  SDL_Window* windows[MAX_BLACKOUT_WINDOWS],
                                  int* count)
{
    if (!main_win || !windows || !count) return;

    if (!fullscreen_active || !blackout_mode) {
#if defined(_WIN32) && BLACKOUT_MODE_TURN_OFF_MONITOR
        blkout_restore_monitors();
#endif
        blackout_destroy_windows(windows, count);
        return;
    }

#if defined(_WIN32) && BLACKOUT_MODE_TURN_OFF_MONITOR
    blkout_restore_monitors();
#endif
    blackout_destroy_windows(windows, count);

    int display_count = SDL_GetNumVideoDisplays();
    if (display_count <= 1) return;

    int main_display = SDL_GetWindowDisplayIndex(main_win);
    for (int i = 0; i < display_count && *count < MAX_BLACKOUT_WINDOWS; i++) {
        if (i == main_display) continue;

        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(i, &bounds) != 0) continue;

        SDL_Window* w = SDL_CreateWindow(
            "",
            bounds.x,
            bounds.y,
            bounds.w,
            bounds.h,
            SDL_WINDOW_BORDERLESS | SDL_WINDOW_SHOWN | SDL_WINDOW_SKIP_TASKBAR | SDL_WINDOW_ALWAYS_ON_TOP
        );
        if (!w) continue;

        SDL_Renderer* r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);
        if (r) {
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            SDL_RenderPresent(r);
            SDL_DestroyRenderer(r);
        }

        windows[*count] = w;
        (*count)++;

#if defined(_WIN32) && BLACKOUT_MODE_TURN_OFF_MONITOR
        if (s_blkout_standby_count < MAX_BLACKOUT_WINDOWS) {
            RECT r2 = { bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h };
            HMONITOR hmon = MonitorFromRect(&r2, MONITOR_DEFAULTTONEAREST);
            if (hmon) {
                blkout_ddc_power(hmon, 4);
                s_blkout_standby_hmons[s_blkout_standby_count++] = hmon;
            }
        }
#endif
    }
}

static void draw_ambient_glow(SDL_Renderer* ren, SDL_Rect video_dst, int win_w, int win_h,
                              SDL_Color top[AMBIENT_ZONES], SDL_Color bottom[AMBIENT_ZONES],
                              SDL_Color left[AMBIENT_ZONES], SDL_Color right[AMBIENT_ZONES]) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    SDL_Vertex verts[(AMBIENT_ZONES+1) * (AMBIENT_FADE_ROWS+1)];
    int        indices[AMBIENT_ZONES * AMBIENT_FADE_ROWS * 6];

    {
        int i = 0;
        for (int zv = 0; zv < AMBIENT_ZONES; zv++) {
            for (int fv = 0; fv < AMBIENT_FADE_ROWS; fv++) {
                int a = zv * (AMBIENT_FADE_ROWS+1) + fv;
                int b = (zv+1) * (AMBIENT_FADE_ROWS+1) + fv;
                int c = zv * (AMBIENT_FADE_ROWS+1) + fv + 1;
                int d = (zv+1) * (AMBIENT_FADE_ROWS+1) + fv + 1;
                indices[i++]=a; indices[i++]=b; indices[i++]=d;
                indices[i++]=a; indices[i++]=d; indices[i++]=c;
            }
        }
    }

#define ZONE_RGB(arr, p, vr, vg, vb) do { \
    float _f = (p) * (float)AMBIENT_ZONES - 0.5f; \
    if (_f < 0.0f) _f = 0.0f; \
    if (_f > (float)(AMBIENT_ZONES-1)) _f = (float)(AMBIENT_ZONES-1); \
    int _zi = (int)_f; float _zf = _f - (float)_zi; \
    int _zi2 = (_zi+1 < AMBIENT_ZONES) ? _zi+1 : _zi; \
    (vr)=(Uint8)((float)(arr)[_zi].r + _zf*((float)(arr)[_zi2].r-(float)(arr)[_zi].r)); \
    (vg)=(Uint8)((float)(arr)[_zi].g + _zf*((float)(arr)[_zi2].g-(float)(arr)[_zi].g)); \
    (vb)=(Uint8)((float)(arr)[_zi].b + _zf*((float)(arr)[_zi2].b-(float)(arr)[_zi].b)); \
} while(0)

    int top_h = video_dst.y;
    if (top_h > 0) {
        for (int zv = 0; zv <= AMBIENT_ZONES; zv++) {
            float p = (float)zv / (float)AMBIENT_ZONES;
            Uint8 r, g, b; ZONE_RGB(top, p, r, g, b);
            float x = (float)video_dst.x + p * (float)video_dst.w;
            for (int fv = 0; fv <= AMBIENT_FADE_ROWS; fv++) {
                float t = (float)fv / (float)AMBIENT_FADE_ROWS;
                SDL_Vertex* v = &verts[zv*(AMBIENT_FADE_ROWS+1)+fv];
                v->position.x = x;  v->position.y = t * (float)top_h;
                v->color.r = r; v->color.g = g; v->color.b = b;
                v->color.a = (Uint8)(t * t * 210.0f);
                v->tex_coord.x = 0; v->tex_coord.y = 0;
            }
        }
        SDL_RenderGeometry(ren, NULL, verts, (AMBIENT_ZONES+1)*(AMBIENT_FADE_ROWS+1),
                           indices, AMBIENT_ZONES*AMBIENT_FADE_ROWS*6);
    }

    int bot_y = video_dst.y + video_dst.h;
    int bot_h = win_h - bot_y;
    if (bot_h > 0) {
        for (int zv = 0; zv <= AMBIENT_ZONES; zv++) {
            float p = (float)zv / (float)AMBIENT_ZONES;
            Uint8 r, g, b; ZONE_RGB(bottom, p, r, g, b);
            float x = (float)video_dst.x + p * (float)video_dst.w;
            for (int fv = 0; fv <= AMBIENT_FADE_ROWS; fv++) {
                float t = 1.0f - (float)fv / (float)AMBIENT_FADE_ROWS;
                SDL_Vertex* v = &verts[zv*(AMBIENT_FADE_ROWS+1)+fv];
                v->position.x = x;
                v->position.y = (float)bot_y + (float)fv*(float)bot_h/(float)AMBIENT_FADE_ROWS;
                v->color.r = r; v->color.g = g; v->color.b = b;
                v->color.a = (Uint8)(t * t * 210.0f);
                v->tex_coord.x = 0; v->tex_coord.y = 0;
            }
        }
        SDL_RenderGeometry(ren, NULL, verts, (AMBIENT_ZONES+1)*(AMBIENT_FADE_ROWS+1),
                           indices, AMBIENT_ZONES*AMBIENT_FADE_ROWS*6);
    }

    int left_w = video_dst.x;
    if (left_w > 0) {
        for (int zv = 0; zv <= AMBIENT_ZONES; zv++) {
            float p = (float)zv / (float)AMBIENT_ZONES;
            Uint8 r, g, b; ZONE_RGB(left, p, r, g, b);
            float y = (float)video_dst.y + p * (float)video_dst.h;
            for (int fv = 0; fv <= AMBIENT_FADE_ROWS; fv++) {
                float t = (float)fv / (float)AMBIENT_FADE_ROWS;
                SDL_Vertex* v = &verts[zv*(AMBIENT_FADE_ROWS+1)+fv];
                v->position.x = t * (float)left_w;  v->position.y = y;
                v->color.r = r; v->color.g = g; v->color.b = b;
                v->color.a = (Uint8)(t * t * 210.0f);
                v->tex_coord.x = 0; v->tex_coord.y = 0;
            }
        }
        SDL_RenderGeometry(ren, NULL, verts, (AMBIENT_ZONES+1)*(AMBIENT_FADE_ROWS+1),
                           indices, AMBIENT_ZONES*AMBIENT_FADE_ROWS*6);
    }

    int right_x = video_dst.x + video_dst.w;
    int right_w = win_w - right_x;
    if (right_w > 0) {
        for (int zv = 0; zv <= AMBIENT_ZONES; zv++) {
            float p = (float)zv / (float)AMBIENT_ZONES;
            Uint8 r, g, b; ZONE_RGB(right, p, r, g, b);
            float y = (float)video_dst.y + p * (float)video_dst.h;
            for (int fv = 0; fv <= AMBIENT_FADE_ROWS; fv++) {
                float t = 1.0f - (float)fv / (float)AMBIENT_FADE_ROWS;
                SDL_Vertex* v = &verts[zv*(AMBIENT_FADE_ROWS+1)+fv];
                v->position.x = (float)right_x + (float)fv*(float)right_w/(float)AMBIENT_FADE_ROWS;
                v->position.y = y;
                v->color.r = r; v->color.g = g; v->color.b = b;
                v->color.a = (Uint8)(t * t * 210.0f);
                v->tex_coord.x = 0; v->tex_coord.y = 0;
            }
        }
        SDL_RenderGeometry(ren, NULL, verts, (AMBIENT_ZONES+1)*(AMBIENT_FADE_ROWS+1),
                           indices, AMBIENT_ZONES*AMBIENT_FADE_ROWS*6);
    }

#undef ZONE_RGB
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static bool abspath(const char* path, char* out, size_t out_size) {
    if (!path || !out || out_size == 0) return false;

#ifdef _WIN32
    if (_fullpath(out, path, out_size) == NULL)
        return false;

    for (char* p = out; *p; ++p) {
        if (*p == '\\') *p = '/';
    }

#else
    char* tmp = realpath(path, NULL);
    if (!tmp) return false;

    size_t len = strlen(tmp) + 1;
    if (len > out_size) {
        free(tmp);
        return false;
    }

    memcpy(out, tmp, len);
    free(tmp);
#endif

    return true;
}

static char* abspath_temp(const char* path) {
    static char buf[PATH_MAX];
    if (!path) return NULL;
    if (!abspath(path, buf, PATH_MAX) || !buf[0]) {
        return NULL;
    }
    return buf;
}

static char** _abspath_list = NULL;
static size_t _abspath_count = 0;
static size_t _abspath_cap = 0;

static char* abspath_temp_safe(const char* path) {
    if (!path) return NULL;

    char* buf = malloc(PATH_MAX);
    if (!buf) return NULL;

    if (!abspath(path, buf, PATH_MAX) || !buf[0]) {
        free(buf);
        return NULL;
    }

    if (_abspath_count == _abspath_cap) {
        size_t new_cap = _abspath_cap ? _abspath_cap * 2 : 8;
        char** n = realloc(_abspath_list, new_cap * sizeof(char*));
        if (!n) {
            free(buf);
            return NULL;
        }
        _abspath_list = n;
        _abspath_cap = new_cap;
    }

    _abspath_list[_abspath_count++] = buf;
    return buf;
}

static void abspath_temp_free(void) {
    for (size_t i = 0; i < _abspath_count; ++i) {
        free(_abspath_list[i]);
    }

    free(_abspath_list);
    _abspath_list = NULL;
    _abspath_count = 0;
    _abspath_cap = 0;
}

static void draw_rect(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &r);
}

static void format_time(double seconds, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    int s = (int)seconds;
    int h = s / 3600;
    int m = (s % 3600) / 60;
    int sec = s % 60;
    if (h > 0) snprintf(out, out_size, "%d:%02d:%02d", h, m, sec);
    else snprintf(out, out_size, "%d:%02d", m, sec);
}

static char* format_time_temp(double seconds) {
    static char buf[64];
    int s = (int)seconds;
    int h = s / 3600;
    int m = (s % 3600) / 60;
    int sec = s % 60;
    if (h > 0) snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, sec);
    else snprintf(buf, sizeof(buf), "%d:%02d", m, sec);
    return buf;
}

static void sanitize_recent_files(void);
#ifdef _WIN32
static void refresh_windows_recent_menu(void);
#endif

static const char* get_media_title(VideoRenderer* vr) {
    if (!vr || !vr->fmt_ctx) return NULL;
    AVDictionaryEntry* entry = av_dict_get(vr->fmt_ctx->metadata, "title", NULL, 0);
    if (!entry || !entry->value || !entry->value[0]) return NULL;
    return entry->value;
}

static int get_media_chapter_count(VideoRenderer* vr) {
    if (!vr || !vr->fmt_ctx || vr->fmt_ctx->nb_chapters <= 0) return 0;
    return (int)vr->fmt_ctx->nb_chapters;
}

static double get_media_chapter_time(VideoRenderer* vr, int chapter_index) {
    if (!vr || !vr->fmt_ctx) return -1.0;
    if (chapter_index < 0 || chapter_index >= (int)vr->fmt_ctx->nb_chapters) return -1.0;
    AVChapter* ch = vr->fmt_ctx->chapters[chapter_index];
    if (!ch) return -1.0;
    return (double)ch->start * av_q2d(ch->time_base);
}

static void get_media_chapter_label(VideoRenderer* vr, int chapter_index, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!vr || !vr->fmt_ctx || chapter_index < 0 || chapter_index >= (int)vr->fmt_ctx->nb_chapters) {
        snprintf(out, out_size, "Chapter %d", chapter_index + 1);
        return;
    }
    AVChapter* ch = vr->fmt_ctx->chapters[chapter_index];
    AVDictionaryEntry* entry = ch ? av_dict_get(ch->metadata, "title", NULL, 0) : NULL;
    if (entry && entry->value && entry->value[0]) {
        snprintf(out, out_size, "%s", entry->value);
    } else {
        snprintf(out, out_size, "Chapter %d", chapter_index + 1);
    }
}

static int find_nearest_chapter(
    VideoRenderer* vr,
    double duration,
    SDL_Rect timeline,
    int mouse_x,
    int threshold_px,
    int* out_index,
    double* out_time
) {
    if (!vr || duration <= 0.0 || timeline.w <= 0) return 0;
    int chapter_count = get_media_chapter_count(vr);
    if (chapter_count <= 0) return 0;

    int best_idx = -1;
    int best_dist = threshold_px + 1;
    double best_time = 0.0;
    for (int i = 0; i < chapter_count; i++) {
        double t = get_media_chapter_time(vr, i);
        if (t < 0.0 || t > duration) continue;
        int cx = timeline.x + (int)((t / duration) * timeline.w);
        int dist = abs(mouse_x - cx);
        if (dist <= threshold_px && dist < best_dist) {
            best_dist = dist;
            best_idx = i;
            best_time = t;
        }
    }

    if (best_idx < 0) return 0;
    if (out_index) *out_index = best_idx;
    if (out_time) *out_time = best_time;
    return 1;
}

static void set_window_title_for_media(SDL_Window* win, VideoRenderer* vr, const char* path) {
    if (!win || !path) return;
    const char* title = get_media_title(vr);
    if (title && title[0]) { 
        char buf[PATH_MAX];
        snprintf(buf, sizeof(buf), "%s - %s", title, path);
        SDL_SetWindowTitle(win, buf);
    } else {
        SDL_SetWindowTitle(win, path);
    }
}

static int parse_resolution_arg(const char* arg, int* out_w, int* out_h) {
    if (!arg || !out_w || !out_h) return 0;
    int w = 0, h = 0;
    if (sscanf(arg, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
        *out_w = w;
        *out_h = h;
        return 1;
    }
    return 0;
}

static void fit_window_to_display(int* w, int* h) {
    if (!w || !h || *w <= 0 || *h <= 0) return;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) != 0) return;

    float sx = (float)usable.w / (float)(*w);
    float sy = (float)usable.h / (float)(*h);
    float s = sx < sy ? sx : sy;
    if (s > 1.0f) s = 1.0f;
    if (s <= 0.0f) return;

    *w = (int)((float)(*w) * s);
    *h = (int)((float)(*h) * s);
    if (*w < 320) *w = 320;
    if (*h < 180) *h = 180;
}

static SDL_Rect compute_video_dst_rect(int win_w, int win_h, int src_w, int src_h, unsigned int ar_x, unsigned int ar_y) {
    SDL_Rect dst = {0, 0, win_w, win_h};
    if (win_w <= 0 || win_h <= 0 || src_w <= 0 || src_h <= 0) return dst;

    int eff_w = (ar_x == UINT_MAX) ? win_w : (ar_x == 0 ? src_w : (int)ar_x);
    int eff_h = (ar_y == UINT_MAX) ? win_h : (ar_y == 0 ? src_h : (int)ar_y);
    if (eff_w <= 0 || eff_h <= 0) return dst;

    float sx = (float)win_w / (float)eff_w;
    float sy = (float)win_h / (float)eff_h;
    float s = sx < sy ? sx : sy;
    if (ar_x == 0 && ar_y == 0 && s > 1.0f) s = 1.0f;
    if (s <= 0.0f) return dst;

    dst.w = (int)(eff_w * s);
    dst.h = (int)(eff_h * s);
    dst.x = (win_w - dst.w) / 2;
    dst.y = (win_h - dst.h) / 2;
    return dst;
}

static void apply_window_size_for_video(SDL_Window* win, SDL_Texture* texture, int requested_w, int requested_h) {
    if (!win || !texture) return;

    Uint32 window_flags = SDL_GetWindowFlags(win);
    if (window_flags & SDL_WINDOW_FULLSCREEN || window_flags & SDL_WINDOW_FULLSCREEN_DESKTOP || window_flags & SDL_WINDOW_MAXIMIZED) {
        return;
    }

    int video_w = 0;
    int video_h = 0;
    SDL_QueryTexture(texture, NULL, NULL, &video_w, &video_h);
    if (video_w <= 0 || video_h <= 0) return;

    int target_w = requested_w > 0 ? requested_w : video_w;
    int target_h = requested_h > 0 ? requested_h : video_h;
    fit_window_to_display(&target_w, &target_h);

    SDL_SetWindowSize(win, target_w, target_h);
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

static float volume_percent_to_gain(float percent) {
    if (percent <= 0.0f) return 0.0f;

    float db;

    if (percent <= 100.0f) {
        float t = percent / 100.0f;
        db = -50.0f * (1.0f - t);
    } else {
        float t = (percent - 100.0f) / 100.0f;
        db = t * 69.0f;
    }

    return powf(10.0f, db / 20.0f);
}

static int point_in_rect(int x, int y, SDL_Rect r) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

#if CHECK_FILE_SIGNATURE
static bool is_supported_video_file(const char* path) {
    if (!path) return false;

    av_log_set_level(AV_LOG_QUIET);

    AVFormatContext* ctx = NULL;

    if (avformat_open_input(&ctx, path, NULL, NULL) < 0)
        return false;

    if (avformat_find_stream_info(ctx, NULL) < 0) {
        avformat_close_input(&ctx);
        return false;
    }

    const char* name = ctx->iformat->name;

    bool ok =
        (name && (
            strstr(name, "matroska") ||   /* mkv */
            strstr(name, "mp4")           /* mp4/mov/m4a family */
        ));

    avformat_close_input(&ctx);

    av_log_set_level(AV_LOG_ERROR);

    return ok;
}
#else
static bool is_supported_video_file(const char* path) {
    if (!path) return false;
    const char* ext = strrchr(path, '.');
    if (!ext || !ext[1]) return false;
#ifdef _WIN32
    return _stricmp(ext, ".mkv") == 0 || _stricmp(ext, ".mp4") == 0;
#else
    return strcasecmp(ext, ".mkv") == 0 || strcasecmp(ext, ".mp4") == 0;
#endif
}
#endif

void amp_log_handler(Nob_Log_Level level, const char* fmt, va_list args) {
    time_t now = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif

    char timebuf[20];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_now);

    const char* level_str = "";
    switch (level) {
        case NOB_INFO:    level_str = "INFO   "; break;
        case NOB_WARNING: level_str = "WARNING"; break;
        case NOB_ERROR:   level_str = "ERROR  "; break;
        case NOB_NO_LOGS: return;
    }

    va_list args_print;
    va_copy(args_print, args);
    fprintf(stderr, "[%s] [%s] ", timebuf, level_str);
    vfprintf(stderr, fmt, args_print);
    va_end(args_print);
    fprintf(stderr, "\n");

    fflush(stderr);

    if (log_file) {
        va_list args_file;
        va_copy(args_file, args);
        fprintf(log_file, "[%s] [%s] ", timebuf, level_str);
        vfprintf(log_file, fmt, args_file);
        va_end(args_file);
        fprintf(log_file, "\n");
        fflush(log_file);
    }

    if (flash_debug_enabled && level == (Nob_Log_Level)flash_debug_level) {
        char flash_buf[256];
        va_list args_flash;
        va_copy(args_flash, args);
        vsnprintf(flash_buf, sizeof(flash_buf), fmt, args_flash);
        va_end(args_flash);
        snprintf(flash_text, sizeof(flash_text), "%s", flash_buf);
        flash_until = SDL_GetTicks() + 1200;
        flash_alpha = 1.0f;
    }
}

void usage(FILE* out, const char* prog_name) {
    fprintf(out, "Usage: %s [OPTIONS] [video_file]\n", prog_name);
    fprintf(out, "Supported video formats: Matroska (MKV), MP4\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  -h, --help                   Show this help message and exit\n");
    fprintf(out, "  -v, --version                Show version information and exit\n");
    fprintf(out, "  -p, --paused                 Start playback in paused state\n");
    fprintf(out, "  -f, --fullscreen             Start in fullscreen mode\n");
    fprintf(out, "  -m, --maximized              Start with window maximized\n");
    fprintf(out, "  --theme THEME_NAME           Load a theme from the assets/themes folder (e.g. --theme femboy)\n");
    fprintf(out, "  --themes                     List available themes in the assets/themes folder\n");
    fprintf(out, "  --resolution [WxH|native]    Set window resolution (e.g. 1280x720 or native)\n");
    fprintf(out, "  --volume [0-200]             Set initial audio volume (default: 100)\n");
    fprintf(out, "  --speed [SPEED > 0]          Set initial playback speed (e.g. 0.5, 1.0, 1.5)\n");
    fprintf(out, "  --flash-debug                Show log messages as on-screen flash\n");
    fprintf(out, "  --no-flash-debug             Disable on-screen flash for log messages\n");
    fprintf(out, "  --flash-debug-level [LEVEL]  Show log messages as on-screen flash (LEVEL: 0 - NO LOGS, 1 - INFO, 2 - WARNING, 3 - ERROR)\n");
    fprintf(out, "  --hw [auto|none|accel|TYPE]  Hardware decode backend (TYPE: vaapi [unix only], dxva2 [win only], d3d11va [win only])\n");
    fprintf(out, "  --log-file FILE              Log to a file in addition to stderr\n");
    fprintf(out, "  --audio-driver DRIVER        Set audio driver (e.g. pulseaudio, alsa, wasapi, directsound)\n");
    #ifdef _WIN32
    fprintf(out, "  --reregister                 Re-run Windows file association and shortcut setup\n");
    fprintf(out, "  --unregister                 Remove Windows file association\n");
    #endif
}

#ifdef _WIN32
static void attach_console_if_present(void) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
    }
}

static void get_self_path(char* buf, size_t size) {
    GetModuleFileNameA(NULL, buf, (DWORD)size);
}

static void get_self_dir(char* buf, size_t size) {
    get_self_path(buf, size);
    char* last = strrchr(buf, '\\');
    if (last) *last = '\0';
}

static void create_shortcut(const char* target) {
    char dir[PATH_MAX];
    get_self_dir(dir, sizeof(dir));

    char path[PATH_MAX+8];
    snprintf(path, sizeof(path), "%s\\amp.lnk", dir);

    CoInitialize(NULL);

    IShellLinkA* link;

    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IShellLinkA, (void**)&link))) {

        link->lpVtbl->SetPath(link, target);
        link->lpVtbl->SetDescription(link, "amp");

        IPersistFile* file;

        if (SUCCEEDED(link->lpVtbl->QueryInterface(link,
            &IID_IPersistFile, (void**)&file))) {

            wchar_t wpath[PATH_MAX];
            MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, PATH_MAX);
            file->lpVtbl->Save(file, wpath, TRUE);
            file->lpVtbl->Release(file);
        }

        link->lpVtbl->Release(link);
    }

    CoUninitialize();
}

static void create_startmenu_shortcut(const char* exe) {
    char path[PATH_MAX];

    snprintf(path, sizeof(path),
        "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\amp.lnk");

    CoInitialize(NULL);

    IShellLinkA* link;

    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IShellLinkA, (void**)&link))) {

        link->lpVtbl->SetPath(link, exe);
        link->lpVtbl->SetDescription(link, "amp");

        IPersistFile* file;

        if (SUCCEEDED(link->lpVtbl->QueryInterface(link,
            &IID_IPersistFile, (void**)&file))) {

            wchar_t wpath[PATH_MAX];
            MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, PATH_MAX);
            file->lpVtbl->Save(file, wpath, TRUE);
            file->lpVtbl->Release(file);
        }

        link->lpVtbl->Release(link);
    }

    CoUninitialize();
}

static void reg_set(HKEY root, const char* subkey,
    const char* name, const char* value) {

    HKEY key;

    if (RegCreateKeyExA(root, subkey, 0, NULL, 0,
        KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {

        if (value) {
            RegSetValueExA(key, name, 0, REG_SZ,
                (const BYTE*)value,
                (DWORD)(strlen(value) + 1));
        }

        RegCloseKey(key);
    }
}

static void reg_delete_value(HKEY root, const char* subkey, const char* name) {
    HKEY key;

    if (RegOpenKeyExA(root, subkey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueA(key, name);
        RegCloseKey(key);
    }
}

static int reg_get_bool(HKEY root, const char* subkey, const char* name) {
    HKEY key;

    if (RegOpenKeyExA(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return 0;

    char buf[8];
    DWORD size = sizeof(buf);
    int result = 0;

    if (RegQueryValueExA(key, name, NULL, NULL,
        (BYTE*)buf, &size) == ERROR_SUCCESS) {
        if (strcmp(buf, "1") == 0)
            result = 1;
    }

    RegCloseKey(key);
    return result;
}

static void register_progid_for_ext(const char* exe, const char* ext) {
    char progid[32];
    char label[64];

    if (strcmp(ext, ".mp4") == 0) {
        snprintf(progid, sizeof(progid), "amp.mp4");
        snprintf(label, sizeof(label), "MP4 File");
    } else {
        snprintf(progid, sizeof(progid), "amp.mkv");
        snprintf(label, sizeof(label), "MKV File");
    }

    char dir[PATH_MAX];
    snprintf(dir, PATH_MAX, "%s", exe);
    char* last = strrchr(dir, '\\');
    if (last) *last = '\0';

    char ico[PATH_MAX+16];
    snprintf(ico, sizeof(ico), "%s\\assets\\file.ico", dir);

    char key[256];
    char cmd[512];

    snprintf(key, sizeof(key), "Software\\Classes\\%s", progid);
    reg_set(HKEY_CURRENT_USER, key, NULL, label);

    snprintf(cmd, sizeof(cmd), "\"%s\" \"%%1\"", exe);
    snprintf(key, sizeof(key), "Software\\Classes\\%s\\shell\\open\\command", progid);
    reg_set(HKEY_CURRENT_USER, key, NULL, cmd);

    snprintf(key, sizeof(key), "Software\\Classes\\%s\\DefaultIcon", progid);
    reg_set(HKEY_CURRENT_USER, key, NULL, ico);
}

static void register_openwith(const char* ext) {
    char progid[32];
    snprintf(progid, sizeof(progid),
        strcmp(ext, ".mp4") == 0 ? "amp.mp4" : "amp.mkv");

    char key[256];
    snprintf(key, sizeof(key), "Software\\Classes\\%s\\OpenWithProgids", ext);
    reg_set(HKEY_CURRENT_USER, key, progid, "");
}

static void register_ext(const char* ext) {
    char progid[32];
    snprintf(progid, sizeof(progid),
        strcmp(ext, ".mp4") == 0 ? "amp.mp4" : "amp.mkv");

    char key[256];

    snprintf(key, sizeof(key), "Software\\Classes\\%s", ext);
    reg_set(HKEY_CURRENT_USER, key, NULL, progid);

    snprintf(key, sizeof(key), "Software\\Classes\\%s\\OpenWithProgids", ext);
    reg_set(HKEY_CURRENT_USER, key, progid, "");
}

static void clear_fileext_cache(const char* ext) {
    char key[256];

    snprintf(key, sizeof(key),
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s",
        ext);

    RegDeleteTreeA(HKEY_CURRENT_USER, key);
}

void amp_register(bool reregister) {
    const char* flag_key = "Software\\amp";

    if (reg_get_bool(HKEY_CURRENT_USER, flag_key, "installed") && !reregister)
        return;

    char exe[PATH_MAX];
    get_self_path(exe, sizeof(exe));

    clear_fileext_cache(".mp4");
    clear_fileext_cache(".mkv");

    create_shortcut(exe);
    create_startmenu_shortcut(exe);

    register_progid_for_ext(exe, ".mp4");
    register_progid_for_ext(exe, ".mkv");

    register_openwith(".mp4");
    register_openwith(".mkv");

    register_ext(".mp4");
    register_ext(".mkv");

    reg_set(HKEY_CURRENT_USER,
        "Software\\Classes\\Applications\\main.exe",
        "FriendlyAppName", "amp");

    reg_set(HKEY_CURRENT_USER, flag_key, "installed", "1");

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    nob_log(NOB_INFO, "amp registered as handler for .mp4 and .mkv files");
}

void amp_unregister(void) {
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\amp.mp4");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\amp.mkv");

    reg_delete_value(HKEY_CURRENT_USER,
        "Software\\Classes\\.mp4\\OpenWithProgids", "amp.mp4");
    reg_delete_value(HKEY_CURRENT_USER,
        "Software\\Classes\\.mkv\\OpenWithProgids", "amp.mkv");

    reg_delete_value(HKEY_CURRENT_USER,
        "Software\\Classes\\Applications\\main.exe", "FriendlyAppName");

    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\amp");

    RegDeleteTreeA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.mp4");
    RegDeleteTreeA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.mkv");

    DeleteFileA(
        "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\amp.lnk");

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    nob_log(NOB_INFO, "amp unregistered as handler for .mp4 and .mkv files");
}
#endif

void add_recent_file(const char* file) {
    nob_log(NOB_INFO, "Adding to recent files: %s", file);

    for (int i = 0; i < recent_count; i++) {
        if (recent_files[i] && strcmp(recent_files[i], file) == 0) {
            free(recent_files[i]);
            for (int j = i; j < recent_count - 1; j++) recent_files[j] = recent_files[j+1];
            recent_files[recent_count - 1] = NULL;
            recent_count--;
            break;
        }
    }

    if (recent_count == MAX_RECENT) {
        free(recent_files[MAX_RECENT - 1]);
        recent_files[MAX_RECENT - 1] = NULL;
        recent_count--;
    }
    for (int i = recent_count; i > 0; i--) {
        recent_files[i] = recent_files[i-1];
    }
    recent_files[0] = strdup(file);
    recent_count++;

    sanitize_recent_files();

#ifdef _WIN32
    refresh_windows_recent_menu();
#endif
}

char* open_file_dialog(const char* filters[], int filter_count, const char* filter_desc, bool allow_multiple, const char* title, const char* default_path, bool (*validator)(const char* path)) {
    char const* filename = tinyfd_openFileDialog(
        title ? title : "Select File",
        default_path ? default_path : "",
        filter_count,
        filters,
        filter_desc,
        allow_multiple ? 1 : 0
    );
    if (!filename) return NULL;
    if (validator && !validator(filename)) return NULL;
    char* abs_path = abspath_temp_safe(filename);
    SDL_PumpEvents();
    return abs_path;
}

static int path_equals(const char* a, const char* b) {
    if (!a || !b) return 0;

    char pa[PATH_MAX];
    char pb[PATH_MAX];

    if (!abspath(a, pa, PATH_MAX)) {
        strncpy(pa, a, PATH_MAX - 1);
        pa[PATH_MAX - 1] = 0;
    }
    if (!abspath(b, pb, PATH_MAX)) {
        strncpy(pb, b, PATH_MAX - 1);
        pb[PATH_MAX - 1] = 0;
    }

    for (char* p = pa; *p; ++p)
        if (*p == '\\') *p = '/';
    for (char* p = pb; *p; ++p)
        if (*p == '\\') *p = '/';

#ifdef _WIN32
    return _stricmp(pa, pb) == 0;
#else
    return strcmp(pa, pb) == 0;
#endif
}

static int recent_path_exists(const char* path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    return _access(path, 0) == 0;
#else
    return access(path, F_OK) == 0;
#endif
}

static void sanitize_recent_files(void) {
    int write_idx = 0;
    for (int i = 0; i < recent_count && i < MAX_RECENT; i++) {
        char* path = recent_files[i];
        if (!path || !path[0] || !is_supported_video_file(path) || !recent_path_exists(path)) {
            if (path) free(path);
            recent_files[i] = NULL;
            continue;
        }

        int duplicate = 0;
        for (int j = 0; j < write_idx; j++) {
            if (path_equals(recent_files[j], path)) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            free(path);
            recent_files[i] = NULL;
            continue;
        }

        recent_files[write_idx++] = path;
    }

    for (int i = write_idx; i < MAX_RECENT; i++) recent_files[i] = NULL;
    recent_count = write_idx;
}

static int path_name_cmp(const void* lhs, const void* rhs) {
    const char* a = *(const char* const*)lhs;
    const char* b = *(const char* const*)rhs;
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

static void free_path_list(char** list, int count) {
    if (!list) return;
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);
}

static int get_adjacent_supported_media(const char* current_media_path, int direction, int wrap, char** out_path) {
    if (!current_media_path || !out_path || (direction != -1 && direction != 1)) return 0;

    const char* slash1 = strrchr(current_media_path, '/');
    const char* slash2 = strrchr(current_media_path, '\\');
    const char* slash = slash1;
    if (!slash || (slash2 && slash2 > slash)) slash = slash2;
    if (!slash) return 0;

    size_t dir_len = (size_t)(slash - current_media_path);
    if (dir_len == 0) return 0;

    char dir_path[PATH_MAX];
    if (dir_len >= sizeof(dir_path)) return 0;
    memcpy(dir_path, current_media_path, dir_len);
    dir_path[dir_len] = '\0';

    char** files = NULL;
    int count = 0;

#ifdef _WIN32
    char pattern[PATH_MAX];
    if (dir_len + 3 >= sizeof(pattern)) return 0;
    memcpy(pattern, dir_path, dir_len);
    pattern[dir_len] = '\\';
    pattern[dir_len + 1] = '*';
    pattern[dir_len + 2] = '\0';

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char full[PATH_MAX];
        size_t name_len = strlen(fd.cFileName);
        if (dir_len + 1 + name_len + 1 >= sizeof(full)) continue;
        memcpy(full, dir_path, dir_len);
        full[dir_len] = '\\';
        memcpy(full + dir_len + 1, fd.cFileName, name_len);
        full[dir_len + 1 + name_len] = '\0';
        char** grown = (char**)realloc(files, sizeof(char*) * (size_t)(count + 1));
        if (!grown) continue;
        files = grown;
        files[count++] = strdup(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir_path);
    if (!d) return 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char full[PATH_MAX];

        size_t n = sizeof(full);
        int written = snprintf(full, n, "%s/%s", dir_path, ent->d_name);
        if (written < 0 || (size_t)written >= n) {
            continue;
        }

        char** grown = (char**)realloc(files, sizeof(char*) * (size_t)(count + 1));
        if (!grown) continue;
        files = grown;
        files[count++] = strdup(full);
    }
    closedir(d);
#endif

    if (count <= 0) {
        free_path_list(files, count);
        return 0;
    }

    qsort(files, (size_t)count, sizeof(files[0]), path_name_cmp);

    int current_index = -1;
    for (int i = 0; i < count; i++) {
        if (path_equals(files[i], current_media_path)) {
            current_index = i;
            break;
        }
    }

    if (current_index < 0) {
        free_path_list(files, count);
        return 0;
    }

    int i = current_index;

    while (1) {
        i += direction;

        if (i < 0 || i >= count) {
            if (!wrap) {
                free_path_list(files, count);
                return 0;
            }
            i = (direction > 0) ? 0 : (count - 1);
        }

        if (i == current_index) {
            free_path_list(files, count);
            return 0;
        }

        if (is_supported_video_file(files[i])) {
            *out_path = strdup(files[i]);
            free_path_list(files, count);
            return *out_path != NULL;
        }
    }
}

static int load_media_file(
    VideoRenderer** vr,
    SDL_Window* win,
    SDL_Renderer* ren,
    char** video_file,
    const char* path,
    float volume_percent,
    uint32_t subtitle_color,
    int subtitle_size,
    int subtitle_margin,
    bool os_passthrough,
    bool night_mode,
    bool ambient_glow,
    int requested_window_w,
    int requested_window_h,
    const char* hw_option
) {
    if (!vr || !win || !ren || !video_file || !path) return 0;

    char* abs_path = strdup(abspath_temp_safe(path));
    if (!abs_path) return 0;
    if (!is_supported_video_file(abs_path)) {
        free(abs_path);
        return 0;
    }

    if (!*vr) *vr = vr_create(win, ren);
    if (!*vr || !vr_load(*vr, abs_path, hw_option)) {
        free(abs_path);
        return 0;
    }

    apply_audio_feature_toggles(*vr, os_passthrough, night_mode, ambient_glow);
    vr_set_volume(*vr, volume_percent_to_gain(volume_percent));
    apply_subtitle_override(*vr, subtitle_color, subtitle_size, subtitle_margin);
    apply_window_size_for_video(win, vr_get_texture(*vr), requested_window_w, requested_window_h);
    set_window_title_for_media(win, *vr, abs_path);
    add_recent_file(abs_path);

    *video_file = abs_path;
    return 1;
}

static void seek_and_preview_if_paused(VideoRenderer* vr, double seconds, int paused) {
    if (!vr) return;
    vr_seek(vr, seconds);
    if (paused) {
        int rendered = 0;
        for (int i = 0; i < 64 && !rendered; i++) {
            vr_demux_packets(vr);
            rendered = vr_render_frame(vr);
        }
    } else {
        vr_demux_packets(vr);
    }
}

static void hist_entry_free(HistoryEntry* e) {
    if (e && e->kind == HIST_SEEK) {
        free(e->seek.before_path);
        e->seek.before_path = NULL;
        free(e->seek.after_path);
        e->seek.after_path = NULL;
    }
}

static void hist_clear_from(HistoryEntry* history, int* cnt, int from) {
    if (!cnt) return;
    if (from < 0) from = 0;
    if (from > *cnt) from = *cnt;
    for (int i = from; i < *cnt; i++) hist_entry_free(&history[i]);
    *cnt = from;
}

static void hist_push(HistoryEntry* history, int* cnt, int* pos, HistoryEntry e) {
    if (!cnt || !pos) return;
    int clear_from = *pos + 1;
    if (clear_from < 0) clear_from = 0;
    hist_clear_from(history, cnt, clear_from);
    if (*cnt >= MAX_HISTORY) {
        hist_entry_free(&history[0]);
        memmove(&history[0], &history[1], sizeof(history[0]) * (MAX_HISTORY - 1));
        memset(&history[MAX_HISTORY - 1], 0, sizeof(history[0]));
        *cnt = MAX_HISTORY - 1;
        (*pos)--;
    }
    history[*cnt] = e;
    *pos = *cnt;
    (*cnt)++;
}

static void hist_push_seek(
    HistoryEntry* history, int* cnt, int* pos,
    const char* before_path, double before_time,
    const char* after_path, double after_time
) {
    HistoryEntry e;
    memset(&e, 0, sizeof(e));
    e.kind = HIST_SEEK;
    e.seek.before_path = before_path ? strdup(before_path) : NULL;
    e.seek.before_time = before_time;
    e.seek.after_path = after_path ? strdup(after_path) : NULL;
    e.seek.after_time = after_time;
    hist_push(history, cnt, pos, e);
}

static void sort_bookmarks_by_time(Bookmark* bms, int count) {
    for (int i = 1; i < count; i++) {
        Bookmark key = bms[i];
        int j = i - 1;
        while (j >= 0 && bms[j].time > key.time) { bms[j + 1] = bms[j]; j--; }
        bms[j + 1] = key;
    }
}

static void hist_push_bookmark(
    HistoryEntry* history, int* cnt, int* pos,
    const Bookmark* before_bms, int before_count,
    const Bookmark* after_bms, int after_count,
    const char* desc
) {
    HistoryEntry e;
    memset(&e, 0, sizeof(e));
    e.kind = HIST_BOOKMARK;
    memcpy(e.bookmark.before.bms, before_bms, sizeof(Bookmark) * (size_t)before_count);
    e.bookmark.before.count = before_count;
    memcpy(e.bookmark.after.bms, after_bms, sizeof(Bookmark) * (size_t)after_count);
    e.bookmark.after.count = after_count;
    snprintf(e.desc, sizeof(e.desc), "%s", desc ? desc : "bookmark change");
    hist_push(history, cnt, pos, e);
}

static void hist_push_volume(
    HistoryEntry* history, int* cnt, int* pos,
    float before, float after
) {
    HistoryEntry e;
    memset(&e, 0, sizeof(e));
    e.kind = HIST_VOLUME;
    e.volume.before = before;
    e.volume.after = after;
    hist_push(history, cnt, pos, e);
}

static int hist_apply_entry(
    const HistoryEntry* e, int apply_before,
    VideoRenderer** vr,
    SDL_Window* win,
    SDL_Renderer* ren,
    char** video_file,
    int paused,
    float* volume_percent,
    uint32_t subtitle_color,
    int subtitle_size,
    int subtitle_margin,
    int requested_window_w,
    int requested_window_h,
    bool os_passthrough,
    bool night_mode,
    bool ambient_glow,
    Bookmark* bookmarks,
    int* bookmark_count,
    SaveState* save_state
) {
    if (!e) return 0;
    switch (e->kind) {
        case HIST_SEEK: {
            const char* path = apply_before ? e->seek.before_path : e->seek.after_path;
            double t = apply_before ? e->seek.before_time : e->seek.after_time;
            if (!path) return 0;
            if (!video_file || !*video_file || !path_equals(*video_file, path)) {
                if (!load_media_file(vr, win, ren, video_file, path,
                        *volume_percent, subtitle_color, subtitle_size, subtitle_margin,
                    os_passthrough, night_mode, ambient_glow,
                        requested_window_w, requested_window_h, hw_option))
                    return 0;
            }
            if (*vr) {
                vr_set_paused(*vr, paused);
                seek_and_preview_if_paused(*vr, t, paused);
            }
            {
                const char* base = strrchr(path, '\\');
                if (!base) base = strrchr(path, '/');
                snprintf(flash_text, sizeof(flash_text), "%s: %s %s",
                    apply_before ? "Undo" : "Redo",
                    base ? base + 1 : path,
                    format_time_temp(t));
            }
            return 1;
        }
        case HIST_BOOKMARK: {
            const BmSnap* snap = apply_before ? &e->bookmark.before : &e->bookmark.after;
            memcpy(bookmarks, snap->bms, sizeof(Bookmark) * (size_t)snap->count);
            *bookmark_count = snap->count;
            #if SAVE_FILE
            update_bookmarks_in_save_state(save_state, *video_file, bookmarks, *bookmark_count);
            write_save_state(SAVE_FILE_PATH, save_state);
            #endif
            snprintf(flash_text, sizeof(flash_text), "%s: %.249s",
                apply_before ? "Undo" : "Redo",
                e->desc[0] ? e->desc : "bookmark change");
            return 1;
        }
        case HIST_VOLUME: {
            float val = apply_before ? e->volume.before : e->volume.after;
            *volume_percent = val;
            if (*vr) vr_set_volume(*vr, volume_percent_to_gain(val));
            snprintf(flash_text, sizeof(flash_text), "%s: volume %d%% -> %d%%",
                apply_before ? "Undo" : "Redo",
                apply_before ? (int)e->volume.after : (int)e->volume.before,
                (int)val);
            return 1;
        }
    }
    return 0;
}

static int hist_undo(
    HistoryEntry* history, int* cnt, int* pos,
    VideoRenderer** vr, SDL_Window* win, SDL_Renderer* ren,
    char** video_file, int paused, float* volume_percent,
    uint32_t subtitle_color, int subtitle_size, int subtitle_margin,
    int req_w, int req_h, Bookmark* bookmarks, int* bookmark_count,
    bool os_passthrough, bool night_mode, bool ambient_glow,
    SaveState* save_state
) {
    (void)cnt;
    if (*pos < 0) return 0;
    int ok = hist_apply_entry(&history[*pos], 1, vr, win, ren, video_file, paused,
        volume_percent, subtitle_color, subtitle_size, subtitle_margin,
        req_w, req_h, os_passthrough, night_mode, ambient_glow, bookmarks, bookmark_count, save_state);
    if (ok) (*pos)--;
    return ok;
}

static int hist_redo(
    HistoryEntry* history, int* cnt, int* pos,
    VideoRenderer** vr, SDL_Window* win, SDL_Renderer* ren,
    char** video_file, int paused, float* volume_percent,
    uint32_t subtitle_color, int subtitle_size, int subtitle_margin,
    int req_w, int req_h, Bookmark* bookmarks, int* bookmark_count,
    bool os_passthrough, bool night_mode, bool ambient_glow,
    SaveState* save_state
) {
    if (*pos + 1 >= *cnt) return 0;
    (*pos)++;
    int ok = hist_apply_entry(&history[*pos], 0, vr, win, ren, video_file, paused,
        volume_percent, subtitle_color, subtitle_size, subtitle_margin,
        req_w, req_h, os_passthrough, night_mode, ambient_glow, bookmarks, bookmark_count, save_state);
    if (!ok) (*pos)--;
    return ok;
}

static int navigate_media(
    VideoRenderer** vr,
    SDL_Window* win,
    SDL_Renderer* ren,
    char** video_file,
    int direction,
    int paused,
    float* volume_percent,
    uint32_t subtitle_color,
    int subtitle_size,
    int subtitle_margin,
    int wrap_when_boundary,
    bool os_passthrough,
    bool night_mode,
    bool ambient_glow,
    int requested_window_w,
    int requested_window_h,
    HistoryEntry history[MAX_HISTORY],
    int* history_count,
    int* history_pos,
    SaveState* save_state,
    Bookmark* bookmarks,
    int* bookmark_count
) {
    if (!vr || !*vr || !video_file || !*video_file) return 0;

    double before = vr_get_time(*vr);

    if (direction < 0 && before > 3.0) {
        hist_push_seek(history, history_count, history_pos, *video_file, before, *video_file, 0.0);
        seek_and_preview_if_paused(*vr, 0.0, paused);
        snprintf(flash_text, sizeof(flash_text), "Restarted");
        flash_until = SDL_GetTicks() + 900;
        return 1;
    }

    char prev_audio_name[256]    = "";
    char prev_subtitle_name[256] = "";
    int  prev_subtitle_idx       = (*vr)->current_subtitle;
    {
        const char* an = vr_get_audio_track_name(*vr, (*vr)->current_audio);
        const char* sn = prev_subtitle_idx >= 0
                       ? vr_get_subtitle_track_name(*vr, prev_subtitle_idx)
                       : NULL;
        if (an) strncpy(prev_audio_name,    an, sizeof(prev_audio_name)    - 1);
        if (sn) strncpy(prev_subtitle_name, sn, sizeof(prev_subtitle_name) - 1);
    }

    char* target_path = NULL;
    if (!get_adjacent_supported_media(*video_file, direction, wrap_when_boundary, &target_path)) {
        snprintf(flash_text, sizeof(flash_text), "%s media", direction > 0 ? "No next" : "No previous");
        flash_until = SDL_GetTicks() + 900;
        return 0;
    }

    const char* before_path = *video_file;

    #if SAVE_FILE
    if (save_state && bookmarks && bookmark_count && *video_file) {
        update_bookmarks_in_save_state(save_state, *video_file, bookmarks, *bookmark_count);
        write_save_state(SAVE_FILE_PATH, save_state);
    }
    #endif

    int ok = load_media_file(
        vr, win, ren, video_file, target_path,
        *volume_percent,
        subtitle_color,
        subtitle_size,
        subtitle_margin,
        os_passthrough,
        night_mode,
        ambient_glow,
        requested_window_w,
        requested_window_h,
        hw_option);
    free(target_path);

    if (!ok) {
        snprintf(flash_text, sizeof(flash_text), "Failed to load media");
        flash_until = SDL_GetTicks() + 900;
        return 0;
    }

    if (*vr) {
        vr_set_paused(*vr, paused);
        seek_and_preview_if_paused(*vr, 0.0, paused);
    }

    #if SAVE_FILE
    if (save_state && *video_file) {
        bool has_saved_entry = get_remembered_file_index(save_state, *video_file, NULL) >= 0;
        apply_save_state_to_vr(*vr, save_state, *video_file, volume_percent);
        vr_set_volume(*vr, volume_percent_to_gain(*volume_percent));
        if (*vr && (*vr)->desired_win_w > 0 && (*vr)->desired_win_h > 0) {
            apply_window_size_for_video(win, vr_get_texture(*vr), (*vr)->desired_win_w, (*vr)->desired_win_h);
        }
        if (!has_saved_entry && *vr) {
            if (prev_audio_name[0]) {
                int ac = vr_get_audio_track_count(*vr);
                for (int i = 0; i < ac; i++) {
                    const char* n = vr_get_audio_track_name(*vr, i);
                    if (n && strcmp(n, prev_audio_name) == 0) {
                        vr_select_audio_track(*vr, i);
                        break;
                    }
                }
            }
            if (prev_subtitle_idx < 0) {
                vr_select_subtitle_track(*vr, -1);
            } else if (prev_subtitle_name[0]) {
                int sc = vr_get_subtitle_track_count(*vr);
                for (int i = 0; i < sc; i++) {
                    const char* n = vr_get_subtitle_track_name(*vr, i);
                    if (n && strcmp(n, prev_subtitle_name) == 0) {
                        vr_select_subtitle_track(*vr, i);
                        break;
                    }
                }
            }
        }
    }
    if (save_state && bookmarks && bookmark_count && *video_file) {
        get_bookmarks_from_save_state(save_state, *video_file, bookmarks, bookmark_count);
    }
    #endif

    hist_push_seek(history, history_count, history_pos, before_path, before, *video_file, 0.0);
    snprintf(flash_text, sizeof(flash_text), "%s media", direction > 0 ? "Next" : "Previous");
    flash_until = SDL_GetTicks() + 900;
    return 1;
}


static void run_playback_tick(VideoRenderer* vr, float playback_speed) {
    if (!vr) return;

    vr_demux_packets(vr);
    if (playback_speed <= 2.0f) {
        vr_decode_audio(vr);
    }
    vr_render_frame(vr);

    if (vr->audio_dev && playback_speed <= 2.0f) {
        double video_time = vr_get_video_time(vr);
        double audio_time = vr_get_audio_time(vr);
        double diff = video_time - audio_time;

        if (diff < -0.05) {
            int catchup = 0;
            while (diff < -0.02 && catchup < 8) {
                if (!vr_render_frame(vr)) {
                    vr_demux_packets(vr);
                    if (!vr_render_frame(vr)) break;
                }
                video_time = vr_get_video_time(vr);
                audio_time = vr_get_audio_time(vr);
                diff = video_time - audio_time;
                catchup++;
            }
        } else if (diff > 0.01) {
            int delay_ms = (int)(diff * 1000.0);
            if (delay_ms > 120) delay_ms = 120;
            SDL_Delay(delay_ms);
        }
    } else {
        double master_time = vr_get_master_time(vr);
        double video_time = vr_get_video_time(vr);
        int catchup = 0;
        while (video_time < master_time - 0.001 && catchup < 30) {
            vr_demux_packets(vr);
            if (!vr_render_frame(vr)) break;
            video_time = vr_get_video_time(vr);
            catchup++;
        }
        SDL_Delay(1);
    }
}

#ifdef _WIN32
static WNDPROC g_prev_menu_wndproc = NULL;
static VideoRenderer** g_menu_vr_ptr = NULL;
static SDL_Window* g_menu_win = NULL;
static SDL_Renderer* g_menu_ren = NULL;
static bool* g_menu_paused_ptr = NULL;
static float* g_menu_playback_speed_ptr = NULL;
static bool* g_menu_ambient_glow_ptr = NULL;
static HMENU g_current_win_menu = NULL;

static SDL_Rect apply_zoom_to_rect(SDL_Rect r, int win_w, int win_h, int zoom_percent);

static void menu_pump_playback_tick(void) {
    if (!g_menu_vr_ptr || !*g_menu_vr_ptr || !g_menu_win || !g_menu_ren || !g_menu_paused_ptr || !g_menu_playback_speed_ptr) return;

    VideoRenderer* vr = *g_menu_vr_ptr;
    if (*g_menu_paused_ptr) return;

    run_playback_tick(vr, *g_menu_playback_speed_ptr);

    SDL_SetRenderDrawColor(g_menu_ren, THEME_LETTERBOX_COLOR[0], THEME_LETTERBOX_COLOR[1], THEME_LETTERBOX_COLOR[2], 255);
    SDL_RenderClear(g_menu_ren);

    SDL_Texture* tex = vr_get_texture(vr);
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(g_menu_win, &window_w, &window_h);
    SDL_Rect video_dst = {0, 0, window_w, window_h};
    if (tex) {
        int src_w = 0;
        int src_h = 0;
        SDL_QueryTexture(tex, NULL, NULL, &src_w, &src_h);
        video_dst = compute_video_dst_rect(window_w, window_h, src_w, src_h, vr->ar_x, vr->ar_y);
        video_dst = apply_zoom_to_rect(video_dst, window_w, window_h, vr->zoom_percent);
        if (g_menu_ambient_glow_ptr && *g_menu_ambient_glow_ptr && vr_get_ambient_glow(vr)) {
            SDL_Color ag_top[AMBIENT_ZONES], ag_bottom[AMBIENT_ZONES], ag_left[AMBIENT_ZONES], ag_right[AMBIENT_ZONES];
            vr_get_ambient_colors(vr, ag_top, ag_bottom, ag_left, ag_right);
            draw_ambient_glow(g_menu_ren, video_dst, window_w, window_h, ag_top, ag_bottom, ag_left, ag_right);
        }
        SDL_RenderCopy(g_menu_ren, tex, NULL, &video_dst);
        if (vr_render_subtitles(vr, vr_get_video_time(vr))) {
            SDL_Texture* sub = vr_get_subtitle_texture(vr);
            if (sub) SDL_RenderCopy(g_menu_ren, sub, NULL, &video_dst);
        }
    }

    SDL_RenderPresent(g_menu_ren);
}

static LRESULT CALLBACK amp_menu_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ENTERMENULOOP:
            SetTimer(hwnd, 1, 15, NULL);
            break;
        case WM_EXITMENULOOP:
            KillTimer(hwnd, 1);
            break;
        case WM_TIMER:
            if (wParam == 1) {
                menu_pump_playback_tick();
                return 0;
            }
            break;
        default:
            break;
    }
    if (g_prev_menu_wndproc) {
        return CallWindowProc(g_prev_menu_wndproc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

enum {
    MENU_OPEN = 1,
    MENU_EXIT,
    MENU_FULLSCREEN,
    MENU_MAXIMIZE,
    MENU_MINIMIZE,
    MENU_RESOLUTION_NATIVE,
    MENU_RESOLUTION_CUSTOM,
    MENU_RESOLUTION_480P,
    MENU_RESOLUTION_720P,
    MENU_RESOLUTION_1080P,
    MENU_RESOLUTION_1440P,
    MENU_ASPECT_RATIO_1_1,
    MENU_ASPECT_RATIO_4_3,
    MENU_ASPECT_RATIO_16_9,
    MENU_ASPECT_RATIO_21_9,
    MENU_ASPECT_RATIO_STRETCH,
    MENU_ASPECT_RATIO_ORIGINAL,
    MENU_ASPECT_RATIO_CUSTOM,
    MENU_ZOOM_IN,
    MENU_ZOOM_OUT,
    MENU_ZOOM_RESET,
    MENU_ZOOM_CUSTOM,
    MENU_PLAY_PAUSE,
    MENU_NEXT_FRAME,
    MENU_PREV_FRAME,
    MENU_NEXT_MEDIA,
    MENU_PREV_MEDIA,
    MENU_NEXT_MEDIA_WRAP,
    MENU_PREV_MEDIA_WRAP,
    MENU_SEEK_BACK,
    MENU_SEEK_FORWARD,
    MENU_SEEK_BEGINNING,
    MENU_SEEK_END,
    MENU_VOLUME_UP,
    MENU_VOLUME_DOWN,
    MENU_VOLUME_UP_FINE,
    MENU_VOLUME_DOWN_FINE,
    MENU_SET_VOLUME0,
    MENU_SET_VOLUME100,
    MENU_SET_VOLUME200,
    MENU_ADD_BOOKMARK,
    MENU_DELETE_BOOKMARK,
    MENU_NEXT_BOOKMARK,
    MENU_PREV_BOOKMARK,
    MENU_UNDO,
    MENU_REDO,
    MENU_DRAW_SCREENSHOT,
    MENU_DRAW_ENTER,
    MENU_DRAW_EXIT,
    MENU_DRAW_EXPORT_WITH_FRAME,
    MENU_DRAW_EXPORT_ONLY,
    MENU_DRAW_CLEAR,
    MENU_DRAW_TOOL_PEN,
    MENU_DRAW_TOOL_ERASER,
    MENU_DRAW_TOOL_MARKER,
    MENU_DRAW_TOOL_LINE,
    MENU_DRAW_TOOL_RECT,
    MENU_DRAW_TOOL_CIRCLE,
    MENU_DRAW_TOOL_FILLED_RECT,
    MENU_DRAW_TOOL_FILLED_CIRCLE,
    MENU_KBD_OVERLAY,
    MENU_RECENT_BASE = 6969
};

static HWND get_hwnd(SDL_Window* window) {
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    if (!SDL_GetWindowWMInfo(window, &wm_info) || wm_info.subsystem != SDL_SYSWM_WINDOWS) {
        return NULL;
    }
    return wm_info.info.win.window;
}

static HBITMAP icon_to_bitmap(HICON hIcon, int w, int h) {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);
    DrawIconEx(hdcMem, 0, 0, hIcon, w, h, 0, NULL, DI_NORMAL);
    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return hBmp;
}

HMENU create_windows_menu(SDL_Window* window) {
    HWND hwnd = get_hwnd(window);
    if (!hwnd) return NULL;
    HMENU hMenu = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    HMENU hViewMenu = CreatePopupMenu();
    HMENU hResolutionMenu = CreatePopupMenu();
    HMENU hAspectRatioMenu = CreatePopupMenu();
    HMENU hZoomMenu = CreatePopupMenu();
    HMENU hPlaybackMenu = CreatePopupMenu();
    HMENU hVolumeMenu = CreatePopupMenu();
    HMENU hBookmarksMenu = CreatePopupMenu();
    HMENU hDrawingMenu = CreatePopupMenu();
    HMENU hDrawToolsMenu = CreatePopupMenu();

    AppendMenu(hFileMenu, MF_STRING, MENU_OPEN, "Open File\tCtrl+O");

    if (recent_count > 0) {
        HMENU hRecent = CreatePopupMenu();
        for (int i = 0; i < recent_count; i++) {
            AppendMenu(hRecent, MF_STRING, MENU_RECENT_BASE + i, recent_files[i]);
        }
        static HBITMAP s_file_bmp = NULL;
        if (!s_file_bmp) {
            int sz = GetSystemMetrics(SM_CXSMICON);
            HICON hIcon = (HICON)LoadImage(NULL,
                                        #ifndef DIST
                                           "assets\\file.ico",
                                        #else
                                           "..\\assets\\file.ico",
                                        #endif
                                           IMAGE_ICON, sz, sz,
                                           LR_LOADFROMFILE | LR_DEFAULTCOLOR);
            if (hIcon) {
                s_file_bmp = icon_to_bitmap(hIcon, sz, sz);
                DestroyIcon(hIcon);
            }
        }
        if (s_file_bmp) {
            for (int i = 0; i < recent_count; i++) {
                SetMenuItemBitmaps(hRecent, MENU_RECENT_BASE + i, MF_BYCOMMAND,
                                   s_file_bmp, s_file_bmp);
            }
        }
        AppendMenu(hFileMenu, MF_POPUP, (UINT_PTR)hRecent, "Recent Files");
    }

    AppendMenu(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hFileMenu, MF_STRING, MENU_EXIT, "Exit\tAlt+F4");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "File");

    AppendMenu(hViewMenu, MF_STRING, MENU_FULLSCREEN, "Fullscreen\tF11");
    AppendMenu(hViewMenu, MF_STRING, MENU_MAXIMIZE, "Maximize\tF10");
    AppendMenu(hViewMenu, MF_STRING, MENU_MINIMIZE, "Minimize\tAlt+M");
    AppendMenu(hViewMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_NATIVE, "Native (Video)\tShift+Alt+R");
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_CUSTOM, "Custom\tAlt+R");
    AppendMenu(hResolutionMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_480P, "854x480");
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_720P, "1280x720");
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_1080P, "1920x1080");
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_1440P, "2560x1440");
    AppendMenu(hViewMenu, MF_POPUP, (UINT_PTR)hResolutionMenu, "Resolution");
    AppendMenu(hAspectRatioMenu, MF_STRING, MENU_ASPECT_RATIO_STRETCH, "Stretch to Window\tShift+Alt+S");
    AppendMenu(hAspectRatioMenu, MF_STRING, MENU_ASPECT_RATIO_ORIGINAL, "Original (Video)\tShift+Alt+A");
    AppendMenu(hAspectRatioMenu, MF_STRING, MENU_ASPECT_RATIO_CUSTOM, "Custom\tAlt+A");
    AppendMenu(hAspectRatioMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hAspectRatioMenu, MF_STRING, MENU_ASPECT_RATIO_1_1, "1:1");
    AppendMenu(hAspectRatioMenu, MF_STRING, MENU_ASPECT_RATIO_4_3, "4:3");
    AppendMenu(hAspectRatioMenu, MF_STRING, MENU_ASPECT_RATIO_16_9, "16:9");
    AppendMenu(hAspectRatioMenu, MF_STRING, MENU_ASPECT_RATIO_21_9, "21:9");
    AppendMenu(hViewMenu, MF_POPUP, (UINT_PTR)hAspectRatioMenu, "Aspect Ratio");
    AppendMenu(hZoomMenu, MF_STRING, MENU_ZOOM_RESET, "Reset Zoom\tCtrl+Alt+Z");
    AppendMenu(hZoomMenu, MF_STRING, MENU_ZOOM_CUSTOM, "Custom Zoom\tAlt+Z");
    AppendMenu(hZoomMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hZoomMenu, MF_STRING, MENU_ZOOM_IN, "Zoom In\tCtrl+Plus");
    AppendMenu(hZoomMenu, MF_STRING, MENU_ZOOM_OUT, "Zoom Out\tCtrl+Minus");
    AppendMenu(hViewMenu, MF_POPUP, (UINT_PTR)hZoomMenu, "Zoom");
    AppendMenu(hViewMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hViewMenu, MF_STRING, MENU_KBD_OVERLAY, "Keyboard Shortcuts\t?");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, "View");

    AppendMenu(hVolumeMenu, MF_STRING, MENU_VOLUME_UP, "Volume Up (by 5%)\tUp");
    AppendMenu(hVolumeMenu, MF_STRING, MENU_VOLUME_DOWN, "Volume Down (by 5%)\tDown");
    AppendMenu(hVolumeMenu, MF_STRING, MENU_VOLUME_UP_FINE, "Volume Up (by 1%)\tAlt+Up");
    AppendMenu(hVolumeMenu, MF_STRING, MENU_VOLUME_DOWN_FINE, "Volume Down (by 1%)\tAlt+Down");
    AppendMenu(hVolumeMenu, MF_STRING, MENU_SET_VOLUME0, "Mute (Set Volume to 0%)\tM");
    AppendMenu(hVolumeMenu, MF_STRING, MENU_SET_VOLUME100, "Set Volume to 100%\tCtrl+1");
    AppendMenu(hVolumeMenu, MF_STRING, MENU_SET_VOLUME200, "Set Volume to 200%\tCtrl+2");

    AppendMenu(hBookmarksMenu, MF_STRING, MENU_ADD_BOOKMARK, "Add Bookmark\tB");
    AppendMenu(hBookmarksMenu, MF_STRING, MENU_DELETE_BOOKMARK, "Delete closest Bookmark\tShift+B");
    AppendMenu(hBookmarksMenu, MF_STRING, MENU_NEXT_BOOKMARK, "Next Bookmark\t]");
    AppendMenu(hBookmarksMenu, MF_STRING, MENU_PREV_BOOKMARK, "Previous Bookmark\t[");

    AppendMenu(hPlaybackMenu, MF_STRING, MENU_PLAY_PAUSE, "Play/Pause\tSpace");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_NEXT_FRAME, "Next Frame\tAlt+Right");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_PREV_FRAME, "Previous Frame\tAlt+Left");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_NEXT_MEDIA, "Next Media\tShift+Right");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_PREV_MEDIA, "Previous Media\tShift+Left");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_NEXT_MEDIA_WRAP, "Next Media (Loop)\tCtrl+Shift+Right");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_PREV_MEDIA_WRAP, "Previous Media (Loop)\tCtrl+Shift+Left");
    AppendMenu(hPlaybackMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_SEEK_BACK, "Seek -5s\tLeft");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_SEEK_FORWARD, "Seek +5s\tRight");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_SEEK_BEGINNING, "Seek to Beginning\tHome");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_SEEK_END, "Seek to End\tEnd");
    AppendMenu(hPlaybackMenu, MF_POPUP, (UINT_PTR)hVolumeMenu, "Volume");
    AppendMenu(hPlaybackMenu, MF_POPUP, (UINT_PTR)hBookmarksMenu, "Bookmarks");
    AppendMenu(hPlaybackMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_UNDO, "Undo\tCtrl+Z");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_REDO, "Redo\tCtrl+Y");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hPlaybackMenu, "Playback");

    AppendMenu(hDrawToolsMenu, MF_STRING, MENU_DRAW_TOOL_PEN, "Pen (1)");
    AppendMenu(hDrawToolsMenu, MF_STRING, MENU_DRAW_TOOL_ERASER, "Eraser (2)");
    AppendMenu(hDrawToolsMenu, MF_STRING, MENU_DRAW_TOOL_MARKER, "Marker (3)");
    AppendMenu(hDrawToolsMenu, MF_STRING, MENU_DRAW_TOOL_LINE, "Line (4)");
    AppendMenu(hDrawToolsMenu, MF_STRING, MENU_DRAW_TOOL_RECT, "Rectangle (5)");
    AppendMenu(hDrawToolsMenu, MF_STRING, MENU_DRAW_TOOL_CIRCLE, "Circle (6)");
    AppendMenu(hDrawToolsMenu, MF_STRING, MENU_DRAW_TOOL_FILLED_RECT, "Filled Rectangle (7)");
    AppendMenu(hDrawToolsMenu, MF_STRING, MENU_DRAW_TOOL_FILLED_CIRCLE, "Filled Circle (8)");

    AppendMenu(hDrawingMenu, MF_STRING, MENU_DRAW_SCREENSHOT, "Screenshot\tF2");
    AppendMenu(hDrawingMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hDrawingMenu, MF_STRING, MENU_DRAW_ENTER, "Enter Draw Mode\tP");
    AppendMenu(hDrawingMenu, MF_STRING, MENU_DRAW_EXIT, "Exit Draw Mode\tEsc");
    AppendMenu(hDrawingMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hDrawingMenu, MF_STRING, MENU_DRAW_EXPORT_WITH_FRAME, "Export with Frame\tE");
    AppendMenu(hDrawingMenu, MF_STRING, MENU_DRAW_EXPORT_ONLY, "Export Drawing Only\tShift+E");
    AppendMenu(hDrawingMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hDrawingMenu, MF_STRING, MENU_DRAW_CLEAR, "Clear Canvas\tCtrl+N");
    AppendMenu(hDrawingMenu, MF_POPUP, (UINT_PTR)hDrawToolsMenu, "Tools");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hDrawingMenu, "Drawing");

    SetMenu(hwnd, hMenu);
    DrawMenuBar(hwnd);
    return hMenu;
}

void menu_set_draw_mode(bool on) {
    if (!g_menu_win) return;

    HWND hwnd = get_hwnd(g_menu_win);
    if (!hwnd || !g_current_win_menu) return;

    HMENU menu = g_current_win_menu;

    UINT disable = MF_BYCOMMAND | MF_GRAYED;
    UINT enable  = MF_BYCOMMAND | MF_ENABLED;

    EnableMenuItem(menu, MENU_DRAW_ENTER, on ? disable : enable);
    EnableMenuItem(menu, MENU_DRAW_EXIT,  on ? enable  : disable);

    EnableMenuItem(menu, MENU_DRAW_TOOL_PEN,           on ? enable : disable);
    EnableMenuItem(menu, MENU_DRAW_TOOL_ERASER,        on ? enable : disable);
    EnableMenuItem(menu, MENU_DRAW_TOOL_MARKER,        on ? enable : disable);
    EnableMenuItem(menu, MENU_DRAW_TOOL_LINE,          on ? enable : disable);
    EnableMenuItem(menu, MENU_DRAW_TOOL_RECT,          on ? enable : disable);
    EnableMenuItem(menu, MENU_DRAW_TOOL_CIRCLE,        on ? enable : disable);
    EnableMenuItem(menu, MENU_DRAW_TOOL_FILLED_RECT,   on ? enable : disable);
    EnableMenuItem(menu, MENU_DRAW_TOOL_FILLED_CIRCLE, on ? enable : disable);

    EnableMenuItem(menu, MENU_DRAW_EXPORT_WITH_FRAME, on ? enable : disable);
    EnableMenuItem(menu, MENU_DRAW_EXPORT_ONLY,       on ? enable : disable);
    EnableMenuItem(menu, MENU_DRAW_CLEAR,             on ? enable : disable);

    DrawMenuBar(hwnd);
}

static void refresh_windows_recent_menu(void) {
    if (!g_menu_win) return;

    HWND hwnd = get_hwnd(g_menu_win);
    if (!hwnd) return;

    HMENU old_menu = g_current_win_menu;
    HMENU new_menu = create_windows_menu(g_menu_win);
    if (!new_menu) return;

    if ((SDL_GetWindowFlags(g_menu_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        SetMenu(hwnd, NULL);
        DrawMenuBar(hwnd);
    }

    g_current_win_menu = new_menu;
    if (old_menu && old_menu != new_menu) {
        DestroyMenu(old_menu);
    }
}
#endif

static int find_nearest_bookmark(const Bookmark* bms, int bm_count,
                                  double duration, SDL_Rect timeline,
                                  int mouse_x, int threshold_px,
                                  int* out_index, double* out_time) {
    if (!bms || bm_count <= 0 || duration <= 0.0 || timeline.w <= 0) return 0;
    int best_idx = -1, best_dist = threshold_px + 1;
    double best_time = 0.0;
    for (int i = 0; i < bm_count; i++) {
        double t = bms[i].time;
        if (t < 0.0 || t > duration) continue;
        int cx = timeline.x + (int)((t / duration) * timeline.w);
        int dist = abs(mouse_x - cx);
        if (dist <= threshold_px && dist < best_dist) {
            best_dist = dist;
            best_idx  = i;
            best_time = t;
        }
    }
    if (best_idx < 0) return 0;
    if (out_index) *out_index = best_idx;
    if (out_time)  *out_time  = best_time;
    return 1;
}

static int parse_time_string(const char* s, double* out) {
    if (!s || !out) return 0;
    int h = 0, m = 0, sec = 0;
    float fsec = 0.0f;
    if (sscanf(s, "%d:%d:%d", &h, &m, &sec) == 3) {
        *out = (double)(h * 3600 + m * 60 + sec);
        return 1;
    }
    if (sscanf(s, "%d:%d", &m, &sec) == 2) {
        *out = (double)(m * 60 + sec);
        return 1;
    }
    if (sscanf(s, "%f", &fsec) == 1) {
        *out = (double)fsec;
        return 1;
    }
    return 0;
}

static int parse_custom_resolution(const char* s, int* out_w, int* out_h) {
    if (!s || !out_w || !out_h) return 0;
    int w = 0, h = 0;
    char buf[128] = {0};
    int j = 0;
    for (int i = 0; s[i] && j < 127; i++) {
        if (s[i] != ' ' && s[i] != '\t') buf[j++] = s[i];
    }
    buf[j] = '\0';
    if (sscanf(buf, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { *out_w = w; *out_h = h; return 1; }
    if (sscanf(buf, "%dX%d", &w, &h) == 2 && w > 0 && h > 0) { *out_w = w; *out_h = h; return 1; }
    if (sscanf(buf, "%d,%d", &w, &h) == 2 && w > 0 && h > 0) { *out_w = w; *out_h = h; return 1; }
    if (sscanf(buf, "%d-%d", &w, &h) == 2 && w > 0 && h > 0) { *out_w = w; *out_h = h; return 1; }
    if (sscanf(s, "%d %d", &w, &h) == 2 && w > 0 && h > 0) { *out_w = w; *out_h = h; return 1; }
    return 0;
}

static int parse_zoom_input(const char* s, int* out_zoom) {
    if (!s || !out_zoom) return 0;
    char buf[64] = {0};
    int j = 0;
    for (int i = 0; s[i] && j < 63; i++) {
        buf[j++] = (char)(s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]);
    }
    buf[j] = '\0';

    char* p = buf;
    while (*p == ' ' || *p == '\t') p++;
    int len = (int)strlen(p);
    while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\t')) p[--len] = '\0';
    if (len == 0) return 0;

    float mult = 0.0f;
    if (p[len-1] == 'x') {
        p[len-1] = '\0';
        len = (int)strlen(p);
        while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\t')) p[--len] = '\0';
        if (sscanf(p, "%f", &mult) == 1 && mult > 0.0f) {
            *out_zoom = (int)(mult * 100.0f + 0.5f);
            return 1;
        }
        return 0;
    }

    if (len > 5 && strcmp(p + len - 5, "times") == 0) {
        p[len - 5] = '\0';
        len = (int)strlen(p);
        while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\t')) p[--len] = '\0';
        if (sscanf(p, "%f", &mult) == 1 && mult > 0.0f) {
            *out_zoom = (int)(mult * 100.0f + 0.5f);
            return 1;
        }
        return 0;
    }

    if (p[len-1] == '%') {
        p[len-1] = '\0';
        len = (int)strlen(p);
        while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\t')) p[--len] = '\0';
    }

    int val = 0;
    if (sscanf(p, "%d", &val) == 1 && val > 0) {
        *out_zoom = val;
        return 1;
    }
    return 0;
}

static SDL_Rect apply_zoom_to_rect(SDL_Rect r, int win_w, int win_h, int zoom_percent) {
    if (zoom_percent == 100) return r;
    float scale = (float)zoom_percent / 100.0f;
    int new_w = (int)((float)r.w * scale);
    int new_h = (int)((float)r.h * scale);
    int new_x = (win_w - new_w) / 2;
    int new_y = (win_h - new_h) / 2;
    return (SDL_Rect){new_x, new_y, new_w, new_h};
}

static int parse_custom_aspect_ratio(const char* s, unsigned int* out_x, unsigned int* out_y) {
    if (!s || !out_x || !out_y) return 0;
    unsigned int x = 0, y = 0;
    char buf[128] = {0};
    int j = 0;
    for (int i = 0; s[i] && j < 127; i++) {
        if (s[i] != ' ' && s[i] != '\t') buf[j++] = s[i];
    }
    buf[j] = '\0';
    if (sscanf(buf, "%u:%u", &x, &y) == 2 && x > 0 && y > 0) { *out_x = x; *out_y = y; return 1; }
    if (sscanf(buf, "%u,%u", &x, &y) == 2 && x > 0 && y > 0) { *out_x = x; *out_y = y; return 1; }
    if (sscanf(buf, "%u-%u", &x, &y) == 2 && x > 0 && y > 0) { *out_x = x; *out_y = y; return 1; }
    if (sscanf(s,   "%u %u", &x, &y) == 2 && x > 0 && y > 0) { *out_x = x; *out_y = y; return 1; }
    return 0;
}

static void take_screenshot(VideoRenderer* vr,
                            SDL_Window* win,
                            SDL_Renderer* ren,
                            char* flash_text,
                            Uint32* flash_until)
{
    if (!vr || !win || !ren) return;

    int win_w, win_h;
    SDL_GetWindowSize(win, &win_w, &win_h);

    SDL_Texture* frame_tex = vr_get_texture(vr);
    if (!frame_tex) {
        snprintf(flash_text, 256, "No video frame to screenshot");
        *flash_until = SDL_GetTicks() + 900;
        return;
    }

    int src_w = 0, src_h = 0;
    SDL_QueryTexture(frame_tex, NULL, NULL, &src_w, &src_h);

    SDL_Rect video_dst = compute_video_dst_rect(win_w, win_h, src_w, src_h, vr->ar_x, vr->ar_y);
    video_dst = apply_zoom_to_rect(video_dst, win_w, win_h, vr->zoom_percent);

    char path[1024];
    char ext[8];

    if (!export_save_dialog(path, sizeof(path), ext, "screenshot")) {
        *flash_until = SDL_GetTicks() + 900;
        return;
    }

    SDL_Surface* screenshot = SDL_CreateRGBSurface(
        0, win_w, win_h, 32,
        0x000000FF,
        0x0000FF00,
        0x00FF0000,
        0xFF000000
    );

    if (!screenshot) {
        snprintf(flash_text, 256, "Failed to create surface");
        *flash_until = SDL_GetTicks() + 900;
        return;
    }

    SDL_Texture* temp_tex = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET,
        win_w,
        win_h
    );

    if (!temp_tex) {
        SDL_FreeSurface(screenshot);
        snprintf(flash_text, 256, "Failed to create texture");
        *flash_until = SDL_GetTicks() + 900;
        return;
    }

    SDL_SetRenderTarget(ren, temp_tex);
    SDL_SetRenderDrawColor(ren, THEME_LETTERBOX_COLOR[0], THEME_LETTERBOX_COLOR[1], THEME_LETTERBOX_COLOR[2], 255);
    SDL_RenderClear(ren);

    SDL_RenderCopy(ren, frame_tex, NULL, &video_dst);

    if (vr_render_subtitles(vr, vr_get_video_time(vr))) {
        SDL_Texture* sub = vr_get_subtitle_texture(vr);
        if (sub) SDL_RenderCopy(ren, sub, NULL, &video_dst);
    }

    SDL_RenderReadPixels(
        ren,
        NULL,
        SDL_PIXELFORMAT_RGBA32,
        screenshot->pixels,
        screenshot->pitch
    );

    SDL_SetRenderTarget(ren, NULL);

    int ok = 0;

    if (strcmp(ext, "png") == 0) {
        ok = stbi_write_png(path, win_w, win_h, 4,
                            screenshot->pixels, screenshot->pitch);

    } else if (strcmp(ext, "jpg") == 0) {
        ok = stbi_write_jpg(path, win_w, win_h, 4,
                            screenshot->pixels, 90);

    } else if (strcmp(ext, "bmp") == 0) {
        ok = (SDL_SaveBMP(screenshot, path) == 0);

    } else if (strcmp(ext, "tga") == 0) {
        ok = stbi_write_tga(path, win_w, win_h, 4,
                            screenshot->pixels);
    }

    if (ok) {
        snprintf(flash_text, 256, "Screenshot saved");
    } else {
        snprintf(flash_text, 256, "Failed to save image");
    }

    SDL_DestroyTexture(temp_tex);
    SDL_FreeSurface(screenshot);

    *flash_until = SDL_GetTicks() + 900;
}

int main(int argc, char** argv) {
    nob_set_log_handler(amp_log_handler);

    {
        char exe_dir[512];
        get_exe_dir(exe_dir, sizeof(exe_dir));
        if (exe_dir[0]) {
#ifdef _WIN32
            SetCurrentDirectoryA(exe_dir);
#else
            chdir(exe_dir);
#endif
        }
    }

    #ifdef _WIN32
    /* SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    AddDllDirectory(L"./dll"); */
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc = {
        sizeof(INITCOMMONCONTROLSEX),
        ICC_WIN95_CLASSES | ICC_LINK_CLASS | ICC_STANDARD_CLASSES
    };

    InitCommonControlsEx(&icc);
    attach_console_if_present();
    SetCurrentProcessExplicitAppUserModelID(L"amp");
    #endif

    #ifdef DIST
    if (strncmp(THEME_FONT.path, "assets/", 7) == 0 ||
        strncmp(THEME_FONT.path, "./assets/", 10) == 0) {

        size_t len = strlen(THEME_FONT.path);

        if (len + 3 < sizeof(THEME_FONT.path)) {
            char temp[sizeof(THEME_FONT.path)];

            snprintf(temp, sizeof(temp), "../%s", THEME_FONT.path);
            memcpy(THEME_FONT.path, temp, sizeof(THEME_FONT.path));
        }
    }
    #endif

    VideoRenderer* vr = NULL;
    char* video_file = NULL;
    bool running = true;
    bool fullscreen = false;
    bool maximized = true;
    int requested_window_w = 0;
    int requested_window_h = 0;
    int windowed_x = SDL_WINDOWPOS_CENTERED;
    int windowed_y = SDL_WINDOWPOS_CENTERED;
    int windowed_w = INITIAL_WINDOW_WIDTH;
    int windowed_h = INITIAL_WINDOW_HEIGHT;
    int windowed_valid = 0;
    bool paused = false;
    bool audio_os_passthrough = false;
    bool night_mode = false;
    bool blackout_mode = false;
    bool ambient_glow = false;
    SDL_Window* blackout_windows[MAX_BLACKOUT_WINDOWS] = {0};
    int blackout_window_count = 0;
    int blackout_last_display = -2;
    int blackout_last_fullscreen = -1;
    int blackout_last_mode = -1;
    bool stay_awake = false;
    bool dragging_timeline = false;
    bool volume_dragging = false;
    float volume_drag_start = 100.0f;
    bool menu_open = false;
    bool audio_menu_open = false;
    bool subtitle_menu_open = false;
    bool theme_menu_open = false;
    bool playback_menu_open = false;
    bool subtitle_settings_menu_open = false;
    bool subtitle_settings_value_menu_open = false;
    int subtitle_settings_value_menu_row = -1;
    bool kbd_overlay_open = false;
    int kbd_scroll = 0;
    double drag_time = 0.0;
    float overlay_alpha = 1.0f;
    float overlay_target = 1.0f;
    Uint32 last_mouse_move = SDL_GetTicks();
    Uint32 last_tick = SDL_GetTicks();
    float volume_percent = 100.0f;
    float playback_speed = 1.0f;
    float pause_alpha = 0.0f;
    int audio_scroll = 0;
    int subtitle_scroll = 0;
    int subtitle_color_idx = 0;
    int subtitle_size_idx = 1;
    int subtitle_move_idx = 1;
    const int subtitle_color_count = (int)(sizeof(subtitle_override_colors) / sizeof(subtitle_override_colors[0]));
    const int subtitle_size_count = (int)(sizeof(subtitle_override_sizes) / sizeof(subtitle_override_sizes[0]));
    const int subtitle_move_count = (int)(sizeof(subtitle_override_margins) / sizeof(subtitle_override_margins[0]));
    SDL_Event e;

    Bookmark bookmarks[MAX_BOOKMARKS_PER_FILE];
    int bookmark_count = 0;
    static HistoryEntry history[MAX_HISTORY];
    memset(history, 0, sizeof(history));
    int history_count = 0;
    int history_pos   = -1;

    TextInputState ti = {0};
    typedef enum { TI_NONE = 0, TI_BOOKMARK_RENAME, TI_GOTO_TIME, TI_SET_RESOLUTION, TI_SET_ASPECT_RATIO, TI_SET_ZOOM, TI_SET_SPEED } TIPurpose;
    TIPurpose ti_purpose = TI_NONE;
    int ti_bm_idx = -1;
    char ti_bm_old_name[BOOKMARK_NAME_MAX] = {0};

    int bm_ctx_open = 0, bm_ctx_x = 0, bm_ctx_y = 0, bm_ctx_idx = -1;

    static DrawingState draw_state = {0};
    draw_init(&draw_state);
    SDL_Texture* draw_canvas = NULL;
    SDL_Texture* draw_zoom_tex = NULL;
    float draw_zoom = 1.0f;
    float draw_zoom_target = 1.0f;
    float draw_zoom_ox = 0.0f, draw_zoom_oy = 0.0f;
    float draw_zoom_ox_target = 0.0f, draw_zoom_oy_target = 0.0f;
    int draw_mode_active = 0;
    bool draw_mode_was_paused = false;

    #ifndef _WIN32
    char audio_driver[64] = "pulseaudio,alsa";
    #else
    char audio_driver[64] = "";
    #endif

#ifndef _WIN32
    setenv("LIBVA_DRIVER_NAME", "", 1);
#else
    int reregister = false;
#endif

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--flash-debug") == 0) {
            flash_debug_enabled = 1;
        } else if (strcmp(argv[i], "--flash-debug-level") == 0 && i + 1 < argc) {
            int lvl = atoi(argv[i + 1]);
            if (lvl >= 0 && lvl <= 3) 
                flash_debug_level = lvl;
            else {
                nob_log(NOB_WARNING, "Invalid flash debug level: %s. Must be 0-3.", argv[i + 1]);
            }
            i++;
        } else if (strcmp(argv[i], "--no-flash-debug") == 0) {
            flash_debug_enabled = 0;
        } else if (strcmp(argv[i], "--start-paused") == 0 || strcmp(argv[i], "--paused") == 0 || strcmp(argv[i], "-p") == 0) {
            paused = true;
        } else if ((strcmp(argv[i], "--volume") == 0 || strcmp(argv[i], "-v") == 0) && i + 1 < argc) {
            float vol = atof(argv[i + 1]);
            if (vol >= 0.0f && vol <= 200.0f)
                volume_percent = vol;
            else {
                nob_log(NOB_WARNING, "Invalid volume percent: %s. Must be 0-200.", argv[i + 1]);
            }
            i++;
        } else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            float spd = atof(argv[i + 1]);
            if (spd > 0.0f)
                playback_speed = spd;
            else {
                nob_log(NOB_WARNING, "Invalid playback speed: %s. Must be > 0.", argv[i + 1]);
            }
            i++;
        } else if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0) {
            fullscreen = true;
        } else if (strcmp(argv[i], "--maximized") == 0 || strcmp(argv[i], "-m") == 0) {
            maximized = true;
        } else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            const char* resolution_arg = argv[i + 1];
#ifdef _WIN32
            if (_stricmp(resolution_arg, "native") == 0)
#else
            if (strcasecmp(resolution_arg, "native") == 0)
#endif      
            {
                requested_window_w = 0;
                requested_window_h = 0;
            } else if (!parse_resolution_arg(resolution_arg, &requested_window_w, &requested_window_h)) {
                nob_log(NOB_WARNING, "Invalid resolution: %s. Use WxH (e.g. 1280x720) or native.", resolution_arg);
                requested_window_w = 0;
                requested_window_h = 0;
            }
            i++;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            if (argv[i + 1]) {
                log_file = fopen(abspath_temp(argv[i + 1]), "a");
                if (!log_file) {
                    nob_log(NOB_ERROR, "Failed to open log file '%s' for writing: %s", argv[i + 1], strerror(errno));
                } else {
                    nob_log(NOB_INFO, "Logging to file: %s", argv[i + 1]);
                }
            } else {
                nob_log(NOB_WARNING, "No log file specified after --log-file");
            }
            i++;
        } else if (strncmp(argv[i], "--hw=", 5) == 0) {
            const char* val = argv[i] + 5;
            strncpy(hw_option, val, sizeof(hw_option) - 1);
            hw_option[sizeof(hw_option) - 1] = '\0';
        } else if (strcmp(argv[i], "--hw") == 0 && i + 1 < argc) {
            const char* val = argv[i + 1];
            strncpy(hw_option, val, sizeof(hw_option) - 1);
            hw_option[sizeof(hw_option) - 1] = '\0';
            i++;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            fprintf(stdout, "amp version %d.%d.%d\n", (AMP_VERSION >> 16) & 0xFF, (AMP_VERSION >> 8) & 0xFF, AMP_VERSION & 0xFF);
            return 0;
        #ifdef _WIN32
        } else if (strcmp(argv[i], "--reregister") == 0) {
            reregister = true;
        } else if (strcmp(argv[i], "--unregister") == 0) {
            reregister = -1;
        #endif
        } else if (strcmp(argv[i], "--info") == 0 || strcmp(argv[i], "-i") == 0) {
            fprintf(stdout, "amp - A simple video player\n");
            fprintf(stdout, "version: %d.%d.%d\n", (AMP_VERSION >> 16) & 0xFF, (AMP_VERSION >> 8) & 0xFF, AMP_VERSION & 0xFF);
            fprintf(stdout, "timestamp: %s\n", TIMESTAMP);
            usage(stdout, argv[0]);
            fprintf(stdout, "Compiled with:\n");
            fprintf(stdout, "  Nob version: %s\n", NOB_VERSION);
            fprintf(stdout, "  SDL2 version: %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
            fprintf(stdout, "  TTF version: %d.%d.%d\n", TTF_MAJOR_VERSION, TTF_MINOR_VERSION, TTF_PATCHLEVEL);
            fprintf(stdout, "  FFmpeg version: %d.%d.%d\n", LIBAVFORMAT_VERSION_MAJOR, LIBAVFORMAT_VERSION_MINOR, LIBAVFORMAT_VERSION_MICRO);
            fprintf(stdout, "  LibAss version: %d.%d.%d\n", (LIBASS_VERSION >> 24) & 0xFF, (LIBASS_VERSION >> 16) & 0xFF, (LIBASS_VERSION >> 8) & 0xFF);
            fprintf(stdout, "  SIMD: %s\n", USE_SSE2_SIMD ? "SSE2" : "None");
            fprintf(stdout, "  Compiler: %s ", CC);
            const char* flags[] = { CFLAGS, NULL };
            for (int j = 0; flags[j]; j++) fprintf(stdout, "%s ", flags[j]);
            fprintf(stdout, "\n");
            fprintf(stdout, "(c) 2026 Markofwitch. All rights reserved.\n");
            return 0;
        } else if (strcmp(argv[i], "--ascii") == 0) {
            print_ascii_art();
            return 0;
        } else if (strcmp(argv[i], "--theme") == 0) {
            if (i + 1 < argc) {
                const char* theme_name = argv[i + 1];
                if (is_valid_theme_name(theme_name)) {
                    load_theme(theme_name);
                    draw_init(&draw_state);
                } else {
                    nob_log(NOB_WARNING, "Invalid theme name: %s. Using default theme.", theme_name);
                }
            } else {
                nob_log(NOB_WARNING, "No theme name specified after --theme");
            }
            i++;
        } else if (strcmp(argv[i], "--themes") == 0) {
            list_themes(stdout);
            return 0;
        } else if (strcmp(argv[i], "--audio-driver") == 0 && i + 1 < argc) {
            const char* driver_name = argv[i + 1];
            if (driver_name && strlen(driver_name) < sizeof(audio_driver)) {
                sprintf(audio_driver, "%s", driver_name);
            } else {
                nob_log(NOB_WARNING, "Invalid audio driver name: %s", driver_name);
            }
            i++;
        } else if (argv[i][0] != '-') {
            video_file = abspath_temp_safe(argv[i]);
        } else {
            nob_log(NOB_WARNING, "Unknown argument: %s", argv[i]);
        }
    }
    
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (audio_driver[0]) SDL_SetHint(SDL_HINT_AUDIODRIVER, audio_driver);
#ifdef _WIN32
    if (reregister == -1) amp_unregister();
    else amp_register((bool)reregister);
    SDL_PumpEvents();
#endif
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        nob_log(NOB_ERROR, "SDL_Init Error: %s", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        nob_log(NOB_ERROR, "TTF_Init Error: %s", TTF_GetError());
        return 1;
    }
    nob_log(NOB_INFO, "SDL initialized successfully");
    nob_log(NOB_INFO, "Using '%s' theme", THEME_NAME);
    nob_log(NOB_INFO, "HW option: %s", hw_option);
    
    bool font_loaded = false;
    if (THEME_FONT.name[0] && THEME_FONT.path[0]) {
        if (load_ui_font(THEME_FONT.path, THEME_FONT.name[0] ? THEME_FONT.name : THEME_NAME)) {
            nob_log(NOB_INFO, "Loaded theme font: %s (%s)", THEME_FONT.name[0] ? THEME_FONT.name : THEME_NAME, THEME_FONT.path);
            font_loaded = true;
        } else {
            nob_log(NOB_WARNING, "Theme font not available: %s", THEME_FONT.path);
        }
    }
    if (!font_loaded) {
        if (load_ui_font(THEME_SYSTEM_DEFAULT_FONT.path, THEME_SYSTEM_DEFAULT_FONT.name[0] ? THEME_SYSTEM_DEFAULT_FONT.name : THEME_NAME)) {
            nob_log(NOB_INFO, "Loaded system default font: %s", THEME_SYSTEM_DEFAULT_FONT.name[0] ? THEME_SYSTEM_DEFAULT_FONT.name : THEME_NAME);
            font_loaded = true;
        } else {
            nob_log(NOB_WARNING, "System default font not available: %s", THEME_SYSTEM_DEFAULT_FONT.path);
        }
    }
    if (!font_loaded) {
        nob_log(NOB_ERROR, "Failed to load any UI font. Exiting.");
        return 1;
    }

#ifdef _WIN32
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif
    if (video_file) {
        nob_log(NOB_INFO, "Checking video file: %s", video_file);
        if (!is_supported_video_file(video_file)) {
            nob_log(NOB_ERROR, "Unsupported video file: %s", video_file);
            video_file = NULL;
        }
    }
    if (!video_file) {
        nob_log(NOB_INFO, "No video file specified. Opening file dialog...");
        video_file = open_file_dialog(
            (const char*[]){"*.mkv", "*.mp4"}, 2, "Video Files (*.mkv, *.mp4)", false,
            "Select Video File", NULL, &is_supported_video_file
        );
        if (!video_file || !is_supported_video_file(video_file)) {
            nob_log(NOB_ERROR, "No file selected or unsupported file type. Exiting.");
            return 1;
        }  
    } else {
        nob_log(NOB_INFO, "Video file specified: %s", video_file);
    }

    SDL_PumpEvents();

    SDL_Window* win = SDL_CreateWindow(
        "(no file selected)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        requested_window_w > 0 ? requested_window_w : INITIAL_WINDOW_WIDTH,
        requested_window_h > 0 ? requested_window_h : INITIAL_WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS |
        (maximized ? SDL_WINDOW_MAXIMIZED : 0)
    );

    if (!win) { nob_log(NOB_ERROR, "SDL_CreateWindow failed: %s", SDL_GetError()); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { SDL_DestroyWindow(win); nob_log(NOB_ERROR, "SDL_CreateRenderer failed: %s", SDL_GetError()); return 1; }

    #ifdef _WIN32
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);

    if (SDL_GetWindowWMInfo(win, &wm_info) &&
        wm_info.subsystem == SDL_SYSWM_WINDOWS) {

        HWND hwnd = wm_info.info.win.window;

        if (IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_SHOW);
        }

        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);
    }
    #endif

    SDL_RaiseWindow(win);

    if (requested_window_w > 0 && requested_window_h > 0) {
        int startup_w = requested_window_w;
        int startup_h = requested_window_h;
        fit_window_to_display(&startup_w, &startup_h);
        SDL_SetWindowSize(win, startup_w, startup_h);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    /*
    if (maximized && !fullscreen) {
        SDL_MaximizeWindow(win);
    }
    */

    #if SAVE_FILE
        SaveState save_state = {0};
        int load_save_state_result = load_save_state(abspath_temp(SAVE_FILE_PATH), &save_state);
        if (load_save_state_result == 0) {
            nob_log(NOB_INFO, "No save file found, starting with default settings");
        } else if (load_save_state_result == -1) {
            nob_log(NOB_ERROR, "Failed to load save file due to an error");
            if (vr) vr_free(vr);
            hist_clear_from(history, &history_count, 0);
            if (ui_font) TTF_CloseFont(ui_font);
            TTF_Quit();
            for (int i = 0; i < recent_count; i++) free(recent_files[i]);
            abspath_temp_free();
            SDL_DestroyRenderer(ren);
            SDL_DestroyWindow(win);
            SDL_Quit();
            nob_log(NOB_INFO, "Exited due to save file load error");
            return 0xF1;
        } else {
            nob_log(NOB_INFO, "Save file loaded successfully (pre-load)");
            recent_count = (int)(save_state.recent_files_count > MAX_RECENT ? MAX_RECENT : save_state.recent_files_count);
            for (int i = 0; i < recent_count; i++) {
                recent_files[i] = save_state.recent_files[i] ? strdup(save_state.recent_files[i]) : NULL;
            }
            audio_os_passthrough = save_state.global.audio_os_passthrough ? true : false;
            night_mode = save_state.global.night_mode ? true : false;
            blackout_mode = save_state.global.blackout_mode ? true : false;
            ambient_glow = save_state.global.ambient_glow ? true : false;
            if (save_state.global.theme[0] && strcmp(THEME_FILE, "config.h") == 0) {
                load_theme(save_state.global.theme);
                draw_init(&draw_state);
                if (THEME_FONT.path[0]) {
                    if (!load_ui_font(THEME_FONT.path, THEME_FONT.name[0] ? THEME_FONT.name : THEME_NAME))
                        nob_log(NOB_WARNING, "Saved theme font not available: %s", THEME_FONT.path);
                } else {
                    if (!load_ui_font(THEME_SYSTEM_DEFAULT_FONT.path, THEME_SYSTEM_DEFAULT_FONT.name[0] ? THEME_SYSTEM_DEFAULT_FONT.name : THEME_NAME))
                        nob_log(NOB_WARNING, "System default font not available: %s", THEME_SYSTEM_DEFAULT_FONT.path);
                }
            }
        }
    #endif

    if (video_file) {
        vr = vr_create(win, ren);
        if (vr_load(vr, video_file, hw_option)) {
            apply_audio_feature_toggles(vr, audio_os_passthrough, night_mode, ambient_glow);
            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
            apply_subtitle_override(
                vr,
                subtitle_override_colors[subtitle_color_idx],
                subtitle_override_sizes[subtitle_size_idx],
                subtitle_override_margins[subtitle_move_idx]
            );
            apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
            set_window_title_for_media(win, vr, video_file);
            nob_log(NOB_INFO, "Loaded %s", video_file);
        }
    }

    #if SAVE_FILE
        if (load_save_state_result > 0) {
            apply_save_state_to_vr(vr, &save_state, video_file, &volume_percent);
            apply_audio_feature_toggles(vr, audio_os_passthrough, night_mode, ambient_glow);
            if (vr) vr_set_volume(vr, volume_percent_to_gain(volume_percent));
            if (vr && vr->desired_win_w > 0 && vr->desired_win_h > 0) {
                requested_window_w = vr->desired_win_w;
                requested_window_h = vr->desired_win_h;
                apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
            }
            if (video_file)
                get_bookmarks_from_save_state(&save_state, video_file,
                                               bookmarks, &bookmark_count);
        }
        #if USE_SAVE_IN_SAVE_FILE
            if (save_state.global.paused) {
                vr_demux_packets(vr);
                vr_render_frame(vr);
                paused = save_state.global.paused;
            }
        #endif
        if (!maximized && !fullscreen && (save_state.global.win_x != 0 || save_state.global.win_y != 0)) {
            SDL_SetWindowPosition(win, save_state.global.win_x, save_state.global.win_y);
        }
    #endif

    if (video_file) {
        add_recent_file(video_file);
    }

    if (vr && video_file) {
        hist_push_seek(history, &history_count, &history_pos, NULL, 0.0, video_file, vr_get_time(vr));
    }

#ifdef _WIN32
    HMENU win_menu = create_windows_menu(win);
    HWND win_hwnd = get_hwnd(win);
    if (win_hwnd) {
        g_current_win_menu = win_menu;
        g_menu_vr_ptr = &vr;
        g_menu_win = win;
        g_menu_ren = ren;
        g_menu_paused_ptr = &paused;
        g_menu_playback_speed_ptr = &playback_speed;
        g_menu_ambient_glow_ptr = &ambient_glow;
        g_prev_menu_wndproc = (WNDPROC)SetWindowLongPtr(win_hwnd, GWLP_WNDPROC, (LONG_PTR)amp_menu_wndproc);
    }
    menu_set_draw_mode(false);
#endif

    SDL_Rect timeline_rect, timeline_hitbox, volume_rect, hamburger, menu_panel, audio_box, subtitle_box, theme_box, playback_box, subtitle_settings_box, overlay_rect;
    int overlay_h = 100;
    int margin = 24;
    int w, h;
    int last_subtitle_track = -1;

    while(running) {
        int subtitle_style_mode = vr ? vr_get_subtitle_style_mode(vr) : 0; /* 0 none, 1 text/srt, 2 ass/ssa */
        bool subtitle_settings_applicable = subtitle_style_mode == 1;
        int subtitle_settings_row_count = subtitle_style_mode == 1 ? 3 : 0;
        if (!subtitle_settings_applicable) {
            subtitle_settings_menu_open = false;
            subtitle_settings_value_menu_open = false;
            subtitle_settings_value_menu_row = -1;
        }

        if (overlay_target == 0.0) {
            SDL_ShowCursor(SDL_DISABLE);
        } else {
            SDL_ShowCursor(SDL_ENABLE);
        }

        {
            bool fullscreen_active = (SDL_GetWindowFlags(win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
            int display_idx = SDL_GetWindowDisplayIndex(win);
            if ((int)fullscreen_active != blackout_last_fullscreen
                || display_idx != blackout_last_display
                || (int)blackout_mode != blackout_last_mode) {
                blackout_sync_windows(win, fullscreen_active, blackout_mode, blackout_windows, &blackout_window_count);
                blackout_last_fullscreen = fullscreen_active ? 1 : 0;
                blackout_last_display = display_idx;
                blackout_last_mode = blackout_mode ? 1 : 0;
            }
        }

#ifdef _WIN32
        {
            bool want_awake = vr && !paused;
            if (want_awake != stay_awake) {
                stay_awake = want_awake;
                SetThreadExecutionState(stay_awake
                    ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
                    : ES_CONTINUOUS);
            }
        }
#endif

        {
            SDL_GetWindowSize(win, &w, &h);
            overlay_rect = (SDL_Rect){ 0, h - overlay_h, w, overlay_h };
            timeline_rect = (SDL_Rect){ margin, h - overlay_h + 12, w - margin * 2 - 40, THEME_TIMELINE_HEIGHT };
            timeline_hitbox = (SDL_Rect){ timeline_rect.x, timeline_rect.y - THEME_TIMELINE_HITBOX_PADDING, timeline_rect.w, THEME_TIMELINE_HEIGHT + THEME_TIMELINE_HITBOX_PADDING * 2 };
            volume_rect = (SDL_Rect){ w - margin - 32, h - overlay_h + 40, 6, 50 };
            hamburger = (SDL_Rect){ w - margin - 28, h - overlay_h + 12, 24, 20 };
            
            int menu_x = hamburger.x - 250;
            if (menu_x < margin) menu_x = margin;
            int menu_row_count = subtitle_settings_applicable ? 5 : 4;
            int menu_h = 24 + menu_row_count * 28 + (menu_row_count - 1) * 10;
            int menu_y = h - overlay_h - menu_h;
            if (menu_y < margin) menu_y = h - overlay_h + 12;
            menu_panel = (SDL_Rect){ menu_x, menu_y, 250, menu_h };
            int row_x = menu_panel.x + 12;
            int row_w = menu_panel.w - 24;
            int row_h = 28;
            int row_step = 38;
            int row_y0 = menu_panel.y + 12;
            audio_box = (SDL_Rect){ row_x, row_y0 + row_step * 0, row_w, row_h };
            subtitle_box = (SDL_Rect){ row_x, row_y0 + row_step * 1, row_w, row_h };
            theme_box = (SDL_Rect){ row_x, row_y0 + row_step * 2, row_w, row_h };
            playback_box = (SDL_Rect){ row_x, row_y0 + row_step * 3, row_w, row_h };
            subtitle_settings_box = (SDL_Rect){ row_x, row_y0 + row_step * 4, row_w, row_h };
        }

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;

            if (ti.active) {
                text_input_handle_event(&ti, &e);
                if (ti.done && !ti.cancelled) {
                    if (ti_purpose == TI_BOOKMARK_RENAME) {
                        if (ti_bm_idx >= 0 && ti_bm_idx < bookmark_count) {
                            int before_cnt_ren = bookmark_count;
                            Bookmark before_bms_ren[MAX_BOOKMARKS_PER_FILE];
                            memcpy(before_bms_ren, bookmarks, sizeof(Bookmark) * (size_t)before_cnt_ren);
                            int len = (int)strlen(ti.value);
                            if (len >= BOOKMARK_NAME_MAX) len = BOOKMARK_NAME_MAX - 1;
                            memcpy(bookmarks[ti_bm_idx].name, ti.value, len);
                            bookmarks[ti_bm_idx].name[len] = '\0';
                            char _d_ren[256];
                            snprintf(_d_ren, sizeof(_d_ren), "bookmark rename %s -> %s", ti_bm_old_name, bookmarks[ti_bm_idx].name);
                            hist_push_bookmark(history, &history_count, &history_pos, before_bms_ren, before_cnt_ren, bookmarks, bookmark_count, _d_ren);
                            #if SAVE_FILE
                            update_bookmarks_in_save_state(&save_state, video_file,
                                                            bookmarks, bookmark_count);
                            write_save_state(SAVE_FILE_PATH, &save_state);
                            #endif
                        }
                    } else if (ti_purpose == TI_GOTO_TIME) {
                        double t = 0.0;
                        if (parse_time_string(ti.value, &t) && vr) {
                            double dur = vr_get_duration(vr);
                            if (t < 0.0) t = 0.0;
                            if (dur > 0.0 && t > dur) t = dur;
                            double bt_goto = vr ? vr_get_time(vr) : 0.0;
                            seek_and_preview_if_paused(vr, t, paused);
                            if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_goto, video_file, t);
                            snprintf(flash_text, sizeof(flash_text), "Jumped to %s",
                                     format_time_temp(t));
                            flash_until = SDL_GetTicks() + 900;
                        } else {
                            snprintf(flash_text, sizeof(flash_text),
                                     "Invalid time format");
                            flash_until = SDL_GetTicks() + 900;
                        }
                    } else if (ti_purpose == TI_SET_RESOLUTION) {
                        int rw = 0, rh = 0;
                        if (parse_custom_resolution(ti.value, &rw, &rh)) {
                            if (fullscreen) {
                                fullscreen = false;
                                SDL_SetWindowFullscreen(win, 0);
#ifdef _WIN32
                                if (win_hwnd) { SetMenu(win_hwnd, g_current_win_menu); DrawMenuBar(win_hwnd); }
#endif
                            }
                            SDL_RestoreWindow(win);
                            maximized = false;
                            requested_window_w = rw;
                            requested_window_h = rh;
                            if (vr) { vr->desired_win_w = rw; vr->desired_win_h = rh; }
                            int tw = rw, th = rh;
                            fit_window_to_display(&tw, &th);
                            SDL_SetWindowSize(win, tw, th);
                            SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                            snprintf(flash_text, sizeof(flash_text), "Resolution: %dx%d", rw, rh);
#ifdef _WIN32
                            if (vr && video_file) {
                                fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent);
                                write_save_state(SAVE_FILE_PATH, &save_state);
                            }
#endif
                        } else {
                            snprintf(flash_text, sizeof(flash_text), "Invalid resolution (use WxH, W H, W,H or W-H)");
                        }
                        flash_until = SDL_GetTicks() + 900;
                    } else if (ti_purpose == TI_SET_ASPECT_RATIO) {
                        unsigned int ax = 0, ay = 0;
                        if (parse_custom_aspect_ratio(ti.value, &ax, &ay)) {
                            if (vr) {
                                vr_set_aspect_ratio_mode(vr, ax, ay);
#if SAVE_FILE
                                if (video_file) {
                                    fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent);
                                    write_save_state(SAVE_FILE_PATH, &save_state);
                                }
#endif
                            }
                            snprintf(flash_text, sizeof(flash_text), "Aspect Ratio: %u:%u", ax, ay);
                        } else {
                            snprintf(flash_text, sizeof(flash_text), "Invalid aspect ratio (use X:Y, X Y, X,Y or X-Y)");
                        }
                        flash_until = SDL_GetTicks() + 900;
                    } else if (ti_purpose == TI_SET_ZOOM) {
                        int new_zoom = 0;
                        if (parse_zoom_input(ti.value, &new_zoom)) {
                            if (vr) {
                                vr_set_zoom(vr, new_zoom);
#if SAVE_FILE
                                if (video_file) {
                                    fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent);
                                    write_save_state(SAVE_FILE_PATH, &save_state);
                                }
#endif
                            }
                            snprintf(flash_text, sizeof(flash_text), "Zoom: %d%%", vr ? vr->zoom_percent : new_zoom);
                        } else {
                            snprintf(flash_text, sizeof(flash_text), "Invalid zoom (use 150, 150%%, 2x, 2 times)");
                        }
                        flash_until = SDL_GetTicks() + 900;
                    } else if (ti_purpose == TI_SET_SPEED) {
                        float spd = 0.0f;

                        char buf[256];
                        snprintf(buf, sizeof(buf), "%s", ti.value);

                        size_t len = strlen(buf);
                        if (len > 0 && (buf[len - 1] == 'x' || buf[len - 1] == 'X')) {
                            buf[len - 1] = '\0';
                        }

                        if (sscanf(buf, "%f", &spd) == 1 && spd > 0.0f) {
                            playback_speed = spd;

                            if (vr) {
                                vr_set_speed(vr, playback_speed);
                                SDL_ClearQueuedAudio(vr->audio_dev);
                                if (playback_speed > 2.0f) vr->audio_clock_valid = 0;
                            }

                            snprintf(flash_text, sizeof(flash_text), "Speed: %.6gx", (double)playback_speed);
                        } else {
                            snprintf(flash_text, sizeof(flash_text), "Invalid speed (must be > 0)");
                        }

                        flash_until = SDL_GetTicks() + 900;
                    }
                    ti_purpose = TI_NONE;
                    ti_bm_idx = -1;
                }
                continue;
            }

            if (e.type == SDL_MOUSEMOTION) {
                last_mouse_move = SDL_GetTicks();
                overlay_target = 1.0f;
            }

#ifdef _WIN32
            if (e.type == SDL_SYSWMEVENT) {
                SDL_SysWMmsg* sysmsg = e.syswm.msg;
                if (sysmsg && sysmsg->subsystem == SDL_SYSWM_WINDOWS) {
                    if (sysmsg->msg.win.msg == WM_COMMAND) {
                        int id = LOWORD(sysmsg->msg.win.wParam);
                        if (id == MENU_OPEN) {
                            char* f = open_file_dialog(
                                (const char*[]){"*.mkv", "*.mp4"}, 2, "Video Files (*.mkv, *.mp4)", false,
                                "Select Video File", NULL, &is_supported_video_file
                            );
                            if (f) {
                                const char* old_file_open = video_file;
                                double old_time_open = vr ? vr_get_time(vr) : 0.0;
                                #if SAVE_FILE
                                if (vr && video_file) {
                                    fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent);
                                    write_save_state(SAVE_FILE_PATH, &save_state);
                                }
                                #endif
                                video_file = f;
                                if (!vr) vr = vr_create(win, ren);
                                if (vr_load(vr, f, hw_option)) {
                                    apply_audio_feature_toggles(vr, audio_os_passthrough, night_mode, ambient_glow);
                                    #if SAVE_FILE
                                    apply_save_state_to_vr(vr, &save_state, video_file, &volume_percent);
                                    get_bookmarks_from_save_state(&save_state, video_file, bookmarks, &bookmark_count);
                                    #endif
                                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                                    apply_subtitle_override(
                                        vr,
                                        subtitle_override_colors[subtitle_color_idx],
                                        subtitle_override_sizes[subtitle_size_idx],
                                        subtitle_override_margins[subtitle_move_idx]
                                    );
                                    if (vr->desired_win_w > 0 && vr->desired_win_h > 0) {
                                        requested_window_w = vr->desired_win_w;
                                        requested_window_h = vr->desired_win_h;
                                    }
                                    apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
                                    add_recent_file(f);
                                    set_window_title_for_media(win, vr, f);
                                    nob_log(NOB_INFO, "Loaded %s", f);
                                    hist_push_seek(history, &history_count, &history_pos, old_file_open, old_time_open, f, 0.0);
                                } else {
                                    nob_log(NOB_ERROR, "Failed to load %s", f);
                                }
                            } else {
                                nob_log(NOB_ERROR, "No file selected or unsupported file type.");
                                snprintf(flash_text, sizeof(flash_text), "No file selected or unsupported file type.");
                                flash_until = SDL_GetTicks() + 900;
                            }
                        } else if (id >= MENU_RECENT_BASE && id < MENU_RECENT_BASE + MAX_RECENT) {
                            int idx = id - MENU_RECENT_BASE;
                            if (idx < recent_count) {
                                char* selected_recent = abspath_temp_safe(recent_files[idx]);
                                if (!selected_recent) {
                                    nob_log(NOB_ERROR, "Failed to resolve recent file path: %s", recent_files[idx]);
                                    continue;
                                }
                                const char* old_file_recent = video_file;
                                double old_time_recent = vr ? vr_get_time(vr) : 0.0;
                                #if SAVE_FILE
                                if (vr && video_file) {
                                    fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent);
                                    write_save_state(SAVE_FILE_PATH, &save_state);
                                }
                                #endif
                                video_file = selected_recent;
                                if (!vr) vr = vr_create(win, ren);
                                if (vr_load(vr, video_file, hw_option)) {
                                    apply_audio_feature_toggles(vr, audio_os_passthrough, night_mode, ambient_glow);
                                    #if SAVE_FILE
                                    apply_save_state_to_vr(vr, &save_state, video_file, &volume_percent);
                                    get_bookmarks_from_save_state(&save_state, video_file, bookmarks, &bookmark_count);
                                    #endif
                                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                                    apply_subtitle_override(
                                        vr,
                                        subtitle_override_colors[subtitle_color_idx],
                                        subtitle_override_sizes[subtitle_size_idx],
                                        subtitle_override_margins[subtitle_move_idx]
                                    );
                                    if (vr->desired_win_w > 0 && vr->desired_win_h > 0) {
                                        requested_window_w = vr->desired_win_w;
                                        requested_window_h = vr->desired_win_h;
                                    }
                                    apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
                                    add_recent_file(video_file);
                                    set_window_title_for_media(win, vr, video_file);
                                    nob_log(NOB_INFO, "Loaded %s", video_file);
                                    hist_push_seek(history, &history_count, &history_pos, old_file_recent, old_time_recent, video_file, 0.0);
                                    paused = save_state.global.paused;
                                } else {
                                    nob_log(NOB_ERROR, "Failed to load %s", video_file);
                                }
                            }
                        } else if (id == MENU_EXIT) running = false;
                        else if (id == MENU_FULLSCREEN) {
                            fullscreen = !fullscreen;
                            if (fullscreen) {
                                maximized = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
                                SDL_GetWindowPosition(win, &windowed_x, &windowed_y);
                                SDL_GetWindowSize(win, &windowed_w, &windowed_h);
                                windowed_valid = 1;
                            }
                            SDL_SetWindowFullscreen(win, fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);
                            if (!fullscreen) {
                                SDL_RestoreWindow(win);
                                if (windowed_valid) {
                                    SDL_SetWindowPosition(win, windowed_x, windowed_y);
                                    SDL_SetWindowSize(win, windowed_w, windowed_h);
                                }
                                if (maximized) {
                                    SDL_MaximizeWindow(win);
                                }
                            }
#ifdef _WIN32
                            if (win_hwnd) {
                                SetMenu(win_hwnd, fullscreen ? NULL : g_current_win_menu);
                                DrawMenuBar(win_hwnd);
                            }
#endif
                        } else if (id == MENU_MINIMIZE) SDL_MinimizeWindow(win);
                        else if (id == MENU_MAXIMIZE) {
                            maximized = !maximized;
                            if (maximized) {
                                SDL_MaximizeWindow(win);
                            } else {
                                SDL_RestoreWindow(win);
                                if (windowed_valid) {
                                    SDL_SetWindowPosition(win, windowed_x, windowed_y);
                                    SDL_SetWindowSize(win, windowed_w, windowed_h);
                                }
                            }
                        } else if (id == MENU_RESOLUTION_NATIVE || id == MENU_RESOLUTION_480P || id == MENU_RESOLUTION_720P || id == MENU_RESOLUTION_1080P || id == MENU_RESOLUTION_1440P) {
                            if (fullscreen) {
                                fullscreen = false;
                                SDL_SetWindowFullscreen(win, 0);
                                if (win_hwnd) {
                                    SetMenu(win_hwnd, g_current_win_menu);
                                    DrawMenuBar(win_hwnd);
                                }
                            }

                            SDL_RestoreWindow(win);
                            maximized = false;

                            if (id == MENU_RESOLUTION_NATIVE) {
                                requested_window_w = 0;
                                requested_window_h = 0;
                                if (vr) { vr->desired_win_w = 0; vr->desired_win_h = 0; }
                                if (vr) {
                                    apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
                                }
                            } else {
                                if (id == MENU_RESOLUTION_480P) {
                                    requested_window_w = 854;
                                    requested_window_h = 480;
                                } else if (id == MENU_RESOLUTION_720P) {
                                    requested_window_w = 1280;
                                    requested_window_h = 720;
                                } else if (id == MENU_RESOLUTION_1080P) {
                                    requested_window_w = 1920;
                                    requested_window_h = 1080;
                                } else if (id == MENU_RESOLUTION_1440P) {
                                    requested_window_w = 2560;
                                    requested_window_h = 1440;
                                }
                                if (vr) { vr->desired_win_w = requested_window_w; vr->desired_win_h = requested_window_h; }

                                int target_w = requested_window_w;
                                int target_h = requested_window_h;
                                fit_window_to_display(&target_w, &target_h);
                                SDL_SetWindowSize(win, target_w, target_h);
                                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                            }
                            #if SAVE_FILE
                            if (vr && video_file) {
                                fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent);
                                write_save_state(SAVE_FILE_PATH, &save_state);
                            }
                            #endif
                        } else if (id == MENU_RESOLUTION_CUSTOM) {
                            text_input_open(&ti, "Resolution (WxH):", "", 20);
                            ti_purpose = TI_SET_RESOLUTION;
                        } else if (id == MENU_ASPECT_RATIO_16_9 || id == MENU_ASPECT_RATIO_4_3 || id == MENU_ASPECT_RATIO_1_1 || id == MENU_ASPECT_RATIO_21_9 || id == MENU_ASPECT_RATIO_STRETCH || id == MENU_ASPECT_RATIO_ORIGINAL) {
                            if (vr) {
                                switch (id) {
                                    case MENU_ASPECT_RATIO_16_9:     vr_set_aspect_ratio_mode(vr, 16, 9);              break;
                                    case MENU_ASPECT_RATIO_4_3:      vr_set_aspect_ratio_mode(vr, 4, 3);               break;
                                    case MENU_ASPECT_RATIO_1_1:      vr_set_aspect_ratio_mode(vr, 1, 1);               break;
                                    case MENU_ASPECT_RATIO_21_9:     vr_set_aspect_ratio_mode(vr, 21, 9);              break;
                                    case MENU_ASPECT_RATIO_STRETCH:  vr_set_aspect_ratio_mode(vr, UINT_MAX, UINT_MAX); break;
                                    case MENU_ASPECT_RATIO_ORIGINAL: vr_set_aspect_ratio_mode(vr, 0, 0);               break;
                                }
                                #if SAVE_FILE
                                if (video_file) {
                                    fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent);
                                    write_save_state(SAVE_FILE_PATH, &save_state);
                                }
                                #endif
                            }
                        } else if (id == MENU_ASPECT_RATIO_CUSTOM) {
                            text_input_open(&ti, "Aspect Ratio (X:Y):", "", 16);
                            ti_purpose = TI_SET_ASPECT_RATIO;
                        } else if (id == MENU_ZOOM_RESET) {
                            if (vr) {
                                vr_set_zoom(vr, 100);
                                vr_set_aspect_ratio_mode(vr, 0, 0);
                                requested_window_w = 0;
                                requested_window_h = 0;
                                vr->desired_win_w = 0;
                                vr->desired_win_h = 0;
                                apply_window_size_for_video(win, vr_get_texture(vr), 0, 0);
                                snprintf(flash_text, sizeof(flash_text), "Reset: zoom, aspect ratio, resolution");
                                flash_until = SDL_GetTicks() + 1200;
                            }
                        } else if (id == MENU_ZOOM_CUSTOM) {
                            char zoom_hint[16] = {0};
                            if (vr) snprintf(zoom_hint, sizeof(zoom_hint), "%d%%", vr->zoom_percent);
                            text_input_open(&ti, "Zoom (e.g. 150%, 2x):", zoom_hint[0] ? zoom_hint : "", 16);
                            ti_purpose = TI_SET_ZOOM;
                        } else if (id == MENU_ZOOM_IN) {
                            if (vr) {
                                vr_set_zoom(vr, vr->zoom_percent + 25);
                                snprintf(flash_text, sizeof(flash_text), "Zoom: %d%%", vr->zoom_percent);
                                flash_until = SDL_GetTicks() + 900;
                            }
                        } else if (id == MENU_ZOOM_OUT) {
                            if (vr) {
                                vr_set_zoom(vr, vr->zoom_percent - 25);
                                snprintf(flash_text, sizeof(flash_text), "Zoom: %d%%", vr->zoom_percent);
                                flash_until = SDL_GetTicks() + 900;
                            }
                        } else if (id == MENU_PLAY_PAUSE) {
                            paused = !paused;
                            if (vr) vr_set_paused(vr, paused);
                            overlay_target = 1.0f;
                        } else if (id == MENU_NEXT_FRAME && vr) {
                            double bt_nxt = vr_get_time(vr);
                            vr_next_frame(vr, 1);
                            if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_nxt, video_file, vr_get_time(vr));
                        } else if (id == MENU_PREV_FRAME && vr) {
                            double bt_prv = vr_get_time(vr);
                            vr_next_frame(vr, -1);
                            if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_prv, video_file, vr_get_time(vr));
                        } else if (id == MENU_NEXT_MEDIA || id == MENU_PREV_MEDIA) {
                            navigate_media(
                                &vr,
                                win,
                                ren,
                                &video_file,
                                id == MENU_NEXT_MEDIA ? 1 : -1,
                                paused,
                                &volume_percent,
                                subtitle_override_colors[subtitle_color_idx],
                                subtitle_override_sizes[subtitle_size_idx],
                                subtitle_override_margins[subtitle_move_idx],
                                0,
                                audio_os_passthrough,
                                night_mode,
                                ambient_glow,
                                requested_window_w,
                                requested_window_h,
                                history,
                                &history_count,
                                &history_pos,
                                &save_state,
                                bookmarks,
                                &bookmark_count
                            );
                        } else if (id == MENU_NEXT_MEDIA_WRAP || id == MENU_PREV_MEDIA_WRAP) {
                            navigate_media(
                                &vr,
                                win,
                                ren,
                                &video_file,
                                id == MENU_NEXT_MEDIA_WRAP ? 1 : -1,
                                paused,
                                &volume_percent,
                                subtitle_override_colors[subtitle_color_idx],
                                subtitle_override_sizes[subtitle_size_idx],
                                subtitle_override_margins[subtitle_move_idx],
                                1,
                                audio_os_passthrough,
                                night_mode,
                                ambient_glow,
                                requested_window_w,
                                requested_window_h,
                                history,
                                &history_count,
                                &history_pos,
                                &save_state,
                                bookmarks,
                                &bookmark_count
                            );
                        } else if (id == MENU_SEEK_BACK && vr) {
                            double bt_back = vr_get_time(vr);
                            double t = bt_back - 5.0;
                            if (t < 0.0) t = 0.0;
                            seek_and_preview_if_paused(vr, t, paused);
                            if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_back, video_file, t);
                            snprintf(flash_text, sizeof(flash_text), "-5s");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_SEEK_FORWARD && vr) {
                            double bt_fwd = vr_get_time(vr);
                            double t = bt_fwd + 5.0;
                            double dur = vr_get_duration(vr);
                            if (dur > 0.0 && t > dur) t = dur;
                            seek_and_preview_if_paused(vr, t, paused);
                            if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_fwd, video_file, t);
                            snprintf(flash_text, sizeof(flash_text), "+5s");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_SEEK_BEGINNING && vr) {
                            double bt_beg = vr_get_time(vr);
                            seek_and_preview_if_paused(vr, 0.0, paused);
                            if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_beg, video_file, 0.0);
                            snprintf(flash_text, sizeof(flash_text), "Beginning");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_SEEK_END && vr) {
                            double dur_end = vr_get_duration(vr);
                            if (dur_end > 0.0) {
                                double bt_end = vr_get_time(vr);
                                seek_and_preview_if_paused(vr, dur_end, paused);
                                if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_end, video_file, dur_end);
                                snprintf(flash_text, sizeof(flash_text), "End");
                                flash_until = SDL_GetTicks() + 900;
                            }
                        } else if (id == MENU_VOLUME_UP && vr) {
                            float v_before_mup = volume_percent;
                            volume_percent = clampf(volume_percent + 5.0f, 0.0f, 200.0f);
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            hist_push_volume(history, &history_count, &history_pos, v_before_mup, volume_percent);
                            snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_VOLUME_DOWN && vr) {
                            float v_before_mdn = volume_percent;
                            volume_percent = clampf(volume_percent - 5.0f, 0.0f, 200.0f);
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            hist_push_volume(history, &history_count, &history_pos, v_before_mdn, volume_percent);
                            snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_VOLUME_UP_FINE && vr) {
                            float v_before_muf = volume_percent;
                            volume_percent = clampf(volume_percent + 1.0f, 0.0f, 200.0f);
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            hist_push_volume(history, &history_count, &history_pos, v_before_muf, volume_percent);
                            snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_VOLUME_DOWN_FINE && vr) {
                            float v_before_mdf = volume_percent;
                            volume_percent = clampf(volume_percent - 1.0f, 0.0f, 200.0f);
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            hist_push_volume(history, &history_count, &history_pos, v_before_mdf, volume_percent);
                            snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_SET_VOLUME0 && vr) {
                            float v_before_sv0 = volume_percent;
                            volume_percent = 0.0f;
                            vr_set_volume(vr, 0.0f);
                            hist_push_volume(history, &history_count, &history_pos, v_before_sv0, volume_percent);
                            snprintf(flash_text, sizeof(flash_text), "VOL 0");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_SET_VOLUME100 && vr) {
                            float v_before_sv100 = volume_percent;
                            volume_percent = 100.0f;
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            hist_push_volume(history, &history_count, &history_pos, v_before_sv100, volume_percent);
                            snprintf(flash_text, sizeof(flash_text), "VOL 100");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_SET_VOLUME200 && vr) {
                            float v_before_sv200 = volume_percent;
                            volume_percent = 200.0f;
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            hist_push_volume(history, &history_count, &history_pos, v_before_sv200, volume_percent);
                            snprintf(flash_text, sizeof(flash_text), "VOL 200");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_ADD_BOOKMARK && vr) {
                            if (bookmark_count < MAX_BOOKMARKS_PER_FILE) {
                                int before_cnt_add = bookmark_count;
                                Bookmark before_bms_add[MAX_BOOKMARKS_PER_FILE];
                                memcpy(before_bms_add, bookmarks, sizeof(Bookmark) * (size_t)before_cnt_add);
                                Bookmark* bm = &bookmarks[bookmark_count];
                                bm->time      = vr_get_time(vr);
                                snprintf(bm->name, BOOKMARK_NAME_MAX, "%s",
                                        format_time_temp(bm->time));
                                bm->color_rgb = THEME_DEFAULT_BOOKMARK_COLOR;
                                bm->is_default_color = true;
                                bookmark_count++;
                                sort_bookmarks_by_time(bookmarks, bookmark_count);
                                { char _d[128]; snprintf(_d, sizeof(_d), "bookmark add %s", bm->name); hist_push_bookmark(history, &history_count, &history_pos, before_bms_add, before_cnt_add, bookmarks, bookmark_count, _d); }
                                #if SAVE_FILE
                                update_bookmarks_in_save_state(&save_state, video_file,
                                                                bookmarks, bookmark_count);
                                write_save_state(SAVE_FILE_PATH, &save_state);
                                #endif
                                snprintf(flash_text, sizeof(flash_text), "Bookmark added: %s", bm->name);
                            } else {
                                snprintf(flash_text, sizeof(flash_text),
                                        "Max bookmarks (%d) reached", MAX_BOOKMARKS_PER_FILE);
                            }
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DELETE_BOOKMARK && vr) {
                            if (bookmark_count > 0) {
                                double cur = vr_get_time(vr);
                                int closest = 0;
                                double best = fabs(bookmarks[0].time - cur);
                                for (int i = 1; i < bookmark_count; i++) {
                                    double d = fabs(bookmarks[i].time - cur);
                                    if (d < best) { best = d; closest = i; }
                                }
                                int before_cnt_del = bookmark_count;
                                Bookmark before_bms_del[MAX_BOOKMARKS_PER_FILE];
                                memcpy(before_bms_del, bookmarks, sizeof(Bookmark) * (size_t)before_cnt_del);
                                char del_name[BOOKMARK_NAME_MAX];
                                snprintf(del_name, sizeof(del_name), "%s", bookmarks[closest].name);
                                memmove(&bookmarks[closest], &bookmarks[closest + 1],
                                        sizeof(Bookmark) * (size_t)(bookmark_count - closest - 1));
                                bookmark_count--;
                                { char _d[128]; snprintf(_d, sizeof(_d), "bookmark remove %s", del_name); hist_push_bookmark(history, &history_count, &history_pos, before_bms_del, before_cnt_del, bookmarks, bookmark_count, _d); }
                                #if SAVE_FILE
                                update_bookmarks_in_save_state(&save_state, video_file,
                                                                bookmarks, bookmark_count);
                                write_save_state(SAVE_FILE_PATH, &save_state);
                                #endif
                                snprintf(flash_text, sizeof(flash_text), "Bookmark deleted: %s", del_name);
                                flash_until = SDL_GetTicks() + 900;
                            }
                        } else if ((id == MENU_NEXT_BOOKMARK || id == MENU_PREV_BOOKMARK) && vr) {
                            int go_next_m = (id == MENU_NEXT_BOOKMARK);
                            double cur_m = vr_get_time(vr);
                            double dur_m = vr_get_duration(vr);
                            double best_m = -1.0;
                            char best_name_m[192] = {0};
                            for (int i = 0; i < bookmark_count; i++) {
                                double t = bookmarks[i].time;
                                if (go_next_m ? (t > cur_m + 0.1) : (t < cur_m - 1.0)) {
                                    if (best_m < 0.0 || (go_next_m ? t < best_m : t > best_m)) {
                                        best_m = t;
                                        snprintf(best_name_m, sizeof(best_name_m), "%s", bookmarks[i].name);
                                    }
                                }
                            }
                            int ch_count_m = get_media_chapter_count(vr);
                            for (int i = 0; i < ch_count_m; i++) {
                                double t = get_media_chapter_time(vr, i);
                                if (t < 0.0 || t > dur_m) continue;
                                if (go_next_m ? (t > cur_m + 0.1) : (t < cur_m - 1.0)) {
                                    if (best_m < 0.0 || (go_next_m ? t < best_m : t > best_m)) {
                                        best_m = t;
                                        get_media_chapter_label(vr, i, best_name_m, sizeof(best_name_m));
                                    }
                                }
                            }
                            if (best_m >= 0.0) {
                                double bt_menu_brk = vr_get_time(vr);
                                seek_and_preview_if_paused(vr, best_m, paused);
                                if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_menu_brk, video_file, best_m);
                                snprintf(flash_text, sizeof(flash_text), "%s (%s)", best_name_m, format_time_temp(best_m));
                            } else {
                                snprintf(flash_text, sizeof(flash_text), go_next_m ? "No next bookmark/chapter" : "No previous bookmark/chapter");
                            }
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_UNDO && vr) {
                            if (!hist_undo(history, &history_count, &history_pos,
                                    &vr, win, ren, &video_file, paused, &volume_percent,
                                    subtitle_override_colors[subtitle_color_idx],
                                    subtitle_override_sizes[subtitle_size_idx],
                                    subtitle_override_margins[subtitle_move_idx],
                                    requested_window_w, requested_window_h,
                                    bookmarks, &bookmark_count, audio_os_passthrough, night_mode, ambient_glow, &save_state)) {
                                snprintf(flash_text, sizeof(flash_text), "Nothing to undo");
                            }
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_REDO && vr) {
                            if (!hist_redo(history, &history_count, &history_pos,
                                    &vr, win, ren, &video_file, paused, &volume_percent,
                                    subtitle_override_colors[subtitle_color_idx],
                                    subtitle_override_sizes[subtitle_size_idx],
                                    subtitle_override_margins[subtitle_move_idx],
                                    requested_window_w, requested_window_h,
                                    bookmarks, &bookmark_count, audio_os_passthrough, night_mode, ambient_glow, &save_state)) {
                                snprintf(flash_text, sizeof(flash_text), "Nothing to redo");
                            }
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_SCREENSHOT && vr) {
                            take_screenshot(vr, win, ren, flash_text, &flash_until);
                        } else if (id == MENU_DRAW_ENTER && vr) {
                            if (!draw_mode_active) {
                                draw_mode_active = 1;
                                draw_mode_was_paused = paused;
                                paused = true;
                                if (vr) vr_set_paused(vr, paused);
                                int win_w, win_h;
                                SDL_GetWindowSize(win, &win_w, &win_h);
                                if (draw_canvas) SDL_DestroyTexture(draw_canvas);
                                draw_canvas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, win_w, win_h);
                                SDL_SetTextureBlendMode(draw_canvas, SDL_BLENDMODE_BLEND);
                                draw_notify_canvas_resized(&draw_state, win_w, win_h);
                                menu_set_draw_mode(true);
                                snprintf(flash_text, sizeof(flash_text), "Draw Mode: ON (Paused)");
                                flash_until = SDL_GetTicks() + 900;
                            }
                        } else if (id == MENU_DRAW_EXIT) {
                            if (draw_mode_active) {
                                draw_mode_active = 0;
                                paused = draw_mode_was_paused;
                                if (vr) vr_set_paused(vr, paused);
                                menu_set_draw_mode(false);
                                snprintf(flash_text, sizeof(flash_text), "Draw Mode: OFF");
                                flash_until = SDL_GetTicks() + 900;
                            }
                        } else if (id == MENU_DRAW_EXPORT_WITH_FRAME && vr && draw_canvas && draw_mode_active) {
                            int win_w, win_h;
                            SDL_GetWindowSize(win, &win_w, &win_h);
                            SDL_Texture* frame_tex = vr_get_texture(vr);
                            SDL_Rect video_dst = {0, 0, win_w, win_h};
                            if (frame_tex) {
                                int src_w = 0, src_h = 0;
                                SDL_QueryTexture(frame_tex, NULL, NULL, &src_w, &src_h);
                                video_dst = compute_video_dst_rect(win_w, win_h, src_w, src_h, vr->ar_x, vr->ar_y);
                                video_dst = apply_zoom_to_rect(video_dst, win_w, win_h, vr->zoom_percent);
                            }
                            draw_export(ren, draw_canvas, frame_tex, video_dst, win_w, win_h, 1, &draw_state);
                            snprintf(flash_text, sizeof(flash_text), "Drawing exported");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_EXPORT_ONLY && draw_canvas && draw_mode_active) {
                            int win_w, win_h;
                            SDL_GetWindowSize(win, &win_w, &win_h);
                            draw_export(ren, draw_canvas, NULL, (SDL_Rect){0,0,0,0}, win_w, win_h, 0, &draw_state);
                            snprintf(flash_text, sizeof(flash_text), "Drawing exported");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_CLEAR && draw_mode_active) {
                            draw_clear(&draw_state);
                            snprintf(flash_text, sizeof(flash_text), "Canvas cleared");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_TOOL_PEN && draw_mode_active) {
                            draw_state.current_tool = TOOL_PEN;
                            snprintf(flash_text, sizeof(flash_text), "Tool: Pen");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_TOOL_ERASER && draw_mode_active) {
                            draw_state.current_tool = TOOL_ERASER;
                            snprintf(flash_text, sizeof(flash_text), "Tool: Eraser");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_TOOL_MARKER && draw_mode_active) {
                            draw_state.current_tool = TOOL_MARKER;
                            snprintf(flash_text, sizeof(flash_text), "Tool: Marker (Contrast)");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_TOOL_LINE && draw_mode_active) {
                            draw_state.current_tool = TOOL_LINE;
                            snprintf(flash_text, sizeof(flash_text), "Tool: Line");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_TOOL_RECT && draw_mode_active) {
                            draw_state.current_tool = TOOL_RECT;
                            snprintf(flash_text, sizeof(flash_text), "Tool: Rectangle");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_TOOL_CIRCLE && draw_mode_active) {
                            draw_state.current_tool = TOOL_CIRCLE;
                            snprintf(flash_text, sizeof(flash_text), "Tool: Circle");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_TOOL_FILLED_RECT && draw_mode_active) {
                            draw_state.current_tool = TOOL_FILLED_RECT;
                            snprintf(flash_text, sizeof(flash_text), "Tool: Filled Rect");
                            flash_until = SDL_GetTicks() + 900;
                        } else if (id == MENU_DRAW_TOOL_FILLED_CIRCLE && draw_mode_active) {
                            draw_state.current_tool = TOOL_FILLED_CIRCLE;
                            snprintf(flash_text, sizeof(flash_text), "Tool: Filled Circle");
                            flash_until = SDL_GetTicks() + 900;
                        }
                    }
                }
            }
#endif

            if (e.type == SDL_TEXTINPUT && vr) {
                const char* txt = e.text.text;
                int go_next_ti = -1;
                if (txt[0] == ']' && txt[1] == '\0') go_next_ti = 1;
                else if (txt[0] == '[' && txt[1] == '\0') go_next_ti = 0;
                else if (txt[0] == '?' && txt[1] == '\0') kbd_overlay_open = !kbd_overlay_open;
                if (go_next_ti >= 0) {
                    double cur_ti = vr_get_time(vr);
                    double dur_ti = vr_get_duration(vr);
                    double best_ti = -1.0;
                    char best_name_ti[192] = {0};
                    for (int i = 0; i < bookmark_count; i++) {
                        double t = bookmarks[i].time;
                        if (go_next_ti ? (t > cur_ti + 0.1) : (t < cur_ti - 1.0)) {
                            if (best_ti < 0.0 || (go_next_ti ? t < best_ti : t > best_ti)) {
                                best_ti = t;
                                snprintf(best_name_ti, sizeof(best_name_ti), "%s", bookmarks[i].name);
                            }
                        }
                    }
                    int ch_count_ti = get_media_chapter_count(vr);
                    for (int i = 0; i < ch_count_ti; i++) {
                        double t = get_media_chapter_time(vr, i);
                        if (t < 0.0 || t > dur_ti) continue;
                        if (go_next_ti ? (t > cur_ti + 0.1) : (t < cur_ti - 1.0)) {
                            if (best_ti < 0.0 || (go_next_ti ? t < best_ti : t > best_ti)) {
                                best_ti = t;
                                get_media_chapter_label(vr, i, best_name_ti, sizeof(best_name_ti));
                            }
                        }
                    }
                    if (best_ti >= 0.0) {
                        double bt_ti = vr_get_time(vr);
                        seek_and_preview_if_paused(vr, best_ti, paused);
                        if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_ti, video_file, best_ti);
                        snprintf(flash_text, sizeof(flash_text), "%s (%s)", best_name_ti, format_time_temp(best_ti));
                    } else {
                        snprintf(flash_text, sizeof(flash_text), go_next_ti ? "No next bookmark/chapter" : "No previous bookmark/chapter");
                    }
                    flash_until = SDL_GetTicks() + 900;
                }
            }

            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;
                if ((key==SDLK_o)&&(e.key.keysym.mod & KMOD_CTRL)) {
                    char* f = open_file_dialog(
                        (const char*[]){"*.mkv", "*.mp4"}, 2, "Video Files (*.mkv, *.mp4)", false,
                        "Select Video File", NULL, &is_supported_video_file
                    );
                    if (f) {
                        const char* old_file_o = video_file;
                        double old_time_o = vr ? vr_get_time(vr) : 0.0;
                        #if SAVE_FILE
                        if (vr && video_file) {
                            fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent);
                            write_save_state(SAVE_FILE_PATH, &save_state);
                        }
                        #endif
                        video_file = f;
                        if (!vr) vr = vr_create(win, ren);
                        if (vr_load(vr, f, hw_option)) {
                            apply_audio_feature_toggles(vr, audio_os_passthrough, night_mode, ambient_glow);
                            #if SAVE_FILE
                            apply_save_state_to_vr(vr, &save_state, video_file, &volume_percent);
                            get_bookmarks_from_save_state(&save_state, video_file, bookmarks, &bookmark_count);
                            #endif
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            apply_subtitle_override(
                                vr,
                                subtitle_override_colors[subtitle_color_idx],
                                subtitle_override_sizes[subtitle_size_idx],
                                subtitle_override_margins[subtitle_move_idx]
                            );
                            if (vr->desired_win_w > 0 && vr->desired_win_h > 0) {
                                requested_window_w = vr->desired_win_w;
                                requested_window_h = vr->desired_win_h;
                            }
                            apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
                            add_recent_file(f);
                            set_window_title_for_media(win, vr, f);
                            nob_log(NOB_INFO, "Loaded %s", f);
                            hist_push_seek(history, &history_count, &history_pos, old_file_o, old_time_o, f, 0.0);
                        } else {
                            nob_log(NOB_ERROR, "Failed to load %s", f);
                        }
                    } else {
                        nob_log(NOB_ERROR, "No file selected or unsupported file type.");
                        snprintf(flash_text, sizeof(flash_text), "No file selected or unsupported file type.");
                        flash_until = SDL_GetTicks() + 900;
                    }
                } else if ((key == SDLK_r) && (e.key.keysym.mod & KMOD_CTRL) && vr && video_file) {
                    double rl_time     = vr_get_time(vr);
                    int    rl_audio    = vr->current_audio;
                    int    rl_subtitle = vr->current_subtitle;
                    if (vr_load(vr, video_file, hw_option)) {
                        apply_audio_feature_toggles(vr, audio_os_passthrough, night_mode, ambient_glow);
                        if (rl_audio >= 0 && rl_audio < vr_get_audio_track_count(vr))
                            vr_select_audio_track(vr, rl_audio);
                        vr_select_subtitle_track(vr, rl_subtitle);
                        vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                        apply_subtitle_override(
                            vr,
                            subtitle_override_colors[subtitle_color_idx],
                            subtitle_override_sizes[subtitle_size_idx],
                            subtitle_override_margins[subtitle_move_idx]
                        );
                        vr_set_speed(vr, playback_speed);
                        if (vr->audio_dev) SDL_ClearQueuedAudio(vr->audio_dev);
                        vr->audio_clock_valid = 0;
                        seek_and_preview_if_paused(vr, rl_time, paused);
                        vr_set_paused(vr, paused);
                        nob_log(NOB_INFO, "Reloaded %s", video_file);
                        snprintf(flash_text, sizeof(flash_text), "Reloaded");
                    } else {
                        nob_log(NOB_ERROR, "Failed to reload %s", video_file);
                        snprintf(flash_text, sizeof(flash_text), "Reload failed");
                    }
                    flash_until = SDL_GetTicks() + 900;
                } else if (key==SDLK_F4 && (e.key.keysym.mod & KMOD_ALT)) running=0;
                #ifndef _WIN32
                else if (key == SDLK_q && (e.key.keysym.mod & KMOD_CTRL) && !(e.key.keysym.mod & ~(KMOD_CTRL))) running=0;
                #endif

                if (key==SDLK_F2 && vr) {
                    take_screenshot(vr, win, ren, flash_text, &flash_until);
                }
                
                if (key==SDLK_F11 || (key==SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    fullscreen =! fullscreen;
                    if (fullscreen) {
                        maximized = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
                        SDL_GetWindowPosition(win, &windowed_x, &windowed_y);
                        SDL_GetWindowSize(win, &windowed_w, &windowed_h);
                        windowed_valid = 1;
                    }
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#ifdef _WIN32
                    if (win_hwnd) {
                        SetMenu(win_hwnd, fullscreen ? NULL : g_current_win_menu);
                        DrawMenuBar(win_hwnd);
                    }
#endif
                    if (!fullscreen) {
                        SDL_RestoreWindow(win);
                        if (windowed_valid) {
                            SDL_SetWindowPosition(win, windowed_x, windowed_y);
                            SDL_SetWindowSize(win, windowed_w, windowed_h);
                        }
                        if (maximized) {
                            SDL_MaximizeWindow(win);
                        }
                    } else {
                        SDL_Rect bounds;
                        int display_index = SDL_GetWindowDisplayIndex(win);
                        if (display_index >= 0 && SDL_GetDisplayBounds(display_index, &bounds) == 0) {
                            SDL_SetWindowPosition(win, bounds.x, bounds.y);
                            SDL_SetWindowSize(win, bounds.w, bounds.h);
                        }
                    }
                }
                if (key==SDLK_F10 || (key==SDLK_RETURN && (e.key.keysym.mod & KMOD_CTRL))) {
                    maximized = !maximized;
                    if (maximized) {
                        SDL_MaximizeWindow(win);
                    } else {
                        SDL_RestoreWindow(win);
                        if (windowed_valid) {
                            SDL_SetWindowPosition(win, windowed_x, windowed_y);
                            SDL_SetWindowSize(win, windowed_w, windowed_h);
                        }
                    }
                }
                if (key==SDLK_m && (e.key.keysym.mod & KMOD_ALT)) {
                    SDL_MinimizeWindow(win);
                }
                if (key == SDLK_LEFT && (e.key.keysym.mod & KMOD_ALT) && vr) {
                    sprintf(flash_text, "Previous Frame");
                    flash_until = SDL_GetTicks() + 900;
                    double bt_altl = vr_get_time(vr);
                    vr_next_frame(vr, -1);
                    if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_altl, video_file, vr_get_time(vr));
                }
                if (key == SDLK_RIGHT && (e.key.keysym.mod & KMOD_ALT) && vr) {
                    sprintf(flash_text, "Next Frame");
                    flash_until = SDL_GetTicks() + 900;
                    double bt_altr = vr_get_time(vr);
                    vr_next_frame(vr, 1);
                    if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_altr, video_file, vr_get_time(vr));
                }
                if (key == SDLK_RIGHT && (e.key.keysym.mod & KMOD_SHIFT) && vr) {
                    if (e.key.keysym.mod & KMOD_CTRL) {
                        navigate_media(
                            &vr,
                            win,
                            ren,
                            &video_file,
                            1,
                            paused,
                            &volume_percent,
                            subtitle_override_colors[subtitle_color_idx],
                            subtitle_override_sizes[subtitle_size_idx],
                            subtitle_override_margins[subtitle_move_idx],
                            1,
                            audio_os_passthrough,
                            night_mode,
                            ambient_glow,
                            requested_window_w,
                            requested_window_h,
                            history,
                            &history_count,
                            &history_pos,
                            &save_state,
                            bookmarks,
                            &bookmark_count
                        );
                    } else {
                        navigate_media(
                            &vr,
                            win,
                            ren,
                            &video_file,
                            1,
                            paused,
                            &volume_percent,
                            subtitle_override_colors[subtitle_color_idx],
                            subtitle_override_sizes[subtitle_size_idx],
                            subtitle_override_margins[subtitle_move_idx],
                            0,
                            audio_os_passthrough,
                            night_mode,
                            ambient_glow,
                            requested_window_w,
                            requested_window_h,
                            history,
                            &history_count,
                            &history_pos,
                            &save_state,
                            bookmarks,
                            &bookmark_count
                        );
                    }
                }
                if (key == SDLK_LEFT && (e.key.keysym.mod & KMOD_SHIFT) && vr) {
                    if (e.key.keysym.mod & KMOD_CTRL) {
                        navigate_media(
                            &vr,
                            win,
                            ren,
                            &video_file,
                            -1,
                            paused,
                            &volume_percent,
                            subtitle_override_colors[subtitle_color_idx],
                            subtitle_override_sizes[subtitle_size_idx],
                            subtitle_override_margins[subtitle_move_idx],
                            1,
                            audio_os_passthrough,
                            night_mode,
                            ambient_glow,
                            requested_window_w,
                            requested_window_h,
                            history,
                            &history_count,
                            &history_pos,
                            &save_state,
                            bookmarks,
                            &bookmark_count
                        );
                    } else {
                    navigate_media(
                        &vr,
                        win,
                        ren,
                        &video_file,
                        -1,
                        paused,
                        &volume_percent,
                        subtitle_override_colors[subtitle_color_idx],
                        subtitle_override_sizes[subtitle_size_idx],
                        subtitle_override_margins[subtitle_move_idx],
                        0,
                        audio_os_passthrough,
                        night_mode,
                        ambient_glow,
                        requested_window_w,
                        requested_window_h,
                        history,
                        &history_count,
                        &history_pos,
                        &save_state,
                        bookmarks,
                        &bookmark_count
                    );
                    }
                }
                if (key == SDLK_ESCAPE) {
                    if (kbd_overlay_open) {
                        kbd_overlay_open = false;
                    } else if (draw_mode_active) {
                        draw_mode_active = 0;
                        paused = draw_mode_was_paused;
                        if (vr) vr_set_paused(vr, paused);
                        snprintf(flash_text, sizeof(flash_text), "Draw Mode: OFF");
                        flash_until = SDL_GetTicks() + 900;
                    } else if (bm_ctx_open) {
                        bm_ctx_open = 0;
                    } else if (menu_open) {
                        menu_open = false;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        theme_menu_open = false;
                        playback_menu_open = false;
                        subtitle_settings_menu_open = false;
                        subtitle_settings_value_menu_open = false;
                        subtitle_settings_value_menu_row = -1;
                    } else if (fullscreen) {
                        fullscreen = false;
                        SDL_SetWindowFullscreen(win, 0);
#ifdef _WIN32
                        if (win_hwnd) {
                            SetMenu(win_hwnd, g_current_win_menu);
                            DrawMenuBar(win_hwnd);
                        }
#endif
                        if (windowed_valid) {
                            SDL_SetWindowPosition(win, windowed_x, windowed_y);
                            SDL_SetWindowSize(win, windowed_w, windowed_h);
                        }
                        if (maximized) {
                            SDL_MaximizeWindow(win);
                        }
                    }
                }
                if (key == SDLK_SPACE) {
                    paused = !paused;
                    if (vr) vr_set_paused(vr, paused);
                    overlay_target = 1.0f;
                }
                if (key == SDLK_LEFT && vr && !(e.key.keysym.mod & KMOD_ALT) && !(e.key.keysym.mod & KMOD_SHIFT)) {
                    double bt_left = vr_get_time(vr);
                    double t = bt_left - 5.0;
                    if (t < 0.0) t = 0.0;
                    seek_and_preview_if_paused(vr, t, paused);
                    if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_left, video_file, t);
                    snprintf(flash_text, sizeof(flash_text), "-5s");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_RIGHT && vr && !(e.key.keysym.mod & KMOD_ALT) && !(e.key.keysym.mod & KMOD_SHIFT)) {
                    double bt_right = vr_get_time(vr);
                    double t = bt_right + 5.0;
                    double dur = vr_get_duration(vr);
                    if (dur > 0.0 && t > dur) t = dur;
                    seek_and_preview_if_paused(vr, t, paused);
                    if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_right, video_file, t);
                    snprintf(flash_text, sizeof(flash_text), "+5s");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_UP && vr) {
                    int percent_step = (e.key.keysym.mod & KMOD_ALT) ? 1 : 5;
                    float vol_before_up = volume_percent;
                    volume_percent = clampf(volume_percent + (float)percent_step, 0.0f, 200.0f);
                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                    hist_push_volume(history, &history_count, &history_pos, vol_before_up, volume_percent);
                    snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_DOWN && vr) {
                    int percent_step = (e.key.keysym.mod & KMOD_ALT) ? 1 : 5;
                    float vol_before_dn = volume_percent;
                    volume_percent = clampf(volume_percent - (float)percent_step, 0.0f, 200.0f);
                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                    hist_push_volume(history, &history_count, &history_pos, vol_before_dn, volume_percent);
                    snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_z && (e.key.keysym.mod & KMOD_CTRL) && !(e.key.keysym.mod & KMOD_SHIFT) && vr && !draw_mode_active) {
                    if (!hist_undo(history, &history_count, &history_pos,
                            &vr, win, ren, &video_file, paused, &volume_percent,
                            subtitle_override_colors[subtitle_color_idx],
                            subtitle_override_sizes[subtitle_size_idx],
                            subtitle_override_margins[subtitle_move_idx],
                            requested_window_w, requested_window_h,
                            bookmarks, &bookmark_count, audio_os_passthrough, night_mode, ambient_glow, &save_state)) {
                        snprintf(flash_text, sizeof(flash_text), "Nothing to undo");
                    }
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_y && (e.key.keysym.mod & KMOD_CTRL) && vr && !draw_mode_active) {
                    if (!hist_redo(history, &history_count, &history_pos,
                            &vr, win, ren, &video_file, paused, &volume_percent,
                            subtitle_override_colors[subtitle_color_idx],
                            subtitle_override_sizes[subtitle_size_idx],
                            subtitle_override_margins[subtitle_move_idx],
                            requested_window_w, requested_window_h,
                            bookmarks, &bookmark_count, audio_os_passthrough, night_mode, ambient_glow, &save_state)) {
                        snprintf(flash_text, sizeof(flash_text), "Nothing to redo");
                    }
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_1 && (e.key.keysym.mod & KMOD_CTRL) && vr) {
                    float v_before_k1 = volume_percent;
                    volume_percent = 100.0f;
                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                    hist_push_volume(history, &history_count, &history_pos, v_before_k1, volume_percent);
                    snprintf(flash_text, sizeof(flash_text), "VOL 100");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_2 && (e.key.keysym.mod & KMOD_CTRL) && vr) {
                    float v_before_k2 = volume_percent;
                    volume_percent = 200.0f;
                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                    hist_push_volume(history, &history_count, &history_pos, v_before_k2, volume_percent);
                    snprintf(flash_text, sizeof(flash_text), "VOL 200");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_c && !(e.key.keysym.mod & KMOD_CTRL) && vr) {
                    int count = vr_get_subtitle_track_count(vr);
                    if (vr->current_subtitle >= 0) {
                        last_subtitle_track = vr->current_subtitle;
                        vr_select_subtitle_track(vr, -1);
                        snprintf(flash_text, sizeof(flash_text), "Subtitles: Off");
                    } else if (count > 0) {
                        int pick = (last_subtitle_track >= 0 && last_subtitle_track < count) ? last_subtitle_track : 0;
                        vr_select_subtitle_track(vr, pick);
                        const char* name = vr_get_subtitle_track_name(vr, pick);
                        snprintf(flash_text, sizeof(flash_text), "Subtitles: %s", name ? name : "");
                    } else {
                        snprintf(flash_text, sizeof(flash_text), "No subtitles");
                    }
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_c && !(e.key.keysym.mod & KMOD_CTRL) && (e.key.keysym.mod & KMOD_ALT) && vr) {
                    int count = vr_get_subtitle_track_count(vr);
                    if (count <= 0) {
                        snprintf(flash_text, sizeof(flash_text), "No subtitles");
                    } else {
                        int next = vr->current_subtitle + 1;
                        if (next >= count) next = -1;
                        vr_select_subtitle_track(vr, next);
                        if (next >= 0) {
                            last_subtitle_track = next;
                            const char* name = vr_get_subtitle_track_name(vr, next);
                            snprintf(flash_text, sizeof(flash_text), "Subtitles: %s", name ? name : "");
                        } else {
                            snprintf(flash_text, sizeof(flash_text), "Subtitles: Off");
                        }
                    }
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_b && !(e.key.keysym.mod & KMOD_SHIFT) && !(e.key.keysym.mod & KMOD_ALT) && vr && !ti.active) {
                    if (bookmark_count < MAX_BOOKMARKS_PER_FILE) {
                        int before_cnt_b = bookmark_count;
                        Bookmark before_bms_b[MAX_BOOKMARKS_PER_FILE];
                        memcpy(before_bms_b, bookmarks, sizeof(Bookmark) * (size_t)before_cnt_b);
                        Bookmark* bm = &bookmarks[bookmark_count];
                        bm->time      = vr_get_time(vr);
                        snprintf(bm->name, BOOKMARK_NAME_MAX, "%s",
                                 format_time_temp(bm->time));
                        bm->color_rgb = THEME_DEFAULT_BOOKMARK_COLOR;
                        bm->is_default_color = true;
                        bookmark_count++;
                        sort_bookmarks_by_time(bookmarks, bookmark_count);
                        { char _d[128]; snprintf(_d, sizeof(_d), "bookmark add %s", bm->name); hist_push_bookmark(history, &history_count, &history_pos, before_bms_b, before_cnt_b, bookmarks, bookmark_count, _d); }
                        #if SAVE_FILE
                        update_bookmarks_in_save_state(&save_state, video_file,
                                                        bookmarks, bookmark_count);
                        write_save_state(SAVE_FILE_PATH, &save_state);
                        #endif
                        snprintf(flash_text, sizeof(flash_text), "Bookmark added: %s", bm->name);
                    } else {
                        snprintf(flash_text, sizeof(flash_text),
                                 "Max bookmarks (%d) reached", MAX_BOOKMARKS_PER_FILE);
                    }
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_b && (e.key.keysym.mod & KMOD_SHIFT) && (!(e.key.keysym.mod & KMOD_ALT)) && vr && bookmark_count > 0 && !ti.active) {
                    double cur = vr_get_time(vr);
                    int closest = 0;
                    double best = fabs(bookmarks[0].time - cur);
                    for (int i = 1; i < bookmark_count; i++) {
                        double d = fabs(bookmarks[i].time - cur);
                        if (d < best) { best = d; closest = i; }
                    }
                    int before_cnt_bd = bookmark_count;
                    Bookmark before_bms_bd[MAX_BOOKMARKS_PER_FILE];
                    memcpy(before_bms_bd, bookmarks, sizeof(Bookmark) * (size_t)before_cnt_bd);
                    char del_name[BOOKMARK_NAME_MAX];
                    snprintf(del_name, sizeof(del_name), "%s", bookmarks[closest].name);
                    memmove(&bookmarks[closest], &bookmarks[closest + 1],
                            sizeof(Bookmark) * (size_t)(bookmark_count - closest - 1));
                    bookmark_count--;
                    { char _d[128]; snprintf(_d, sizeof(_d), "bookmark remove %s", del_name); hist_push_bookmark(history, &history_count, &history_pos, before_bms_bd, before_cnt_bd, bookmarks, bookmark_count, _d); }
                    #if SAVE_FILE
                    update_bookmarks_in_save_state(&save_state, video_file,
                                                    bookmarks, bookmark_count);
                    write_save_state(SAVE_FILE_PATH, &save_state);
                    #endif
                    snprintf(flash_text, sizeof(flash_text), "Bookmark deleted: %s", del_name);
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_m && (!(e.key.keysym.mod & KMOD_ALT)) && vr) {
                    static int last_volume = -1;
                    if (last_volume < 0) last_volume = (int)volume_percent;
                    float v_before_mute = volume_percent;
                    if (volume_percent > 0.0f) {
                        last_volume = (int)volume_percent;
                        volume_percent = 0.0f;
                        vr_set_volume(vr, 0.0f);
                        snprintf(flash_text, sizeof(flash_text), "Volume Muted");
                    } else {
                        volume_percent = (float)last_volume;
                        vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                        snprintf(flash_text, sizeof(flash_text), "Volume Unmuted: %d", last_volume);
                    }
                    hist_push_volume(history, &history_count, &history_pos, v_before_mute, volume_percent);
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_g && (e.key.keysym.mod & KMOD_CTRL) && vr && !ti.active) {
                    text_input_open(&ti, "Jump to:", format_time_temp(vr_get_time(vr)), 16);
                    ti_purpose = TI_GOTO_TIME;
                }
                if (key == SDLK_r && (e.key.keysym.mod & KMOD_ALT) && !ti.active) {
                    if (e.key.keysym.mod & KMOD_SHIFT) {
                        requested_window_w = 0;
                        requested_window_h = 0;
                        if (vr) {
                            vr->desired_win_w = 0;
                            vr->desired_win_h = 0;
                            apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
                            #if SAVE_FILE
                            if (video_file) { fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent); write_save_state(SAVE_FILE_PATH, &save_state); }
                            #endif
                        }
                        snprintf(flash_text, sizeof(flash_text), "Resolution: Native");
                        flash_until = SDL_GetTicks() + 900;
                    } else {
                        text_input_open(&ti, "Resolution (WxH):", "", 20);
                        ti_purpose = TI_SET_RESOLUTION;
                    }
                }
                if (key == SDLK_a && (e.key.keysym.mod & KMOD_ALT) && vr && !ti.active) {
                    if (e.key.keysym.mod & KMOD_SHIFT) {
                        vr_set_aspect_ratio_mode(vr, 0, 0);
                        #if SAVE_FILE
                        if (video_file) { fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent); write_save_state(SAVE_FILE_PATH, &save_state); }
                        #endif
                        snprintf(flash_text, sizeof(flash_text), "Aspect Ratio: Original");
                        flash_until = SDL_GetTicks() + 900;
                    } else {
                        text_input_open(&ti, "Aspect Ratio (X:Y):", "", 16);
                        ti_purpose = TI_SET_ASPECT_RATIO;
                    }
                }
                if (key == SDLK_s && (e.key.keysym.mod & KMOD_ALT) && (e.key.keysym.mod & KMOD_SHIFT) && vr && !ti.active) {
                    vr_set_aspect_ratio_mode(vr, UINT_MAX, UINT_MAX);
                    #if SAVE_FILE
                    if (video_file) { fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent); write_save_state(SAVE_FILE_PATH, &save_state); }
                    #endif
                    snprintf(flash_text, sizeof(flash_text), "Aspect Ratio: Stretch to Window");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_HOME && vr) {
                    double bt_home = vr_get_time(vr);
                    seek_and_preview_if_paused(vr, 0.0, paused);
                    if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_home, video_file, 0.0);
                    snprintf(flash_text, sizeof(flash_text), "Beginning");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_END && vr) {
                    double dur_end = vr_get_duration(vr);
                    if (dur_end > 0.0) {
                        double bt_end = vr_get_time(vr);
                        seek_and_preview_if_paused(vr, dur_end, paused);
                        if (video_file) hist_push_seek(history, &history_count, &history_pos, video_file, bt_end, video_file, dur_end);
                        snprintf(flash_text, sizeof(flash_text), "End");
                        flash_until = SDL_GetTicks() + 900;
                    }
                }
                if ((key == SDLK_MINUS || key == SDLK_KP_MINUS) && !(e.key.keysym.mod & KMOD_CTRL) && vr) {
                    int offset = (e.key.keysym.mod & KMOD_SHIFT) ? -100 : -1;
                    vr->subtitle_offset_ms += offset;
                    snprintf(flash_text, sizeof(flash_text), "Shifted subtitles %dms (%+"PRId64")", offset, vr->subtitle_offset_ms);
                    flash_until = SDL_GetTicks() + 900;
                }
                if ((key == SDLK_PLUS || key == SDLK_KP_PLUS) && !(e.key.keysym.mod & KMOD_CTRL) && vr) {
                    int offset = (e.key.keysym.mod & KMOD_SHIFT) ? 100 : 1;
                    vr->subtitle_offset_ms += offset;
                    snprintf(flash_text, sizeof(flash_text), "Shifted subtitles %dms (%+"PRId64")", offset, vr->subtitle_offset_ms);
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_0 && (e.key.keysym.mod & KMOD_CTRL) && !(e.key.keysym.mod & KMOD_ALT) && vr) {
                    vr_set_zoom(vr, 100);
                    vr_set_aspect_ratio_mode(vr, 0, 0);
                    requested_window_w = 0;
                    requested_window_h = 0;
                    vr->desired_win_w = 0;
                    vr->desired_win_h = 0;
                    apply_window_size_for_video(win, vr_get_texture(vr), 0, 0);
                    snprintf(flash_text, sizeof(flash_text), "Reset: zoom, aspect ratio, resolution");
                    flash_until = SDL_GetTicks() + 1200;
                }
                if (key == SDLK_z && (e.key.keysym.mod & KMOD_CTRL) && (e.key.keysym.mod & KMOD_ALT) && !(e.key.keysym.mod & KMOD_SHIFT) && vr) {
                    vr_set_zoom(vr, 100);
                    snprintf(flash_text, sizeof(flash_text), "Zoom: 100%%");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_z && !((e.key.keysym.mod & KMOD_CTRL) && (e.key.keysym.mod & KMOD_SHIFT)) && (e.key.keysym.mod & KMOD_ALT) && !ti.active) {
                    char zoom_hint[16] = {0};
                    if (vr) snprintf(zoom_hint, sizeof(zoom_hint), "%d%%", vr->zoom_percent);
                    text_input_open(&ti, "Zoom (e.g. 150%, 2x):", zoom_hint[0] ? zoom_hint : "", 16);
                    ti_purpose = TI_SET_ZOOM;
                }
                if (key == SDLK_s && !((e.key.keysym.mod & KMOD_CTRL) && (e.key.keysym.mod & KMOD_SHIFT)) && (e.key.keysym.mod & KMOD_ALT) && !ti.active) {
                    char speed_hint[16] = {0};
                    if (vr) snprintf(speed_hint, sizeof(speed_hint), "%.6gx", vr->playback_speed);
                    text_input_open(&ti, "Speed (e.g. 2, 3x):", speed_hint[0] ? speed_hint : "", 16);
                    ti_purpose = TI_SET_SPEED;
                }
                if ((key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS) && (e.key.keysym.mod & KMOD_CTRL) && vr) {
                    vr_set_zoom(vr, vr->zoom_percent + 25);
                    snprintf(flash_text, sizeof(flash_text), "Zoom: %d%%", vr->zoom_percent);
                    flash_until = SDL_GetTicks() + 900;
                }
                if ((key == SDLK_MINUS || key == SDLK_KP_MINUS) && (e.key.keysym.mod & KMOD_CTRL) && vr) {
                    vr_set_zoom(vr, vr->zoom_percent - 25);
                    snprintf(flash_text, sizeof(flash_text), "Zoom: %d%%", vr->zoom_percent);
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_p && !(e.key.keysym.mod & KMOD_CTRL) && vr) {
                    draw_mode_active = !draw_mode_active;
                    #ifdef _WIN32
                    menu_set_draw_mode(draw_mode_active);
                    #endif
                    if (draw_mode_active) {
                        draw_mode_was_paused = paused;
                        paused = true;
                        if (vr) vr_set_paused(vr, paused);
                        
                        int win_w, win_h;
                        SDL_GetWindowSize(win, &win_w, &win_h);
                        if (draw_canvas) SDL_DestroyTexture(draw_canvas);
                        draw_canvas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, win_w, win_h);
                        SDL_SetTextureBlendMode(draw_canvas, SDL_BLENDMODE_BLEND);
                        draw_notify_canvas_resized(&draw_state, win_w, win_h);
                        snprintf(flash_text, sizeof(flash_text), "Draw Mode: ON (Paused)");
                    } else {
                        paused = draw_mode_was_paused;
                        if (vr) vr_set_paused(vr, paused);
                        snprintf(flash_text, sizeof(flash_text), "Draw Mode: OFF");
                    }
                    flash_until = SDL_GetTicks() + 900;
                }
                
                if (draw_mode_active) {
                    if ((key == SDLK_e || key == SDLK_F3) && vr && draw_canvas) {
                        int win_w, win_h;
                        SDL_GetWindowSize(win, &win_w, &win_h);
                        SDL_Texture* frame_tex = vr_get_texture(vr);
                        SDL_Rect video_dst = {0, 0, win_w, win_h};
                        if (frame_tex) {
                            int src_w = 0, src_h = 0;
                            SDL_QueryTexture(frame_tex, NULL, NULL, &src_w, &src_h);
                            video_dst = compute_video_dst_rect(win_w, win_h, src_w, src_h, vr->ar_x, vr->ar_y);
                            video_dst = apply_zoom_to_rect(video_dst, win_w, win_h, vr->zoom_percent);
                        }
                        int include_video = !(e.key.keysym.mod & KMOD_SHIFT);
                        draw_export(ren, draw_canvas, frame_tex, video_dst, win_w, win_h, include_video, &draw_state);
                        snprintf(flash_text, sizeof(flash_text), "Drawing exported");
                        flash_until = SDL_GetTicks() + 900;
                    }
                    
                    if (key == SDLK_n && (e.key.keysym.mod & KMOD_CTRL)) {
                        draw_clear(&draw_state);
                        snprintf(flash_text, sizeof(flash_text), "Canvas cleared");
                        flash_until = SDL_GetTicks() + 900;
                    }
                    
                    if (key == SDLK_z && (e.key.keysym.mod & KMOD_CTRL) && !(e.key.keysym.mod & KMOD_SHIFT)) {
                        if (draw_undo(&draw_state)) {
                            snprintf(flash_text, sizeof(flash_text), "Undo");
                        } else {
                            snprintf(flash_text, sizeof(flash_text), "Nothing to undo");
                        }
                        flash_until = SDL_GetTicks() + 900;
                    }
                    
                    if (key == SDLK_y && (e.key.keysym.mod & KMOD_CTRL)) {
                        if (draw_redo(&draw_state)) {
                            snprintf(flash_text, sizeof(flash_text), "Redo");
                        } else {
                            snprintf(flash_text, sizeof(flash_text), "Nothing to redo");
                        }
                        flash_until = SDL_GetTicks() + 900;
                    }
                    
                    if (key == SDLK_1) { draw_state.current_tool = TOOL_PEN; snprintf(flash_text, sizeof(flash_text), "Tool: Pen"); flash_until = SDL_GetTicks() + 900; }
                    if (key == SDLK_2) { draw_state.current_tool = TOOL_ERASER; snprintf(flash_text, sizeof(flash_text), "Tool: Eraser"); flash_until = SDL_GetTicks() + 900; }
                    if (key == SDLK_3) { draw_state.current_tool = TOOL_MARKER; snprintf(flash_text, sizeof(flash_text), "Tool: Marker (Contrast)"); flash_until = SDL_GetTicks() + 900; }
                    if (key == SDLK_4) { draw_state.current_tool = TOOL_LINE; snprintf(flash_text, sizeof(flash_text), "Tool: Line"); flash_until = SDL_GetTicks() + 900; }
                    if (key == SDLK_5) { draw_state.current_tool = TOOL_RECT; snprintf(flash_text, sizeof(flash_text), "Tool: Rectangle"); flash_until = SDL_GetTicks() + 900; }
                    if (key == SDLK_6) { draw_state.current_tool = TOOL_CIRCLE; snprintf(flash_text, sizeof(flash_text), "Tool: Circle"); flash_until = SDL_GetTicks() + 900; }
                    if (key == SDLK_7) { draw_state.current_tool = TOOL_FILLED_RECT; snprintf(flash_text, sizeof(flash_text), "Tool: Filled Rect"); flash_until = SDL_GetTicks() + 900; }
                    if (key == SDLK_8) { draw_state.current_tool = TOOL_FILLED_CIRCLE; snprintf(flash_text, sizeof(flash_text), "Tool: Filled Circle"); flash_until = SDL_GetTicks() + 900; }
                }
            }

            if (e.type == SDL_MOUSEWHEEL) {
                if (draw_mode_active && !menu_open) {
                    int dy = e.wheel.y;
                    if (SDL_GetModState() & KMOD_ALT) {
                        int mx_z, my_z;
                        SDL_GetMouseState(&mx_z, &my_z);
                        float new_target = draw_zoom_target * (dy > 0 ? 1.15f : (1.0f / 1.15f));
                        if (new_target < 0.25f) new_target = 0.25f;
                        if (new_target > 8.0f)  new_target = 8.0f;
                        float factor = new_target / draw_zoom_target;
                        draw_zoom_ox_target = mx_z - (mx_z - draw_zoom_ox_target) * factor;
                        draw_zoom_oy_target = my_z - (my_z - draw_zoom_oy_target) * factor;
                        draw_zoom_target = new_target;
                    } else {
                        draw_state.brush_size += dy;
                        if (draw_state.brush_size < DRAW_BRUSH_SIZE_MIN) draw_state.brush_size = DRAW_BRUSH_SIZE_MIN;
                        if (draw_state.brush_size > DRAW_BRUSH_SIZE_MAX) draw_state.brush_size = DRAW_BRUSH_SIZE_MAX;
                        snprintf(flash_text, sizeof(flash_text), "Brush Size: %d", draw_state.brush_size);
                        flash_until = SDL_GetTicks() + 900;
                    }
                }
            }
            
            if (e.type == SDL_MOUSEWHEEL && menu_open) {
                int dy = e.wheel.y;
                if (audio_menu_open) {
                    int count = vr ? vr_get_audio_track_count(vr) : 0;
                    int total = count + 3;
                    audio_scroll -= dy * 3;
                    int max_scroll = total > 10 ? total - 10 : 0;
                    audio_scroll = audio_scroll < 0 ? 0 : (audio_scroll > max_scroll ? max_scroll : audio_scroll);
                }
                if (subtitle_menu_open) {
                    int count = vr ? vr_get_subtitle_track_count(vr) : 0;
                    subtitle_scroll -= dy * 3;
                    int max_scroll = (count + 1) > 10 ? (count + 1) - 10 : 0;
                    subtitle_scroll = subtitle_scroll < 0 ? 0 : (subtitle_scroll > max_scroll ? max_scroll : subtitle_scroll);
                }
            }

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                int mx = e.button.x;
                int my = e.button.y;
                
                if (draw_mode_active && draw_canvas) {
                    int dmx = mx, dmy = my;
                    if (fabsf(draw_zoom - 1.0f) > 0.001f) {
                        dmx = (int)((mx - draw_zoom_ox) / draw_zoom);
                        dmy = (int)((my - draw_zoom_oy) / draw_zoom);
                    }
                    DrawTool saved_tool = draw_state.current_tool;
                    draw_state.current_tool = TOOL_ERASER;
                    draw_begin_stroke(&draw_state, dmx, dmy, ren, draw_canvas);
                    draw_state.current_tool = saved_tool;
                } else if (point_in_rect(mx, my, timeline_hitbox)) {
                    double dur = vr ? vr_get_duration(vr) : 0.0;
                    if (dur > 0.0 && vr) {
                        int bm_idx = -1;
                        double bm_time = 0.0;
                        if (find_nearest_bookmark(bookmarks, bookmark_count, dur, timeline_rect, mx, THEME_TIMELINE_BOOKMARK_MARKER_HITBOX_WIDTH, &bm_idx, &bm_time)) {
                            bm_ctx_open = 1;
                            bm_ctx_x = mx;
                            bm_ctx_y = my;
                            bm_ctx_idx = bm_idx;
                        }
                    }
                }
            }

            if (kbd_overlay_open && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                int col_w = 360;
                int cols = w > col_w * 2 + 80 ? 2 : 1;
                int rows = (kbd_entry_count + cols - 1) / cols;
                int panel_w = col_w * cols + 24;
                int panel_h = rows * item_h + 40;
                if (panel_h > h - 40) panel_h = h - 40;
                int panel_x = (w - panel_w) / 2;
                int panel_y = (h - panel_h) / 2;
                SDL_Rect panel = { panel_x, panel_y, panel_w, panel_h };
                int mx_k = e.button.x, my_k = e.button.y;
                if (!point_in_rect(mx_k, my_k, panel)) {
                    kbd_overlay_open = false;
                } else {
                    int inner_y = panel_y + 30;
                    int max_visible = (panel_h - 30) / item_h;
                    int btn_y_pad = 4;
                    int key_col_w = 130;
                    int bx = panel_x + 12;
                    for (int i = 0; i < max_visible && (kbd_scroll + i) < kbd_entry_count; i++) {
                        int entry_idx = kbd_scroll + i;
                        int by = inner_y + i * item_h;
                        SDL_Rect btn = { bx, by + btn_y_pad, key_col_w, item_h - btn_y_pad * 2 };
                        if (point_in_rect(mx_k, my_k, btn)) {
                            KbdEntry k = kbd_entries[entry_idx];
                            SDL_Event ev;
                            switch (k.tag) {
                                case KBD_ENTRY_KEYSYM: {
                                    ev.type = SDL_KEYDOWN;
                                    ev.key.type = SDL_KEYDOWN;
                                    ev.key.state = SDL_PRESSED;
                                    ev.key.keysym.sym = k.key.keysym.key;
                                    ev.key.keysym.mod = k.key.keysym.mod;
                                } break;
                                case KBD_ENTRY_TEXT_INPUT: {
                                    ev.type = SDL_TEXTINPUT;
                                    snprintf(ev.text.text, sizeof(ev.text.text), "%s", k.key.textinput);
                                } break;
                            }
                            SDL_PushEvent(&ev);
                            snprintf(flash_text, sizeof(flash_text), "%s", kbd_entries[entry_idx].desc);
                            flash_until = SDL_GetTicks() + 700;
                            break;
                        }
                    }
                }
                continue;
            }

            if (e.type == SDL_MOUSEWHEEL && kbd_overlay_open) {
                kbd_scroll -= e.wheel.y;
                if (kbd_scroll < 0) kbd_scroll = 0;
            }

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x;
                int my = e.button.y;
                bool click_processed = false;

                if (draw_mode_active && draw_canvas) {
                    int dmx = mx, dmy = my;
                    if (fabsf(draw_zoom - 1.0f) > 0.001f) {
                        dmx = (int)((mx - draw_zoom_ox) / draw_zoom);
                        dmy = (int)((my - draw_zoom_oy) / draw_zoom);
                    }
                    if (!draw_palette_click(&draw_state, dmx, dmy, w, h)) {
                        draw_begin_stroke(&draw_state, dmx, dmy, ren, draw_canvas);
                    }
                    click_processed = true;
                }

                if (!click_processed && point_in_rect(mx, my, hamburger)) {
                    menu_open = !menu_open;
                    audio_menu_open = false;
                    subtitle_menu_open = false;
                    theme_menu_open = false;
                    playback_menu_open = false;
                    subtitle_settings_menu_open = false;
                    subtitle_settings_value_menu_open = false;
                    subtitle_settings_value_menu_row = -1;
                    click_processed = true;
                }
                else if (menu_open && point_in_rect(mx, my, menu_panel)) {
                    bool handled = false;
                    
                    if (point_in_rect(mx, my, audio_box)) {
                        audio_menu_open = !audio_menu_open;
                        subtitle_menu_open = false;
                        theme_menu_open = false;
                        playback_menu_open = false;
                        subtitle_settings_menu_open = false;
                        subtitle_settings_value_menu_open = false;
                        subtitle_settings_value_menu_row = -1;
                        handled = true;
                    } else if (point_in_rect(mx, my, subtitle_box)) {
                        subtitle_menu_open = !subtitle_menu_open;
                        audio_menu_open = false;
                        theme_menu_open = false;
                        playback_menu_open = false;
                        subtitle_settings_menu_open = false;
                        subtitle_settings_value_menu_open = false;
                        subtitle_settings_value_menu_row = -1;
                        handled = true;
                    } else if (point_in_rect(mx, my, theme_box)) {
                        theme_menu_open = !theme_menu_open;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        playback_menu_open = false;
                        subtitle_settings_menu_open = false;
                        subtitle_settings_value_menu_open = false;
                        subtitle_settings_value_menu_row = -1;
                        handled = true;
                    } else if (point_in_rect(mx, my, playback_box)) {
                        playback_menu_open = !playback_menu_open;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        theme_menu_open = false;
                        subtitle_settings_menu_open = false;
                        subtitle_settings_value_menu_open = false;
                        subtitle_settings_value_menu_row = -1;
                        handled = true;
                    } else if (point_in_rect(mx, my, subtitle_settings_box)) {
                        if (subtitle_settings_applicable) {
                            subtitle_settings_menu_open = !subtitle_settings_menu_open;
                            subtitle_settings_value_menu_open = false;
                            subtitle_settings_value_menu_row = -1;
                            audio_menu_open = false;
                            subtitle_menu_open = false;
                            theme_menu_open = false;
                            playback_menu_open = false;
                            handled = true;
                        }
                    }
                    
                    if (!handled) {
                        menu_open = false;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        theme_menu_open = false;
                        playback_menu_open = false;
                        subtitle_settings_menu_open = false;
                        subtitle_settings_value_menu_open = false;
                        subtitle_settings_value_menu_row = -1;
                    }
                    click_processed = true;
                }
                else if (menu_open) {
                    bool handled = false;
                    
                    if (audio_menu_open && !handled) {
                        int count = vr ? vr_get_audio_track_count(vr) : 0;
                        int total = count + 3;
                        int max_items = THEME_MENU_MAX_VISIBLE_ITEMS;
                        int display_count = total > max_items ? max_items : total;
                        int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                        int list_h_click = 0;
                        for (int _i = 0; _i < display_count; _i++) {
                            int _row = audio_scroll + _i;
                            list_h_click += (_row == count) ? item_h / 2 : item_h;
                        }
                        SDL_Rect list = { menu_panel.x - THEME_MENU_DROPDOWN_WIDTH, audio_box.y, THEME_MENU_DROPDOWN_WIDTH - (total > max_items ? THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), list_h_click };
                        if (list.x < margin) list.x = margin;
                        if (point_in_rect(mx, my, list)) {
                            int y_cur = list.y;
                            int idx = -1;
                            for (int _i = 0; _i < display_count; _i++) {
                                int _row = audio_scroll + _i;
                                int _rh = (_row == count) ? item_h / 2 : item_h;
                                if (my >= y_cur && my < y_cur + _rh) { idx = _row; break; }
                                y_cur += _rh;
                            }
                            if (idx >= 0) {
                                if (vr && idx < count) {
                                    vr_select_audio_track(vr, idx);
                                    audio_menu_open = false;
                                } else if (idx == count + 1) {
                                    audio_os_passthrough = !audio_os_passthrough;
                                    if (vr) {
                                        vr_set_os_passthrough(vr, audio_os_passthrough ? 1 : 0);
                                    }
                                    if (audio_os_passthrough && vr && !vr_is_os_passthrough_active(vr)) {
                                        const char* _cn = vr->audio_ctx ? avcodec_get_name(vr->audio_ctx->codec_id) : "<unavailable>";
                                        int _cs = vr->audio_ctx ? vr_audio_codec_supports_passthrough(vr->audio_ctx->codec_id) : 0;
                                        if (!_cs) {
                                            snprintf(flash_text, sizeof(flash_text), "Passthrough: codec '%s' not supported", _cn);
                                            nob_log(NOB_WARNING, "Passthrough unavailable: track %d codec '%s' not supported (need ac3/eac3/dts/truehd)",
                                                vr->current_audio, _cn);
                                        } else if (vr->passthrough_no_digital_device) {
                                            snprintf(flash_text, sizeof(flash_text), "Passthrough: not a digital audio output");
                                            nob_log(NOB_WARNING, "Passthrough unavailable: output is not a digital audio device (need HDMI/S-PDIF)");
                                        } else {
                                            snprintf(flash_text, sizeof(flash_text), "Passthrough unavailable: BSF/init failed");
                                            nob_log(NOB_WARNING, "Passthrough unavailable: track %d codec '%s' is supported but SPDIF BSF or audio device init failed",
                                                vr->current_audio, _cn);
                                        }
                                    } else {
                                        snprintf(flash_text, sizeof(flash_text), "Audio OS Passthrough: %s", audio_os_passthrough ? "ON" : "OFF");
                                    }
                                    flash_until = SDL_GetTicks() + 900;
#if SAVE_FILE
                                    save_state.global.audio_os_passthrough = audio_os_passthrough ? 1 : 0;
                                    write_save_state(SAVE_FILE_PATH, &save_state);
#endif
                                } else if (idx == count + 2) {
                                    night_mode = !night_mode;
                                    if (vr) vr_set_night_mode(vr, night_mode ? 1 : 0);
                                    snprintf(flash_text, sizeof(flash_text), "Night Mode: %s", night_mode ? "ON" : "OFF");
                                    flash_until = SDL_GetTicks() + 900;
#if SAVE_FILE
                                    save_state.global.night_mode = night_mode ? 1 : 0;
                                    write_save_state(SAVE_FILE_PATH, &save_state);
#endif
                                }
                            }
                            handled = true;
                        }
                    }
                    
                    if (subtitle_menu_open && !handled) {
                        int count = vr ? vr_get_subtitle_track_count(vr) : 0;
                        int max_items = THEME_MENU_MAX_VISIBLE_ITEMS;
                        int total = count + 1;
                        int display_count = total > max_items ? max_items : total;
                        int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                        SDL_Rect list = {
                            menu_panel.x - THEME_MENU_DROPDOWN_WIDTH,
                            subtitle_box.y,
                            THEME_MENU_DROPDOWN_WIDTH - (total > max_items ? THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH : 0),
                            item_h * display_count
                        };
                        if (list.x < margin) list.x = margin;
                        int win_w, win_h;
                        SDL_GetWindowSize(win, &win_w, &win_h);
                        if (list.y + list.h > win_h) {
                            list.y = win_h - list.h;
                            if (list.y < margin) list.y = margin;
                        }
                        if (point_in_rect(mx, my, list)) {
                            int item_y = (my - list.y) / item_h;
                            if (item_y >= 0 && item_y < display_count) {
                                int idx = subtitle_scroll + item_y - 1;
                                if (vr) vr_select_subtitle_track(vr, idx);
                            }
                            subtitle_menu_open = false;
                            handled = true;
                        }
                    }
                    
                    if (theme_menu_open && !handled) {
                        char theme_names[32][64];
                        int theme_count = get_theme_list(theme_names, 32);
                        int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                        SDL_Rect list = {
                            menu_panel.x - THEME_MENU_DROPDOWN_WIDTH,
                            theme_box.y,
                            THEME_MENU_DROPDOWN_WIDTH,
                            (theme_count + 3) * item_h + item_h / 2
                        };
                        if (list.x < margin) list.x = margin;
                        if (point_in_rect(mx, my, list)) {
                            int rel_y = my - list.y;
                            int idx;
                            if (rel_y < (theme_count + 1) * item_h)
                                idx = rel_y / item_h;
                            else if (rel_y < (theme_count + 1) * item_h + item_h / 2)
                                idx = -1;
                            else if (rel_y < (theme_count + 2) * item_h + item_h / 2)
                                idx = theme_count + 2;
                            else
                                idx = theme_count + 3;
                            if (idx >= 0 && idx < theme_count) {
                                load_theme(theme_names[idx]);
                                draw_init(&draw_state);
                                if (THEME_FONT.path[0]) {
                                    if (!load_ui_font(THEME_FONT.path, THEME_FONT.name[0] ? THEME_FONT.name : THEME_NAME)) {
                                        nob_log(NOB_WARNING, "Theme font not available: %s", THEME_FONT.path);
                                        const char *prefix = "Theme font unavailable: ";
                                        size_t prefix_len = strlen(prefix);

                                        size_t max_path = sizeof(flash_text) - prefix_len - 1;

                                        snprintf(
                                            flash_text,
                                            sizeof(flash_text),
                                            "%s%.*s",
                                            prefix,
                                            (int)max_path,
                                            THEME_FONT.path
                                        );
                                        flash_until = SDL_GetTicks() + 2500;
                                    }
                                } else {
                                    if (!load_ui_font(THEME_SYSTEM_DEFAULT_FONT.path, THEME_SYSTEM_DEFAULT_FONT.name[0] ? THEME_SYSTEM_DEFAULT_FONT.name : THEME_NAME))
                                        nob_log(NOB_WARNING, "System default font not available: %s", THEME_SYSTEM_DEFAULT_FONT.path);
                                }
                                strncpy(save_state.global.theme, theme_names[idx], sizeof(save_state.global.theme) - 1);
                                save_state.global.theme[sizeof(save_state.global.theme) - 1] = '\0';
                                write_save_state(SAVE_FILE_PATH, &save_state);
                            } else if (idx == theme_count) {
                                const char* filters[] = { "*.h" };
                                char* chosen = open_file_dialog(filters, 1, "Theme files (*.h)", false, "Select Theme File", NULL, NULL);
                                if (chosen) {
                                    if (!load_theme_probe(chosen)) {
                                        nob_log(NOB_ERROR, "Invalid theme file: %s", chosen);
                                        snprintf(flash_text, sizeof(flash_text), "Invalid theme file");
                                        flash_until = SDL_GetTicks() + 2500;
                                    } else {
                                        char exe_dir[512];
                                        get_exe_dir(exe_dir, sizeof(exe_dir));
                                        char dest_dir[1024];
                                        snprintf(dest_dir, sizeof(dest_dir), "%s" D_ASSETS "themes", exe_dir);
                                        const char* src_base = chosen;
                                        for (const char* p = chosen; *p; p++)
                                            if (*p == '/' || *p == '\\') src_base = p + 1;
                                        char dest_path[1536];
                                        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, src_base);
                                        bool copy_ok = false;
                                        FILE* in = fopen(chosen, "rb");
                                        FILE* out_f = in ? fopen(dest_path, "wb") : NULL;
                                        if (in && out_f) {
                                            char buf[4096];
                                            size_t n;
                                            while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
                                                fwrite(buf, 1, n, out_f);
                                            copy_ok = !ferror(in) && !ferror(out_f);
                                        }
                                        if (in)  fclose(in);
                                        if (out_f) fclose(out_f);
                                        if (!copy_ok) {
                                            nob_log(NOB_ERROR, "Failed to copy theme to %s", dest_path);
                                            snprintf(flash_text, sizeof(flash_text), "Failed to copy theme file");
                                            flash_until = SDL_GetTicks() + 2500;
                                        } else {
                                            load_theme_file(dest_path);
                                            draw_init(&draw_state);
                                            if (THEME_FONT.path[0]) {
                                                if (!load_ui_font(THEME_FONT.path, THEME_FONT.name[0] ? THEME_FONT.name : THEME_NAME)) {
                                                    nob_log(NOB_WARNING, "Theme font not available: %s", THEME_FONT.path);
                                                    snprintf(flash_text, sizeof(flash_text), "Theme font unavailable");
                                                    flash_until = SDL_GetTicks() + 2500;
                                                }
                                            } else {
                                                if (!load_ui_font(THEME_SYSTEM_DEFAULT_FONT.path, THEME_SYSTEM_DEFAULT_FONT.name[0] ? THEME_SYSTEM_DEFAULT_FONT.name : THEME_NAME))
                                                    nob_log(NOB_WARNING, "System default font not available: %s", THEME_SYSTEM_DEFAULT_FONT.path);
                                            }
                                            char theme_id[64];
                                            strncpy(theme_id, src_base, sizeof(theme_id) - 1);
                                            theme_id[sizeof(theme_id) - 1] = '\0';
                                            size_t tid_len = strlen(theme_id);
                                            if (tid_len > 2 && strcmp(theme_id + tid_len - 2, ".h") == 0)
                                                theme_id[tid_len - 2] = '\0';
                                            strncpy(save_state.global.theme, theme_id, sizeof(save_state.global.theme) - 1);
                                            save_state.global.theme[sizeof(save_state.global.theme) - 1] = '\0';
                                            write_save_state(SAVE_FILE_PATH, &save_state);
                                            nob_log(NOB_INFO, "Custom theme installed: %s", dest_path);
                                        }
                                    }
                                }
                            } else if (idx == theme_count + 2) {
                                blackout_mode = !blackout_mode;
                                snprintf(flash_text, sizeof(flash_text), "Blackout Mode: %s", blackout_mode ? "ON" : "OFF");
                                flash_until = SDL_GetTicks() + 900;
#if SAVE_FILE
                                save_state.global.blackout_mode = blackout_mode ? 1 : 0;
                                write_save_state(SAVE_FILE_PATH, &save_state);
#endif
                            } else if (idx == theme_count + 3) {
                                ambient_glow = !ambient_glow;
                                if (vr) vr_set_ambient_glow(vr, ambient_glow ? 1 : 0);
                                snprintf(flash_text, sizeof(flash_text), "Ambient Glow: %s", ambient_glow ? "ON" : "OFF");
                                flash_until = SDL_GetTicks() + 900;
#if SAVE_FILE
                                save_state.global.ambient_glow = ambient_glow ? 1 : 0;
                                write_save_state(SAVE_FILE_PATH, &save_state);
#endif
                            }
                            if (idx >= 0 && idx <= theme_count) theme_menu_open = false;
                            handled = true;
                        }
                    }

                    if (playback_menu_open && !handled) {
                        int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                        SDL_Rect list = { menu_panel.x - THEME_MENU_DROPDOWN_WIDTH, playback_box.y, THEME_MENU_DROPDOWN_WIDTH, item_h * 8 };
                        if (list.x < margin) list.x = margin;
                        int win_w, win_h;
                        SDL_GetWindowSize(win, &win_w, &win_h);
                        if (list.y + list.h > win_h) {
                            list.y = win_h - list.h;
                            if (list.y < 0) list.y = 0;
                        }
                        if (point_in_rect(mx, my, list)) {
                            int idx = (my - list.y) / item_h;
                            float speeds[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 5.0f };
                            if (idx >= 0 && idx < 8) {
                                playback_speed = speeds[idx];
                                if (vr) {
                                    vr_set_speed(vr, playback_speed);
                                    SDL_ClearQueuedAudio(vr->audio_dev);
                                    if (playback_speed > 2.0f) {
                                        vr->audio_clock_valid = 0;
                                    }
                                }
                            }
                            playback_menu_open = false;
                            handled = true;
                        }
                    }

                    if (subtitle_settings_menu_open && !handled) {
                        int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                        SDL_Rect list = { menu_panel.x - THEME_MENU_DROPDOWN_WIDTH, subtitle_settings_box.y, THEME_MENU_DROPDOWN_WIDTH, item_h * subtitle_settings_row_count };
                        if (list.x < margin) list.x = margin;
                        int win_w, win_h;
                        SDL_GetWindowSize(win, &win_w, &win_h);
                        if (list.y + list.h > win_h) {
                            list.y = win_h - list.h;
                            if (list.y < margin) list.y = margin;
                        }

                        int value_count = 0;
                        if (subtitle_settings_value_menu_row == 0) value_count = subtitle_color_count;
                        if (subtitle_settings_value_menu_row == 1) value_count = subtitle_size_count;
                        if (subtitle_settings_value_menu_row == 2) value_count = subtitle_move_count;

                        SDL_Rect value_list = { list.x - THEME_MENU_DROPDOWN_WIDTH - 8, list.y, THEME_MENU_DROPDOWN_WIDTH, item_h * value_count };
                        if (value_list.x < margin) value_list.x = margin;
                        if (value_list.y + value_list.h > win_h) {
                            value_list.y = win_h - value_list.h;
                            if (value_list.y < margin) value_list.y = margin;
                        }

                        if (subtitle_settings_value_menu_open && value_count > 0 && point_in_rect(mx, my, value_list)) {
                            int selected = (my - value_list.y) / item_h;
                            if (selected >= 0 && selected < value_count) {
                                if (subtitle_settings_value_menu_row == 0) subtitle_color_idx = selected;
                                if (subtitle_settings_value_menu_row == 1) subtitle_size_idx = selected;
                                if (subtitle_settings_value_menu_row == 2) subtitle_move_idx = selected;
                                apply_subtitle_override(
                                    vr,
                                    subtitle_override_colors[subtitle_color_idx],
                                    subtitle_override_sizes[subtitle_size_idx],
                                    subtitle_override_margins[subtitle_move_idx]
                                );
                                if (vr && vr->current_subtitle >= 0) {
                                    vr_select_subtitle_track(vr, vr->current_subtitle);
                                }
                            }
                            subtitle_settings_value_menu_open = false;
                            subtitle_settings_value_menu_row = -1;
                            handled = true;
                        } else if (point_in_rect(mx, my, list)) {
                            int row = (my - list.y) / item_h;
                            if (row >= 0 && row < subtitle_settings_row_count) {
                                subtitle_settings_value_menu_row = row;
                                subtitle_settings_value_menu_open = true;
                            }
                            handled = true;
                        } else if (subtitle_settings_value_menu_open) {
                            subtitle_settings_value_menu_open = false;
                            subtitle_settings_value_menu_row = -1;
                        }
                    }
                    
                    if (!handled && !point_in_rect(mx, my, menu_panel)) {
                        menu_open = false;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        theme_menu_open = false;
                        playback_menu_open = false;
                        subtitle_settings_menu_open = false;
                    }
                    click_processed = true;
                }
                else if (bm_ctx_open) {
                    int ctx_item = -1;
                    int ctx_w = 150, ctx_item_h = 26;
                    int total_h = ctx_item_h * 3 + 8;
                    int cx = bm_ctx_x, cy = bm_ctx_y;
                    if (cx + ctx_w > w) cx = w - ctx_w;
                    if (cy + total_h > h) cy = h - total_h;
                    if (point_in_rect(mx, my, (SDL_Rect){ cx, cy, ctx_w, total_h })) {
                        if (my < cy + ctx_item_h) ctx_item = 0;
                        else if (my < cy + ctx_item_h * 2) ctx_item = 1;
                        else if (my >= cy + ctx_item_h * 2 + 8) ctx_item = 2;
                        
                        if (ctx_item == 0) {
                            if (bm_ctx_idx >= 0 && bm_ctx_idx < bookmark_count) {
                                snprintf(ti_bm_old_name, sizeof(ti_bm_old_name), "%s", bookmarks[bm_ctx_idx].name);
                                text_input_open(&ti, "Rename bookmark:",
                                                bookmarks[bm_ctx_idx].name, BOOKMARK_NAME_MAX);
                                ti_purpose = TI_BOOKMARK_RENAME;
                                ti_bm_idx  = bm_ctx_idx;
                            }
                            bm_ctx_open = 0;
                        } else if (ctx_item == 1) {
                            if (bm_ctx_idx >= 0 && bm_ctx_idx < bookmark_count) {
                                uint32_t col = bookmarks[bm_ctx_idx].color_rgb;
                                unsigned char rgb[3] = {
                                    (unsigned char)((col >> 16) & 0xFF),
                                    (unsigned char)((col >> 8) & 0xFF),
                                    (unsigned char)(col & 0xFF)
                                };
                                unsigned char out_rgb[3];
                                int before_cnt_col = bookmark_count;
                                Bookmark before_bms_col[MAX_BOOKMARKS_PER_FILE];
                                memcpy(before_bms_col, bookmarks, sizeof(Bookmark) * (size_t)before_cnt_col);
                                const char* res = tinyfd_colorChooser("Choose bookmark color",
                                                                       NULL, rgb, out_rgb);
                                if (res) {
                                    bookmarks[bm_ctx_idx].color_rgb = 
                                        ((uint32_t)out_rgb[0] << 16) |
                                        ((uint32_t)out_rgb[1] << 8) |
                                        (uint32_t)out_rgb[2];
                                    if (bookmarks[bm_ctx_idx].color_rgb != (uint32_t)THEME_DEFAULT_BOOKMARK_COLOR) {
                                        bookmarks[bm_ctx_idx].is_default_color = false;
                                    } else {
                                        bookmarks[bm_ctx_idx].is_default_color = true;
                                    }
                                    { char _d[128]; snprintf(_d, sizeof(_d), "bookmark color %s", bookmarks[bm_ctx_idx].name); hist_push_bookmark(history, &history_count, &history_pos, before_bms_col, before_cnt_col, bookmarks, bookmark_count, _d); }
                                    #if SAVE_FILE
                                    update_bookmarks_in_save_state(&save_state, video_file,
                                                                    bookmarks, bookmark_count);
                                    write_save_state(SAVE_FILE_PATH, &save_state);
                                    #endif
                                }
                            }
                            bm_ctx_open = 0;
                        } else if (ctx_item == 2) {
                            if (bm_ctx_idx >= 0 && bm_ctx_idx < bookmark_count) {
                                int before_cnt_ctx = bookmark_count;
                                Bookmark before_bms_ctx[MAX_BOOKMARKS_PER_FILE];
                                memcpy(before_bms_ctx, bookmarks, sizeof(Bookmark) * (size_t)before_cnt_ctx);
                                char del_name_ctx[BOOKMARK_NAME_MAX];
                                snprintf(del_name_ctx, sizeof(del_name_ctx), "%s", bookmarks[bm_ctx_idx].name);
                                for (int i = bm_ctx_idx; i < bookmark_count - 1; i++) {
                                    bookmarks[i] = bookmarks[i + 1];
                                }
                                bookmark_count--;
                                { char _d[128]; snprintf(_d, sizeof(_d), "bookmark remove %s", del_name_ctx); hist_push_bookmark(history, &history_count, &history_pos, before_bms_ctx, before_cnt_ctx, bookmarks, bookmark_count, _d); }
                                #if SAVE_FILE
                                update_bookmarks_in_save_state(&save_state, video_file,
                                                                bookmarks, bookmark_count);
                                write_save_state(SAVE_FILE_PATH, &save_state);
                                #endif
                                snprintf(flash_text, sizeof(flash_text), "Bookmark deleted: %s", del_name_ctx);
                                flash_until = SDL_GetTicks() + 900;
                            }
                            bm_ctx_open = 0;
                        }
                    } else {
                        bm_ctx_open = 0;
                    }
                    click_processed = true;
                }
                else if (point_in_rect(mx, my, overlay_rect)) {
                    overlay_target = 1.0f;
                    if (point_in_rect(mx, my, timeline_hitbox)) {
                        double dur = vr ? vr_get_duration(vr) : 0.0;
                        if (dur > 0.0 && vr) {
                            int bm_idx = -1;
                            double bm_time = 0.0;
                            if (find_nearest_bookmark(bookmarks, bookmark_count, dur, timeline_rect, mx, THEME_TIMELINE_BOOKMARK_MARKER_HITBOX_WIDTH, &bm_idx, &bm_time)) {
                                if (video_file) {
                                    double bt_bm = vr_get_time(vr);
                                    seek_and_preview_if_paused(vr, bm_time, paused);
                                    hist_push_seek(history, &history_count, &history_pos, video_file, bt_bm, video_file, bm_time);
                                } else {
                                    seek_and_preview_if_paused(vr, bm_time, paused);
                                }
                                snprintf(flash_text, sizeof(flash_text), "%s (%s)", bookmarks[bm_idx].name, format_time_temp(bm_time));
                                flash_until = SDL_GetTicks() + 900;
                                click_processed = true;
                            } else {
                                int chapter_idx = -1;
                                double chapter_time = 0.0;
                                if (find_nearest_chapter(vr, dur, timeline_rect, mx, 6, &chapter_idx, &chapter_time)) {
                                    if (video_file) {
                                        double bt_ch = vr_get_time(vr);
                                        seek_and_preview_if_paused(vr, chapter_time, paused);
                                        hist_push_seek(history, &history_count, &history_pos, video_file, bt_ch, video_file, chapter_time);
                                    } else {
                                        seek_and_preview_if_paused(vr, chapter_time, paused);
                                    }
                                    char chapter_name[160];
                                    char chapter_ts[32];
                                    get_media_chapter_label(vr, chapter_idx, chapter_name, sizeof(chapter_name));
                                    format_time(chapter_time, chapter_ts, sizeof(chapter_ts));
                                    snprintf(flash_text, sizeof(flash_text), "%s (%s)", chapter_name, chapter_ts);
                                    flash_until = SDL_GetTicks() + 900;
                                    click_processed = true;
                                } else {
                                    double t = (double)(mx - timeline_rect.x) / (double)timeline_rect.w;
                                    t = clampf((float)t, 0.0f, 1.0f);
                                    drag_time = t * dur;
                                    dragging_timeline = true;
                                }
                            }
                        }
                        click_processed = true;
                    } else if (point_in_rect(mx, my, volume_rect)) {
                        volume_drag_start = volume_percent;
                        volume_dragging = true;
                        click_processed = true;
                    }
                } else {
                    if (!menu_open && !click_processed) {
                        paused = !paused;
                        if (vr) vr_set_paused(vr, paused);
                    }
                }
            }

            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (draw_mode_active && draw_canvas && draw_state.is_drawing) {
                    int ux = e.button.x, uy = e.button.y;
                    if (fabsf(draw_zoom - 1.0f) > 0.001f) {
                        ux = (int)((ux - draw_zoom_ox) / draw_zoom);
                        uy = (int)((uy - draw_zoom_oy) / draw_zoom);
                    }
                    draw_end_stroke(&draw_state, ux, uy, ren, draw_canvas);
                }
                
                if (dragging_timeline && vr) {
                    double dur = vr_get_duration(vr);
                    double bt_drag = vr_get_time(vr);
                    double seek_time = clampf((float)drag_time, 0.0f, (float)dur);
                    seek_and_preview_if_paused(vr, seek_time, paused);
                    if (video_file) {
                        hist_push_seek(history, &history_count, &history_pos, video_file, bt_drag, video_file, seek_time);
                    }
                }
                if (volume_dragging && vr && volume_percent != volume_drag_start) {
                    hist_push_volume(history, &history_count, &history_pos, volume_drag_start, volume_percent);
                }
                dragging_timeline = false;
                volume_dragging = false;
            }
            
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) {
                if (draw_mode_active && draw_canvas && draw_state.is_drawing) {
                    int ux = e.button.x, uy = e.button.y;
                    if (fabsf(draw_zoom - 1.0f) > 0.001f) {
                        ux = (int)((ux - draw_zoom_ox) / draw_zoom);
                        uy = (int)((uy - draw_zoom_oy) / draw_zoom);
                    }
                    draw_end_stroke(&draw_state, ux, uy, ren, draw_canvas);
                }
            }

            if (e.type == SDL_MOUSEMOTION) {
                if (draw_mode_active && draw_canvas && draw_state.is_drawing) {
                    int cx = e.motion.x, cy = e.motion.y;
                    if (fabsf(draw_zoom - 1.0f) > 0.001f) {
                        cx = (int)((cx - draw_zoom_ox) / draw_zoom);
                        cy = (int)((cy - draw_zoom_oy) / draw_zoom);
                    }
                    draw_continue_stroke(&draw_state, cx, cy, ren, draw_canvas);
                }
                
                if (dragging_timeline && vr) {
                    int mx = e.motion.x;
                    double dur = vr_get_duration(vr);
                    if (dur > 0.0) {
                        double t = (double)(mx - timeline_rect.x) / (double)timeline_rect.w;
                        t = clampf((float)t, 0.0f, 1.0f);
                        drag_time = t * dur;
                    }
                }
                if (volume_dragging && vr) {
                    int my = e.motion.y;
                    float t = (float)(volume_rect.y + volume_rect.h - my) / (float)volume_rect.h;
                    volume_percent = clampf(t * 200.0f, 0.0f, 200.0f);
                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                    snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                    flash_until = SDL_GetTicks() + 900;
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last_tick) / 1000.0f;
        last_tick = now;
        if (!dragging_timeline && !volume_dragging && !menu_open && !audio_menu_open && !subtitle_menu_open && !theme_menu_open && !playback_menu_open && !subtitle_settings_menu_open && !subtitle_settings_value_menu_open) {
            if (now - last_mouse_move > 3000) overlay_target = 0.0f;
        }
        
        overlay_alpha = lerpf(overlay_alpha, overlay_target, clampf(dt * 6.0f, 0.0f, 1.0f));

        if (draw_mode_active && !(SDL_GetModState() & KMOD_ALT)) {
            draw_zoom_target = 1.0f;
            draw_zoom_ox_target = 0.0f;
            draw_zoom_oy_target = 0.0f;
        }
        if (draw_mode_active) {
            float zlp = clampf(dt * 14.0f, 0.0f, 1.0f);
            draw_zoom    = lerpf(draw_zoom,    draw_zoom_target,    zlp);
            draw_zoom_ox = lerpf(draw_zoom_ox, draw_zoom_ox_target, zlp);
            draw_zoom_oy = lerpf(draw_zoom_oy, draw_zoom_oy_target, zlp);
            if (fabsf(draw_zoom - draw_zoom_target) < 0.0005f) {
                draw_zoom    = draw_zoom_target;
                draw_zoom_ox = draw_zoom_ox_target;
                draw_zoom_oy = draw_zoom_oy_target;
            }
        }

        int window_w = 0;
        int window_h = 0;
        SDL_GetWindowSize(win, &window_w, &window_h);

        if (draw_mode_active) {
            int ztw = 0, zth = 0;
            if (draw_zoom_tex) SDL_QueryTexture(draw_zoom_tex, NULL, NULL, &ztw, &zth);
            if (!draw_zoom_tex || ztw != window_w || zth != window_h) {
                if (draw_zoom_tex) SDL_DestroyTexture(draw_zoom_tex);
                draw_zoom_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                                  SDL_TEXTUREACCESS_TARGET, window_w, window_h);
            }
        }

        bool using_zoom_tex = draw_mode_active && draw_zoom_tex && fabsf(draw_zoom - 1.0f) > 0.001f;
        if (using_zoom_tex)
            SDL_SetRenderTarget(ren, draw_zoom_tex);

        SDL_SetRenderDrawColor(ren, THEME_LETTERBOX_COLOR[0], THEME_LETTERBOX_COLOR[1], THEME_LETTERBOX_COLOR[2], 255);
        SDL_RenderClear(ren);

        SDL_Rect video_dst = {0, 0, window_w, window_h};
        double   video_time = 0.0;
        if (vr) {
            if (!paused) {
                run_playback_tick(vr, playback_speed);
            }
            SDL_Texture* tex = vr_get_texture(vr);
            if (tex) {
                int src_w = 0;
                int src_h = 0;
                SDL_QueryTexture(tex, NULL, NULL, &src_w, &src_h);
                video_dst = compute_video_dst_rect(window_w, window_h, src_w, src_h, vr->ar_x, vr->ar_y);
                video_dst = apply_zoom_to_rect(video_dst, window_w, window_h, vr->zoom_percent);
                if (ambient_glow && vr_get_ambient_glow(vr)) {
                    SDL_Color ag_top[AMBIENT_ZONES], ag_bottom[AMBIENT_ZONES], ag_left[AMBIENT_ZONES], ag_right[AMBIENT_ZONES];
                    vr_get_ambient_colors(vr, ag_top, ag_bottom, ag_left, ag_right);
                    draw_ambient_glow(ren, video_dst, window_w, window_h, ag_top, ag_bottom, ag_left, ag_right);
                }
                SDL_RenderCopy(ren, tex, NULL, &video_dst);
            }
            video_time = vr_get_video_time(vr);
            if (vr_render_subtitles(vr, video_time)) {
                SDL_Texture* sub = vr_get_subtitle_texture(vr);
                if (sub) SDL_RenderCopy(ren, sub, NULL, &video_dst);
            }
        }

        if (draw_mode_active && draw_canvas) {
            int mouse_x, mouse_y;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            int lmx = mouse_x, lmy = mouse_y;
            if (fabsf(draw_zoom - 1.0f) > 0.001f) {
                lmx = (int)((mouse_x - draw_zoom_ox) / draw_zoom);
                lmy = (int)((mouse_y - draw_zoom_oy) / draw_zoom);
            }
            draw_render_all(ren, draw_canvas, &draw_state, (SDL_Rect){0, 0, window_w, window_h}, video_dst, video_time);
            draw_render_preview(ren, &draw_state, lmx, lmy);
            draw_render_palette(ren, &draw_state, window_w, window_h);
        }

        if (overlay_alpha > 0.01f) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            int w, h;
            SDL_GetWindowSize(win, &w, &h);

            SDL_Color panel =  { THEME_PANEL_COLOR[0],  THEME_PANEL_COLOR[1],  THEME_PANEL_COLOR[2],  (Uint8)(200 * overlay_alpha) };
            SDL_Color text =   { THEME_TEXT_COLOR[0],   THEME_TEXT_COLOR[1],   THEME_TEXT_COLOR[2],   (Uint8)(255 * overlay_alpha) };
            SDL_Color accent = { THEME_ACCENT_COLOR[0], THEME_ACCENT_COLOR[1], THEME_ACCENT_COLOR[2], (Uint8)(230 * overlay_alpha) };
            SDL_Color muted =  { THEME_MUTED_COLOR[0],  THEME_MUTED_COLOR[1],  THEME_MUTED_COLOR[2],  (Uint8)(200 * overlay_alpha) };

            draw_rect(ren, overlay_rect, panel);

            double cur = vr ? (dragging_timeline ? drag_time : vr_get_time(vr)) : 0.0;
            double dur = vr ? vr_get_duration(vr) : 0.0;
            float t = (dur > 0.0) ? (float)(cur / dur) : 0.0f;
            t = clampf(t, 0.0f, 1.0f);

            SDL_Rect base = timeline_rect;
            base.h = 6;
            draw_rect(ren, base, (SDL_Color){ THEME_OVERLAY_COLOR[0], THEME_OVERLAY_COLOR[1], THEME_OVERLAY_COLOR[2], (Uint8)(180 * overlay_alpha) });
            if (vr && dur > 0.0) {
                int chapter_count = get_media_chapter_count(vr);
                for (int i = 0; i < chapter_count; i++) {
                    double chapter_time = get_media_chapter_time(vr, i);
                    if (chapter_time < 0.0 || chapter_time > dur) continue;
                    int chapter_x = base.x + (int)((chapter_time / dur) * base.w);
                    SDL_Rect chapter_line = { chapter_x, base.y, THEME_TIMELINE_CHAPTER_MARKER_WIDTH, base.h };
                    draw_rect(ren, chapter_line, (SDL_Color){ THEME_CHAPTER_MARKER_COLOR[0], THEME_CHAPTER_MARKER_COLOR[1], THEME_CHAPTER_MARKER_COLOR[2], (Uint8)(190 * overlay_alpha) });
                }
                for (int i = 0; i < bookmark_count; i++) {
                    double bm_time = bookmarks[i].time;
                    if (bm_time < 0.0 || bm_time > dur) continue;
                    int bm_x = base.x + (int)((bm_time / dur) * base.w);
                    SDL_Rect bm_line = { bm_x, base.y, THEME_TIMELINE_BOOKMARK_MARKER_WIDTH, base.h };
                    uint32_t col_rgb;
                    #if !BM_OVERRIDE_DEFAULT_COLOR
                    if (bookmarks[i].is_default_color) {
                        col_rgb = THEME_DEFAULT_BOOKMARK_COLOR;
                    } else {
                        col_rgb = bookmarks[i].color_rgb;
                    }
                    #else
                    col_rgb = bookmarks[i].color_rgb;
                    #endif
                    uint8_t r = (col_rgb >> 16) & 0xFF;
                    uint8_t g = (col_rgb >> 8) & 0xFF;
                    uint8_t b = col_rgb & 0xFF;
                    draw_rect(ren, bm_line, (SDL_Color){ r, g, b, (Uint8)(230 * overlay_alpha) });
                }
            }
            SDL_Rect fill = { base.x, base.y, (int)(base.w * t), base.h };
            draw_rect(ren, fill, accent);
            SDL_Rect handle = { base.x + (int)(base.w * t) - ( THEME_TIMELINE_THUMB_SIZE / 2), base.y - (THEME_TIMELINE_THUMB_SIZE / 3), THEME_TIMELINE_THUMB_SIZE, THEME_TIMELINE_THUMB_SIZE };
            draw_rect(ren, handle, (SDL_Color){ THEME_TIMELINE_THUMB_COLOR[0], THEME_TIMELINE_THUMB_COLOR[1], THEME_TIMELINE_THUMB_COLOR[2], (Uint8)(220 * overlay_alpha) });

            if (vr) {
                const char* media_title = get_media_title(vr);
                if (!media_title) media_title = "";
                draw_text_shadow(ren, margin, base.y - 45, media_title, (SDL_Color){ THEME_MEDIA_TITLE_COLOR[0], THEME_MEDIA_TITLE_COLOR[1], THEME_MEDIA_TITLE_COLOR[2], (Uint8)(220 * overlay_alpha) });
            }

            if (vr && dur > 0.0) {
                int mouse_x = 0;
                int mouse_y = 0;
                SDL_GetMouseState(&mouse_x, &mouse_y);
                if (point_in_rect(mouse_x, mouse_y, timeline_hitbox)) {
                    int bm_idx = -1;
                    double bm_time = 0.0;
                    if (find_nearest_bookmark(bookmarks, bookmark_count, dur, timeline_rect, mouse_x, THEME_TIMELINE_BOOKMARK_MARKER_HITBOX_WIDTH, &bm_idx, &bm_time)) {
                        char bm_ts[32];
                        char hover_text[224];
                        format_time(bm_time, bm_ts, sizeof(bm_ts));
                        snprintf(hover_text, sizeof(hover_text), "%s (%s)", bookmarks[bm_idx].name, bm_ts);
                        int hover_w = 0, hover_h = 0;
                        TTF_SizeUTF8(ui_font, hover_text, &hover_w, &hover_h);
                        int hover_x = mouse_x + 10, hover_y = base.y - hover_h - 14;
                        if (hover_x + hover_w + 8 > w) hover_x = w - hover_w - 8;
                        if (hover_x < margin) hover_x = margin;
                        if (hover_y < margin) hover_y = margin;
                        SDL_Rect hover_bg = { hover_x - 4, hover_y - 2, hover_w + 8, hover_h + 4 };
                        draw_rect(ren, hover_bg, (SDL_Color){ THEME_BOOKMARK_HOVER_BG_COLOR[0], THEME_BOOKMARK_HOVER_BG_COLOR[1], THEME_BOOKMARK_HOVER_BG_COLOR[2], (Uint8)(220 * overlay_alpha) });
                        draw_text_shadow(ren, hover_x, hover_y, hover_text, text);
                    } else {
                        int chapter_idx = -1;
                        double chapter_time = 0.0;
                        if (find_nearest_chapter(vr, dur, timeline_rect, mouse_x, THEME_TIMELINE_CHAPTER_MARKER_HITBOX_WIDTH, &chapter_idx, &chapter_time)) {
                            char chapter_name[160];
                            char chapter_ts[32];
                            char hover_text[224];
                            get_media_chapter_label(vr, chapter_idx, chapter_name, sizeof(chapter_name));
                            format_time(chapter_time, chapter_ts, sizeof(chapter_ts));
                            snprintf(hover_text, sizeof(hover_text), "%s (%s)", chapter_name, chapter_ts);

                            int hover_w = 0;
                            int hover_h = 0;
                            TTF_SizeUTF8(ui_font, hover_text, &hover_w, &hover_h);
                            int hover_x = mouse_x + 10;
                            int hover_y = base.y - hover_h - 14;
                            if (hover_x + hover_w + 8 > w) hover_x = w - hover_w - 8;
                            if (hover_x < margin) hover_x = margin;
                            if (hover_y < margin) hover_y = margin;

                            SDL_Rect hover_bg = { hover_x - 4, hover_y - 2, hover_w + 8, hover_h + 4 };
                            draw_rect(ren, hover_bg, (SDL_Color){ THEME_CHAPTER_HOVER_BG_COLOR[0], THEME_CHAPTER_HOVER_BG_COLOR[1], THEME_CHAPTER_HOVER_BG_COLOR[2], (Uint8)(220 * overlay_alpha) });
                            draw_text_shadow(ren, hover_x, hover_y, hover_text, text);
                        }
                    }
                }
            }

            char left_time[32];
            char right_time[32];
            format_time(cur, left_time, sizeof(left_time));
            format_time(dur, right_time, sizeof(right_time));
            draw_text_shadow(ren, margin, h - overlay_h + 24, left_time, text);
            int right_w = 0, right_h = 0;
            TTF_SizeUTF8(ui_font, right_time, &right_w, &right_h);
            draw_text_shadow(ren, w - margin - right_w - 40, h - overlay_h + 24, right_time, text);

            draw_rect(ren, volume_rect, (SDL_Color){ THEME_OVERLAY_COLOR[0], THEME_OVERLAY_COLOR[1], THEME_OVERLAY_COLOR[2], (Uint8)(180 * overlay_alpha) });
            float vol_t = clampf(volume_percent / 200.0f, 0.0f, 1.0f);
            SDL_Rect vol_fill = { volume_rect.x, volume_rect.y + (int)(volume_rect.h * (1.0f - vol_t)), volume_rect.w, (int)(volume_rect.h * vol_t) };
            draw_rect(ren, vol_fill, accent);
            char vol_text[32];
            snprintf(vol_text, sizeof(vol_text), "%d%%", (int)volume_percent);
            draw_text_shadow(ren, volume_rect.x - 28, volume_rect.y + volume_rect.h + 6, vol_text, muted);

            draw_rect(ren, hamburger, (SDL_Color){ THEME_HAMBURGER_BG_COLOR[0], THEME_HAMBURGER_BG_COLOR[1], THEME_HAMBURGER_BG_COLOR[2], (Uint8)(200 * overlay_alpha) });

            int line_width  = hamburger.w - THEME_HAMBURGER_LINE_MARGIN * 2;

            float first_center_y  = hamburger.y + hamburger.h / 4.0f;
            float second_center_y = hamburger.y + hamburger.h / 2.0f;
            float third_center_y  = hamburger.y + 3 * hamburger.h / 4.0f;

            SDL_Color shadow_color = { THEME_SHADOW_COLOR[0], THEME_SHADOW_COLOR[1], THEME_SHADOW_COLOR[2], (Uint8)(100 * overlay_alpha)};

            float centers[3] = { first_center_y, second_center_y, third_center_y };
            for (int i = 0; i < 3; i++) {
                SDL_Rect line = {
                    hamburger.x + THEME_HAMBURGER_LINE_MARGIN,
                    (int)(centers[i] - THEME_HAMBURGER_LINE_HEIGHT / 2.0f),
                    line_width,
                    THEME_HAMBURGER_LINE_HEIGHT
                };
                SDL_Rect shadow = line;
                shadow.x += THEME_SHADOW_OFFSET;
                shadow.y += THEME_SHADOW_OFFSET;
                draw_rect(ren, shadow, shadow_color);

                draw_rect(ren, line, text);
            }

            if (menu_open) {
                draw_rect(ren, menu_panel, (SDL_Color){ THEME_MENU_PANEL_BG_COLOR[0], THEME_MENU_PANEL_BG_COLOR[1], THEME_MENU_PANEL_BG_COLOR[2], (Uint8)(220 * overlay_alpha) });
                draw_rect(ren, audio_box, (SDL_Color){ THEME_MENU_PANEL_ITEM_BG_COLOR[0], THEME_MENU_PANEL_ITEM_BG_COLOR[1], THEME_MENU_PANEL_ITEM_BG_COLOR[2], (Uint8)(200 * overlay_alpha) });
                draw_rect(ren, subtitle_box, (SDL_Color){ THEME_MENU_PANEL_ITEM_BG_COLOR[0], THEME_MENU_PANEL_ITEM_BG_COLOR[1], THEME_MENU_PANEL_ITEM_BG_COLOR[2], (Uint8)(200 * overlay_alpha) });
                draw_rect(ren, theme_box, (SDL_Color){ THEME_MENU_PANEL_ITEM_BG_COLOR[0], THEME_MENU_PANEL_ITEM_BG_COLOR[1], THEME_MENU_PANEL_ITEM_BG_COLOR[2], (Uint8)(200 * overlay_alpha) });
                draw_rect(ren, playback_box, (SDL_Color){ THEME_MENU_PANEL_ITEM_BG_COLOR[0], THEME_MENU_PANEL_ITEM_BG_COLOR[1], THEME_MENU_PANEL_ITEM_BG_COLOR[2], (Uint8)(200 * overlay_alpha) });
                if (subtitle_settings_applicable) {
                    draw_rect(ren, subtitle_settings_box, (SDL_Color){ THEME_MENU_PANEL_ITEM_BG_COLOR[0], THEME_MENU_PANEL_ITEM_BG_COLOR[1], THEME_MENU_PANEL_ITEM_BG_COLOR[2], (Uint8)(200 * overlay_alpha) });
                }

                char audio_label[160];
                if (vr && vr_get_audio_track_count(vr) > 0) {
                    snprintf(audio_label, sizeof(audio_label), "Audio: %s", vr_get_audio_track_name(vr, vr->current_audio));
                } else {
                    snprintf(audio_label, sizeof(audio_label), "Audio");
                }
                const char* sub_name = (vr && vr_get_subtitle_track_count(vr) > 0 && vr->current_subtitle >= 0) ? vr_get_subtitle_track_name(vr, vr->current_subtitle) : "Subtitles: Off";
                char theme_label[256+8];
                snprintf(theme_label, sizeof(theme_label), "Theme: %s", THEME_NAME);
                char playback_label[160];
                char subtitle_settings_label[200];
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%.6f", playback_speed);
                char* dot = strchr(tmp, '.');
                char* end = tmp + strlen(tmp) - 1;
                while (end > dot + 1 && *end == '0') *end-- = '\0';
                snprintf(playback_label, sizeof(playback_label), "Speed: %sx", tmp);
                snprintf(subtitle_settings_label, sizeof(subtitle_settings_label), "Subtitle Style...");
                draw_text_shadow(ren, audio_box.x + 8, audio_box.y + 3, audio_label, text);
                draw_text_shadow(ren, subtitle_box.x + 8, subtitle_box.y + 3, sub_name, text);
                draw_text_shadow(ren, theme_box.x + 8, theme_box.y + 3, theme_label, text);
                draw_text_shadow(ren, playback_box.x + 8, playback_box.y + 3, playback_label, text);
                if (subtitle_settings_applicable) {
                    draw_text_shadow(ren, subtitle_settings_box.x + 8, subtitle_settings_box.y + 3, subtitle_settings_label, text);
                }

                const char* menu_hover_tip = NULL;
                int menu_hover_tip_x = 0;
                int menu_hover_tip_y = 0;
                int mouse_x_menu = 0;
                int mouse_y_menu = 0;
                SDL_GetMouseState(&mouse_x_menu, &mouse_y_menu);

                if (audio_menu_open && vr) {
                    int count = vr_get_audio_track_count(vr);
                    int total = count + 3;
                    int max_items = THEME_MENU_MAX_VISIBLE_ITEMS;
                    int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                    int display_count = total > max_items ? max_items : total;
                    int list_h_audio = 0;
                    for (int _i = 0; _i < display_count; _i++) {
                        int _row = audio_scroll + _i;
                        list_h_audio += (_row == count) ? item_h / 2 : item_h;
                    }
                    SDL_Rect list = { menu_panel.x - THEME_MENU_DROPDOWN_WIDTH, audio_box.y, THEME_MENU_DROPDOWN_WIDTH, list_h_audio };
                    if (list.x < margin) list.x = margin;
                    
                    int max_scroll = total > max_items ? total - max_items : 0;
                    if (audio_scroll > max_scroll) audio_scroll = max_scroll;
                    if (audio_scroll < 0) audio_scroll = 0;
                    
                    draw_rect(ren, list, (SDL_Color){ THEME_LIST_BG_COLOR[0], THEME_LIST_BG_COLOR[1], THEME_LIST_BG_COLOR[2], (Uint8)(220 * overlay_alpha) });
                    int audio_y_cursor = 0;
                    for (int i = 0; i < display_count; i++) {
                        int row = audio_scroll + i;
                        int row_h = (row == count) ? item_h / 2 : item_h;
                        SDL_Rect item = { list.x, list.y + audio_y_cursor, list.w - (total > max_items ? THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), row_h };

                        if (row < count) {
                            if (vr->current_audio == row) {
                                draw_rect(ren, item, (SDL_Color){ THEME_LIST_ITEM_BG_COLOR[0], THEME_LIST_ITEM_BG_COLOR[1], THEME_LIST_ITEM_BG_COLOR[2], (Uint8)(180 * overlay_alpha) });
                            }
                            draw_text_shadow(ren, item.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, item.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, vr_get_audio_track_name(vr, row), text);
                        } else if (row == count) {
                            SDL_Rect sep = { item.x + 6, item.y + row_h / 2, item.w - 12, 1 };
                            draw_rect(ren, sep, (SDL_Color){ THEME_MUTED_COLOR[0], THEME_MUTED_COLOR[1], THEME_MUTED_COLOR[2], (Uint8)(190 * overlay_alpha) });
                        } else if (row == count + 1) {
                            char line[160];
                            snprintf(line, sizeof(line), "[%c] Audio OS Passthrough", audio_os_passthrough ? 'x' : ' ');
                            if (point_in_rect(mouse_x_menu, mouse_y_menu, item)) {
                                draw_rect(ren, item, (SDL_Color){ THEME_LIST_ITEM_BG_COLOR[0], THEME_LIST_ITEM_BG_COLOR[1], THEME_LIST_ITEM_BG_COLOR[2], (Uint8)(140 * overlay_alpha) });
                                menu_hover_tip = "Send supported encoded audio to output without mixer resampling.";
                                menu_hover_tip_x = item.x;
                                menu_hover_tip_y = item.y;
                            }
                            draw_text_shadow(ren, item.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, item.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, line, text);
                        } else if (row == count + 2) {
                            char line[160];
                            snprintf(line, sizeof(line), "[%c] Night Mode", night_mode ? 'x' : ' ');
                            if (point_in_rect(mouse_x_menu, mouse_y_menu, item)) {
                                draw_rect(ren, item, (SDL_Color){ THEME_LIST_ITEM_BG_COLOR[0], THEME_LIST_ITEM_BG_COLOR[1], THEME_LIST_ITEM_BG_COLOR[2], (Uint8)(140 * overlay_alpha) });
                                menu_hover_tip = "Compress loud peaks so dialogue stays more consistent at low volume.";
                                menu_hover_tip_x = item.x;
                                menu_hover_tip_y = item.y;
                            }
                            draw_text_shadow(ren, item.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, item.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, line, text);
                        }
                        audio_y_cursor += row_h;
                    }
                    
                    if (total > max_items) {
                        SDL_Rect scrollbar_bg = { list.x + list.w - THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH, list.y, THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH, list.h };
                        draw_rect(ren, scrollbar_bg, (SDL_Color){ THEME_SCROLLBAR_BG_COLOR[0], THEME_SCROLLBAR_BG_COLOR[1], THEME_SCROLLBAR_BG_COLOR[2], (Uint8)(200 * overlay_alpha) });
                        int scroll_h = (max_items * list.h) / total;
                        int scroll_y = list.y + (audio_scroll * list.h) / total;
                        SDL_Rect scrollbar = { list.x + list.w - THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_y, THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_h };
                        draw_rect(ren, scrollbar, (SDL_Color){ THEME_SCROLLBAR_THUMB_COLOR[0], THEME_SCROLLBAR_THUMB_COLOR[1], THEME_SCROLLBAR_THUMB_COLOR[2], (Uint8)(220 * overlay_alpha) });
                    }
                }

                if (subtitle_menu_open && vr) {
                    int count = vr_get_subtitle_track_count(vr);
                    int max_items = THEME_MENU_MAX_VISIBLE_ITEMS;
                    int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                    int display_count = (count + 1) > max_items ? max_items : (count + 1);
                    SDL_Rect list = { menu_panel.x - THEME_MENU_DROPDOWN_WIDTH, subtitle_box.y, THEME_MENU_DROPDOWN_WIDTH, item_h * display_count };
                    if (list.x < margin) list.x = margin;
                    int max_scroll = (count + 1) > max_items ? (count + 1) - max_items : 0;
                    if (subtitle_scroll > max_scroll) subtitle_scroll = max_scroll;
                    if (subtitle_scroll < 0) subtitle_scroll = 0;
                    int win_w, win_h;
                    SDL_GetWindowSize(win, &win_w, &win_h);
                    if (list.y + list.h > win_h) {
                        list.y = win_h - list.h;
                        if (list.y < margin) list.y = margin;
                    }
                    draw_rect(ren, list, (SDL_Color){ THEME_LIST_BG_COLOR[0], THEME_LIST_BG_COLOR[1], THEME_LIST_BG_COLOR[2], (Uint8)(220 * overlay_alpha) });
                    if (subtitle_scroll == 0) {
                        SDL_Rect off_item = { list.x, list.y, list.w - ((count + 1) > max_items ? THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                        if (vr->current_subtitle < 0) draw_rect(ren, off_item, (SDL_Color){ THEME_LIST_ITEM_BG_COLOR[0], THEME_LIST_ITEM_BG_COLOR[1], THEME_LIST_ITEM_BG_COLOR[2], (Uint8)(180 * overlay_alpha) });
                        draw_text_shadow(ren, off_item.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, off_item.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, "Subtitles: Off", text);
                        for (int i = 1; i < display_count; i++) {
                            int idx = (subtitle_scroll + i) - 1;
                            if (idx >= 0 && idx < count) {
                                SDL_Rect item = { list.x, list.y + i * item_h, list.w - ((count + 1) > max_items ? THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                                if (vr->current_subtitle == idx) draw_rect(ren, item, (SDL_Color){ THEME_LIST_ITEM_BG_COLOR[0], THEME_LIST_ITEM_BG_COLOR[1], THEME_LIST_ITEM_BG_COLOR[2], (Uint8)(180 * overlay_alpha) });
                                draw_text_shadow(ren, item.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, item.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, vr_get_subtitle_track_name(vr, idx), text);
                            }
                        }
                    } else {
                        for (int i = 0; i < display_count; i++) {
                            int idx = (subtitle_scroll + i) - 1;
                            if (idx >= 0 && idx < count) {
                                SDL_Rect item = { list.x, list.y + i * item_h, list.w - ((count + 1) > max_items ? THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                                if (vr->current_subtitle == idx) draw_rect(ren, item, (SDL_Color){ THEME_LIST_ITEM_BG_COLOR[0], THEME_LIST_ITEM_BG_COLOR[1], THEME_LIST_ITEM_BG_COLOR[2], (Uint8)(180 * overlay_alpha) });
                                draw_text_shadow(ren, item.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, item.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, vr_get_subtitle_track_name(vr, idx), text);
                            }
                        }
                    }
                    if ((count + 1) > max_items) {
                        SDL_Rect scrollbar_bg = { list.x + list.w - THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH, list.y, THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH, list.h };
                        draw_rect(ren, scrollbar_bg, (SDL_Color){ THEME_SCROLLBAR_BG_COLOR[0], THEME_SCROLLBAR_BG_COLOR[1], THEME_SCROLLBAR_BG_COLOR[2], (Uint8)(200 * overlay_alpha) });
                        int scroll_h = (max_items * list.h) / (count + 1);
                        int scroll_y = list.y + (subtitle_scroll * list.h) / (count + 1);
                        SDL_Rect scrollbar = { list.x + list.w - THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_y, THEME_MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_h };
                        draw_rect(ren, scrollbar, (SDL_Color){ THEME_SCROLLBAR_THUMB_COLOR[0], THEME_SCROLLBAR_THUMB_COLOR[1], THEME_SCROLLBAR_THUMB_COLOR[2], (Uint8)(220 * overlay_alpha) });
                    }
                }

                if (theme_menu_open) {
                    char theme_names[32][64];
                    int theme_count = get_theme_list(theme_names, 32);
                    int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                    SDL_Rect list = {
                        menu_panel.x - THEME_MENU_DROPDOWN_WIDTH,
                        theme_box.y,
                        THEME_MENU_DROPDOWN_WIDTH,
                        (theme_count + 3) * item_h + item_h / 2
                    };
                    if (list.x < margin) list.x = margin;
                    draw_rect(ren, list, (SDL_Color){ THEME_LIST_BG_COLOR[0], THEME_LIST_BG_COLOR[1], THEME_LIST_BG_COLOR[2], (Uint8)(220 * overlay_alpha) });
                    char current_theme[256] = "default";
                    if (strcmp(THEME_FILE, "config.h") != 0) {
                        snprintf(current_theme, sizeof(current_theme), "%s", THEME_FILE);
                        size_t cl = strlen(current_theme);
                        if (cl > 2 && strcmp(current_theme + cl - 2, ".h") == 0)
                            current_theme[cl - 2] = '\0';
                    }
                    for (int i = 0; i < theme_count; i++) {
                        if (strcmp(theme_names[i], current_theme) == 0) {
                            SDL_Rect item = { list.x, list.y + i * item_h, list.w, item_h };
                            draw_rect(ren, item, (SDL_Color){ THEME_LIST_ITEM_BG_COLOR[0], THEME_LIST_ITEM_BG_COLOR[1], THEME_LIST_ITEM_BG_COLOR[2], (Uint8)(180 * overlay_alpha) });
                        }
                        draw_text_shadow(ren,
                            list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X,
                            list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * i,
                            theme_names[i], text);
                    }
                    draw_text_shadow(ren,
                        list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X,
                        list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * theme_count,
                        "Custom", text);

                    {
                        SDL_Rect sep = { list.x + 6, list.y + item_h * (theme_count + 1), list.w - 12, item_h / 2 };
                        SDL_Rect sep_line = { sep.x, sep.y + sep.h / 2, sep.w, 1 };
                        draw_rect(ren, sep_line, (SDL_Color){ THEME_MUTED_COLOR[0], THEME_MUTED_COLOR[1], THEME_MUTED_COLOR[2], (Uint8)(190 * overlay_alpha) });
                    }

                    {
                        SDL_Rect item = { list.x, list.y + (theme_count + 1) * item_h + item_h / 2, list.w, item_h };
                        char line[160];
                        snprintf(line, sizeof(line), "[%c] Blackout Mode", blackout_mode ? 'x' : ' ');
                        if (point_in_rect(mouse_x_menu, mouse_y_menu, item)) {
                            draw_rect(ren, item, (SDL_Color){ THEME_LIST_ITEM_BG_COLOR[0], THEME_LIST_ITEM_BG_COLOR[1], THEME_LIST_ITEM_BG_COLOR[2], (Uint8)(140 * overlay_alpha) });
                            #if !BLACKOUT_MODE_TURN_OFF_MONITOR
                            menu_hover_tip = "When fullscreen, black out the other monitors with borderless windows.";
                            #else
                            menu_hover_tip = "When fullscreen, turn off the other monitors.";
                            #endif
                            menu_hover_tip_x = item.x;
                            menu_hover_tip_y = item.y;
                        }
                        draw_text_shadow(ren, item.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, item.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, line, text);
                    }

                    {
                        SDL_Rect item = { list.x, list.y + (theme_count + 2) * item_h + item_h / 2, list.w, item_h };
                        char line[160];
                        snprintf(line, sizeof(line), "[%c] Ambient Glow", ambient_glow ? 'x' : ' ');
                        if (point_in_rect(mouse_x_menu, mouse_y_menu, item)) {
                            draw_rect(ren, item, (SDL_Color){ THEME_LIST_ITEM_BG_COLOR[0], THEME_LIST_ITEM_BG_COLOR[1], THEME_LIST_ITEM_BG_COLOR[2], (Uint8)(140 * overlay_alpha) });
                            menu_hover_tip = "Project edge colors onto the black bars as a soft ambilight glow.";
                            menu_hover_tip_x = item.x;
                            menu_hover_tip_y = item.y;
                        }
                        draw_text_shadow(ren, item.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, item.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, line, text);
                    }
                }

                if (playback_menu_open) {
                    int count = 8;
                    int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                    SDL_Rect list = { menu_panel.x - THEME_MENU_DROPDOWN_WIDTH, playback_box.y, THEME_MENU_DROPDOWN_WIDTH, item_h * count };
                    if (list.x < margin) list.x = margin;
                    if (list.y + list.h > h) {
                        list.y = h - list.h;
                        if (list.y < 0) list.y = 0;
                    }
                    draw_rect(ren, list, (SDL_Color){ THEME_LIST_BG_COLOR[0], THEME_LIST_BG_COLOR[1], THEME_LIST_BG_COLOR[2], (Uint8)(220 * overlay_alpha) });
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, "0.5x", text);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h, "0.75x", text);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 2, "1.0x (Normal)", text);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 3, "1.25x", text);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 4, "1.5x", text);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 5, "2.0x", text);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 6, "3.0x", text);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 7, "5.0x", text);
                }

                if (subtitle_settings_menu_open && subtitle_settings_applicable) {
                    int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
                    SDL_Rect list = { menu_panel.x - THEME_MENU_DROPDOWN_WIDTH, subtitle_settings_box.y, THEME_MENU_DROPDOWN_WIDTH, item_h * subtitle_settings_row_count };
                    if (list.x < margin) list.x = margin;
                    if (list.y + list.h > h) {
                        list.y = h - list.h;
                        if (list.y < 0) list.y = 0;
                    }
                    draw_rect(ren, list, (SDL_Color){ THEME_LIST_BG_COLOR[0], THEME_LIST_BG_COLOR[1], THEME_LIST_BG_COLOR[2], (Uint8)(220 * overlay_alpha) });

                    char row0[128];
                    char row1[128];
                    char row2[128];
                    snprintf(row0, sizeof(row0), "Color: %s", subtitle_override_color_labels[subtitle_color_idx]);
                    snprintf(row1, sizeof(row1), "Size: %s", subtitle_override_size_labels[subtitle_size_idx]);
                    snprintf(row2, sizeof(row2), "Position: %s", subtitle_override_move_labels[subtitle_move_idx]);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y, row0, text);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h, row1, text);
                    draw_text_shadow(ren, list.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, list.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 2, row2, text);

                    if (subtitle_settings_value_menu_open && subtitle_settings_value_menu_row >= 0) {
                        int value_count = 0;
                        if (subtitle_settings_value_menu_row == 0) value_count = subtitle_color_count;
                        if (subtitle_settings_value_menu_row == 1) value_count = subtitle_size_count;
                        if (subtitle_settings_value_menu_row == 2) value_count = subtitle_move_count;

                        if (value_count > 0) {
                            SDL_Rect vlist = { list.x - THEME_MENU_DROPDOWN_WIDTH - 8, list.y, THEME_MENU_DROPDOWN_WIDTH, item_h * value_count };
                            if (vlist.x < margin) vlist.x = margin;
                            if (vlist.y + vlist.h > h) {
                                vlist.y = h - vlist.h;
                                if (vlist.y < 0) vlist.y = 0;
                            }
                            draw_rect(ren, vlist, (SDL_Color){ THEME_SUBTITLE_VLIST_BG_COLOR[0], THEME_SUBTITLE_VLIST_BG_COLOR[1], THEME_SUBTITLE_VLIST_BG_COLOR[2], (Uint8)(220 * overlay_alpha) });

                            for (int i = 0; i < value_count; i++) {
                                const char* label = "";
                                if (subtitle_settings_value_menu_row == 0) label = subtitle_override_color_labels[i];
                                if (subtitle_settings_value_menu_row == 1) label = subtitle_override_size_labels[i];
                                if (subtitle_settings_value_menu_row == 2) label = subtitle_override_move_labels[i];
                                draw_text_shadow(ren, vlist.x + THEME_MENU_DROPDOWN_TEXT_PADDING_X, vlist.y + THEME_MENU_DROPDOWN_TEXT_PADDING_Y + item_h * i, label, text);
                            }
                        }
                    }
                }

                if (menu_hover_tip && menu_hover_tip[0]) {
                    int tip_w = 0;
                    int tip_h = 0;
                    TTF_SizeUTF8(ui_font, menu_hover_tip, &tip_w, &tip_h);
                    SDL_Rect tip = {
                        menu_hover_tip_x - tip_w - 16,
                        menu_hover_tip_y - 4,
                        tip_w + 12,
                        tip_h + 8
                    };
                    if (tip.x < margin) tip.x = margin;
                    if (tip.y < margin) tip.y = margin;
                    draw_rect(ren, tip, (SDL_Color){ THEME_CONTEXT_MENU_BG_COLOR[0], THEME_CONTEXT_MENU_BG_COLOR[1], THEME_CONTEXT_MENU_BG_COLOR[2], (Uint8)(235 * overlay_alpha) });
                    draw_text_shadow(ren, tip.x + 6, tip.y + 3, menu_hover_tip, text);
                }
            }
        }

        pause_alpha = lerpf(pause_alpha, paused ? 1.0f : 0.0f, clampf(dt * 6.0f, 0.0f, 1.0f));
        if (pause_alpha > 0.01f && !ti.active) {
            SDL_Color pcol = { THEME_PAUSED_TEXT_COLOR[0], THEME_PAUSED_TEXT_COLOR[1], THEME_PAUSED_TEXT_COLOR[2], (Uint8)(255 * pause_alpha) };
            draw_text_shadow(ren, 20, 20, "PAUSED", pcol);
        }

        Uint32 now_ticks = SDL_GetTicks();
        if (flash_text[0]) {
            if (now_ticks < flash_until) {
                flash_alpha = lerpf(flash_alpha, 1.0f, clampf(dt * 8.0f, 0.0f, 1.0f));
            } else {
                flash_alpha = lerpf(flash_alpha, 0.0f, clampf(dt * 6.0f, 0.0f, 1.0f));
                if (flash_alpha < 0.01f) flash_text[0] = 0;
            }
            if (flash_alpha > 0.01f) {
                SDL_Color fcol = { THEME_FLASH_TEXT_COLOR[0], THEME_FLASH_TEXT_COLOR[1], THEME_FLASH_TEXT_COLOR[2], (Uint8)(220 * flash_alpha) };
                draw_text_shadow(ren, 20, 56, flash_text, fcol);
            }
        }

        if (vr && playback_speed > 2.0f) {
            SDL_Color acol = { THEME_ACOL_TEXT_COLOR[0], THEME_ACOL_TEXT_COLOR[1], THEME_ACOL_TEXT_COLOR[2], THEME_ACOL_TEXT_ALPHA };
            draw_text_shadow(ren, 20, 92, "Audio disabled at high speed", acol);
        }

        if (bm_ctx_open) {
            #define CTX_ITEM_H 26
            #define CTX_W 150
            int total_h = CTX_ITEM_H * 3 + 8;
            int cx = bm_ctx_x, cy = bm_ctx_y;
            if (cx + CTX_W > w) cx = w - CTX_W;
            if (cy + total_h > h) cy = h - total_h;
            SDL_Rect ctxbg = { cx, cy, CTX_W, total_h };
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            draw_rect(ren, ctxbg, (SDL_Color){ THEME_CONTEXT_MENU_BG_COLOR[0], THEME_CONTEXT_MENU_BG_COLOR[1], THEME_CONTEXT_MENU_BG_COLOR[2], 240 });
            SDL_Rect r0 = { cx, cy, CTX_W, CTX_ITEM_H };
            SDL_Rect r1 = { cx, cy + CTX_ITEM_H, CTX_W, CTX_ITEM_H };
            SDL_Rect r2 = { cx, cy + CTX_ITEM_H * 2 + 8, CTX_W, CTX_ITEM_H };
            SDL_Color text_col = { THEME_TEXT_COLOR[0], THEME_TEXT_COLOR[1], THEME_TEXT_COLOR[2], 255 };
            int mx2 = 0, my2 = 0;
            SDL_GetMouseState(&mx2, &my2);
            if (point_in_rect(mx2, my2, ctxbg)) {
                if (my2 < cy + CTX_ITEM_H) {
                    draw_rect(ren, r0, (SDL_Color){ THEME_CONTEXT_MENU_ITEM_HL[0], THEME_CONTEXT_MENU_ITEM_HL[1], THEME_CONTEXT_MENU_ITEM_HL[2], 200 });
                } else if (my2 < cy + CTX_ITEM_H * 2) {
                    draw_rect(ren, r1, (SDL_Color){ THEME_CONTEXT_MENU_ITEM_HL[0], THEME_CONTEXT_MENU_ITEM_HL[1], THEME_CONTEXT_MENU_ITEM_HL[2], 200 });
                } else if (my2 >= cy + CTX_ITEM_H * 2 + 8) {
                    draw_rect(ren, r2, (SDL_Color){ THEME_CONTEXT_MENU_ITEM_HL[0], THEME_CONTEXT_MENU_ITEM_HL[1], THEME_CONTEXT_MENU_ITEM_HL[2], 200 });
                }
            }
            draw_text_shadow(ren, cx + 8, cy + 4, "Rename", text_col);
            draw_text_shadow(ren, cx + 8, cy + CTX_ITEM_H + 4, "Change Color", text_col);
            SDL_Rect sep = { cx + 4, cy + CTX_ITEM_H * 2 + 4, CTX_W - 8, 1 };
            draw_rect(ren, sep, (SDL_Color){ THEME_MUTED_COLOR[0], THEME_MUTED_COLOR[1], THEME_MUTED_COLOR[2], 160 });
            draw_text_shadow(ren, cx + 8, cy + CTX_ITEM_H * 2 + 10, "Remove", text_col);
            #undef CTX_ITEM_H
            #undef CTX_W
        }

        if (kbd_overlay_open) {
            int item_h = THEME_MENU_DROPDOWN_ITEM_HEIGHT;
            int col_w = 360;
            int cols = w > col_w * 2 + 80 ? 2 : 1;
            int rows = (kbd_entry_count + cols - 1) / cols;
            int panel_w = col_w * cols + 24;
            int panel_h = rows * item_h + 40;
            if (panel_h > h - 40) panel_h = h - 40;
            int panel_x = (w - panel_w) / 2;
            int panel_y = (h - panel_h) / 2;
            SDL_Rect panel = { panel_x, panel_y, panel_w, panel_h };
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            draw_rect(ren, panel, (SDL_Color){ THEME_CONTEXT_MENU_BG_COLOR[0], THEME_CONTEXT_MENU_BG_COLOR[1], THEME_CONTEXT_MENU_BG_COLOR[2], 240 });
            SDL_Color kbd_text = { THEME_TEXT_COLOR[0], THEME_TEXT_COLOR[1], THEME_TEXT_COLOR[2], 255 };
            SDL_Color kbd_key_bg = { THEME_OVERLAY_COLOR[0], THEME_OVERLAY_COLOR[1], THEME_OVERLAY_COLOR[2], 220 };
            SDL_Color kbd_hl = { THEME_CONTEXT_MENU_ITEM_HL[0], THEME_CONTEXT_MENU_ITEM_HL[1], THEME_CONTEXT_MENU_ITEM_HL[2], 200 };
            draw_text_shadow(ren, panel_x + 12, panel_y + 8, "Keyboard Shortcuts  (? to close)", kbd_text);
            int inner_y = panel_y + 30;
            int max_visible = (panel_h - 30) / item_h;
            int max_scroll_kbd = kbd_entry_count - max_visible;
            if (max_scroll_kbd < 0) max_scroll_kbd = 0;
            if (kbd_scroll > max_scroll_kbd) kbd_scroll = max_scroll_kbd;
            int key_col_w = 130;
            int btn_y_pad = 4;
            int mx_r = 0, my_r = 0;
            SDL_GetMouseState(&mx_r, &my_r);
            for (int i = 0; i < max_visible && (kbd_scroll + i) < kbd_entry_count; i++) {
                int entry_idx = kbd_scroll + i;
                int by = inner_y + i * item_h;
                int bx = panel_x + 12;
                SDL_Rect btn = { bx, by + btn_y_pad, key_col_w, item_h - btn_y_pad * 2 };
                if (point_in_rect(mx_r, my_r, btn)) draw_rect(ren, btn, kbd_hl);
                else draw_rect(ren, btn, kbd_key_bg);
                draw_text_shadow(ren, btn.x + 6, btn.y - 1, display_kbd((KbdEntry*)(&(kbd_entries[entry_idx]))), kbd_text);
                draw_text_shadow(ren, bx + key_col_w + 10, by + btn_y_pad + 3, kbd_entries[entry_idx].desc, kbd_text);
            }
        }

        if (ti.active) {
            text_input_draw(ren, &ti);
        }

        if (using_zoom_tex) {
            SDL_SetRenderTarget(ren, NULL);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_Rect dst = {
                (int)draw_zoom_ox,
                (int)draw_zoom_oy,
                (int)(window_w * draw_zoom),
                (int)(window_h * draw_zoom)
            };
            SDL_RenderCopy(ren, draw_zoom_tex, NULL, &dst);
        }

        SDL_RenderPresent(ren);
    }

#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
#endif

#if defined(_WIN32) && BLACKOUT_MODE_TURN_OFF_MONITOR
    blkout_restore_monitors();
#endif
    blackout_destroy_windows(blackout_windows, &blackout_window_count);

    #if SAVE_FILE
        fill_save_state_from_vr(vr, &save_state, video_file, paused, volume_percent);
        save_state.global.audio_os_passthrough = audio_os_passthrough ? 1 : 0;
        save_state.global.night_mode = night_mode ? 1 : 0;
        save_state.global.blackout_mode = blackout_mode ? 1 : 0;
        save_state.global.ambient_glow = ambient_glow ? 1 : 0;
        if (!fullscreen && !maximized) {
            int _sx = 0, _sy = 0;
            SDL_GetWindowPosition(win, &_sx, &_sy);
            save_state.global.win_x = (int32_t)_sx;
            save_state.global.win_y = (int32_t)_sy;
        }
        for (uint64_t i = 0; i < save_state.recent_files_count && i < MAX_RECENT; i++) {
            if (save_state.recent_files[i]) {
                free(save_state.recent_files[i]);
                save_state.recent_files[i] = NULL;
            }
        }
        save_state.recent_files_count = recent_count;
        for (int i = 0; i < recent_count && i < MAX_RECENT; i++) {
            save_state.recent_files[i] = recent_files[i] ? strdup(recent_files[i]) : NULL;
        }
    #endif
    if (vr) vr_free(vr);
    hist_clear_from(history, &history_count, 0);
    if (ui_font) TTF_CloseFont(ui_font);
    TTF_Quit();
    for (int i = 0; i < recent_count; i++) free(recent_files[i]);
    abspath_temp_free();
#ifdef _WIN32
    if (win_hwnd && g_prev_menu_wndproc) {
        SetWindowLongPtr(win_hwnd, GWLP_WNDPROC, (LONG_PTR)g_prev_menu_wndproc);
    }
    g_prev_menu_wndproc = NULL;
    g_menu_vr_ptr = NULL;
    g_menu_win = NULL;
    g_menu_ren = NULL;
    g_menu_paused_ptr = NULL;
    g_menu_playback_speed_ptr = NULL;
    g_menu_ambient_glow_ptr = NULL;
#endif
    draw_free(&draw_state);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    #if SAVE_FILE
        char* save_path = abspath_temp(SAVE_FILE_PATH);
        if (!write_save_state(save_path, &save_state)) {
            nob_log(NOB_ERROR, "Failed to write save state to %s", save_path);
        } else {
            nob_log(NOB_INFO, "Save state written to %s", save_path);
        }
        debug_save_state(&save_state);
        free_save_state(&save_state);
    #endif
    nob_log(NOB_INFO, "Exited.");
    return 0;
}
