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
        if (detected_encoding) *detected_encoding = "UTF-8 or other";
        return text;
    }

#ifdef _WIN32

    const uint8_t* data = (const uint8_t*)text;
    size_t size = strlen(text);

    size_t wchar_count = size/2;
    wchar_t tmp[1024];
    if (wchar_count >= 1023) wchar_count = 1023;

    for(size_t i = 0; i < wchar_count; i++){
        uint16_t v = is_utf16be
            ? ((uint16_t)data[i*2] << 8) | data[i*2+1]
            : ((uint16_t)data[i*2+1] << 8) | data[i*2];
        tmp[i] = (wchar_t)v;
    }
    tmp[wchar_count] = 0;

    wchar_t* start = tmp;
    if(start[0] == 0xFEFF) start++;

    int needed = WideCharToMultiByte(CP_UTF8, 0, start, -1, outbuf, (int)outbuf_size, NULL, NULL);
    if(needed > 0) return outbuf;

    return text;

#else

    iconv_t cd = iconv_open("UTF-8", is_utf16le ? "UTF-16LE" : "UTF-16BE");
    if(cd == (iconv_t)(-1)) return text;

    char* inbuf = (char*)text;
    size_t inbytes = strlen(text);
    char* out = outbuf;
    size_t outbytes = outbuf_size - 1;

    if(iconv(cd, &inbuf, &inbytes, &out, &outbytes) != (size_t)(-1)) {
        *out = 0;
        iconv_close(cd);
        return outbuf;
    }

    iconv_close(cd);
    return text;

#endif
}