#define NOB_IMPLEMENTATION
#define NOB_UNSTRIP_PREFIX
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#endif
#include "../thirdparty/SDL2/SDL.h"
#include "../thirdparty/SDL2/SDL_ttf.h"
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#endif
#include "../thirdparty/nob.h"
#include "../thirdparty/tinyfd.c"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifndef _WIN32
#include <strings.h>
#endif
#undef NOB_IMPLEMENTATION

#include "config.h"
#include "text.c"
#include "renderer.c"

#if SAVE_FILE
#include "save.c"
#endif

char* recent_files[MAX_RECENT] = {0};
int recent_count = 0;

char flash_text[256] = {0};
Uint32 flash_until = 0;
float flash_alpha = 0.0f;

typedef struct {
    const char *name;
    const char *path;
} FontEntry;

static FontEntry default_fonts[] = DEFAULT_FONTS_MAP;
static const int default_font_count =
    sizeof(default_fonts) / sizeof(default_fonts[0]);

static int flash_debug_enabled = AMP_FLASH_DEBUG_DEFAULT;
static int flash_debug_level = AMP_FLASH_DEBUG_LEVEL_DEFAULT;

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
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

static float volume_percent_to_gain(float percent) {
    float db;
    if (percent <= 100.0f) {
        float t = percent / 100.0f;
        db = -50.0f * (1.0f - t);
    } else {
        float t = (percent - 100.0f) / 100.0f;
        db = 12.0f * t;
    }
    return powf(10.0f, db / 20.0f);
}

/*
static float gain_to_volume_percent(float gain) {
    if (gain <= 0.0f) return 0.0f;
    float db = 20.0f * log10f(gain);
    if (db <= 0.0f) {
        float t = (db + 50.0f) / 50.0f;
        return clampf(t * 100.0f, 0.0f, 100.0f);
    } else {
        float t = db / 12.0f;
        return clampf(100.0f + t * 100.0f, 100.0f, 200.0f);
    }
}
*/

static int point_in_rect(int x, int y, SDL_Rect r) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

#if CHECK_FILE_SIGNATURE
static bool is_supported_video_file(const char* path) {
    if (!path) return false;

    av_log_set_level(AV_LOG_ERROR);

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

    const char *level_str = "";
    switch (level) {
        case NOB_INFO:    level_str = "INFO   "; break;
        case NOB_WARNING: level_str = "WARNING"; break;
        case NOB_ERROR:   level_str = "ERROR  "; break;
        case NOB_NO_LOGS: return;
    }

    fprintf(stderr, "[%s] [%s] ", timebuf, level_str);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    if (flash_debug_enabled && level == (Nob_Log_Level)flash_debug_level) {
        char flash_buf[256];
        vsnprintf(flash_buf, sizeof(flash_buf), fmt, args);
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
    fprintf(out, "  --volume [0-200]             Set initial audio volume (default: 100)\n");
    fprintf(out, "  --speed [SPEED > 0]          Set initial playback speed (e.g. 0.5, 1.0, 1.5)\n");
    fprintf(out, "  --flash-debug                Show log messages as on-screen flash\n");
    fprintf(out, "  --no-flash-debug             Disable on-screen flash for log messages\n");
    fprintf(out, "  --flash-debug-level [LEVEL]  Show log messages as on-screen flash (LEVEL: 0 - NO LOGS, 1 - INFO, 2 - WARNING, 3 - ERROR)\n");
}

void add_recent_file(const char* file) {
    for(int i = MAX_RECENT-1; i>0; i--) recent_files[i] = recent_files[i-1];
    recent_files[0] = strdup(file);
    if(recent_count < MAX_RECENT) recent_count++;
}

char* get_absolute_path(const char* path) {
    if (!path) return NULL;

#ifdef _WIN32
    char buf[4096];
    if (_fullpath(buf, path, sizeof(buf))) {
        return strdup(buf);
    }
#else
    char buf[4096];
    if (realpath(path, buf)) {
        return strdup(buf);
    }
#endif

    return strdup(path);
}

char* open_file_dialog(const char* filters[], int filter_count, const char* filter_desc, bool allow_multiple, const char* title, const char* default_path) {
    char const* filename = tinyfd_openFileDialog(
        title ? title : "Select File",
        default_path ? default_path : "",
        filter_count,
        filters,
        filter_desc,
        allow_multiple ? 1 : 0
    );
    if (!filename) return NULL;
    if (!is_supported_video_file(filename)) return NULL;
    char* abs_path = get_absolute_path(filename);
    return abs_path;
}

#ifdef _WIN32
enum {
    MENU_OPEN = 1,
    MENU_EXIT,
    MENU_FULLSCREEN,
    MENU_MINIMIZE,
    MENU_RECENT_BASE = 100
};

static HWND get_hwnd(SDL_Window* window) {
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    if (!SDL_GetWindowWMInfo(window, &wm_info) || wm_info.subsystem != SDL_SYSWM_WINDOWS) {
        return NULL;
    }
    return wm_info.info.win.window;
}

HMENU create_windows_menu(SDL_Window* window) {
    HWND hwnd = get_hwnd(window);
    if (!hwnd) return NULL;
    HMENU hMenu = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    HMENU hViewMenu = CreatePopupMenu();

    AppendMenu(hFileMenu, MF_STRING, MENU_OPEN, "Open File\tCtrl+O");

    if(recent_count > 0) {
        HMENU hRecent = CreatePopupMenu();
        for(int i=0; i<recent_count; i++) {
            AppendMenu(hRecent, MF_STRING, MENU_RECENT_BASE+i, recent_files[i]);
        }
        AppendMenu(hFileMenu, MF_POPUP, (UINT_PTR)hRecent, "Recent Files");
    }

    AppendMenu(hFileMenu, MF_STRING, MENU_EXIT, "Exit\tAlt+F4");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "File");

    AppendMenu(hViewMenu, MF_STRING, MENU_FULLSCREEN, "Fullscreen\tF11");
    AppendMenu(hViewMenu, MF_STRING, MENU_MINIMIZE, "Minimize\tAlt+M");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, "View");

    SetMenu(hwnd, hMenu);
    DrawMenuBar(hwnd);
    return hMenu;
}
#endif

int main(int argc, char** argv) {
    nob_set_log_handler(amp_log_handler);

    VideoRenderer* vr = NULL;
    char* video_file = NULL;
    bool running = true;
    bool fullscreen = false;
    bool maximized = false;
    int windowed_x = SDL_WINDOWPOS_CENTERED;
    int windowed_y = SDL_WINDOWPOS_CENTERED;
    int windowed_w = INITIAL_WINDOW_WIDTH;
    int windowed_h = INITIAL_WINDOW_HEIGHT;
    int windowed_valid = 0;
    bool paused = false;
    bool dragging_timeline = false;
    bool volume_dragging = false;
    bool menu_open = false;
    bool audio_menu_open = false;
    bool subtitle_menu_open = false;
    bool font_menu_open = false;
    bool playback_menu_open = false;
    double drag_time = 0.0;
    double timestamp_history[MAX_HISTORY] = {0};
    int history_pos = 0;
    int history_count = 0;
    float overlay_alpha = 1.0f;
    float overlay_target = 1.0f;
    Uint32 last_mouse_move = SDL_GetTicks();
    Uint32 last_tick = SDL_GetTicks();
    float volume_percent = 100.0f;
    float playback_speed = 1.0f;
    float pause_alpha = 0.0f;
    int audio_scroll = 0;
    int subtitle_scroll = 0;
    SDL_Event e;

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
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            fprintf(stdout, "amp version %d.%d.%d\n", (AMP_VERSION >> 16) & 0xFF, (AMP_VERSION >> 8) & 0xFF, AMP_VERSION & 0xFF);
            return 0;
        } else if (strcmp(argv[i], "--info") == 0 || strcmp(argv[i], "-i") == 0) {
            fprintf(stdout, "amp - A simple video player\n");
            fprintf(stdout, "version: %d.%d.%d\n", (AMP_VERSION >> 16) & 0xFF, (AMP_VERSION >> 8) & 0xFF, AMP_VERSION & 0xFF);
            usage(stdout, argv[0]);
            fprintf(stdout, "Compiled with:\n");
            fprintf(stdout, "  Nob version: %s\n", NOB_VERSION);
            fprintf(stdout, "  SDL2 version: %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
            fprintf(stdout, "  TTF version: %d.%d.%d\n", TTF_MAJOR_VERSION, TTF_MINOR_VERSION, TTF_PATCHLEVEL);
            fprintf(stdout, "  FFmpeg version: %d.%d.%d\n", LIBAVFORMAT_VERSION_MAJOR, LIBAVFORMAT_VERSION_MINOR, LIBAVFORMAT_VERSION_MICRO);
            fprintf(stdout, "  LibAss version: %d.%d.%d\n", (LIBASS_VERSION >> 24) & 0xFF, (LIBASS_VERSION >> 16) & 0xFF, (LIBASS_VERSION >> 8) & 0xFF);
            fprintf(stdout, "  Compiler: %s ", CC);
            const char* flags[] = { CFLAGS, NULL };
            for (int j = 0; flags[j]; j++) fprintf(stdout, "%s ", flags[j]);
            fprintf(stdout, "\n");
            fprintf(stdout, "(c) 2026 Markofwitch. All rights reserved.\n");
            return 0;
        } else if (argv[i][0] != '-') {
            video_file = get_absolute_path(argv[i]);
        } else {
            nob_log(NOB_WARNING, "Unknown argument: %s", argv[i]);
        }
    }
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        nob_log(NOB_ERROR, "SDL_Init Error: %s", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        nob_log(NOB_ERROR, "TTF_Init Error: %s", TTF_GetError());
        return 1;
    }
    nob_log(NOB_INFO, "SDL initialized successfully");
    
    bool font_loaded = false;
    for (int i = 0; i < default_font_count; i++) {
        if (load_ui_font(default_fonts[i].path, default_fonts[i].name)) {
            nob_log(NOB_INFO, "Loaded UI font: %s from %s", default_fonts[i].name, default_fonts[i].path);
            font_loaded = true;
            break;
        } else {
            nob_log(NOB_WARNING, "Failed to load UI font: %s from %s", default_fonts[i].name, default_fonts[i].path);
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
            "Select Video File", NULL
        );
        if (!video_file || !is_supported_video_file(video_file)) {
            nob_log(NOB_ERROR, "No file selected or unsupported file type. Exiting.");
            return 1;
        }  
    } else {
        nob_log(NOB_INFO, "Video file specified: %s", video_file);
    }

    SDL_Window* win = SDL_CreateWindow(
        "(no file selected)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS
    );

    if(!win) { nob_log(NOB_ERROR, "SDL_CreateWindow failed: %s", SDL_GetError()); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!ren) { SDL_DestroyWindow(win); nob_log(NOB_ERROR, "SDL_CreateRenderer failed: %s", SDL_GetError()); return 1; }

#ifdef _WIN32
    HMENU win_menu = create_windows_menu(win);
    HWND win_hwnd = get_hwnd(win);
#endif

    if(video_file) {
        vr = vr_create(win, ren);
        if(vr_load(vr, video_file)) {
            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
            add_recent_file(video_file);
            SDL_SetWindowTitle(win, video_file);
            nob_log(NOB_INFO, "Loaded %s", video_file);
        }
    }

    #if SAVE_FILE
        SaveState save_state = {0};
        if (!load_save_state(SAVE_FILE_PATH, &save_state)) {
            nob_log(NOB_INFO, "No save file found, starting with default settings");
        } else {
            nob_log(NOB_INFO, "Save file loaded successfully");
            apply_save_state_to_vr(vr, &save_state, video_file);
        }
    #endif

    SDL_Rect timeline_rect, timeline_hitbox, volume_rect, hamburger, menu_panel, audio_box, subtitle_box, font_box, playback_box, overlay_rect;
    int overlay_h = 100;
    int margin = 24;
    int w, h;

    while(running) {
        {
            SDL_GetWindowSize(win, &w, &h);
            overlay_rect = (SDL_Rect){ 0, h - overlay_h, w, overlay_h };
            timeline_rect = (SDL_Rect){ margin, h - overlay_h + 12, w - margin * 2 - 40, TIMELINE_HEIGHT };
            timeline_hitbox = (SDL_Rect){ timeline_rect.x, timeline_rect.y - TIMELINE_HITBOX_PADDING, timeline_rect.w, TIMELINE_HEIGHT + TIMELINE_HITBOX_PADDING * 2 };
            volume_rect = (SDL_Rect){ w - margin - 32, h - overlay_h + 40, 6, 50 };
            hamburger = (SDL_Rect){ w - margin - 28, h - overlay_h + 12, 24, 20 };
            
            int menu_x = hamburger.x - 250;
            if (menu_x < margin) menu_x = margin;
            int menu_y = h - overlay_h - 180;
            if (menu_y < margin) menu_y = h - overlay_h + 12;
            menu_panel = (SDL_Rect){ menu_x, menu_y, 250, 180 };
            audio_box = (SDL_Rect){ menu_panel.x + 12, menu_panel.y + 12, menu_panel.w - 24, 28 };
            subtitle_box = (SDL_Rect){ menu_panel.x + 12, menu_panel.y + 50, menu_panel.w - 24, 28 };
            font_box = (SDL_Rect){ menu_panel.x + 12, menu_panel.y + 88, menu_panel.w - 24, 28 };
            playback_box = (SDL_Rect){ menu_panel.x + 12, menu_panel.y + 126, menu_panel.w - 24, 28 };
        }

        while(SDL_PollEvent(&e)) {
            if(e.type == SDL_QUIT) running = false;

            if(e.type == SDL_MOUSEMOTION) {
                last_mouse_move = SDL_GetTicks();
                overlay_target = 1.0f;
            }

#ifdef _WIN32
            if(e.type == SDL_SYSWMEVENT) {
                SDL_SysWMmsg* sysmsg = e.syswm.msg;
                if(sysmsg && sysmsg->subsystem == SDL_SYSWM_WINDOWS) {
                    if(sysmsg->msg.win.msg == WM_COMMAND) {
                        int id = LOWORD(sysmsg->msg.win.wParam);
                        if(id == MENU_OPEN) {
                            char* f = open_file_dialog(
                                (const char*[]){"*.mkv", "*.mp4"}, 2, "Video Files (*.mkv, *.mp4)", false,
                                "Select Video File", NULL
                            );
                            if(f) {
                                video_file = f;
                                if(!vr) vr = vr_create(win, ren);
                                if(vr_load(vr, f)) {
                                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                                    add_recent_file(f);
                                    SDL_SetWindowTitle(win, f);
                                    nob_log(NOB_INFO, "Loaded %s", f);
                                } else {
                                    nob_log(NOB_ERROR, "Failed to load %s", f);
                                }
                            }
                            #if SAVE_FILE
                                fill_save_state_from_vr(vr, &save_state, video_file);
                            #endif
                        } else if(id >= MENU_RECENT_BASE && id < MENU_RECENT_BASE+MAX_RECENT) {
                            int idx = id - MENU_RECENT_BASE;
                            if(idx < recent_count) {
                                if(!vr) vr = vr_create(win, ren);
                                if(vr_load(vr, recent_files[idx])) {
                                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                                    SDL_SetWindowTitle(win, recent_files[idx]);
                                    nob_log(NOB_INFO, "Loaded %s", recent_files[idx]);
                                } else
                                    nob_log(NOB_ERROR, "Failed to load %s", recent_files[idx]);
                            }
                        } else if(id == MENU_EXIT) running = false;
                        else if(id == MENU_FULLSCREEN) {
                            fullscreen = !fullscreen;
                            if (fullscreen) {
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
                                SDL_MaximizeWindow(win);
                                maximized = true;
                            } else {
                                maximized = false;
                            }
#ifdef _WIN32
                            if (win_hwnd) {
                                SetMenu(win_hwnd, fullscreen ? NULL : win_menu);
                                DrawMenuBar(win_hwnd);
                            }
#endif
                        } else if(id == MENU_MINIMIZE) SDL_MinimizeWindow(win);
                    }
                }
            }
#endif

            if(e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;
                if((key==SDLK_o)&&(e.key.keysym.mod & KMOD_CTRL)) {
                    char* f = open_file_dialog(
                        (const char*[]){"*.mkv", "*.mp4"}, 2, "Video Files (*.mkv, *.mp4)", false,
                        "Select Video File", NULL
                    );
                    if(f) {
                        video_file = f;
                        if(!vr) vr = vr_create(win, ren);
                        if(vr_load(vr, f)) {
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            add_recent_file(f);
                            SDL_SetWindowTitle(win, f);
                            nob_log(NOB_INFO, "Loaded %s", f);
                        } else {
                            nob_log(NOB_ERROR, "Failed to load %s", f);
                        }
                    } else {
                        nob_log(NOB_INFO, "No file selected");
                    }
                    #if SAVE_FILE
                        fill_save_state_from_vr(vr, &save_state, video_file);
                    #endif
                } else if(key==SDLK_F4 && (e.key.keysym.mod & KMOD_ALT)) running=0;
                if(key==SDLK_F11 || (key==SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    fullscreen =! fullscreen;
                    if (fullscreen) {
                        SDL_GetWindowPosition(win, &windowed_x, &windowed_y);
                        SDL_GetWindowSize(win, &windowed_w, &windowed_h);
                        windowed_valid = 1;
                    }
                    SDL_SetWindowFullscreen(win, fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);
#ifdef _WIN32
                    if (win_hwnd) {
                        SetMenu(win_hwnd, fullscreen ? NULL : win_menu);
                        DrawMenuBar(win_hwnd);
                    }
#endif
                    if (!fullscreen) {
                        SDL_RestoreWindow(win);
                        if (windowed_valid) {
                            SDL_SetWindowPosition(win, windowed_x, windowed_y);
                            SDL_SetWindowSize(win, windowed_w, windowed_h);
                        }
                        SDL_MaximizeWindow(win);
                        maximized = true;
                    } else {
                        SDL_Rect bounds;
                        int display_index = SDL_GetWindowDisplayIndex(win);
                        if (display_index >= 0 && SDL_GetDisplayBounds(display_index, &bounds) == 0) {
                            SDL_SetWindowPosition(win, bounds.x, bounds.y);
                            SDL_SetWindowSize(win, bounds.w, bounds.h);
                        }
                        maximized = false;
                    }
                }
                if (key==SDLK_m && (e.key.keysym.mod & KMOD_ALT)) {
                    SDL_MinimizeWindow(win);
                }
                if (key == SDLK_ESCAPE) {
                    if (menu_open) {
                        menu_open = false;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        font_menu_open = false;
                        playback_menu_open = false;
                    } else if (fullscreen) {
                        fullscreen = false;
                        SDL_SetWindowFullscreen(win, 0);
#ifdef _WIN32
                        if (win_hwnd) {
                            SetMenu(win_hwnd, win_menu);
                            DrawMenuBar(win_hwnd);
                        }
#endif
                        if (windowed_valid) {
                            SDL_SetWindowPosition(win, windowed_x, windowed_y);
                            SDL_SetWindowSize(win, windowed_w, windowed_h);
                        }
                        SDL_MaximizeWindow(win);
                        maximized = true;
                    }
                }
                if (key == SDLK_SPACE) {
                    paused = !paused;
                    if (vr) vr_set_paused(vr, paused);
                    overlay_target = 1.0f;
                }
                if (key == SDLK_LEFT && vr) {
                    double t = vr_get_time(vr) - 5.0;
                    if (t < 0.0) t = 0.0;
                    vr_seek(vr, t);
                    snprintf(flash_text, sizeof(flash_text), "-5s");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_RIGHT && vr) {
                    double t = vr_get_time(vr) + 5.0;
                    double dur = vr_get_duration(vr);
                    if (dur > 0.0 && t > dur) t = dur;
                    vr_seek(vr, t);
                    snprintf(flash_text, sizeof(flash_text), "+5s");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_UP && vr) {
                    volume_percent = clampf(volume_percent + 5.0f, 0.0f, 200.0f);
                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                    snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_DOWN && vr) {
                    volume_percent = clampf(volume_percent - 5.0f, 0.0f, 200.0f);
                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                    snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_z && (e.key.keysym.mod & KMOD_CTRL) && vr) {
                    if (history_pos > 0) {
                        history_pos--;
                        vr_seek(vr, timestamp_history[history_pos]);
                        snprintf(flash_text, sizeof(flash_text), "Undo to %.2fs", timestamp_history[history_pos]);
                        nob_log(NOB_INFO, "Undo to %.2fs", timestamp_history[history_pos]);
                        flash_until = SDL_GetTicks() + 900;
                    }
                }
                if (key == SDLK_y && (e.key.keysym.mod & KMOD_CTRL) && vr) {
                    if (history_pos < history_count - 1) {
                        history_pos++;
                        vr_seek(vr, timestamp_history[history_pos]);
                        snprintf(flash_text, sizeof(flash_text), "Redo to %.2fs", timestamp_history[history_pos]);
                        nob_log(NOB_INFO, "Redo to %.2fs", timestamp_history[history_pos]);
                        flash_until = SDL_GetTicks() + 900;
                    }
                }
            }

            if (e.type == SDL_MOUSEWHEEL && menu_open) {
                int dy = e.wheel.y;
                if (audio_menu_open) {
                    int count = vr ? vr_get_audio_track_count(vr) : 0;
                    audio_scroll -= dy * 3;
                    int max_scroll = count > 10 ? count - 10 : 0;
                    audio_scroll = audio_scroll < 0 ? 0 : (audio_scroll > max_scroll ? max_scroll : audio_scroll);
                }
                if (subtitle_menu_open) {
                    int count = vr ? vr_get_subtitle_track_count(vr) : 0;
                    subtitle_scroll -= dy * 3;
                    int max_scroll = (count + 1) > 10 ? (count + 1) - 10 : 0;
                    subtitle_scroll = subtitle_scroll < 0 ? 0 : (subtitle_scroll > max_scroll ? max_scroll : subtitle_scroll);
                }
            }

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x;
                int my = e.button.y;
                bool click_processed = false;

                if (point_in_rect(mx, my, hamburger)) {
                    menu_open = !menu_open;
                    audio_menu_open = false;
                    subtitle_menu_open = false;
                    font_menu_open = false;
                    playback_menu_open = false;
                    click_processed = true;
                }
                else if (menu_open && point_in_rect(mx, my, menu_panel)) {
                    bool handled = false;
                    
                    if (point_in_rect(mx, my, audio_box)) {
                        audio_menu_open = !audio_menu_open;
                        subtitle_menu_open = false;
                        font_menu_open = false;
                        playback_menu_open = false;
                        handled = true;
                    } else if (point_in_rect(mx, my, subtitle_box)) {
                        subtitle_menu_open = !subtitle_menu_open;
                        audio_menu_open = false;
                        font_menu_open = false;
                        playback_menu_open = false;
                        handled = true;
                    } else if (point_in_rect(mx, my, font_box)) {
                        font_menu_open = !font_menu_open;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        playback_menu_open = false;
                        handled = true;
                    } else if (point_in_rect(mx, my, playback_box)) {
                        playback_menu_open = !playback_menu_open;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        font_menu_open = false;
                        handled = true;
                    }
                    
                    if (!handled) {
                        menu_open = false;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        font_menu_open = false;
                        playback_menu_open = false;
                    }
                    click_processed = true;
                }
                else if (menu_open) {
                    bool handled = false;
                    
                    if (audio_menu_open && !handled) {
                        int count = vr ? vr_get_audio_track_count(vr) : 0;
                        int max_items = MENU_MAX_VISIBLE_ITEMS;
                        int display_count = count > max_items ? max_items : count;
                        int item_h = MENU_DROPDOWN_ITEM_HEIGHT;
                        SDL_Rect list = { menu_panel.x - MENU_DROPDOWN_WIDTH, audio_box.y, MENU_DROPDOWN_WIDTH - (count > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h * display_count };
                        if (list.x < margin) list.x = margin;
                        if (point_in_rect(mx, my, list)) {
                            int item_y = (my - list.y) / item_h;
                            if (item_y >= 0 && item_y < display_count) {
                                int idx = audio_scroll + item_y;
                                if (vr && idx >= 0 && idx < count) vr_select_audio_track(vr, idx);
                            }
                            audio_menu_open = false;
                            handled = true;
                        }
                    }
                    
                    if (subtitle_menu_open && !handled) {
                        int count = vr ? vr_get_subtitle_track_count(vr) : 0;
                        int max_items = MENU_MAX_VISIBLE_ITEMS;
                        int total = count + 1;
                        int display_count = total > max_items ? max_items : total;
                        int item_h = MENU_DROPDOWN_ITEM_HEIGHT;
                        SDL_Rect list = {
                            menu_panel.x - MENU_DROPDOWN_WIDTH,
                            subtitle_box.y,
                            MENU_DROPDOWN_WIDTH - (total > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0),
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
                    
                    if (font_menu_open && !handled) {

                        int item_h = MENU_DROPDOWN_ITEM_HEIGHT;
                        int count = default_font_count + 1;

                        SDL_Rect list = {
                            menu_panel.x - MENU_DROPDOWN_WIDTH,
                            font_box.y,
                            MENU_DROPDOWN_WIDTH,
                            item_h * count
                        };

                        if (list.x < margin) list.x = margin;

                        if (point_in_rect(mx, my, list)) {

                            int idx = (my - list.y) / item_h;

                            if (idx >= 0 && idx < default_font_count) {
                                load_ui_font(default_fonts[idx].path,
                                            default_fonts[idx].name);
                            }
                            else if (idx == default_font_count) {

                                const char* font_file = open_file_dialog(
                                    (const char*[]){"*.ttf", "*.ttc", "*.otf"}, 3, "Font Files (*.ttf, *.ttc, *.otf)", false,
                                    "Select Font File", NULL
                                );

                                if (font_file) load_ui_font(font_file,"Custom");
                            }

                            font_menu_open = false;
                            handled = true;
                        }
                    }

                    if (playback_menu_open && !handled) {
                        int item_h = MENU_DROPDOWN_ITEM_HEIGHT;
                        SDL_Rect list = { menu_panel.x - MENU_DROPDOWN_WIDTH, playback_box.y, MENU_DROPDOWN_WIDTH, item_h * 8 };
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
                    
                    if (!handled && !point_in_rect(mx, my, menu_panel)) {
                        menu_open = false;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        font_menu_open = false;
                        playback_menu_open = false;
                    }
                    click_processed = true;
                }
                else if (point_in_rect(mx, my, overlay_rect)) {
                    overlay_target = 1.0f;
                    if (point_in_rect(mx, my, timeline_hitbox)) {
                        double dur = vr ? vr_get_duration(vr) : 0.0;
                        if (dur > 0.0 && vr) {
                            double t = (double)(mx - timeline_rect.x) / (double)timeline_rect.w;
                            t = clampf((float)t, 0.0f, 1.0f);
                            drag_time = t * dur;
                            dragging_timeline = true;
                        }
                        click_processed = true;
                    } else if (point_in_rect(mx, my, volume_rect)) {
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
                if (dragging_timeline && vr) {
                    double dur = vr_get_duration(vr);
                    double seek_time = clampf((float)drag_time, 0.0f, (float)dur);
                    if (history_pos < history_count) {
                        history_count = history_pos;
                    }
                    if (history_count < MAX_HISTORY) {
                        timestamp_history[history_count] = vr_get_time(vr);
                        history_count++;
                        history_pos = history_count;
                    }
                    vr_seek(vr, seek_time);
                }
                dragging_timeline = false;
                volume_dragging = false;
            }

            if (e.type == SDL_MOUSEMOTION) {
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
                }
            }
        }

        if(!fullscreen && !maximized) { SDL_MaximizeWindow(win); maximized=true; }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last_tick) / 1000.0f;
        last_tick = now;
        if (!dragging_timeline && !volume_dragging && !menu_open && !audio_menu_open && !subtitle_menu_open && !font_menu_open && !playback_menu_open) {
            if (now - last_mouse_move > 3000) overlay_target = 0.0f;
        }
        
        overlay_alpha = lerpf(overlay_alpha, overlay_target, clampf(dt * 6.0f, 0.0f, 1.0f));

        SDL_SetRenderDrawColor(ren,0,0,0,255);
        SDL_RenderClear(ren);

        if(vr) {
            if (!paused) {
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
                            if (!vr_render_frame(vr)) break;
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
            SDL_Texture* tex = vr_get_texture(vr);
            if (tex) SDL_RenderCopy(ren, tex, NULL, NULL);
            if (vr_render_subtitles(vr, vr_get_time(vr))) {
                SDL_Texture* sub = vr_get_subtitle_texture(vr);
                if (sub) SDL_RenderCopy(ren, sub, NULL, NULL);
            }
        }

        if (overlay_alpha > 0.01f) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            int w, h;
            SDL_GetWindowSize(win, &w, &h);

            SDL_Color panel = { 20, 20, 24, (Uint8)(200 * overlay_alpha) };
            SDL_Color text = { 230, 230, 235, (Uint8)(255 * overlay_alpha) };
            SDL_Color accent = { 68, 160, 255, (Uint8)(230 * overlay_alpha) };
            SDL_Color muted = { 120, 120, 130, (Uint8)(200 * overlay_alpha) };

            draw_rect(ren, overlay_rect, panel);

            double cur = vr ? (dragging_timeline ? drag_time : vr_get_time(vr)) : 0.0;
            double dur = vr ? vr_get_duration(vr) : 0.0;
            float t = (dur > 0.0) ? (float)(cur / dur) : 0.0f;
            t = clampf(t, 0.0f, 1.0f);

            SDL_Rect base = timeline_rect;
            base.h = 6;
            draw_rect(ren, base, (SDL_Color){ 70, 70, 80, (Uint8)(180 * overlay_alpha) });
            SDL_Rect fill = { base.x, base.y, (int)(base.w * t), base.h };
            draw_rect(ren, fill, accent);
            SDL_Rect handle = { base.x + (int)(base.w * t) - 6, base.y - 4, 12, 12 };
            draw_rect(ren, handle, (SDL_Color){ 220, 220, 230, (Uint8)(220 * overlay_alpha) });

            char left_time[32];
            char right_time[32];
            format_time(cur, left_time, sizeof(left_time));
            format_time(dur, right_time, sizeof(right_time));
            draw_text_shadow(ren, margin, h - overlay_h + 24, left_time, text);
            int right_w = 0, right_h = 0;
            TTF_SizeUTF8(ui_font, right_time, &right_w, &right_h);
            draw_text_shadow(ren, w - margin - right_w - 40, h - overlay_h + 24, right_time, text);

            draw_rect(ren, volume_rect, (SDL_Color){ 70, 70, 80, (Uint8)(180 * overlay_alpha) });
            float vol_t = clampf(volume_percent / 200.0f, 0.0f, 1.0f);
            SDL_Rect vol_fill = { volume_rect.x, volume_rect.y + (int)(volume_rect.h * (1.0f - vol_t)), volume_rect.w, (int)(volume_rect.h * vol_t) };
            draw_rect(ren, vol_fill, accent);
            char vol_text[32];
            snprintf(vol_text, sizeof(vol_text), "%d%%", (int)volume_percent);
            draw_text_shadow(ren, volume_rect.x - 28, volume_rect.y + volume_rect.h + 6, vol_text, muted);

            draw_rect(ren, hamburger, (SDL_Color){ 35, 35, 45, (Uint8)(200 * overlay_alpha) });
            draw_text_shadow(ren, hamburger.x + 6, hamburger.y + 2, "≡", text);

            if (menu_open) {
                draw_rect(ren, menu_panel, (SDL_Color){ 28, 28, 36, (Uint8)(220 * overlay_alpha) });
                draw_rect(ren, audio_box, (SDL_Color){ 35, 35, 45, (Uint8)(200 * overlay_alpha) });
                draw_rect(ren, subtitle_box, (SDL_Color){ 35, 35, 45, (Uint8)(200 * overlay_alpha) });
                draw_rect(ren, font_box, (SDL_Color){ 35, 35, 45, (Uint8)(200 * overlay_alpha) });
                draw_rect(ren, playback_box, (SDL_Color){ 35, 35, 45, (Uint8)(200 * overlay_alpha) });

                char audio_label[160];
                if (vr && vr_get_audio_track_count(vr) > 0) {
                    snprintf(audio_label, sizeof(audio_label), "Audio: %s", vr_get_audio_track_name(vr, vr->current_audio));
                } else {
                    snprintf(audio_label, sizeof(audio_label), "Audio");
                }
                const char* sub_name = (vr && vr_get_subtitle_track_count(vr) > 0 && vr->current_subtitle >= 0) ? vr_get_subtitle_track_name(vr, vr->current_subtitle) : "Subtitles: Off";
                char font_label[160];
                snprintf(font_label, sizeof(font_label), "Font: %s", ui_font_label);
                char playback_label[160];
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%.6f", playback_speed);
                char *dot = strchr(tmp, '.');
                char *end = tmp + strlen(tmp) - 1;
                while (end > dot + 1 && *end == '0') *end-- = '\0';
                snprintf(playback_label, sizeof(playback_label), "Speed: %sx", tmp);
                draw_text_shadow(ren, audio_box.x + 8, audio_box.y + 3, audio_label, text);
                draw_text_shadow(ren, subtitle_box.x + 8, subtitle_box.y + 3, sub_name, text);
                draw_text_shadow(ren, font_box.x + 8, font_box.y + 3, font_label, text);
                draw_text_shadow(ren, playback_box.x + 8, playback_box.y + 3, playback_label, text);

                if (audio_menu_open && vr) {
                    int count = vr_get_audio_track_count(vr);
                    int max_items = MENU_MAX_VISIBLE_ITEMS;
                    int item_h = MENU_DROPDOWN_ITEM_HEIGHT;
                    int display_count = count > max_items ? max_items : count;
                    SDL_Rect list = { menu_panel.x - MENU_DROPDOWN_WIDTH, audio_box.y, MENU_DROPDOWN_WIDTH, item_h * display_count };
                    if (list.x < margin) list.x = margin;
                    
                    int max_scroll = count > max_items ? count - max_items : 0;
                    if (audio_scroll > max_scroll) audio_scroll = max_scroll;
                    if (audio_scroll < 0) audio_scroll = 0;
                    
                    draw_rect(ren, list, (SDL_Color){ 26, 26, 34, (Uint8)(220 * overlay_alpha) });
                    for (int i = 0; i < display_count; i++) {
                        int idx = audio_scroll + i;
                        SDL_Rect item = { list.x, list.y + i * item_h, list.w - (count > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                        if (vr->current_audio == idx) draw_rect(ren, item, (SDL_Color){ 40, 80, 120, (Uint8)(180 * overlay_alpha) });
                        draw_text_shadow(ren, item.x + MENU_DROPDOWN_TEXT_PADDING_X, item.y + MENU_DROPDOWN_TEXT_PADDING_Y, vr_get_audio_track_name(vr, idx), text);
                    }
                    
                    if (count > max_items) {
                        SDL_Rect scrollbar_bg = { list.x + list.w - MENU_DROPDOWN_SCROLLBAR_WIDTH, list.y, MENU_DROPDOWN_SCROLLBAR_WIDTH, list.h };
                        draw_rect(ren, scrollbar_bg, (SDL_Color){ 35, 35, 45, (Uint8)(200 * overlay_alpha) });
                        int scroll_h = (max_items * list.h) / count;
                        int scroll_y = list.y + (audio_scroll * list.h) / count;
                        SDL_Rect scrollbar = { list.x + list.w - MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_y, MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_h };
                        draw_rect(ren, scrollbar, (SDL_Color){ 80, 80, 100, (Uint8)(220 * overlay_alpha) });
                    }
                }

                if (subtitle_menu_open && vr) {
                    int count = vr_get_subtitle_track_count(vr);
                    int max_items = MENU_MAX_VISIBLE_ITEMS;
                    int item_h = MENU_DROPDOWN_ITEM_HEIGHT;
                    int display_count = (count + 1) > max_items ? max_items : (count + 1);
                    SDL_Rect list = { menu_panel.x - MENU_DROPDOWN_WIDTH, subtitle_box.y, MENU_DROPDOWN_WIDTH, item_h * display_count };
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
                    draw_rect(ren, list, (SDL_Color){ 26, 26, 34, (Uint8)(220 * overlay_alpha) });
                    if (subtitle_scroll == 0) {
                        SDL_Rect off_item = { list.x, list.y, list.w - ((count + 1) > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                        if (vr->current_subtitle < 0) draw_rect(ren, off_item, (SDL_Color){ 40, 80, 120, (Uint8)(180 * overlay_alpha) });
                        draw_text_shadow(ren, off_item.x + MENU_DROPDOWN_TEXT_PADDING_X, off_item.y + MENU_DROPDOWN_TEXT_PADDING_Y, "Subtitles: Off", text);
                        for (int i = 1; i < display_count; i++) {
                            int idx = (subtitle_scroll + i) - 1;
                            if (idx >= 0 && idx < count) {
                                SDL_Rect item = { list.x, list.y + i * item_h, list.w - ((count + 1) > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                                if (vr->current_subtitle == idx) draw_rect(ren, item, (SDL_Color){ 40, 80, 120, (Uint8)(180 * overlay_alpha) });
                                draw_text_shadow(ren, item.x + MENU_DROPDOWN_TEXT_PADDING_X, item.y + MENU_DROPDOWN_TEXT_PADDING_Y, vr_get_subtitle_track_name(vr, idx), text);
                            }
                        }
                    } else {
                        for (int i = 0; i < display_count; i++) {
                            int idx = (subtitle_scroll + i) - 1;
                            if (idx >= 0 && idx < count) {
                                SDL_Rect item = { list.x, list.y + i * item_h, list.w - ((count + 1) > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                                if (vr->current_subtitle == idx) draw_rect(ren, item, (SDL_Color){ 40, 80, 120, (Uint8)(180 * overlay_alpha) });
                                draw_text_shadow(ren, item.x + MENU_DROPDOWN_TEXT_PADDING_X, item.y + MENU_DROPDOWN_TEXT_PADDING_Y, vr_get_subtitle_track_name(vr, idx), text);
                            }
                        }
                    }
                    if ((count + 1) > max_items) {
                        SDL_Rect scrollbar_bg = { list.x + list.w - MENU_DROPDOWN_SCROLLBAR_WIDTH, list.y, MENU_DROPDOWN_SCROLLBAR_WIDTH, list.h };
                        draw_rect(ren, scrollbar_bg, (SDL_Color){ 35, 35, 45, (Uint8)(200 * overlay_alpha) });
                        int scroll_h = (max_items * list.h) / (count + 1);
                        int scroll_y = list.y + (subtitle_scroll * list.h) / (count + 1);
                        SDL_Rect scrollbar = { list.x + list.w - MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_y, MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_h };
                        draw_rect(ren, scrollbar, (SDL_Color){ 80, 80, 100, (Uint8)(220 * overlay_alpha) });
                    }
                }

                if (font_menu_open) {
                    int count = default_font_count + 1;
                    int item_h = MENU_DROPDOWN_ITEM_HEIGHT;

                    SDL_Rect list = {
                        menu_panel.x - MENU_DROPDOWN_WIDTH,
                        font_box.y,
                        MENU_DROPDOWN_WIDTH,
                        item_h * count
                    };

                    if (list.x < margin) list.x = margin;

                    draw_rect(ren, list, (SDL_Color){26,26,34,(Uint8)(220*overlay_alpha)});

                    for (int i = 0; i < default_font_count; ++i) {
                        draw_text_shadow(
                            ren,
                            list.x + MENU_DROPDOWN_TEXT_PADDING_X,
                            list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * i,
                            default_fonts[i].name,
                            text
                        );
                    }

                    draw_text_shadow(
                        ren,
                        list.x + MENU_DROPDOWN_TEXT_PADDING_X,
                        list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * default_font_count,
                        "Custom...",
                        text
                    );
                }

                if (playback_menu_open) {
                    int count = 8;
                    int item_h = MENU_DROPDOWN_ITEM_HEIGHT;
                    SDL_Rect list = { menu_panel.x - MENU_DROPDOWN_WIDTH, playback_box.y, MENU_DROPDOWN_WIDTH, item_h * count };
                    if (list.x < margin) list.x = margin;
                    if (list.y + list.h > h) {
                        list.y = h - list.h;
                        if (list.y < 0) list.y = 0;
                    }
                    draw_rect(ren, list, (SDL_Color){ 26, 26, 34, (Uint8)(220 * overlay_alpha) });
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y, "0.5x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h, "0.75x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 2, "1.0x (Normal)", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 3, "1.25x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 4, "1.5x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 5, "2.0x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 6, "3.0x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 7, "5.0x", text);
                }
            }
        }

        pause_alpha = lerpf(pause_alpha, paused ? 1.0f : 0.0f, clampf(dt * 6.0f, 0.0f, 1.0f));
        if (pause_alpha > 0.01f) {
            SDL_Color pcol = { 240, 240, 245, (Uint8)(255 * pause_alpha) };
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
                SDL_Color fcol = { 240, 240, 245, (Uint8)(220 * flash_alpha) };
                draw_text_shadow(ren, 20, 56, flash_text, fcol);
            }
        }

        if (vr && playback_speed > 2.0f) {
            SDL_Color acol = { 255, 180, 100, 200 };
            draw_text_shadow(ren, 20, 92, "Audio disabled at high speed", acol);
        }

        SDL_RenderPresent(ren);
    }

    #if SAVE_FILE
        fill_save_state_from_vr(vr, &save_state, video_file);
    #endif
    if (vr) vr_free(vr);
    if (ui_font) TTF_CloseFont(ui_font);
    TTF_Quit();
    for (int i = 0; i < recent_count; i++) free(recent_files[i]);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    #if SAVE_FILE
        if (!write_save_state(SAVE_FILE_PATH, &save_state)) {
            nob_log(NOB_ERROR, "Failed to write save state to %s", SAVE_FILE_PATH);
        } else {
            nob_log(NOB_INFO, "Save state written to %s", SAVE_FILE_PATH);
        }
        debug_save_state(&save_state);
        free_save_state(&save_state);
    #endif
    nob_log(NOB_INFO, "Exited.");
    return 0;
}