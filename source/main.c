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

#include "renderer.c"

#define MAX_RECENT 5
char* recent_files[MAX_RECENT] = {0};
int recent_count = 0;

#define MAX_HISTORY 100

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

static TTF_Font* ui_font = NULL;
static int ui_font_size = 18;
static char ui_font_label[128] = "Iosevka";
static char ui_font_path[260] = "";

static int load_ui_font(const char* path, const char* label) {
    if (!path || !path[0]) return 0;
    TTF_Font* font = TTF_OpenFont(path, ui_font_size);
    if (!font) return 0;
    if (ui_font) TTF_CloseFont(ui_font);
    ui_font = font;
    strncpy(ui_font_path, path, sizeof(ui_font_path) - 1);
    ui_font_path[sizeof(ui_font_path) - 1] = '\0';
    if (label) {
        strncpy(ui_font_label, label, sizeof(ui_font_label) - 1);
        ui_font_label[sizeof(ui_font_label) - 1] = '\0';
    }
    return 1;
}

static void draw_text(SDL_Renderer* ren, int x, int y, const char* text, SDL_Color color) {
    if (!text || !ui_font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(ui_font, text, color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_FreeSurface(surf);
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

static void draw_text_shadow(SDL_Renderer* ren, int x, int y, const char* text, SDL_Color color) {
    SDL_Color shadow = { 0, 0, 0, (Uint8)(color.a * 0.8f) };
    draw_text(ren, x + 2, y + 2, text, shadow);
    draw_text(ren, x, y, text, color);
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
    float min_db = -50.0f;
    float max_db = 20.0f * log10f(2.0f);
    float t = clampf(percent / 200.0f, 0.0f, 1.0f);
    float db = min_db + (max_db - min_db) * t;
    return powf(10.0f, db / 20.0f);
}

static float gain_to_volume_percent(float gain) {
    float min_db = -50.0f;
    float max_db = 20.0f * log10f(2.0f);
    float g = clampf(gain, 0.0001f, 2.0f);
    float db = 20.0f * log10f(g);
    float t = (db - min_db) / (max_db - min_db);
    return clampf(t * 200.0f, 0.0f, 200.0f);
}

static int point_in_rect(int x, int y, SDL_Rect r) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

static int is_supported_video_file(const char* path) {
    if (!path) return 0;
    const char* ext = strrchr(path, '.');
    if (!ext || !ext[1]) return 0;
#ifdef _WIN32
    return _stricmp(ext, ".mkv") == 0 || _stricmp(ext, ".mp4") == 0;
#else
    return strcasecmp(ext, ".mkv") == 0 || strcasecmp(ext, ".mp4") == 0;
#endif
}

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
}

void add_recent_file(const char* file) {
    for(int i = MAX_RECENT-1; i>0; i--) recent_files[i] = recent_files[i-1];
    recent_files[0] = strdup(file);
    if(recent_count < MAX_RECENT) recent_count++;
}

char* open_file_dialog() {
    const char* filters[] = { "*.mkv", "*.mp4" };
    char const* filename = tinyfd_openFileDialog(
        "Open Video File",
        "",
        2,
        filters,
        "Video Files",
        0
    );
    if (!filename) return NULL;
    if (!is_supported_video_file(filename)) return NULL;
    return strdup(filename);
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        nob_log(NOB_ERROR, "SDL_Init Error: %s", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        nob_log(NOB_ERROR, "TTF_Init Error: %s", TTF_GetError());
        return 1;
    }
    nob_log(NOB_INFO, "SDL and TTF initialized successfully");
#ifdef _WIN32
    if (!load_ui_font("assets/Iosevka-Regular.ttc", "Iosevka") &&
        !load_ui_font("C:/Windows/Fonts/iosevka.ttf", "Iosevka") &&
        !load_ui_font("C:/Windows/Fonts/seguiemj.ttf", "Segoe UI")) {
        load_ui_font("C:/Windows/Fonts/segoeui.ttf", "Segoe UI");
    }
#else
    load_ui_font("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVu Sans");
#endif
#ifdef _WIN32
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif

    VideoRenderer* vr = NULL;
    char* video_file = NULL;
    if(argc > 1 && is_supported_video_file(argv[1])) video_file = argv[1];

    SDL_Window* win = SDL_CreateWindow(
        "(no file selected)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED
    );

    if(!win) { nob_log(NOB_ERROR, "SDL_CreateWindow failed: %s", SDL_GetError()); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!ren) { SDL_DestroyWindow(win); nob_log(NOB_ERROR, "SDL_CreateRenderer failed: %s", SDL_GetError()); return 1; }

#ifdef _WIN32
    HMENU win_menu = create_windows_menu(win);
    HWND win_hwnd = get_hwnd(win);
#endif

    bool running = true;
    bool fullscreen = false;
    bool maximized = false;
    bool paused = false;
    bool was_paused = false;
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
    char flash_text[64] = {0};
    Uint32 flash_until = 0;
    float pause_alpha = 0.0f;
    int audio_scroll = 0;
    int subtitle_scroll = 0;
    int font_scroll = 0;
    int playback_scroll = 0;
    SDL_Event e;

    if(video_file) {
        vr = vr_create(win, ren);
        if(vr_load(vr, video_file)) {
            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
            add_recent_file(video_file);
            SDL_SetWindowTitle(win, video_file);
            nob_log(NOB_INFO, "Loaded %s", video_file);
        }
    }

    // Pre-calculate UI layout for both input and rendering
    SDL_Rect timeline_rect, volume_rect, hamburger, menu_panel, audio_box, subtitle_box, font_box, playback_box, overlay_rect;
    int overlay_h = 100;
    int margin = 24;
    int w, h;

    while(running) {
        // Update layout each frame based on window size
        {
            SDL_GetWindowSize(win, &w, &h);
            overlay_rect = (SDL_Rect){ 0, h - overlay_h, w, overlay_h };
            timeline_rect = (SDL_Rect){ margin, h - overlay_h + 12, w - margin * 2 - 40, 6 };
            volume_rect = (SDL_Rect){ w - margin - 32, h - overlay_h + 40, 6, 50 };
            hamburger = (SDL_Rect){ w - margin - 28, h - overlay_h + 12, 24, 20 };
            
            // Position menu panel to the LEFT of hamburger - right edge of menu touches left edge of hamburger
            int menu_x = hamburger.x - 250;  // Menu width is 250
            if (menu_x < margin) menu_x = margin;
            int menu_y = h - overlay_h - 180;  // Position above overlay
            if (menu_y < margin) menu_y = h - overlay_h + 12;  // Fallback below if not enough space above
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
                            char* f = open_file_dialog();
                            if(f) {
                                if(!vr) vr = vr_create(win, ren);
                                if(vr_load(vr, f)) {
                                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                                    add_recent_file(f);
                                    SDL_SetWindowTitle(win, f);
                                    nob_log(NOB_INFO, "Loaded %s", f);
                                } else {
                                    nob_log(NOB_ERROR, "Failed to load %s", f);
                                }
                                free(f);
                            }
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
                            SDL_SetWindowFullscreen(win, fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);
                            if (!fullscreen) SDL_MaximizeWindow(win);
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
                    char* f = open_file_dialog();
                    if(f) {
                        if(!vr) vr = vr_create(win, ren);
                        if(vr_load(vr, f)) {
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            add_recent_file(f);
                            SDL_SetWindowTitle(win, f);
                            nob_log(NOB_INFO, "Loaded %s", f);
                        } else {
                            nob_log(NOB_ERROR, "Failed to load %s", f);
                        }
                        free(f);
                    }
                } else if(key==SDLK_F4 && (e.key.keysym.mod & KMOD_ALT)) running=0;
                if(key==SDLK_F11 || (key==SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    fullscreen=!fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);
                    if (!fullscreen) SDL_MaximizeWindow(win);
#ifdef _WIN32
                    if (win_hwnd) {
                        SetMenu(win_hwnd, fullscreen ? NULL : win_menu);
                        DrawMenuBar(win_hwnd);
                    }
#endif
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
                        snprintf(flash_text, sizeof(flash_text), "Undo");
                        flash_until = SDL_GetTicks() + 900;
                    }
                }
                if (key == SDLK_y && (e.key.keysym.mod & KMOD_CTRL) && vr) {
                    if (history_pos < history_count - 1) {
                        history_pos++;
                        vr_seek(vr, timestamp_history[history_pos]);
                        snprintf(flash_text, sizeof(flash_text), "Redo");
                        flash_until = SDL_GetTicks() + 900;
                    }
                }
            }

            if (e.type == SDL_MOUSEWHEEL && menu_open) {
                int dy = e.wheel.y;  // positive = scroll up, negative = scroll down
                if (audio_menu_open) {
                    int count = vr ? vr_get_audio_track_count(vr) : 0;
                    audio_scroll -= dy * 3;  // Faster scrolling
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

                // Priority 1: Check hamburger button (can be outside overlay)
                if (point_in_rect(mx, my, hamburger)) {
                    menu_open = !menu_open;
                    audio_menu_open = false;
                    subtitle_menu_open = false;
                    font_menu_open = false;
                    playback_menu_open = false;
                    click_processed = true;
                }
                // Priority 2: Check menu panel clicks (can be outside overlay)
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
                // Priority 3: Check menu dropdown items
                else if (menu_open) {
                    bool handled = false;
                    
                    if (audio_menu_open && !handled) {
                        int count = vr ? vr_get_audio_track_count(vr) : 0;
                        int max_items = 10;
                        int display_count = count > max_items ? max_items : count;
                        int item_h = 28;
                        SDL_Rect list = { menu_panel.x - 250, audio_box.y, 250 - (count > max_items ? 12 : 0), item_h * display_count };
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
                        int max_items = 10;
                        // Add 1 for "Off" option
                        int total = count + 1;
                        int display_count = total > max_items ? max_items : total;
                        int item_h = 22;
                        SDL_Rect list = { menu_panel.x - 250, subtitle_box.y, 250 - (total > max_items ? 12 : 0), item_h * display_count };
                        if (list.x < margin) list.x = margin;
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
                        int item_h = 28;
                        // Position dropdown to the LEFT of the menu panel
                        SDL_Rect list = { menu_panel.x - 250, font_box.y, 250, item_h * 4 };
                        if (list.x < margin) list.x = margin;
                        if (point_in_rect(mx, my, list)) {
                            int idx = (my - list.y) / item_h;
                            if (idx >= 0 && idx < 4) {
                                if (idx == 0) load_ui_font("thirdparty/fonts/Iosevka-Regular.ttf", "Iosevka");
                                if (idx == 1) {
#ifdef _WIN32
                                    load_ui_font("C:/Windows/Fonts/segoeui.ttf", "Segoe UI");
#elif __APPLE__
                                    load_ui_font("/Library/Fonts/Arial.ttf", "Arial");
#else
                                    load_ui_font("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVu Sans");
#endif
                                }
                                if (idx == 2) {
#ifdef _WIN32
                                    load_ui_font("C:/Windows/Fonts/consola.ttf", "Consolas");
#elif __APPLE__
                                    load_ui_font("/Library/Fonts/Monaco.ttf", "Monaco");
#else
                                    load_ui_font("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "DejaVu Mono");
#endif
                                }
                                if (idx == 3) {
                                    const char* ff[] = { "*.ttf", "*.otf" };
                                    char const* font_file = tinyfd_openFileDialog("Select Font", "", 2, ff, "Fonts", 0);
                                    if (font_file) load_ui_font(font_file, "Custom");
                                }
                            }
                            font_menu_open = false;
                            handled = true;
                        }
                    }
                    
                    if (playback_menu_open && !handled) {
                        int item_h = 28;
                        // Position dropdown to the LEFT of the menu panel
                        SDL_Rect list = { menu_panel.x - 250, playback_box.y, 250, item_h * 6 };
                        if (list.x < margin) list.x = margin;
                        if (point_in_rect(mx, my, list)) {
                            int idx = (my - list.y) / item_h;
                            float speeds[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
                            if (idx >= 0 && idx < 6) {
                                float old_speed = playback_speed;
                                playback_speed = speeds[idx];
                                if (vr && fabs(old_speed - playback_speed) > 0.01f) {
                                    SDL_ClearQueuedAudio(vr->audio_dev);
                                }
                            }
                            playback_menu_open = false;
                            handled = true;
                        }
                    }
                    
                    // If clicked outside menu, close it
                    if (!handled && !point_in_rect(mx, my, menu_panel)) {
                        menu_open = false;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        font_menu_open = false;
                        playback_menu_open = false;
                    }
                    click_processed = true;
                }
                // Priority 4: Handle overlay controls
                else if (point_in_rect(mx, my, overlay_rect)) {
                    overlay_target = 1.0f;
                    if (point_in_rect(mx, my, timeline_rect)) {
                        // Click on timeline to seek directly
                        double dur = vr ? vr_get_duration(vr) : 0.0;
                        if (dur > 0.0 && vr) {
                            double t = (double)(mx - timeline_rect.x) / (double)timeline_rect.w;
                            t = clampf((float)t, 0.0f, 1.0f);
                            double seek_time = t * dur;
                            // Add to history
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
                        click_processed = true;
                    } else if (point_in_rect(mx, my, volume_rect)) {
                        volume_dragging = true;
                        click_processed = true;
                    }
                } else {
                    // Clicked outside overlay (on video) - toggle pause if menu is closed
                    if (!menu_open && !click_processed) {
                        paused = !paused;
                        if (vr) vr_set_paused(vr, paused);
                    }
                }
            }

            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                dragging_timeline = false;
                volume_dragging = false;
            }

            if (e.type == SDL_MOUSEMOTION) {
                if (volume_dragging && vr) {
                    int my = e.motion.y;
                    float t = (float)(volume_rect.y + volume_rect.h - my) / (float)volume_rect.h;
                    volume_percent = clampf(t, 0.0f, 1.0f) * 200.0f;
                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                }
            }
        }

        if(!maximized) { SDL_MaximizeWindow(win); maximized=1; }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last_tick) / 1000.0f;
        last_tick = now;
        if (!dragging_timeline && !volume_dragging && !menu_open && !audio_menu_open && !subtitle_menu_open && !font_menu_open && !playback_menu_open) {
            if (now - last_mouse_move > 3000) overlay_target = 0.0f;
        }
        
        if (vr) vr->playback_speed = playback_speed;
        overlay_alpha = lerpf(overlay_alpha, overlay_target, clampf(dt * 6.0f, 0.0f, 1.0f));

        SDL_SetRenderDrawColor(ren,0,0,0,255);
        SDL_RenderClear(ren);

        if(vr) {
            if (!paused) {
                vr_render_frame(vr);
                int base_delay = 16;  // ~60fps
                int speed_adjusted_delay = (int)(base_delay / (vr->playback_speed > 0 ? vr->playback_speed : 1.0f));
                SDL_Delay(speed_adjusted_delay);
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
                snprintf(playback_label, sizeof(playback_label), "Speed: %.1fx", playback_speed);
                draw_text_shadow(ren, audio_box.x + 8, audio_box.y + 3, audio_label, text);
                draw_text_shadow(ren, subtitle_box.x + 8, subtitle_box.y + 3, sub_name, text);
                draw_text_shadow(ren, font_box.x + 8, font_box.y + 3, font_label, text);
                draw_text_shadow(ren, playback_box.x + 8, playback_box.y + 3, playback_label, text);

                if (audio_menu_open && vr) {
                    int count = vr_get_audio_track_count(vr);
                    int max_items = 10;
                    int item_h = 22;
                    int display_count = count > max_items ? max_items : count;
                    SDL_Rect list = { menu_panel.x - 230, audio_box.y, 230, item_h * display_count };
                    if (list.x < margin) list.x = margin;
                    
                    // Clamp scroll
                    int max_scroll = count > max_items ? count - max_items : 0;
                    if (audio_scroll > max_scroll) audio_scroll = max_scroll;
                    if (audio_scroll < 0) audio_scroll = 0;
                    
                    draw_rect(ren, list, (SDL_Color){ 26, 26, 34, (Uint8)(220 * overlay_alpha) });
                    for (int i = 0; i < display_count; i++) {
                        int idx = audio_scroll + i;
                        SDL_Rect item = { list.x, list.y + i * item_h, list.w - (count > max_items ? 12 : 0), item_h };
                        if (vr->current_audio == idx) draw_rect(ren, item, (SDL_Color){ 40, 80, 120, (Uint8)(180 * overlay_alpha) });
                        draw_text_shadow(ren, item.x + 8, item.y + 2, vr_get_audio_track_name(vr, idx), text);
                    }
                    
                    // Draw scrollbar if needed
                    if (count > max_items) {
                        SDL_Rect scrollbar_bg = { list.x + list.w - 10, list.y, 10, list.h };
                        draw_rect(ren, scrollbar_bg, (SDL_Color){ 35, 35, 45, (Uint8)(200 * overlay_alpha) });
                        int scroll_h = (max_items * list.h) / count;
                        int scroll_y = list.y + (audio_scroll * list.h) / count;
                        SDL_Rect scrollbar = { list.x + list.w - 10, scroll_y, 10, scroll_h };
                        draw_rect(ren, scrollbar, (SDL_Color){ 80, 80, 100, (Uint8)(220 * overlay_alpha) });
                    }
                }

                if (subtitle_menu_open && vr) {
                    int count = vr_get_subtitle_track_count(vr);
                    int max_items = 10;
                    int item_h = 22;
                    int display_count = (count + 1) > max_items ? max_items : (count + 1);  // +1 for "Off" option
                    SDL_Rect list = { menu_panel.x - 230, subtitle_box.y, 230, item_h * display_count };
                    if (list.x < margin) list.x = margin;
                    
                    // Clamp scroll
                    int max_scroll = (count + 1) > max_items ? (count + 1) - max_items : 0;
                    if (subtitle_scroll > max_scroll) subtitle_scroll = max_scroll;
                    if (subtitle_scroll < 0) subtitle_scroll = 0;
                    
                    draw_rect(ren, list, (SDL_Color){ 26, 26, 34, (Uint8)(220 * overlay_alpha) });
                    
                    // Draw "Off" option if scrolled to top
                    if (subtitle_scroll == 0) {
                        SDL_Rect off_item = { list.x, list.y, list.w - ((count + 1) > max_items ? 12 : 0), item_h };
                        if (vr->current_subtitle < 0) draw_rect(ren, off_item, (SDL_Color){ 40, 80, 120, (Uint8)(180 * overlay_alpha) });
                        draw_text_shadow(ren, off_item.x + 8, off_item.y + 2, "Subtitles: Off", text);
                        
                        for (int i = 1; i < display_count; i++) {
                            int idx = (subtitle_scroll + i) - 1;
                            if (idx >= 0 && idx < count) {
                                SDL_Rect item = { list.x, list.y + i * item_h, list.w - ((count + 1) > max_items ? 12 : 0), item_h };
                                if (vr->current_subtitle == idx) draw_rect(ren, item, (SDL_Color){ 40, 80, 120, (Uint8)(180 * overlay_alpha) });
                                draw_text_shadow(ren, item.x + 8, item.y + 2, vr_get_subtitle_track_name(vr, idx), text);
                            }
                        }
                    } else {
                        for (int i = 0; i < display_count; i++) {
                            int idx = (subtitle_scroll + i) - 1;
                            if (idx >= 0 && idx < count) {
                                SDL_Rect item = { list.x, list.y + i * item_h, list.w - ((count + 1) > max_items ? 12 : 0), item_h };
                                if (vr->current_subtitle == idx) draw_rect(ren, item, (SDL_Color){ 40, 80, 120, (Uint8)(180 * overlay_alpha) });
                                draw_text_shadow(ren, item.x + 8, item.y + 2, vr_get_subtitle_track_name(vr, idx), text);
                            }
                        }
                    }
                    
                    // Draw scrollbar if needed
                    if ((count + 1) > max_items) {
                        SDL_Rect scrollbar_bg = { list.x + list.w - 10, list.y, 10, list.h };
                        draw_rect(ren, scrollbar_bg, (SDL_Color){ 35, 35, 45, (Uint8)(200 * overlay_alpha) });
                        int scroll_h = (max_items * list.h) / (count + 1);
                        int scroll_y = list.y + (subtitle_scroll * list.h) / (count + 1);
                        SDL_Rect scrollbar = { list.x + list.w - 10, scroll_y, 10, scroll_h };
                        draw_rect(ren, scrollbar, (SDL_Color){ 80, 80, 100, (Uint8)(220 * overlay_alpha) });
                    }
                }

                if (font_menu_open) {
                    int count = 4;  // Iosevka, Segoe UI, Consolas, Custom
                    int item_h = 22;
                    SDL_Rect list = { menu_panel.x - 230, font_box.y, 230, item_h * count };
                    if (list.x < margin) list.x = margin;
                    draw_rect(ren, list, (SDL_Color){ 26, 26, 34, (Uint8)(220 * overlay_alpha) });
                    draw_text_shadow(ren, list.x + 8, list.y + 2, "Iosevka (bundled)", text);
                    draw_text_shadow(ren, list.x + 8, list.y + 2 + item_h, "Segoe UI", text);
                    draw_text_shadow(ren, list.x + 8, list.y + 2 + item_h * 2, "Consolas", text);
                    draw_text_shadow(ren, list.x + 8, list.y + 2 + item_h * 3, "Custom...", text);
                }

                if (playback_menu_open) {
                    int count = 6;  // 0.5x to 2.0x
                    int item_h = 22;
                    SDL_Rect list = { menu_panel.x - 230, playback_box.y, 230, item_h * count };
                    if (list.x < margin) list.x = margin;
                    draw_rect(ren, list, (SDL_Color){ 26, 26, 34, (Uint8)(220 * overlay_alpha) });
                    draw_text_shadow(ren, list.x + 8, list.y + 2, "0.5x", text);
                    draw_text_shadow(ren, list.x + 8, list.y + 2 + item_h, "0.75x", text);
                    draw_text_shadow(ren, list.x + 8, list.y + 2 + item_h * 2, "1.0x (Normal)", text);
                    draw_text_shadow(ren, list.x + 8, list.y + 2 + item_h * 3, "1.25x", text);
                    draw_text_shadow(ren, list.x + 8, list.y + 2 + item_h * 4, "1.5x", text);
                    draw_text_shadow(ren, list.x + 8, list.y + 2 + item_h * 5, "2.0x", text);
                }
            }
        }

        pause_alpha = lerpf(pause_alpha, paused ? 1.0f : 0.0f, clampf(dt * 6.0f, 0.0f, 1.0f));
        if (pause_alpha > 0.01f) {
            SDL_Color pcol = { 240, 240, 245, (Uint8)(255 * pause_alpha) };
            draw_text_shadow(ren, 20, 20, "PAUSED", pcol);
        }

        if (flash_text[0] && SDL_GetTicks() < flash_until) {
            SDL_Color fcol = { 240, 240, 245, 220 };
            draw_text_shadow(ren, 20, 56, flash_text, fcol);
        }

        SDL_RenderPresent(ren);
    }

    if(vr) vr_free(vr);
    if(ui_font) TTF_CloseFont(ui_font);
    TTF_Quit();
    for(int i=0;i<recent_count;i++) free(recent_files[i]);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}