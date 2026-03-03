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

static bool load_ui_font(const char* path, const char* label) {
    if (!path || !path[0]) return false;
    TTF_Font* font = TTF_OpenFont(path, ui_font_size);
    if (!font) return false;
    if (ui_font) TTF_CloseFont(ui_font);
    ui_font = font;
    strncpy(ui_font_path, path, sizeof(ui_font_path) - 1);
    ui_font_path[sizeof(ui_font_path) - 1] = '\0';
    if (label) {
        strncpy(ui_font_label, label, sizeof(ui_font_label) - 1);
        ui_font_label[sizeof(ui_font_label) - 1] = '\0';
    }
    return true;
}

static const char* try_load_ui_font(const char* path, const char* label) {
    if (load_ui_font(path, label)) return NULL;
    return SDL_GetError();
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

bool is_valid_utf8(const char* s) {
    const unsigned char *p = (const unsigned char*)s;
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

    bool is_utf16le = (t[0] == 0xFF && t[1] == 0xFE);
    bool is_utf16be = (t[0] == 0xFE && t[1] == 0xFF);

    bool is_ascii = true;
    for (size_t i = 0; text[i]; ++i){
        if ((unsigned char)text[i] >= 0x80){ is_ascii = false; break; }
    }

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
        if (detected_encoding) *detected_encoding = "unknown/legacy";
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

    UINT encodings[] = { 1250, 28592, 850 };
    wchar_t tmpw[1024];

    for (int i = 0; i < 3; i++) {
        int needed = MultiByteToWideChar(encodings[i], 0, text, -1, tmpw, 1024);
        if (needed > 0) {
            if (detected_encoding) {
                if (i == 0) *detected_encoding = "Windows-1250";
                else if (i == 1) *detected_encoding = "ISO-8859-2";
                else *detected_encoding = "CP850";
            }

            int utf8_needed = WideCharToMultiByte(CP_UTF8, 0, tmpw, -1, outbuf, (int)outbuf_size, NULL, NULL);
            if (utf8_needed > 0) return outbuf;
        }
    }

    return text;

#else

    const char* encodings[] = {
        "UTF-16LE", "UTF-16BE", "WINDOWS-1250", "ISO-8859-2", "CP850"
    };

    for(int i=0;i<5;i++){
        iconv_t cd = iconv_open("UTF-8", encodings[i]);
        if(cd == (iconv_t)(-1)) continue;

        char* inbuf = (char*)text;
        size_t inbytes = strlen(text);
        char* out = outbuf;
        size_t outbytes = outbuf_size - 1;

        if(iconv(cd, &inbuf, &inbytes, &out, &outbytes) != (size_t)(-1)) {
            *out = 0;
            if(detected_encoding) *detected_encoding = encodings[i];
            iconv_close(cd);
            return outbuf;
        }

        iconv_close(cd);
    }

    return text;

#endif
}
