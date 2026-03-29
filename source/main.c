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
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#ifndef _WIN32
#include <strings.h>
#include <dirent.h>
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
static char hw_option[32] = "auto";

typedef struct {
    const char *name;
    const char *path;
} FontEntry;

typedef struct {
    char* media_path;
    double time_sec;
} PlaybackHistoryEntry;

static FontEntry default_fonts[] = DEFAULT_FONTS_MAP;
static const int default_font_count =
    sizeof(default_fonts) / sizeof(default_fonts[0]);

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

static void apply_subtitle_override(VideoRenderer* vr, uint32_t color_rgb, int size, int margin_bottom) {
    if (!vr) return;
    vr_set_subtitle_style_override(vr, color_rgb, size, margin_bottom);
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

    for (char *p = out; *p; ++p) {
        if (*p == '\\') *p = '/';
    }

#else
    char *tmp = realpath(path, NULL);
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
    static char buf[4096];
    if (!path) return NULL;
    if (!abspath(path, buf, 4096)) {
        return NULL;
    }
    return buf;
}

static char** _abspath_list = NULL;
static size_t _abspath_count = 0;
static size_t _abspath_cap = 0;

static char* abspath_temp_safe(const char* path) {
    if (!path) return NULL;

    char* buf = malloc(4096);
    if (!buf) return NULL;

    if (!abspath(path, buf, 4096) || !buf[0]) {
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
        char buf[4096];
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

static SDL_Rect compute_video_dst_rect(int win_w, int win_h, int src_w, int src_h) {
    SDL_Rect dst = {0, 0, win_w, win_h};
    if (win_w <= 0 || win_h <= 0 || src_w <= 0 || src_h <= 0) return dst;

    float sx = (float)win_w / (float)src_w;
    float sy = (float)win_h / (float)src_h;
    float s = sx < sy ? sx : sy;
    if (s > 1.0f) s = 1.0f;
    if (s <= 0.0f) return dst;

    dst.w = (int)(src_w * s);
    dst.h = (int)(src_h * s);
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
    float db;
    float t = percent / 100.0f;
    db = -50.0f * (1.0f - t);
    return powf(10.0f, db / 20.0f);
}

/*
static float gain_to_volume_percent(float gain) {
    if (gain <= 0.0f) return 0.0f;
    float db = 20.0f * log10f(gain);
    float t = (db + 50.0f) / 50.0f;
    return clampf(t * 100.0f, 0.0f, 200.0f);
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

    va_list args_print;
    va_copy(args_print, args);
    fprintf(stderr, "[%s] [%s] ", timebuf, level_str);
    vfprintf(stderr, fmt, args_print);
    va_end(args_print);
    fprintf(stderr, "\n");

    /* ensure logs are flushed immediately (help when running from some shells) */
    fflush(stderr);

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
    fprintf(out, "  --resolution [WxH|native]    Set window resolution (e.g. 1280x720 or native)\n");
    fprintf(out, "  --volume [0-200]             Set initial audio volume (default: 100)\n");
    fprintf(out, "  --speed [SPEED > 0]          Set initial playback speed (e.g. 0.5, 1.0, 1.5)\n");
    fprintf(out, "  --flash-debug                Show log messages as on-screen flash\n");
    fprintf(out, "  --no-flash-debug             Disable on-screen flash for log messages\n");
    fprintf(out, "  --flash-debug-level [LEVEL]  Show log messages as on-screen flash (LEVEL: 0 - NO LOGS, 1 - INFO, 2 - WARNING, 3 - ERROR)\n");
    fprintf(out, "  --hw [auto|accel|vaapi|dxva2|d3d11va|none]  Hardware decode backend (auto tries available)\n");
}

void add_recent_file(const char* file) {
    nob_log(NOB_INFO, "Adding to recent files: %s", file);
    char* new_file = strdup(file);

    if (recent_count == MAX_RECENT) {
        free(recent_files[MAX_RECENT - 1]);
        recent_count--;
    }
    for (int i = recent_count; i > 0; i--) {
        recent_files[i] = recent_files[i-1];
    }
    recent_files[0] = new_file;
    recent_count++;

    for (int i = 1; i < recent_count; i++) {
        if (strcmp(recent_files[i], file) == 0) {
            free(recent_files[i]);
            for (int j = i; j < recent_count - 1; j++) recent_files[j] = recent_files[j+1];
            recent_files[recent_count - 1] = NULL;
            recent_count--;
            break;
        }
    }

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
    return abs_path;
}

static int path_equals(const char* a, const char* b) {
    if (!a || !b) return 0;
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcmp(a, b) == 0;
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

    char dir_path[4096];
    if (dir_len >= sizeof(dir_path)) return 0;
    memcpy(dir_path, current_media_path, dir_len);
    dir_path[dir_len] = '\0';

    char** files = NULL;
    int count = 0;

#ifdef _WIN32
    char pattern[4096];
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
        char full[4096];
        size_t name_len = strlen(fd.cFileName);
        if (dir_len + 1 + name_len + 1 >= sizeof(full)) continue;
        memcpy(full, dir_path, dir_len);
        full[dir_len] = '\\';
        memcpy(full + dir_len + 1, fd.cFileName, name_len);
        full[dir_len + 1 + name_len] = '\0';
        if (!is_supported_video_file(full)) continue;
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
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
        if (!is_supported_video_file(full)) continue;
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

    int target_index = current_index + direction;
    if (target_index < 0 || target_index >= count) {
        if (!wrap) {
            free_path_list(files, count);
            return 0;
        }
        target_index = direction > 0 ? 0 : (count - 1);
    }

    if (target_index == current_index) {
        free_path_list(files, count);
        return 0;
    }

    *out_path = strdup(files[target_index]);
    free_path_list(files, count);
    return *out_path != NULL;
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
    int requested_window_w,
    int requested_window_h,
    const char* hw_option
) {
    if (!vr || !win || !ren || !video_file || !path) return 0;

    char* abs_path = abspath_temp_safe(path);
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

    vr_set_volume(*vr, volume_percent_to_gain(volume_percent));
    apply_subtitle_override(*vr, subtitle_color, subtitle_size, subtitle_margin);
    apply_window_size_for_video(win, vr_get_texture(*vr), requested_window_w, requested_window_h);
    set_window_title_for_media(win, *vr, abs_path);
    add_recent_file(abs_path);

    if (*video_file) free(*video_file);
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
    }
}

static void history_clear_from(PlaybackHistoryEntry history[MAX_HISTORY], int* history_count, int from_index) {
    if (!history_count) return;
    if (from_index < 0) from_index = 0;
    if (from_index > *history_count) from_index = *history_count;
    for (int i = from_index; i < *history_count; i++) {
        free(history[i].media_path);
        history[i].media_path = NULL;
        history[i].time_sec = 0.0;
    }
    *history_count = from_index;
}

static void history_push(
    PlaybackHistoryEntry history[MAX_HISTORY],
    int* history_count,
    int* history_pos,
    const char* media_path,
    double time_sec
) {
    if (!history_count || !history_pos || !media_path) return;

    if (*history_pos >= 0 && *history_pos < *history_count) {
        PlaybackHistoryEntry* cur = &history[*history_pos];
        if (cur->media_path && path_equals(cur->media_path, media_path) && fabs(cur->time_sec - time_sec) < 0.001) {
            return;
        }
    }

    if (*history_pos + 1 < *history_count) {
        history_clear_from(history, history_count, *history_pos + 1);
    }

    if (*history_count >= MAX_HISTORY) {
        free(history[0].media_path);
        memmove(&history[0], &history[1], sizeof(history[0]) * (MAX_HISTORY - 1));
        history[MAX_HISTORY - 1].media_path = NULL;
        history[MAX_HISTORY - 1].time_sec = 0.0;
        *history_count = MAX_HISTORY - 1;
        if (*history_pos > 0) (*history_pos)--;
    }

    history[*history_count].media_path = strdup(media_path);
    history[*history_count].time_sec = time_sec;
    *history_pos = *history_count;
    (*history_count)++;
}

static int history_apply_entry(
    VideoRenderer** vr,
    SDL_Window* win,
    SDL_Renderer* ren,
    char** video_file,
    int paused,
    const PlaybackHistoryEntry* entry,
    float volume_percent,
    uint32_t subtitle_color,
    int subtitle_size,
    int subtitle_margin,
    int requested_window_w,
    int requested_window_h
) {
    if (!entry || !entry->media_path) return 0;

    if (!video_file || !*video_file || !path_equals(*video_file, entry->media_path)) {
        if (!load_media_file(
                    vr, win, ren, video_file, entry->media_path,
                    volume_percent,
                    subtitle_color,
                    subtitle_size,
                    subtitle_margin,
                    requested_window_w,
                    requested_window_h,
                    hw_option)) {
            return 0;
        }
    }

    if (*vr) {
        vr_set_paused(*vr, paused);
        seek_and_preview_if_paused(*vr, entry->time_sec, paused);
    }
    return 1;
}

static int navigate_media(
    VideoRenderer** vr,
    SDL_Window* win,
    SDL_Renderer* ren,
    char** video_file,
    int direction,
    int paused,
    float volume_percent,
    uint32_t subtitle_color,
    int subtitle_size,
    int subtitle_margin,
    int wrap_when_boundary,
    int requested_window_w,
    int requested_window_h,
    PlaybackHistoryEntry history[MAX_HISTORY],
    int* history_count,
    int* history_pos
) {
    if (!vr || !*vr || !video_file || !*video_file) return 0;

    double before = vr_get_time(*vr);

    if (direction < 0 && before > 3.0) {
        history_push(history, history_count, history_pos, *video_file, before);
        seek_and_preview_if_paused(*vr, 0.0, paused);
        history_push(history, history_count, history_pos, *video_file, 0.0);
        snprintf(flash_text, sizeof(flash_text), "Restarted");
        flash_until = SDL_GetTicks() + 900;
        return 1;
    }

    char* target_path = NULL;
    if (!get_adjacent_supported_media(*video_file, direction, wrap_when_boundary, &target_path)) {
        snprintf(flash_text, sizeof(flash_text), "%s media", direction > 0 ? "No next" : "No previous");
        flash_until = SDL_GetTicks() + 900;
        return 0;
    }

    int old_count = *history_count;
    int old_pos = *history_pos;
    history_push(history, history_count, history_pos, *video_file, before);

    int ok = load_media_file(
        vr, win, ren, video_file, target_path,
        volume_percent,
        subtitle_color,
        subtitle_size,
        subtitle_margin,
        requested_window_w,
        requested_window_h,
        hw_option);
    free(target_path);

    if (!ok) {
        history_clear_from(history, history_count, old_count);
        *history_pos = old_pos;
        snprintf(flash_text, sizeof(flash_text), "Failed to load media");
        flash_until = SDL_GetTicks() + 900;
        return 0;
    }

    if (*vr) {
        vr_set_paused(*vr, paused);
        seek_and_preview_if_paused(*vr, 0.0, paused);
    }
    history_push(history, history_count, history_pos, *video_file, 0.0);
    snprintf(flash_text, sizeof(flash_text), "%s media", direction > 0 ? "Next" : "Previous");
    flash_until = SDL_GetTicks() + 900;
    return 1;
}

#ifdef _WIN32
static WNDPROC g_prev_menu_wndproc = NULL;
static VideoRenderer** g_menu_vr_ptr = NULL;
static SDL_Window* g_menu_win = NULL;
static SDL_Renderer* g_menu_ren = NULL;
static bool* g_menu_paused_ptr = NULL;
static float* g_menu_playback_speed_ptr = NULL;
static HMENU g_current_win_menu = NULL;

static void run_playback_tick(VideoRenderer* vr, float playback_speed);

static void menu_pump_playback_tick(void) {
    if (!g_menu_vr_ptr || !*g_menu_vr_ptr || !g_menu_win || !g_menu_ren || !g_menu_paused_ptr || !g_menu_playback_speed_ptr) return;

    VideoRenderer* vr = *g_menu_vr_ptr;
    if (*g_menu_paused_ptr) return;

    run_playback_tick(vr, *g_menu_playback_speed_ptr);

    SDL_SetRenderDrawColor(g_menu_ren, 0, 0, 0, 255);
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
        video_dst = compute_video_dst_rect(window_w, window_h, src_w, src_h);
        SDL_RenderCopy(g_menu_ren, tex, NULL, &video_dst);
    }

    if (vr_render_subtitles(vr, vr_get_time(vr))) {
        SDL_Texture* sub = vr_get_subtitle_texture(vr);
        if (sub) SDL_RenderCopy(g_menu_ren, sub, NULL, &video_dst);
    }

    SDL_RenderPresent(g_menu_ren);
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
    MENU_RESOLUTION_480P,
    MENU_RESOLUTION_720P,
    MENU_RESOLUTION_1080P,
    MENU_RESOLUTION_1440P,
    MENU_PLAY_PAUSE,
    MENU_NEXT_FRAME,
    MENU_PREV_FRAME,
    MENU_NEXT_MEDIA,
    MENU_PREV_MEDIA,
    MENU_NEXT_MEDIA_WRAP,
    MENU_PREV_MEDIA_WRAP,
    MENU_SEEK_BACK,
    MENU_SEEK_FORWARD,
    MENU_VOLUME_UP,
    MENU_VOLUME_DOWN,
    MENU_UNDO,
    MENU_REDO,
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
    HMENU hResolutionMenu = CreatePopupMenu();
    HMENU hPlaybackMenu = CreatePopupMenu();

    AppendMenu(hFileMenu, MF_STRING, MENU_OPEN, "Open File\tCtrl+O");

    if(recent_count > 0) {
        HMENU hRecent = CreatePopupMenu();
        for(int i=0; i<recent_count; i++) {
            AppendMenu(hRecent, MF_STRING, MENU_RECENT_BASE+i, recent_files[i]);
        }
        AppendMenu(hFileMenu, MF_POPUP, (UINT_PTR)hRecent, "Recent Files");
    }

    AppendMenu(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hFileMenu, MF_STRING, MENU_EXIT, "Exit\tAlt+F4");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "File");

    AppendMenu(hViewMenu, MF_STRING, MENU_FULLSCREEN, "Fullscreen\tF11");
    AppendMenu(hViewMenu, MF_STRING, MENU_MAXIMIZE, "Maximize\tF10");
    AppendMenu(hViewMenu, MF_STRING, MENU_MINIMIZE, "Minimize\tAlt+M");
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_NATIVE, "Native (Video)");
    AppendMenu(hResolutionMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_480P, "854x480");
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_720P, "1280x720");
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_1080P, "1920x1080");
    AppendMenu(hResolutionMenu, MF_STRING, MENU_RESOLUTION_1440P, "2560x1440");
    AppendMenu(hViewMenu, MF_POPUP, (UINT_PTR)hResolutionMenu, "Resolution");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, "View");

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
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_VOLUME_UP, "Volume Up\tUp");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_VOLUME_DOWN, "Volume Down\tDown");
    AppendMenu(hPlaybackMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_UNDO, "Undo\tCtrl+Z");
    AppendMenu(hPlaybackMenu, MF_STRING, MENU_REDO, "Redo\tCtrl+Y");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hPlaybackMenu, "Playback");

    SetMenu(hwnd, hMenu);
    DrawMenuBar(hwnd);
    return hMenu;
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

int main(int argc, char** argv) {
    nob_set_log_handler(amp_log_handler);

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
    bool dragging_timeline = false;
    bool volume_dragging = false;
    bool menu_open = false;
    bool audio_menu_open = false;
    bool subtitle_menu_open = false;
    bool font_menu_open = false;
    bool playback_menu_open = false;
    bool subtitle_settings_menu_open = false;
    bool subtitle_settings_value_menu_open = false;
    int subtitle_settings_value_menu_row = -1;
    double drag_time = 0.0;
    PlaybackHistoryEntry playback_history[MAX_HISTORY] = {0};
    int history_pos = -1;
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
    int subtitle_color_idx = 0;
    int subtitle_size_idx = 1;
    int subtitle_move_idx = 1;
    const int subtitle_color_count = (int)(sizeof(subtitle_override_colors) / sizeof(subtitle_override_colors[0]));
    const int subtitle_size_count = (int)(sizeof(subtitle_override_sizes) / sizeof(subtitle_override_sizes[0]));
    const int subtitle_move_count = (int)(sizeof(subtitle_override_margins) / sizeof(subtitle_override_margins[0]));
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
            video_file = abspath_temp_safe(argv[i]);
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
    nob_log(NOB_INFO, "Using '"THEME_NAME"' theme");
    nob_log(NOB_INFO, "HW option: %s", hw_option);
    
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
            "Select Video File", NULL, &is_supported_video_file
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
        requested_window_w > 0 ? requested_window_w : INITIAL_WINDOW_WIDTH,
        requested_window_h > 0 ? requested_window_h : INITIAL_WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS |
        (maximized ? SDL_WINDOW_MAXIMIZED : 0)
    );

    if(!win) { nob_log(NOB_ERROR, "SDL_CreateWindow failed: %s", SDL_GetError()); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!ren) { SDL_DestroyWindow(win); nob_log(NOB_ERROR, "SDL_CreateRenderer failed: %s", SDL_GetError()); return 1; }

    if (requested_window_w > 0 && requested_window_h > 0) {
        int startup_w = requested_window_w;
        int startup_h = requested_window_h;
        fit_window_to_display(&startup_w, &startup_h);
        SDL_SetWindowSize(win, startup_w, startup_h);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    // if (maximized && !fullscreen) {
    //     SDL_MaximizeWindow(win);
    // }

    if(video_file) {
        vr = vr_create(win, ren);
        if(vr_load(vr, video_file, hw_option)) {
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
        SaveState save_state = {0};
        if (!load_save_state(abspath_temp(SAVE_FILE_PATH), &save_state)) {
            nob_log(NOB_INFO, "No save file found, starting with default settings");
        } else {
            nob_log(NOB_INFO, "Save file loaded successfully");
            apply_save_state_to_vr(vr, &save_state, video_file);
            recent_count = (int)(save_state.recent_files_count > MAX_RECENT ? MAX_RECENT : save_state.recent_files_count);
            for (int i = 0; i < recent_count; i++) {
                recent_files[i] = save_state.recent_files[i] ? strdup(save_state.recent_files[i]) : NULL;
            }
        }
    #endif

    if (video_file) {
        add_recent_file(video_file);
    }

    if (vr && video_file) {
        history_push(playback_history, &history_count, &history_pos, video_file, vr_get_time(vr));
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
        g_prev_menu_wndproc = (WNDPROC)SetWindowLongPtr(win_hwnd, GWLP_WNDPROC, (LONG_PTR)amp_menu_wndproc);
    }
#endif

    SDL_Rect timeline_rect, timeline_hitbox, volume_rect, hamburger, menu_panel, audio_box, subtitle_box, font_box, playback_box, subtitle_settings_box, overlay_rect;
    int overlay_h = 100;
    int margin = 24;
    int w, h;

    while(running) {
        int subtitle_style_mode = vr ? vr_get_subtitle_style_mode(vr) : 0; /* 0 none, 1 text/srt, 2 ass/ssa */
        bool subtitle_settings_applicable = subtitle_style_mode == 1;
        int subtitle_settings_row_count = subtitle_style_mode == 1 ? 3 : 0;
        if (!subtitle_settings_applicable) {
            subtitle_settings_menu_open = false;
            subtitle_settings_value_menu_open = false;
            subtitle_settings_value_menu_row = -1;
        }

        {
            SDL_GetWindowSize(win, &w, &h);
            overlay_rect = (SDL_Rect){ 0, h - overlay_h, w, overlay_h };
            timeline_rect = (SDL_Rect){ margin, h - overlay_h + 12, w - margin * 2 - 40, TIMELINE_HEIGHT };
            timeline_hitbox = (SDL_Rect){ timeline_rect.x, timeline_rect.y - TIMELINE_HITBOX_PADDING, timeline_rect.w, TIMELINE_HEIGHT + TIMELINE_HITBOX_PADDING * 2 };
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
            font_box = (SDL_Rect){ row_x, row_y0 + row_step * 2, row_w, row_h };
            playback_box = (SDL_Rect){ row_x, row_y0 + row_step * 3, row_w, row_h };
            subtitle_settings_box = (SDL_Rect){ row_x, row_y0 + row_step * 4, row_w, row_h };
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
                                "Select Video File", NULL, &is_supported_video_file
                            );
                            if(f) {
                                video_file = f;
                                if(!vr) vr = vr_create(win, ren);
                                if(vr_load(vr, f, hw_option)) {
                                    fill_save_state_from_vr(vr, &save_state, video_file);
                                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                                    apply_subtitle_override(
                                        vr,
                                        subtitle_override_colors[subtitle_color_idx],
                                        subtitle_override_sizes[subtitle_size_idx],
                                        subtitle_override_margins[subtitle_move_idx]
                                    );
                                    apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
                                    add_recent_file(f);
                                    set_window_title_for_media(win, vr, f);
                                    nob_log(NOB_INFO, "Loaded %s", f);
                                    history_push(playback_history, &history_count, &history_pos, f, vr_get_time(vr));
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
                                char* selected_recent = abspath_temp_safe(recent_files[idx]);
                                if (!selected_recent) {
                                    nob_log(NOB_ERROR, "Failed to resolve recent file path: %s", recent_files[idx]);
                                    continue;
                                }
                                if (video_file) free(video_file);
                                video_file = selected_recent;
                                if(!vr) vr = vr_create(win, ren);
                                if(vr_load(vr, video_file, hw_option)) {
                                    fill_save_state_from_vr(vr, &save_state, video_file);
                                    vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                                    apply_subtitle_override(
                                        vr,
                                        subtitle_override_colors[subtitle_color_idx],
                                        subtitle_override_sizes[subtitle_size_idx],
                                        subtitle_override_margins[subtitle_move_idx]
                                    );
                                    apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
                                    add_recent_file(video_file);
                                    set_window_title_for_media(win, vr, video_file);
                                    nob_log(NOB_INFO, "Loaded %s", video_file);
                                    history_push(playback_history, &history_count, &history_pos, video_file, vr_get_time(vr));
                                } else {
                                    nob_log(NOB_ERROR, "Failed to load %s", video_file);
                                }
                            }
                        } else if(id == MENU_EXIT) running = false;
                        else if(id == MENU_FULLSCREEN) {
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
                        } else if(id == MENU_MINIMIZE) SDL_MinimizeWindow(win);
                        else if(id == MENU_MAXIMIZE) {
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

                                int target_w = requested_window_w;
                                int target_h = requested_window_h;
                                fit_window_to_display(&target_w, &target_h);
                                SDL_SetWindowSize(win, target_w, target_h);
                                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                            }
                        }
                        else if(id == MENU_PLAY_PAUSE) {
                            paused = !paused;
                            if (vr) vr_set_paused(vr, paused);
                            overlay_target = 1.0f;
                        } else if(id == MENU_NEXT_FRAME && vr) {
                            vr_next_frame(vr, 1);
                            if (video_file) history_push(playback_history, &history_count, &history_pos, video_file, vr_get_time(vr));
                        } else if(id == MENU_PREV_FRAME && vr) {
                            vr_next_frame(vr, -1);
                            if (video_file) history_push(playback_history, &history_count, &history_pos, video_file, vr_get_time(vr));
                        } else if(id == MENU_NEXT_MEDIA || id == MENU_PREV_MEDIA) {
                            navigate_media(
                                &vr,
                                win,
                                ren,
                                &video_file,
                                id == MENU_NEXT_MEDIA ? 1 : -1,
                                paused,
                                volume_percent,
                                subtitle_override_colors[subtitle_color_idx],
                                subtitle_override_sizes[subtitle_size_idx],
                                subtitle_override_margins[subtitle_move_idx],
                                0,
                                requested_window_w,
                                requested_window_h,
                                playback_history,
                                &history_count,
                                &history_pos
                            );
                        } else if(id == MENU_NEXT_MEDIA_WRAP || id == MENU_PREV_MEDIA_WRAP) {
                            navigate_media(
                                &vr,
                                win,
                                ren,
                                &video_file,
                                id == MENU_NEXT_MEDIA_WRAP ? 1 : -1,
                                paused,
                                volume_percent,
                                subtitle_override_colors[subtitle_color_idx],
                                subtitle_override_sizes[subtitle_size_idx],
                                subtitle_override_margins[subtitle_move_idx],
                                1,
                                requested_window_w,
                                requested_window_h,
                                playback_history,
                                &history_count,
                                &history_pos
                            );
                        } else if(id == MENU_SEEK_BACK && vr) {
                            double t = vr_get_time(vr) - 5.0;
                            if (t < 0.0) t = 0.0;
                            seek_and_preview_if_paused(vr, t, paused);
                            if (video_file) history_push(playback_history, &history_count, &history_pos, video_file, t);
                            snprintf(flash_text, sizeof(flash_text), "-5s");
                            flash_until = SDL_GetTicks() + 900;
                        } else if(id == MENU_SEEK_FORWARD && vr) {
                            double t = vr_get_time(vr) + 5.0;
                            double dur = vr_get_duration(vr);
                            if (dur > 0.0 && t > dur) t = dur;
                            seek_and_preview_if_paused(vr, t, paused);
                            if (video_file) history_push(playback_history, &history_count, &history_pos, video_file, t);
                            snprintf(flash_text, sizeof(flash_text), "+5s");
                            flash_until = SDL_GetTicks() + 900;
                        } else if(id == MENU_VOLUME_UP && vr) {
                            volume_percent = clampf(volume_percent + 5.0f, 0.0f, 200.0f);
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                            flash_until = SDL_GetTicks() + 900;
                        } else if(id == MENU_VOLUME_DOWN && vr) {
                            volume_percent = clampf(volume_percent - 5.0f, 0.0f, 200.0f);
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            snprintf(flash_text, sizeof(flash_text), "VOL %d", (int)volume_percent);
                            flash_until = SDL_GetTicks() + 900;
                        } else if(id == MENU_UNDO && vr) {
                            if (history_pos > 0) {
                                history_pos--;
                                PlaybackHistoryEntry* entry = &playback_history[history_pos];
                                if (history_apply_entry(
                                        &vr,
                                        win,
                                        ren,
                                        &video_file,
                                        paused,
                                        entry,
                                        volume_percent,
                                        subtitle_override_colors[subtitle_color_idx],
                                        subtitle_override_sizes[subtitle_size_idx],
                                        subtitle_override_margins[subtitle_move_idx],
                                        requested_window_w,
                                        requested_window_h)) {
                                    snprintf(flash_text, sizeof(flash_text), "Undo: %s %.2fs", strrchr(entry->media_path, '\\') ? strrchr(entry->media_path, '\\') + 1 : entry->media_path, entry->time_sec);
                                } else {
                                    snprintf(flash_text, sizeof(flash_text), "Undo failed");
                                }
                                flash_until = SDL_GetTicks() + 900;
                            }
                        } else if(id == MENU_REDO && vr) {
                            if (history_pos < history_count - 1) {
                                history_pos++;
                                PlaybackHistoryEntry* entry = &playback_history[history_pos];
                                if (history_apply_entry(
                                        &vr,
                                        win,
                                        ren,
                                        &video_file,
                                        paused,
                                        entry,
                                        volume_percent,
                                        subtitle_override_colors[subtitle_color_idx],
                                        subtitle_override_sizes[subtitle_size_idx],
                                        subtitle_override_margins[subtitle_move_idx],
                                        requested_window_w,
                                        requested_window_h)) {
                                    snprintf(flash_text, sizeof(flash_text), "Redo: %s %.2fs", strrchr(entry->media_path, '\\') ? strrchr(entry->media_path, '\\') + 1 : entry->media_path, entry->time_sec);
                                } else {
                                    snprintf(flash_text, sizeof(flash_text), "Redo failed");
                                }
                                flash_until = SDL_GetTicks() + 900;
                            }
                        }
                    }
                }
            }
#endif

            if(e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;
                if((key==SDLK_o)&&(e.key.keysym.mod & KMOD_CTRL)) {
                    char* f = open_file_dialog(
                        (const char*[]){"*.mkv", "*.mp4"}, 2, "Video Files (*.mkv, *.mp4)", false,
                        "Select Video File", NULL, &is_supported_video_file
                    );
                    if(f) {
                        video_file = f;
                        if(!vr) vr = vr_create(win, ren);
                        if(vr_load(vr, f, hw_option)) {
                            fill_save_state_from_vr(vr, &save_state, video_file);
                            vr_set_volume(vr, volume_percent_to_gain(volume_percent));
                            apply_subtitle_override(
                                vr,
                                subtitle_override_colors[subtitle_color_idx],
                                subtitle_override_sizes[subtitle_size_idx],
                                subtitle_override_margins[subtitle_move_idx]
                            );
                            apply_window_size_for_video(win, vr_get_texture(vr), requested_window_w, requested_window_h);
                            add_recent_file(f);
                            set_window_title_for_media(win, vr, f);
                            nob_log(NOB_INFO, "Loaded %s", f);
                            history_push(playback_history, &history_count, &history_pos, f, vr_get_time(vr));
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
                        maximized = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
                        SDL_GetWindowPosition(win, &windowed_x, &windowed_y);
                        SDL_GetWindowSize(win, &windowed_w, &windowed_h);
                        windowed_valid = 1;
                    }
                    SDL_SetWindowFullscreen(win, fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);
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
                    vr_next_frame(vr, -1);
                    if (video_file) history_push(playback_history, &history_count, &history_pos, video_file, vr_get_time(vr));
                }
                if (key == SDLK_RIGHT && (e.key.keysym.mod & KMOD_ALT) && vr) {
                    sprintf(flash_text, "Next Frame");
                    flash_until = SDL_GetTicks() + 900;
                    vr_next_frame(vr, 1);
                    if (video_file) history_push(playback_history, &history_count, &history_pos, video_file, vr_get_time(vr));
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
                            volume_percent,
                            subtitle_override_colors[subtitle_color_idx],
                            subtitle_override_sizes[subtitle_size_idx],
                            subtitle_override_margins[subtitle_move_idx],
                            1,
                            requested_window_w,
                            requested_window_h,
                            playback_history,
                            &history_count,
                            &history_pos
                        );
                    } else {
                    navigate_media(
                        &vr,
                        win,
                        ren,
                        &video_file,
                        1,
                        paused,
                        volume_percent,
                        subtitle_override_colors[subtitle_color_idx],
                        subtitle_override_sizes[subtitle_size_idx],
                        subtitle_override_margins[subtitle_move_idx],
                        0,
                        requested_window_w,
                        requested_window_h,
                        playback_history,
                        &history_count,
                        &history_pos
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
                            volume_percent,
                            subtitle_override_colors[subtitle_color_idx],
                            subtitle_override_sizes[subtitle_size_idx],
                            subtitle_override_margins[subtitle_move_idx],
                            1,
                            requested_window_w,
                            requested_window_h,
                            playback_history,
                            &history_count,
                            &history_pos
                        );
                    } else {
                    navigate_media(
                        &vr,
                        win,
                        ren,
                        &video_file,
                        -1,
                        paused,
                        volume_percent,
                        subtitle_override_colors[subtitle_color_idx],
                        subtitle_override_sizes[subtitle_size_idx],
                        subtitle_override_margins[subtitle_move_idx],
                        0,
                        requested_window_w,
                        requested_window_h,
                        playback_history,
                        &history_count,
                        &history_pos
                    );
                    }
                }
                if (key == SDLK_ESCAPE) {
                    if (menu_open) {
                        menu_open = false;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        font_menu_open = false;
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
                    double t = vr_get_time(vr) - 5.0;
                    if (t < 0.0) t = 0.0;
                    seek_and_preview_if_paused(vr, t, paused);
                    if (video_file) history_push(playback_history, &history_count, &history_pos, video_file, t);
                    snprintf(flash_text, sizeof(flash_text), "-5s");
                    flash_until = SDL_GetTicks() + 900;
                }
                if (key == SDLK_RIGHT && vr && !(e.key.keysym.mod & KMOD_ALT) && !(e.key.keysym.mod & KMOD_SHIFT)) {
                    double t = vr_get_time(vr) + 5.0;
                    double dur = vr_get_duration(vr);
                    if (dur > 0.0 && t > dur) t = dur;
                    seek_and_preview_if_paused(vr, t, paused);
                    if (video_file) history_push(playback_history, &history_count, &history_pos, video_file, t);
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
                        PlaybackHistoryEntry* entry = &playback_history[history_pos];
                        if (history_apply_entry(
                                &vr,
                                win,
                                ren,
                                &video_file,
                                paused,
                                entry,
                                volume_percent,
                                subtitle_override_colors[subtitle_color_idx],
                                subtitle_override_sizes[subtitle_size_idx],
                                subtitle_override_margins[subtitle_move_idx],
                                requested_window_w,
                                requested_window_h)) {
                            snprintf(flash_text, sizeof(flash_text), "Undo: %s %.2fs", strrchr(entry->media_path, '\\') ? strrchr(entry->media_path, '\\') + 1 : entry->media_path, entry->time_sec);
                            nob_log(NOB_INFO, "Undo to %s %.2fs", entry->media_path, entry->time_sec);
                        } else {
                            snprintf(flash_text, sizeof(flash_text), "Undo failed");
                        }
                        flash_until = SDL_GetTicks() + 900;
                    }
                }
                if (key == SDLK_y && (e.key.keysym.mod & KMOD_CTRL) && vr) {
                    if (history_pos < history_count - 1) {
                        history_pos++;
                        PlaybackHistoryEntry* entry = &playback_history[history_pos];
                        if (history_apply_entry(
                                &vr,
                                win,
                                ren,
                                &video_file,
                                paused,
                                entry,
                                volume_percent,
                                subtitle_override_colors[subtitle_color_idx],
                                subtitle_override_sizes[subtitle_size_idx],
                                subtitle_override_margins[subtitle_move_idx],
                                requested_window_w,
                                requested_window_h)) {
                            snprintf(flash_text, sizeof(flash_text), "Redo: %s %.2fs", strrchr(entry->media_path, '\\') ? strrchr(entry->media_path, '\\') + 1 : entry->media_path, entry->time_sec);
                            nob_log(NOB_INFO, "Redo to %s %.2fs", entry->media_path, entry->time_sec);
                        } else {
                            snprintf(flash_text, sizeof(flash_text), "Redo failed");
                        }
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
                        font_menu_open = false;
                        playback_menu_open = false;
                        subtitle_settings_menu_open = false;
                        subtitle_settings_value_menu_open = false;
                        subtitle_settings_value_menu_row = -1;
                        handled = true;
                    } else if (point_in_rect(mx, my, subtitle_box)) {
                        subtitle_menu_open = !subtitle_menu_open;
                        audio_menu_open = false;
                        font_menu_open = false;
                        playback_menu_open = false;
                        subtitle_settings_menu_open = false;
                        subtitle_settings_value_menu_open = false;
                        subtitle_settings_value_menu_row = -1;
                        handled = true;
                    } else if (point_in_rect(mx, my, font_box)) {
                        font_menu_open = !font_menu_open;
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
                        font_menu_open = false;
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
                            font_menu_open = false;
                            playback_menu_open = false;
                            handled = true;
                        }
                    }
                    
                    if (!handled) {
                        menu_open = false;
                        audio_menu_open = false;
                        subtitle_menu_open = false;
                        font_menu_open = false;
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
                                    "Select Font File", NULL, NULL
                                );

                                const char* result = "No font selected";
                                if (font_file) result = try_load_ui_font(font_file, "Custom");
                                if (result) {
                                    nob_log(NOB_ERROR, "Failed to load font: %s", result);
                                    snprintf(flash_text, sizeof(flash_text), "Failed to load font: %s", result);
                                    flash_until = SDL_GetTicks() + 1800;
                                } else {
                                    nob_log(NOB_INFO, "Loaded custom font: %s", font_file);
                                }
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

                    if (subtitle_settings_menu_open && !handled) {
                        int item_h = MENU_DROPDOWN_ITEM_HEIGHT;
                        SDL_Rect list = { menu_panel.x - MENU_DROPDOWN_WIDTH, subtitle_settings_box.y, MENU_DROPDOWN_WIDTH, item_h * subtitle_settings_row_count };
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

                        SDL_Rect value_list = { list.x - MENU_DROPDOWN_WIDTH - 8, list.y, MENU_DROPDOWN_WIDTH, item_h * value_count };
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
                        font_menu_open = false;
                        playback_menu_open = false;
                        subtitle_settings_menu_open = false;
                    }
                    click_processed = true;
                }
                else if (point_in_rect(mx, my, overlay_rect)) {
                    overlay_target = 1.0f;
                    if (point_in_rect(mx, my, timeline_hitbox)) {
                        double dur = vr ? vr_get_duration(vr) : 0.0;
                        if (dur > 0.0 && vr) {
                            int chapter_idx = -1;
                            double chapter_time = 0.0;
                            if (find_nearest_chapter(vr, dur, timeline_rect, mx, 6, &chapter_idx, &chapter_time)) {
                                seek_and_preview_if_paused(vr, chapter_time, paused);
                                if (video_file) {
                                    history_push(playback_history, &history_count, &history_pos, video_file, chapter_time);
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
                    seek_and_preview_if_paused(vr, seek_time, paused);
                    if (video_file) {
                        history_push(playback_history, &history_count, &history_pos, video_file, seek_time);
                    }
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

        Uint32 now = SDL_GetTicks();
        float dt = (now - last_tick) / 1000.0f;
        last_tick = now;
        if (!dragging_timeline && !volume_dragging && !menu_open && !audio_menu_open && !subtitle_menu_open && !font_menu_open && !playback_menu_open && !subtitle_settings_menu_open && !subtitle_settings_value_menu_open) {
            if (now - last_mouse_move > 3000) overlay_target = 0.0f;
        }
        
        overlay_alpha = lerpf(overlay_alpha, overlay_target, clampf(dt * 6.0f, 0.0f, 1.0f));

        SDL_SetRenderDrawColor(ren,0,0,0,255);
        SDL_RenderClear(ren);

        if(vr) {
            if (!paused) {
                run_playback_tick(vr, playback_speed);
            }
            SDL_Texture* tex = vr_get_texture(vr);
            int window_w = 0;
            int window_h = 0;
            SDL_GetWindowSize(win, &window_w, &window_h);
            SDL_Rect video_dst = {0, 0, window_w, window_h};
            if (tex) {
                int src_w = 0;
                int src_h = 0;
                SDL_QueryTexture(tex, NULL, NULL, &src_w, &src_h);
                video_dst = compute_video_dst_rect(window_w, window_h, src_w, src_h);
                SDL_RenderCopy(ren, tex, NULL, &video_dst);
            }
            if (vr_render_subtitles(vr, vr_get_time(vr))) {
                SDL_Texture* sub = vr_get_subtitle_texture(vr);
                if (sub) SDL_RenderCopy(ren, sub, NULL, &video_dst);
            }
        }

        if (overlay_alpha > 0.01f) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            int w, h;
            SDL_GetWindowSize(win, &w, &h);

            SDL_Color panel =  { PANEL_COLOR,  (Uint8)(200 * overlay_alpha) };
            SDL_Color text =   { TEXT_COLOR,   (Uint8)(255 * overlay_alpha) };
            SDL_Color accent = { ACCENT_COLOR, (Uint8)(230 * overlay_alpha) };
            SDL_Color muted =  { MUTED_COLOR,  (Uint8)(200 * overlay_alpha) };

            draw_rect(ren, overlay_rect, panel);

            double cur = vr ? (dragging_timeline ? drag_time : vr_get_time(vr)) : 0.0;
            double dur = vr ? vr_get_duration(vr) : 0.0;
            float t = (dur > 0.0) ? (float)(cur / dur) : 0.0f;
            t = clampf(t, 0.0f, 1.0f);

            SDL_Rect base = timeline_rect;
            base.h = 6;
            draw_rect(ren, base, (SDL_Color){ OVERLAY_COLOR, (Uint8)(180 * overlay_alpha) });
            if (vr && dur > 0.0) {
                int chapter_count = get_media_chapter_count(vr);
                for (int i = 0; i < chapter_count; i++) {
                    double chapter_time = get_media_chapter_time(vr, i);
                    if (chapter_time < 0.0 || chapter_time > dur) continue;
                    int chapter_x = base.x + (int)((chapter_time / dur) * base.w);
                    SDL_Rect chapter_line = { chapter_x, base.y, TIMELINE_CHAPTER_MARKER_WIDTH, base.h };
                    draw_rect(ren, chapter_line, (SDL_Color){ CHAPTER_MARKER_COLOR, (Uint8)(190 * overlay_alpha) });
                }
            }
            SDL_Rect fill = { base.x, base.y, (int)(base.w * t), base.h };
            draw_rect(ren, fill, accent);
            SDL_Rect handle = { base.x + (int)(base.w * t) - TIMELINE_THUMB_SIZE / 2, base.y - TIMELINE_THUMB_SIZE / 3, TIMELINE_THUMB_SIZE, TIMELINE_THUMB_SIZE };
            draw_rect(ren, handle, (SDL_Color){ TIMELINE_THUMB_COLOR, (Uint8)(220 * overlay_alpha) });

            if (vr) {
                const char* media_title = get_media_title(vr);
                if (!media_title) media_title = "";
                draw_text_shadow(ren, margin, base.y - 45, media_title, (SDL_Color){ MEDIA_TITLE_COLOR, (Uint8)(220 * overlay_alpha) });
            }

            if (vr && dur > 0.0) {
                int mouse_x = 0;
                int mouse_y = 0;
                SDL_GetMouseState(&mouse_x, &mouse_y);
                if (point_in_rect(mouse_x, mouse_y, timeline_hitbox)) {
                    int chapter_idx = -1;
                    double chapter_time = 0.0;
                    if (find_nearest_chapter(vr, dur, timeline_rect, mouse_x, TIMELINE_CHAPTER_MARKER_HITBOX_WIDTH, &chapter_idx, &chapter_time)) {
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
                        draw_rect(ren, hover_bg, (SDL_Color){ CHAPTER_HOVER_BG_COLOR, (Uint8)(220 * overlay_alpha) });
                        draw_text_shadow(ren, hover_x, hover_y, hover_text, text);
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

            draw_rect(ren, volume_rect, (SDL_Color){ OVERLAY_COLOR, (Uint8)(180 * overlay_alpha) });
            float vol_t = clampf(volume_percent / 200.0f, 0.0f, 1.0f);
            SDL_Rect vol_fill = { volume_rect.x, volume_rect.y + (int)(volume_rect.h * (1.0f - vol_t)), volume_rect.w, (int)(volume_rect.h * vol_t) };
            draw_rect(ren, vol_fill, accent);
            char vol_text[32];
            snprintf(vol_text, sizeof(vol_text), "%d%%", (int)volume_percent);
            draw_text_shadow(ren, volume_rect.x - 28, volume_rect.y + volume_rect.h + 6, vol_text, muted);

            draw_rect(ren, hamburger, (SDL_Color){ HAMBURGER_BG_COLOR, (Uint8)(200 * overlay_alpha) });
            draw_text_shadow(ren, hamburger.x + 6, hamburger.y + 2, "≡", text);

            if (menu_open) {
                draw_rect(ren, menu_panel, (SDL_Color){ MENU_PANEL_BG_COLOR, (Uint8)(220 * overlay_alpha) });
                draw_rect(ren, audio_box, (SDL_Color){ MENU_PANEL_ITEM_BG_COLOR, (Uint8)(200 * overlay_alpha) });
                draw_rect(ren, subtitle_box, (SDL_Color){ MENU_PANEL_ITEM_BG_COLOR, (Uint8)(200 * overlay_alpha) });
                draw_rect(ren, font_box, (SDL_Color){ MENU_PANEL_ITEM_BG_COLOR, (Uint8)(200 * overlay_alpha) });
                draw_rect(ren, playback_box, (SDL_Color){ MENU_PANEL_ITEM_BG_COLOR, (Uint8)(200 * overlay_alpha) });
                if (subtitle_settings_applicable) {
                    draw_rect(ren, subtitle_settings_box, (SDL_Color){ MENU_PANEL_ITEM_BG_COLOR, (Uint8)(200 * overlay_alpha) });
                }

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
                char subtitle_settings_label[200];
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%.6f", playback_speed);
                char *dot = strchr(tmp, '.');
                char *end = tmp + strlen(tmp) - 1;
                while (end > dot + 1 && *end == '0') *end-- = '\0';
                snprintf(playback_label, sizeof(playback_label), "Speed: %sx", tmp);
                snprintf(subtitle_settings_label, sizeof(subtitle_settings_label), "Subtitle Style...");
                draw_text_shadow(ren, audio_box.x + 8, audio_box.y + 3, audio_label, text);
                draw_text_shadow(ren, subtitle_box.x + 8, subtitle_box.y + 3, sub_name, text);
                draw_text_shadow(ren, font_box.x + 8, font_box.y + 3, font_label, text);
                draw_text_shadow(ren, playback_box.x + 8, playback_box.y + 3, playback_label, text);
                if (subtitle_settings_applicable) {
                    draw_text_shadow(ren, subtitle_settings_box.x + 8, subtitle_settings_box.y + 3, subtitle_settings_label, text);
                }

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
                    
                    draw_rect(ren, list, (SDL_Color){ LIST_BG_COLOR, (Uint8)(220 * overlay_alpha) });
                    for (int i = 0; i < display_count; i++) {
                        int idx = audio_scroll + i;
                        SDL_Rect item = { list.x, list.y + i * item_h, list.w - (count > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                        if (vr->current_audio == idx) draw_rect(ren, item, (SDL_Color){ LIST_ITEM_BG_COLOR, (Uint8)(180 * overlay_alpha) });
                        draw_text_shadow(ren, item.x + MENU_DROPDOWN_TEXT_PADDING_X, item.y + MENU_DROPDOWN_TEXT_PADDING_Y, vr_get_audio_track_name(vr, idx), text);
                    }
                    
                    if (count > max_items) {
                        SDL_Rect scrollbar_bg = { list.x + list.w - MENU_DROPDOWN_SCROLLBAR_WIDTH, list.y, MENU_DROPDOWN_SCROLLBAR_WIDTH, list.h };
                        draw_rect(ren, scrollbar_bg, (SDL_Color){ SCROLLBAR_BG_COLOR, (Uint8)(200 * overlay_alpha) });
                        int scroll_h = (max_items * list.h) / count;
                        int scroll_y = list.y + (audio_scroll * list.h) / count;
                        SDL_Rect scrollbar = { list.x + list.w - MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_y, MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_h };
                        draw_rect(ren, scrollbar, (SDL_Color){ SCROLLBAR_THUMB_COLOR, (Uint8)(220 * overlay_alpha) });
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
                    draw_rect(ren, list, (SDL_Color){ LIST_BG_COLOR, (Uint8)(220 * overlay_alpha) });
                    if (subtitle_scroll == 0) {
                        SDL_Rect off_item = { list.x, list.y, list.w - ((count + 1) > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                        if (vr->current_subtitle < 0) draw_rect(ren, off_item, (SDL_Color){ LIST_ITEM_BG_COLOR, (Uint8)(180 * overlay_alpha) });
                        draw_text_shadow(ren, off_item.x + MENU_DROPDOWN_TEXT_PADDING_X, off_item.y + MENU_DROPDOWN_TEXT_PADDING_Y, "Subtitles: Off", text);
                        for (int i = 1; i < display_count; i++) {
                            int idx = (subtitle_scroll + i) - 1;
                            if (idx >= 0 && idx < count) {
                                SDL_Rect item = { list.x, list.y + i * item_h, list.w - ((count + 1) > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                                if (vr->current_subtitle == idx) draw_rect(ren, item, (SDL_Color){ LIST_ITEM_BG_COLOR, (Uint8)(180 * overlay_alpha) });
                                draw_text_shadow(ren, item.x + MENU_DROPDOWN_TEXT_PADDING_X, item.y + MENU_DROPDOWN_TEXT_PADDING_Y, vr_get_subtitle_track_name(vr, idx), text);
                            }
                        }
                    } else {
                        for (int i = 0; i < display_count; i++) {
                            int idx = (subtitle_scroll + i) - 1;
                            if (idx >= 0 && idx < count) {
                                SDL_Rect item = { list.x, list.y + i * item_h, list.w - ((count + 1) > max_items ? MENU_DROPDOWN_SCROLLBAR_WIDTH : 0), item_h };
                                if (vr->current_subtitle == idx) draw_rect(ren, item, (SDL_Color){ LIST_ITEM_BG_COLOR, (Uint8)(180 * overlay_alpha) });
                                draw_text_shadow(ren, item.x + MENU_DROPDOWN_TEXT_PADDING_X, item.y + MENU_DROPDOWN_TEXT_PADDING_Y, vr_get_subtitle_track_name(vr, idx), text);
                            }
                        }
                    }
                    if ((count + 1) > max_items) {
                        SDL_Rect scrollbar_bg = { list.x + list.w - MENU_DROPDOWN_SCROLLBAR_WIDTH, list.y, MENU_DROPDOWN_SCROLLBAR_WIDTH, list.h };
                        draw_rect(ren, scrollbar_bg, (SDL_Color){ SCROLLBAR_BG_COLOR, (Uint8)(200 * overlay_alpha) });
                        int scroll_h = (max_items * list.h) / (count + 1);
                        int scroll_y = list.y + (subtitle_scroll * list.h) / (count + 1);
                        SDL_Rect scrollbar = { list.x + list.w - MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_y, MENU_DROPDOWN_SCROLLBAR_WIDTH, scroll_h };
                        draw_rect(ren, scrollbar, (SDL_Color){ SCROLLBAR_THUMB_COLOR, (Uint8)(220 * overlay_alpha) });
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

                    draw_rect(ren, list, (SDL_Color){ LIST_BG_COLOR, (Uint8)(220 * overlay_alpha) });

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
                    draw_rect(ren, list, (SDL_Color){ LIST_BG_COLOR, (Uint8)(220 * overlay_alpha) });
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y, "0.5x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h, "0.75x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 2, "1.0x (Normal)", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 3, "1.25x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 4, "1.5x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 5, "2.0x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 6, "3.0x", text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 7, "5.0x", text);
                }

                if (subtitle_settings_menu_open && subtitle_settings_applicable) {
                    int item_h = MENU_DROPDOWN_ITEM_HEIGHT;
                    SDL_Rect list = { menu_panel.x - MENU_DROPDOWN_WIDTH, subtitle_settings_box.y, MENU_DROPDOWN_WIDTH, item_h * subtitle_settings_row_count };
                    if (list.x < margin) list.x = margin;
                    if (list.y + list.h > h) {
                        list.y = h - list.h;
                        if (list.y < 0) list.y = 0;
                    }
                    draw_rect(ren, list, (SDL_Color){ LIST_BG_COLOR, (Uint8)(220 * overlay_alpha) });

                    char row0[128];
                    char row1[128];
                    char row2[128];
                    snprintf(row0, sizeof(row0), "Color: %s", subtitle_override_color_labels[subtitle_color_idx]);
                    snprintf(row1, sizeof(row1), "Size: %s", subtitle_override_size_labels[subtitle_size_idx]);
                    snprintf(row2, sizeof(row2), "Position: %s", subtitle_override_move_labels[subtitle_move_idx]);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y, row0, text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h, row1, text);
                    draw_text_shadow(ren, list.x + MENU_DROPDOWN_TEXT_PADDING_X, list.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * 2, row2, text);

                    if (subtitle_settings_value_menu_open && subtitle_settings_value_menu_row >= 0) {
                        int value_count = 0;
                        if (subtitle_settings_value_menu_row == 0) value_count = subtitle_color_count;
                        if (subtitle_settings_value_menu_row == 1) value_count = subtitle_size_count;
                        if (subtitle_settings_value_menu_row == 2) value_count = subtitle_move_count;

                        if (value_count > 0) {
                            SDL_Rect vlist = { list.x - MENU_DROPDOWN_WIDTH - 8, list.y, MENU_DROPDOWN_WIDTH, item_h * value_count };
                            if (vlist.x < margin) vlist.x = margin;
                            if (vlist.y + vlist.h > h) {
                                vlist.y = h - vlist.h;
                                if (vlist.y < 0) vlist.y = 0;
                            }
                            draw_rect(ren, vlist, (SDL_Color){ SUBTITLE_VLIST_BG_COLOR, (Uint8)(220 * overlay_alpha) });

                            for (int i = 0; i < value_count; i++) {
                                const char* label = "";
                                if (subtitle_settings_value_menu_row == 0) label = subtitle_override_color_labels[i];
                                if (subtitle_settings_value_menu_row == 1) label = subtitle_override_size_labels[i];
                                if (subtitle_settings_value_menu_row == 2) label = subtitle_override_move_labels[i];
                                draw_text_shadow(ren, vlist.x + MENU_DROPDOWN_TEXT_PADDING_X, vlist.y + MENU_DROPDOWN_TEXT_PADDING_Y + item_h * i, label, text);
                            }
                        }
                    }
                }
            }
        }

        pause_alpha = lerpf(pause_alpha, paused ? 1.0f : 0.0f, clampf(dt * 6.0f, 0.0f, 1.0f));
        if (pause_alpha > 0.01f) {
            SDL_Color pcol = { PAUSED_TEXT_COLOR, (Uint8)(255 * pause_alpha) };
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
                SDL_Color fcol = { FLASH_TEXT_COLOR, (Uint8)(220 * flash_alpha) };
                draw_text_shadow(ren, 20, 56, flash_text, fcol);
            }
        }

        if (vr && playback_speed > 2.0f) {
            SDL_Color acol = { ACOL_TEXT_COLOR, ACOL_TEXT_ALPHA };
            draw_text_shadow(ren, 20, 92, "Audio disabled at high speed", acol);
        }

        SDL_RenderPresent(ren);
    }

    #if SAVE_FILE
        fill_save_state_from_vr(vr, &save_state, video_file);
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
    history_clear_from(playback_history, &history_count, 0);
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
#endif
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
