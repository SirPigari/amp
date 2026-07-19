#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#endif
#include "../thirdparty/SDL2/SDL_ttf.h"

static TTF_Font* ui_font = NULL;
static int ui_font_size = 18;
static char ui_font_label[128] = "Iosevka";
static char ui_font_path[260] = "";

/* i just discovered this word it literally describes it perfectly */
/* i feel like zozin */
typedef struct {
    float    offset;
    Uint32   hover_start_ms;
    int      is_hovered;
} MarqueeState;

static MarqueeState subtitle_marquee[64] = { 0 };

static bool is_absolute_path(const char* path) {
#ifdef _WIN32
    return (path[0] && path[1] == ':' && (path[2] == '\\' || path[2] == '/'));
#else
    return path[0] == '/';
#endif
}

static bool load_ui_font(const char* path, const char* label) {
    if (!path || !path[0]) return false;

    char final_path[1024];

    if (!is_absolute_path(path)) {
        char exe_dir[512];
        get_exe_dir(exe_dir, sizeof(exe_dir));

        snprintf(final_path, sizeof(final_path), "%s/%s", exe_dir, path);
    } else {
        strncpy(final_path, path, sizeof(final_path) - 1);
        final_path[sizeof(final_path) - 1] = '\0';
    }

    TTF_Font* font = TTF_OpenFont(final_path, ui_font_size);
    if (!font) return false;

    if (ui_font) TTF_CloseFont(ui_font);
    ui_font = font;

    strncpy(ui_font_path, final_path, sizeof(ui_font_path) - 1);
    ui_font_path[sizeof(ui_font_path) - 1] = '\0';

    if (label) {
        strncpy(ui_font_label, label, sizeof(ui_font_label) - 1);
        ui_font_label[sizeof(ui_font_label) - 1] = '\0';
    }

    return true;
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
    SDL_Color shadow = { THEME_SHADOW_COLOR[0], THEME_SHADOW_COLOR[1], THEME_SHADOW_COLOR[2], (Uint8)(color.a * 0.8f) };
    draw_text(ren, x + THEME_SHADOW_OFFSET, y + THEME_SHADOW_OFFSET, text, shadow);
    draw_text(ren, x, y, text, color);
}

static void draw_text_marquee(
    SDL_Renderer* ren,
    int x, int y,
    int max_w,
    const char* text,
    SDL_Color color,
    MarqueeState* ms,
    int is_hovered,
    Uint32 now_ms
) {
    if (!text || !ui_font) return;

    int text_w = 0, text_h = 0;
    TTF_SizeUTF8(ui_font, text, &text_w, &text_h);

    if (text_w <= max_w) {
        draw_text_shadow(ren, x, y, text, color);
        ms->offset = 0;
        ms->hover_start_ms = 0;
        ms->is_hovered = 0;
        return;
    }

    if (is_hovered && !ms->is_hovered) {
        ms->hover_start_ms = now_ms;
        ms->offset = 0;
    }
    if (!is_hovered) {
        ms->offset = 0;
        ms->hover_start_ms = 0;
    }
    ms->is_hovered = is_hovered;

    int total_chars = 0;
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        if      ((*p & 0x80) == 0x00) p += 1;
        else if ((*p & 0xE0) == 0xC0) p += 2;
        else if ((*p & 0xF0) == 0xE0) p += 3;
        else if ((*p & 0xF8) == 0xF0) p += 4;
        else                           p += 1;
        total_chars++;
    }

    #define BYTE_OFFSET_OF_CHAR(src, n, out_offset) do { \
        const unsigned char* _p = (const unsigned char*)(src); \
        int _i = 0; \
        while (*_p && _i < (n)) { \
            if      ((*_p & 0x80) == 0x00) _p += 1; \
            else if ((*_p & 0xE0) == 0xC0) _p += 2; \
            else if ((*_p & 0xF0) == 0xE0) _p += 3; \
            else if ((*_p & 0xF8) == 0xF0) _p += 4; \
            else                            _p += 1; \
            _i++; \
        } \
        (out_offset) = (int)(_p - (const unsigned char*)(src)); \
    } while(0)

    if (is_hovered && now_ms - ms->hover_start_ms > MARQUEE_DELAY_MS) {
        float elapsed_s = (float)(now_ms - ms->hover_start_ms - MARQUEE_DELAY_MS) / 1000.0f;
        float raw = elapsed_s * MARQUEE_SPEED;
        int cycle = total_chars + 1;
        ms->offset = (float)((int)raw % cycle);
    }

    int char_skip = (int)ms->offset;

    SDL_Rect clip = { x, y - 2, max_w, text_h + 4 };
    SDL_RenderSetClipRect(ren, &clip);

    if (!is_hovered || (now_ms - ms->hover_start_ms <= MARQUEE_DELAY_MS)) {
        int ellipsis_w = 0, dummy_h = 0;
        TTF_SizeUTF8(ui_font, MARQUEE_ELLIPSIS, &ellipsis_w, &dummy_h);
        int budget = max_w - ellipsis_w;

        int fit_bytes = 0;
        const unsigned char* q = (const unsigned char*)text;
        while (*q) {
            int cb = 1;
            if      ((*q & 0x80) == 0x00) cb = 1;
            else if ((*q & 0xE0) == 0xC0) cb = 2;
            else if ((*q & 0xF0) == 0xE0) cb = 3;
            else if ((*q & 0xF8) == 0xF0) cb = 4;

            char tmp[512];
            int candidate = fit_bytes + cb;
            if (candidate >= (int)sizeof(tmp) - 1) break;
            memcpy(tmp, text, candidate);
            tmp[candidate] = '\0';

            int w = 0, h = 0;
            TTF_SizeUTF8(ui_font, tmp, &w, &h);
            if (w > budget) break;

            fit_bytes = candidate;
            q += cb;
        }

        char truncated[512];
        memcpy(truncated, text, fit_bytes);
        snprintf(truncated + fit_bytes, sizeof(truncated) - fit_bytes, "%s", MARQUEE_ELLIPSIS);
        draw_text_shadow(ren, x, y, truncated, color);

    } else {
        int byte_start = 0;
        BYTE_OFFSET_OF_CHAR(text, char_skip, byte_start);
        draw_text_shadow(ren, x, y, text + byte_start, color);
    }

    SDL_RenderSetClipRect(ren, NULL);

    #undef BYTE_OFFSET_OF_CHAR
}

bool is_valid_utf8(const char* s) {
    const unsigned char* p = (const unsigned char*)s;
    while(*p) {
        if(*p <= 0x7F) { p++; continue; }
        else if((*p & 0xE0) == 0xC0) {
            if((p[1] & 0xC0) != 0x80) return false;
            p += 2;
        }
        else if((*p & 0xF0) == 0xE0) {
            if((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
            p += 3;
        }
        else if((*p & 0xF8) == 0xF0) {
            if((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
                return false;
            p += 4;
        }
        else return false;
    }
    return true;
}

const char* subtitle_normalize_to_utf8(
    const char* text,
    char* outbuf,
    size_t outbuf_size,
    const char** detected_encoding
) {
    if (!text || !text[0]) {
        if (detected_encoding) *detected_encoding = "empty";
        return text;
    }

    const unsigned char* t = (const unsigned char*)text;

    if (t[0] == 0xEF && t[1] == 0xBB && t[2] == 0xBF) {
        if (detected_encoding) *detected_encoding = "UTF-8 (BOM)";
        return text + 3;
    }

    bool is_utf16le = (t[0] == 0xFF && t[1] == 0xFE);
    bool is_utf16be = (t[0] == 0xFE && t[1] == 0xFF);

    bool is_ascii = true;
    for (size_t i = 0; text[i]; ++i){
        if ((unsigned char)text[i] >= 0x80){ is_ascii = false; break; }
    }

    bool likely_cyrillic = false;

    if (is_utf16le) {
        if (detected_encoding) *detected_encoding = "UTF-16LE";
    } else if (is_utf16be) {
        if (detected_encoding) *detected_encoding = "UTF-16BE";
    } else if (is_ascii) {
        if (detected_encoding) *detected_encoding = "ASCII";
        return text;
    } else {
        if (is_valid_utf8(text)) {
            if (detected_encoding) *detected_encoding = "UTF-8";
            return text;
        }
        int high = 0, cyrillic_range = 0;
        for (size_t i = 0; text[i]; i++) {
            unsigned char c = (unsigned char)text[i];
            if (c >= 0x80) { high++; if (c >= 0xC0) cyrillic_range++; }
        }
        likely_cyrillic = (high >= 4) && (cyrillic_range * 100 / high >= 55);
        if (detected_encoding) *detected_encoding = likely_cyrillic ? "CP1251 (heuristic)" : "unknown/legacy";
    }

#ifdef _WIN32

    if (is_utf16le || is_utf16be) {
        const uint8_t* data = (const uint8_t*)text;
        size_t size = strlen(text);
        size_t wchar_count = size / 2;
        wchar_t tmp[1024];
        if (wchar_count >= 1023) wchar_count = 1023;

        for (size_t i = 0; i < wchar_count; i++) {
            uint16_t v = is_utf16be
                ? ((uint16_t)data[i*2] << 8) | data[i*2 + 1]
                : ((uint16_t)data[i*2 + 1] << 8) | data[i*2];
            tmp[i] = (wchar_t)v;
        }
        tmp[wchar_count] = 0;

        wchar_t* start = tmp;
        if (start[0] == 0xFEFF) start++;

        int needed = WideCharToMultiByte(CP_UTF8, 0, start, -1, outbuf, (int)outbuf_size, NULL, NULL);
        if (needed > 0) return outbuf;
        return text;
    }

    static const UINT enc_western[] = { 1250, 1252, 28592, 1251, 850 };
    static const UINT enc_cyrillic[] = { 1251, 1250, 1252, 28592, 850 };
    static const char* name_western[] = { "Windows-1250", "Windows-1252", "ISO-8859-2", "CP1251", "CP850" };
    static const char* name_cyrillic[] = { "CP1251", "Windows-1250", "Windows-1252", "ISO-8859-2", "CP850" };
    const UINT* encodings = likely_cyrillic ? enc_cyrillic : enc_western;
    const char** enc_names = (const char**)(likely_cyrillic ? name_cyrillic : name_western);

    wchar_t tmpw[1024];
    for (int i = 0; i < 5; i++) {
        int needed = MultiByteToWideChar(encodings[i], MB_ERR_INVALID_CHARS, text, -1, tmpw, 1024);
        if (needed > 0) {
            if (detected_encoding) *detected_encoding = enc_names[i];
            int utf8_needed = WideCharToMultiByte(CP_UTF8, 0, tmpw, -1, outbuf, (int)outbuf_size, NULL, NULL);
            if (utf8_needed > 0) return outbuf;
        }
    }

    {
        int needed = MultiByteToWideChar(1252, 0, text, -1, tmpw, 1024);
        if (needed > 0) {
            if (detected_encoding) *detected_encoding = "Windows-1252 (fallback)";
            int utf8_needed = WideCharToMultiByte(CP_UTF8, 0, tmpw, -1, outbuf, (int)outbuf_size, NULL, NULL);
            if (utf8_needed > 0) return outbuf;
        }
    }

    return text;

#else

    if (is_utf16le || is_utf16be) {
        const char* enc = is_utf16le ? "UTF-16LE" : "UTF-16BE";
        iconv_t cd = iconv_open("UTF-8", enc);
        if (cd != (iconv_t)(-1)) {
            char* inbuf = (char*)text;
            size_t inbytes = strlen(text);
            char* out = outbuf;
            size_t outbytes = outbuf_size - 1;
            if (iconv(cd, &inbuf, &inbytes, &out, &outbytes) != (size_t)(-1)) {
                *out = 0;
                if (detected_encoding) *detected_encoding = enc;
                iconv_close(cd);
                return outbuf;
            }
            iconv_close(cd);
        }
        return text;
    }

    static const char* enc_western_lx[] = {
        "WINDOWS-1250", "WINDOWS-1252", "ISO-8859-2", "WINDOWS-1251", "CP850", "ISO-8859-1"
    };
    static const char* enc_cyrillic_lx[] = {
        "WINDOWS-1251", "KOI8-R", "WINDOWS-1250", "WINDOWS-1252", "ISO-8859-5", "ISO-8859-1"
    };
    const char** encodings = (const char**)(likely_cyrillic ? enc_cyrillic_lx : enc_western_lx);

    for (int i = 0; i < 6; i++) {
        iconv_t cd = iconv_open("UTF-8", encodings[i]);
        if (cd == (iconv_t)(-1)) continue;

        char* inbuf = (char*)text;
        size_t inbytes = strlen(text);
        char* out = outbuf;
        size_t outbytes = outbuf_size - 1;

        if (iconv(cd, &inbuf, &inbytes, &out, &outbytes) != (size_t)(-1)) {
            *out = 0;
            if (detected_encoding) *detected_encoding = encodings[i];
            iconv_close(cd);
            return outbuf;
        }

        iconv_close(cd);
    }

    return text;

#endif
}

static void draw_rect(SDL_Renderer* ren, SDL_Rect r, SDL_Color c);

typedef struct {
    char     value[TEXT_INPUT_MAX_LEN];
    char     default_val[TEXT_INPUT_MAX_LEN];
    char     prompt[64];
    int      max_len;
    int      active;
    int      done;
    int      cancelled;
    int      has_typed;
} TextInputState;

static void text_input_open(TextInputState* ti, const char* prompt,
                             const char* default_val, int max_len) {
    memset(ti, 0, sizeof(*ti));
    ti->active = 1;
    ti->has_typed = 0;
    if (prompt) snprintf(ti->prompt, sizeof(ti->prompt), "%s", prompt);
    if (default_val) {
        snprintf(ti->default_val, TEXT_INPUT_MAX_LEN, "%s", default_val);
        snprintf(ti->value, TEXT_INPUT_MAX_LEN, "%s", default_val);
    }
    ti->max_len = (max_len > 0 && max_len < TEXT_INPUT_MAX_LEN)
                ? max_len : (TEXT_INPUT_MAX_LEN - 1);
    SDL_StartTextInput();
}

static void text_input_handle_event(TextInputState* ti, SDL_Event* e) {
    if (!ti->active) return;
    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            ti->done     = 1;
            ti->active   = 0;
            SDL_StopTextInput();
        } else if (k == SDLK_ESCAPE) {
            ti->cancelled = 1;
            ti->active    = 0;
            SDL_StopTextInput();
        } else if (k == SDLK_BACKSPACE) {
            if (!ti->has_typed) {
                ti->has_typed = 1;
                ti->value[0] = '\0';
            } else {
                int len = (int)strlen(ti->value);
                if (len > 0) ti->value[len - 1] = '\0';
            }
        }
    } else if (e->type == SDL_TEXTINPUT) {
        if (!ti->has_typed) {
            ti->value[0] = '\0';
            ti->has_typed = 1;
        }
        int len = (int)strlen(ti->value);
        int add = (int)strlen(e->text.text);
        if (len + add < ti->max_len) {
            strncat(ti->value, e->text.text, ti->max_len - len);
        }
    }
}

static void text_input_draw(SDL_Renderer* r, TextInputState* ti) {
    if (!ti->active) return;
    
    int px = 20, py = 20, padding = 6;
    
    int prompt_w = 0, prompt_h = 0;
    TTF_SizeUTF8(ui_font, ti->prompt, &prompt_w, &prompt_h);
    
    const char* content = ti->has_typed ? ti->value : ti->default_val;
    int content_w = 0, content_h = 0;
    TTF_SizeUTF8(ui_font, content, &content_w, &content_h);
    
    int total_w = prompt_w + padding + content_w + padding * 2;
    int total_h = prompt_h + padding;
    
    SDL_Rect bg = { px, py, total_w, total_h };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    draw_rect(r, bg, (SDL_Color){ THEME_TEXT_INPUT_BG_COLOR[0], THEME_TEXT_INPUT_BG_COLOR[1], THEME_TEXT_INPUT_BG_COLOR[2], 200 });
    
    draw_text_shadow(r, px + padding, py + padding / 2, ti->prompt,
                      (SDL_Color){ THEME_TEXT_INPUT_PROMPT_COLOR[0], THEME_TEXT_INPUT_PROMPT_COLOR[1], THEME_TEXT_INPUT_PROMPT_COLOR[2], 255 });
    
    SDL_Color content_color = ti->has_typed 
        ? (SDL_Color){ THEME_TEXT_COLOR[0], THEME_TEXT_COLOR[1], THEME_TEXT_COLOR[2], 255 }
        : (SDL_Color){ THEME_MUTED_COLOR[0], THEME_MUTED_COLOR[1], THEME_MUTED_COLOR[2], 200 };
    draw_text_shadow(r, px + padding + prompt_w + padding, py + padding / 2, 
                      content, content_color);
}
