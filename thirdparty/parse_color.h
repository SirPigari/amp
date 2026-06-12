/*
 * parse_color.h - Overkill color parsing library, stb-style header-only
 *
 * USAGE:
 *   #define PARSE_COLOR_IMPLEMENTATION
 *   #include "parse_color.h"
 *
 * API:
 *   bool parse_color(const char* s, SDL_Color* out);
 *
 * Supports: CSS named colors, hex (#RGB, #RRGGBB, 0x..., bare hex),
 *   rgb/rgba/hsl/hsla/hsv/hsva/hsb/hsba/hwb/cmy/cmyk/lab/lch/oklab/oklch,
 *   color(srgb ...), color(display-p3 ...), key=value, CSV, float tuples,
 *   vector brackets, JSON-ish, SDL_Color{...}, integer packed, percentage,
 *   alpha modifiers (@0.5, /50%), natural language ("bright red", "pastel
 * pink", etc.)
 *
 * LICENSE: www.unlicense.org (public domain)
 */

#ifndef PARSE_COLOR_H
#define PARSE_COLOR_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
#include <stdint.h>
typedef int pc_bool;
#define PC_TRUE 1
#define PC_FALSE 0
#else
#include <stdbool.h>
#include <stdint.h>
typedef bool pc_bool;
#define PC_TRUE true
#define PC_FALSE false
#endif

/* SDL_Color forward compat: define a minimal one if SDL isn't present */
#ifndef SDL_pixels_h_
#ifndef PC_SDL_COLOR_DEFINED
    #define PC_SDL_COLOR_DEFINED
    typedef struct {
        uint8_t r, g, b, a;
    } SDL_Color;
    #define SDL_Colour SDL_Color
#endif
#endif

pc_bool parse_color(const char* s, SDL_Color* out);

#ifdef __cplusplus
}
#endif

/* ============================================================
 * IMPLEMENTATION
 * ============================================================ */
#ifdef PARSE_COLOR_IMPLEMENTATION

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- internal helpers ---- */

static void pc__trim(const char* src, char* dst, int dstsz) {
    int i = 0, j, len;
    /* convert tabs/newlines to spaces */
    char tmp[1024];
    len = (int) strlen(src);
    if (len >= (int) sizeof(tmp))
        len = (int) sizeof(tmp) - 1;
    for (j = 0; j < len; j++) {
        char c = src[j];
        tmp[j] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    tmp[len] = '\0';
    /* ltrim */
    while (tmp[i] == ' ')
        i++;
    /* rtrim */
    j = (int) strlen(tmp + i);
    while (j > 0 && tmp[i + j - 1] == ' ')
        j--;
    if (j >= dstsz)
        j = dstsz - 1;
    memcpy(dst, tmp + i, j);
    dst[j] = '\0';
    /* collapse multiple spaces */
    {
        char buf2[1024];
        int wi = 0;
        int prev_sp = 0;
        for (i = 0; dst[i]; i++) {
            if (dst[i] == ' ') {
                if (!prev_sp) {
                    buf2[wi++] = ' ';
                }
                prev_sp = 1;
            } else {
                buf2[wi++] = dst[i];
                prev_sp = 0;
            }
        }
        buf2[wi] = '\0';
        if (wi < dstsz) {
            memcpy(dst, buf2, wi + 1);
        }
    }
}

static void pc__lower(char* s) {
    for (; *s; s++)
        *s = (char) tolower((unsigned char) *s);
}

/* normalize a color name: strip hyphens, underscores, spaces, lowercase */
static void pc__normalize_name(const char* src, char* dst, int dstsz) {
    int wi = 0;
    for (; *src && wi < dstsz - 1; src++) {
        char c = (char) tolower((unsigned char) *src);
        if (c == '-' || c == '_' || c == ' ')
            continue;
        dst[wi++] = c;
    }
    dst[wi] = '\0';
}

static uint8_t pc__clamp_u8(float v) {
    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;
    return (uint8_t) (v + 0.5f);
}

static uint8_t pc__f01_to_u8(float v) {
    if (v < 0)
        v = 0;
    if (v > 1)
        v = 1;
    return (uint8_t) (v * 255.0f + 0.5f);
}

/* ---- named colors ---- */

typedef struct {
    const char* name;
    uint8_t r, g, b, a;
} pc__NamedColor;

static const pc__NamedColor pc__named_colors[] = {
    {"aliceblue",            240, 248, 255, 255},
    {"antiquewhite",         250, 235, 215, 255},
    {"aqua",                 0,   255, 255, 255},
    {"aquamarine",           127, 255, 212, 255},
    {"azure",                240, 255, 255, 255},
    {"beige",                245, 245, 220, 255},
    {"bisque",               255, 228, 196, 255},
    {"black",                0,   0,   0,   255},
    {"blanchedalmond",       255, 235, 205, 255},
    {"blue",                 0,   0,   255, 255},
    {"blueviolet",           138, 43,  226, 255},
    {"brown",                165, 42,  42,  255},
    {"burlywood",            222, 184, 135, 255},
    {"cadetblue",            95,  158, 160, 255},
    {"chartreuse",           127, 255, 0,   255},
    {"chocolate",            210, 105, 30,  255},
    {"coral",                255, 127, 80,  255},
    {"cornflowerblue",       100, 149, 237, 255},
    {"cornsilk",             255, 248, 220, 255},
    {"crimson",              220, 20,  60,  255},
    {"cyan",                 0,   255, 255, 255},
    {"darkblue",             0,   0,   139, 255},
    {"darkcyan",             0,   139, 139, 255},
    {"darkgoldenrod",        184, 134, 11,  255},
    {"darkgray",             169, 169, 169, 255},
    {"darkgreen",            0,   100, 0,   255},
    {"darkgrey",             169, 169, 169, 255},
    {"darkkhaki",            189, 183, 107, 255},
    {"darkmagenta",          139, 0,   139, 255},
    {"darkolivegreen",       85,  107, 47,  255},
    {"darkorange",           255, 140, 0,   255},
    {"darkorchid",           153, 50,  204, 255},
    {"darkred",              139, 0,   0,   255},
    {"darksalmon",           233, 150, 122, 255},
    {"darkseagreen",         143, 188, 143, 255},
    {"darkslateblue",        72,  61,  139, 255},
    {"darkslategray",        47,  79,  79,  255},
    {"darkslategrey",        47,  79,  79,  255},
    {"darkturquoise",        0,   206, 209, 255},
    {"darkviolet",           148, 0,   211, 255},
    {"deeppink",             255, 20,  147, 255},
    {"deepskyblue",          0,   191, 255, 255},
    {"dimgray",              105, 105, 105, 255},
    {"dimgrey",              105, 105, 105, 255},
    {"dodgerblue",           30,  144, 255, 255},
    {"firebrick",            178, 34,  34,  255},
    {"floralwhite",          255, 250, 240, 255},
    {"forestgreen",          34,  139, 34,  255},
    {"fuchsia",              255, 0,   255, 255},
    {"gainsboro",            220, 220, 220, 255},
    {"ghostwhite",           248, 248, 255, 255},
    {"gold",                 255, 215, 0,   255},
    {"goldenrod",            218, 165, 32,  255},
    {"gray",                 128, 128, 128, 255},
    {"green",                0,   128, 0,   255},
    {"greenyellow",          173, 255, 47,  255},
    {"grey",                 128, 128, 128, 255},
    {"honeydew",             240, 255, 240, 255},
    {"hotpink",              255, 105, 180, 255},
    {"indianred",            205, 92,  92,  255},
    {"indigo",               75,  0,   130, 255},
    {"ivory",                255, 255, 240, 255},
    {"khaki",                240, 230, 140, 255},
    {"lavender",             230, 230, 250, 255},
    {"lavenderblush",        255, 240, 245, 255},
    {"lawngreen",            124, 252, 0,   255},
    {"lemonchiffon",         255, 250, 205, 255},
    {"lightblue",            173, 216, 230, 255},
    {"lightcoral",           240, 128, 128, 255},
    {"lightcyan",            224, 255, 255, 255},
    {"lightgoldenrodyellow", 250, 250, 210, 255},
    {"lightgray",            211, 211, 211, 255},
    {"lightgreen",           144, 238, 144, 255},
    {"lightgrey",            211, 211, 211, 255},
    {"lightpink",            255, 182, 193, 255},
    {"lightsalmon",          255, 160, 122, 255},
    {"lightseagreen",        32,  178, 170, 255},
    {"lightskyblue",         135, 206, 250, 255},
    {"lightslategray",       119, 136, 153, 255},
    {"lightslategrey",       119, 136, 153, 255},
    {"lightsteelblue",       176, 196, 222, 255},
    {"lightyellow",          255, 255, 224, 255},
    {"lime",                 0,   255, 0,   255},
    {"limegreen",            50,  205, 50,  255},
    {"linen",                250, 240, 230, 255},
    {"magenta",              255, 0,   255, 255},
    {"maroon",               128, 0,   0,   255},
    {"mediumaquamarine",     102, 205, 170, 255},
    {"mediumblue",           0,   0,   205, 255},
    {"mediumorchid",         186, 85,  211, 255},
    {"mediumpurple",         147, 112, 219, 255},
    {"mediumseagreen",       60,  179, 113, 255},
    {"mediumslateblue",      123, 104, 238, 255},
    {"mediumspringgreen",    0,   250, 154, 255},
    {"mediumturquoise",      72,  209, 204, 255},
    {"mediumvioletred",      199, 21,  133, 255},
    {"midnightblue",         25,  25,  112, 255},
    {"mintcream",            245, 255, 250, 255},
    {"mistyrose",            255, 228, 225, 255},
    {"moccasin",             255, 228, 181, 255},
    {"navajowhite",          255, 222, 173, 255},
    {"navy",                 0,   0,   128, 255},
    {"oldlace",              253, 245, 230, 255},
    {"olive",                128, 128, 0,   255},
    {"olivedrab",            107, 142, 35,  255},
    {"orange",               255, 165, 0,   255},
    {"orangered",            255, 69,  0,   255},
    {"orchid",               218, 112, 214, 255},
    {"palegoldenrod",        238, 232, 170, 255},
    {"palegreen",            152, 251, 152, 255},
    {"paleturquoise",        175, 238, 238, 255},
    {"palevioletred",        219, 112, 147, 255},
    {"papayawhip",           255, 239, 213, 255},
    {"peachpuff",            255, 218, 185, 255},
    {"peru",                 205, 133, 63,  255},
    {"pink",                 255, 192, 203, 255},
    {"plum",                 221, 160, 221, 255},
    {"powderblue",           176, 224, 230, 255},
    {"purple",               128, 0,   128, 255},
    {"rebeccapurple",        102, 51,  153, 255},
    {"red",                  255, 0,   0,   255},
    {"rosybrown",            188, 143, 143, 255},
    {"royalblue",            65,  105, 225, 255},
    {"saddlebrown",          139, 69,  19,  255},
    {"salmon",               250, 128, 114, 255},
    {"sandybrown",           244, 164, 96,  255},
    {"seagreen",             46,  139, 87,  255},
    {"seashell",             255, 245, 238, 255},
    {"sienna",               160, 82,  45,  255},
    {"silver",               192, 192, 192, 255},
    {"skyblue",              135, 206, 235, 255},
    {"slateblue",            106, 90,  205, 255},
    {"slategray",            112, 128, 144, 255},
    {"slategrey",            112, 128, 144, 255},
    {"snow",                 255, 250, 250, 255},
    {"springgreen",          0,   255, 127, 255},
    {"steelblue",            70,  130, 180, 255},
    {"tan",                  210, 180, 140, 255},
    {"teal",                 0,   128, 128, 255},
    {"thistle",              216, 191, 216, 255},
    {"tomato",               255, 99,  71,  255},
    {"transparent",          0,   0,   0,   0  },
    {"turquoise",            64,  224, 208, 255},
    {"violet",               238, 130, 238, 255},
    {"wheat",                245, 222, 179, 255},
    {"white",                255, 255, 255, 255},
    {"whitesmoke",           245, 245, 245, 255},
    {"yellow",               255, 255, 0,   255},
    {"yellowgreen",          154, 205, 50,  255},
    {NULL,                   0,   0,   0,   0  }
};

static pc_bool pc__lookup_named(const char* s, SDL_Color* out) {
    char norm[64];
    int i;
    pc__normalize_name(s, norm, sizeof(norm));
    for (i = 0; pc__named_colors[i].name; i++) {
        if (strcmp(norm, pc__named_colors[i].name) == 0) {
            out->r = pc__named_colors[i].r;
            out->g = pc__named_colors[i].g;
            out->b = pc__named_colors[i].b;
            out->a = pc__named_colors[i].a;
            return PC_TRUE;
        }
    }
    return PC_FALSE;
}

/* ---- color space conversions ---- */

static void pc__hsl_to_rgb(float h, float s, float l, float* r, float* g, float* b) {
    float c, x, m;
    float r1 = 0, g1 = 0, b1 = 0;
    h = fmodf(h, 360.0f);
    if (h < 0)
        h += 360.0f;
    s /= 100.0f;
    l /= 100.0f;
    c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    m = l - c / 2.0f;
    if (h < 60) {
        r1 = c;
        g1 = x;
        b1 = 0;
    } else if (h < 120) {
        r1 = x;
        g1 = c;
        b1 = 0;
    } else if (h < 180) {
        r1 = 0;
        g1 = c;
        b1 = x;
    } else if (h < 240) {
        r1 = 0;
        g1 = x;
        b1 = c;
    } else if (h < 300) {
        r1 = x;
        g1 = 0;
        b1 = c;
    } else {
        r1 = c;
        g1 = 0;
        b1 = x;
    }
    *r = (r1 + m) * 255.0f;
    *g = (g1 + m) * 255.0f;
    *b = (b1 + m) * 255.0f;
}

static void pc__rgb_to_hsl(float r, float g, float b, float* h, float* s, float* l) {
    r /= 255.0f;
    g /= 255.0f;
    b /= 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float delta = mx - mn;
    *l = (mx + mn) * 0.5f;
    if (delta < 1e-6f) {
        *h = 0;
        *s = 0;
        return;
    }
    *s = delta / (1.0f - fabsf(2.0f * (*l) - 1.0f));
    if (mx == r)
        *h = fmodf((g - b) / delta + 6.0f, 6.0f) * 60.0f;
    else if (mx == g)
        *h = ((b - r) / delta + 2.0f) * 60.0f;
    else
        *h = ((r - g) / delta + 4.0f) * 60.0f;
    *s *= 100.0f;
    *l *= 100.0f;
}

static void pc__hsv_to_rgb(float h, float s, float v, float* r, float* g, float* b) {
    float c, x, m, r1 = 0, g1 = 0, b1 = 0;
    h = fmodf(h, 360.0f);
    if (h < 0)
        h += 360.0f;
    s /= 100.0f;
    v /= 100.0f;
    c = v * s;
    x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    m = v - c;
    if (h < 60) {
        r1 = c;
        g1 = x;
        b1 = 0;
    } else if (h < 120) {
        r1 = x;
        g1 = c;
        b1 = 0;
    } else if (h < 180) {
        r1 = 0;
        g1 = c;
        b1 = x;
    } else if (h < 240) {
        r1 = 0;
        g1 = x;
        b1 = c;
    } else if (h < 300) {
        r1 = x;
        g1 = 0;
        b1 = c;
    } else {
        r1 = c;
        g1 = 0;
        b1 = x;
    }
    *r = (r1 + m) * 255.0f;
    *g = (g1 + m) * 255.0f;
    *b = (b1 + m) * 255.0f;
}

static void pc__hwb_to_rgb(float h, float w, float blk, float* r, float* g, float* b) {
    float s, v;
    w /= 100.0f;
    blk /= 100.0f;
    if (w + blk >= 1.0f) {
        float gray = w / (w + blk) * 255.0f;
        *r = *g = *b = gray;
        return;
    }
    s = 1.0f - w / (1.0f - blk);
    v = (1.0f - blk) * 100.0f;
    pc__hsv_to_rgb(h, s * 100.0f, v, r, g, b);
}

static void pc__cmy_to_rgb(float c, float m, float y, float* r, float* g, float* b) {
    *r = (1.0f - c) * 255.0f;
    *g = (1.0f - m) * 255.0f;
    *b = (1.0f - y) * 255.0f;
}

static void pc__cmyk_to_rgb(float c, float m, float y, float k, float* r, float* g, float* b) {
    *r = 255.0f * (1.0f - c) * (1.0f - k);
    *g = 255.0f * (1.0f - m) * (1.0f - k);
    *b = 255.0f * (1.0f - y) * (1.0f - k);
}

/* oklab conversion helpers */
static float pc__srgb(float c) {
    return c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static void pc__oklab_to_rgb(float L, float a, float b_, float* r, float* g, float* b_out) {
    float l_ = L + 0.3963377774f * a + 0.2158037573f * b_;
    float m_ = L - 0.1055613458f * a - 0.0638541728f * b_;
    float s_ = L - 0.0894841775f * a - 1.2914855480f * b_;
    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    float rlin = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float glin = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float blin = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
    *r = pc__srgb(rlin < 0 ? 0 : rlin > 1 ? 1 : rlin) * 255.0f;
    *g = pc__srgb(glin < 0 ? 0 : glin > 1 ? 1 : glin) * 255.0f;
    *b_out = pc__srgb(blin < 0 ? 0 : blin > 1 ? 1 : blin) * 255.0f;
}

static void pc__oklch_to_rgb(float L, float C, float H, float* r, float* g, float* b) {
    float a = C * cosf(H * (float) (3.14159265358979323846 / 180.0));
    float b_ = C * sinf(H * (float) (3.14159265358979323846 / 180.0));
    pc__oklab_to_rgb(L, a, b_, r, g, b);
}

/* CIE Lab to XYZ to sRGB */
static void pc__lab_to_rgb(float L, float a, float b_, float* r, float* g, float* b_out) {
    float fy = (L + 16.0f) / 116.0f;
    float fx = a / 500.0f + fy;
    float fz = fy - b_ / 200.0f;
    float x = (fx > 0.206897f) ? fx * fx * fx : (fx - 16.0f / 116.0f) / 7.787f;
    float y = (fy > 0.206897f) ? fy * fy * fy : (fy - 16.0f / 116.0f) / 7.787f;
    float z = (fz > 0.206897f) ? fz * fz * fz : (fz - 16.0f / 116.0f) / 7.787f;
    x *= 0.95047f;
    z *= 1.08883f;
    float rlin = 3.2406f * x - 1.5372f * y - 0.4986f * z;
    float glin = -0.9689f * x + 1.8758f * y + 0.0415f * z;
    float blin = 0.0557f * x - 0.2040f * y + 1.0570f * z;
    *r = pc__srgb(rlin < 0 ? 0 : rlin > 1 ? 1 : rlin) * 255.0f;
    *g = pc__srgb(glin < 0 ? 0 : glin > 1 ? 1 : glin) * 255.0f;
    *b_out = pc__srgb(blin < 0 ? 0 : blin > 1 ? 1 : blin) * 255.0f;
}

static void pc__lch_to_rgb(float L, float C, float H, float* r, float* g, float* b) {
    float a = C * cosf(H * (float) (3.14159265358979323846 / 180.0));
    float b_ = C * sinf(H * (float) (3.14159265358979323846 / 180.0));
    pc__lab_to_rgb(L, a, b_, r, g, b);
}

/* ---- token / value parsing ---- */

/*
 * Split a functional arg string like "255, 0, 0, 0.5" or "255 0 0 / 50%"
 * into up to 4 tokens. Handles commas, spaces, slash separator.
 */
static int pc__split_args(const char* s, char tokens[4][64]) {
    int n = 0, i;
    const char* p = s;
    /* replace / with , for uniform splitting */
    char buf[256];
    int bi = 0;
    while (*p && bi < 255) {
        char c = *p++;
        if (c == '/')
            buf[bi++] = ',';
        else
            buf[bi++] = c;
    }
    buf[bi] = '\0';

    p = buf;
    for (i = 0; i < 4; i++)
        tokens[i][0] = '\0';

    while (*p && n < 4) {
        while (*p == ' ' || *p == ',')
            p++;
        if (!*p)
            break;
        int ti = 0;
        while (*p && *p != ',' && !((*p == ' ') && (strchr(p + 1, ',') == NULL))) {
            if (*p == ',')
                break;
            if (*p == ' ') {
                const char* q = p + 1;
                while (*q == ' ')
                    q++;
                if (!*q)
                    break;
            }
            if (ti < 63)
                tokens[n][ti++] = *p;
            p++;
        }
        tokens[n][ti] = '\0';
        if (ti > 0)
            n++;
        if (*p == ',')
            p++;
    }
    return n;
}

/* simpler split on comma or space */
static int pc__split_sep(const char* s, char sep, char tokens[5][64], int max_tok) {
    int n = 0;
    const char* p = s;
    while (*p && n < max_tok) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        int ti = 0;
        while (*p && *p != sep && (sep != ' ' || *p != ' ')) {
            if (ti < 63)
                tokens[n][ti++] = *p;
            p++;
        }
        tokens[n][ti] = '\0';
        if (ti > 0)
            n++;
        if (*p == sep)
            p++;
    }
    return n;
}

/* ---- individual parsers ---- */

/* strip outer quotes */
static pc_bool pc__strip_quotes(const char* s, char* out, int outsz) {
    int len = (int) strlen(s);
    if (len >= 2) {
        if ((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\'')) {
            if (len - 2 < outsz) {
                memcpy(out, s + 1, len - 2);
                out[len - 2] = '\0';
                return PC_TRUE;
            }
        }
    }
    return PC_FALSE;
}

/* hex: #RGB #RGBA #RRGGBB #RRGGBBAA  0x...  bare hex  */
static pc_bool pc__parse_hex(const char* s, SDL_Color* out) {
    const char* p = s;
    char hex[12];
    int len, i;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    else if (p[0] == '#')
        p++;

    len = 0;
    for (i = 0; p[i] && len < 10; i++) {
        char c = p[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
            hex[len++] = c;
        else
            break;
    }
    if (p[i] != '\0' && p[i] != ' ')
        return PC_FALSE;
    hex[len] = '\0';

    if (len == 3) {
        out->r = (uint8_t) (((hex[0] >= 'a')   ? hex[0] - 'a' + 10
                             : (hex[0] >= 'A') ? hex[0] - 'A' + 10
                                               : hex[0] - '0') *
                            17);
        out->g = (uint8_t) (((hex[1] >= 'a')   ? hex[1] - 'a' + 10
                             : (hex[1] >= 'A') ? hex[1] - 'A' + 10
                                               : hex[1] - '0') *
                            17);
        out->b = (uint8_t) (((hex[2] >= 'a')   ? hex[2] - 'a' + 10
                             : (hex[2] >= 'A') ? hex[2] - 'A' + 10
                                               : hex[2] - '0') *
                            17);
        out->a = 255;
        return PC_TRUE;
    } else if (len == 4) {
        out->r = (uint8_t) (((hex[0] >= 'a')   ? hex[0] - 'a' + 10
                             : (hex[0] >= 'A') ? hex[0] - 'A' + 10
                                               : hex[0] - '0') *
                            17);
        out->g = (uint8_t) (((hex[1] >= 'a')   ? hex[1] - 'a' + 10
                             : (hex[1] >= 'A') ? hex[1] - 'A' + 10
                                               : hex[1] - '0') *
                            17);
        out->b = (uint8_t) (((hex[2] >= 'a')   ? hex[2] - 'a' + 10
                             : (hex[2] >= 'A') ? hex[2] - 'A' + 10
                                               : hex[2] - '0') *
                            17);
        out->a = (uint8_t) (((hex[3] >= 'a')   ? hex[3] - 'a' + 10
                             : (hex[3] >= 'A') ? hex[3] - 'A' + 10
                                               : hex[3] - '0') *
                            17);
        return PC_TRUE;
    } else if (len == 6) {
        unsigned int v = (unsigned int) strtoul(hex, NULL, 16);
        out->r = (v >> 16) & 0xFF;
        out->g = (v >> 8) & 0xFF;
        out->b = v & 0xFF;
        out->a = 255;
        return PC_TRUE;
    } else if (len == 8) {
        unsigned int v = (unsigned int) strtoul(hex, NULL, 16);
        out->r = (v >> 24) & 0xFF;
        out->g = (v >> 16) & 0xFF;
        out->b = (v >> 8) & 0xFF;
        out->a = v & 0xFF;
        return PC_TRUE;
    }
    return PC_FALSE;
}

/* functional: rgb/rgba/hsl/hsla/hsv/hsva/hsb/hsba/hwb/cmy/cmyk */
static pc_bool pc__parse_functional(const char* s, SDL_Color* out) {
    char func[16], args_str[256];
    const char* lp = strchr(s, '(');
    const char* rp = strrchr(s, ')');
    int fn_len;
    if (!lp || !rp || rp < lp)
        return PC_FALSE;
    fn_len = (int) (lp - s);
    if (fn_len <= 0 || fn_len >= 16)
        return PC_FALSE;
    memcpy(func, s, fn_len);
    func[fn_len] = '\0';
    pc__lower(func);
    int args_len = (int) (rp - lp - 1);
    if (args_len < 0 || args_len >= 256)
        return PC_FALSE;
    memcpy(args_str, lp + 1, args_len);
    args_str[args_len] = '\0';

    char tokens[4][64];
    int n = pc__split_args(args_str, tokens);
    float v[4] = {0, 0, 0, 255};

    if (strcmp(func, "rgb") == 0 || strcmp(func, "rgba") == 0) {
        int i;
        for (i = 0; i < n && i < 4; i++) {
            const char* t = tokens[i];
            while (*t == ' ')
                t++;
            char tmp[64];
            int ti = 0;
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[ti - 1] = '\0';
                float pv = (float) atof(tmp);
                v[i] = pv / 100.0f * 255.0f;
            } else {
                v[i] = (float) atof(tmp);
                if (i == 3 && v[i] <= 1.0f) {
                    v[i] *= 255.0f;
                }
            }
        }
        if (n < 4)
            v[3] = 255;
        out->r = pc__clamp_u8(v[0]);
        out->g = pc__clamp_u8(v[1]);
        out->b = pc__clamp_u8(v[2]);
        out->a = pc__clamp_u8(v[3]);
        return PC_TRUE;
    }
    if (strcmp(func, "hsl") == 0 || strcmp(func, "hsla") == 0) {
        float h = 0, sat = 100, l = 50, a = 255;
        if (n >= 1) {
            char* t = tokens[0];
            if (strstr(t, "rad")) {
                h = (float) atof(t) * (180.0f / (float) 3.14159265f);
            } else if (strstr(t, "turn")) {
                h = (float) atof(t) * 360.0f;
            } else {
                h = (float) atof(t);
            }
        }
        if (n >= 2) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[1];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
            }
            sat = (float) atof(tmp);
        }
        if (n >= 3) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[2];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
            }
            l = (float) atof(tmp);
        }
        if (n >= 4) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[3];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
                a = (float) atof(tmp) / 100.0f * 255.0f;
            } else {
                float av = (float) atof(tmp);
                a = av <= 1.0f ? av * 255.0f : av;
            }
        } else {
            a = 255;
        }
        float r, g, b;
        pc__hsl_to_rgb(h, sat, l, &r, &g, &b);
        out->r = pc__clamp_u8(r);
        out->g = pc__clamp_u8(g);
        out->b = pc__clamp_u8(b);
        out->a = pc__clamp_u8(a);
        return PC_TRUE;
    }
    if (strcmp(func, "hsv") == 0 || strcmp(func, "hsva") == 0 || strcmp(func, "hsb") == 0 ||
        strcmp(func, "hsba") == 0) {
        float h = 0, s = 100, v2 = 100, a = 255;
        if (n >= 1) {
            h = (float) atof(tokens[0]);
        }
        if (n >= 2) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[1];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
            }
            s = (float) atof(tmp);
        }
        if (n >= 3) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[2];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
            }
            v2 = (float) atof(tmp);
        }
        if (n >= 4) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[3];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
                a = (float) atof(tmp) / 100.0f * 255.0f;
            } else {
                float av = (float) atof(tmp);
                a = av <= 1.0f ? av * 255.0f : av;
            }
        } else {
            a = 255;
        }
        float r, g, b;
        pc__hsv_to_rgb(h, s, v2, &r, &g, &b);
        out->r = pc__clamp_u8(r);
        out->g = pc__clamp_u8(g);
        out->b = pc__clamp_u8(b);
        out->a = pc__clamp_u8(a);
        return PC_TRUE;
    }
    if (strcmp(func, "hwb") == 0) {
        float h = 0, w = 0, blk = 0, a = 255;
        if (n >= 1) {
            h = (float) atof(tokens[0]);
        }
        if (n >= 2) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[1];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
            }
            w = (float) atof(tmp);
        }
        if (n >= 3) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[2];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
            }
            blk = (float) atof(tmp);
        }
        if (n >= 4) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[3];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
                a = (float) atof(tmp) / 100.0f * 255.0f;
            } else {
                float av = (float) atof(tmp);
                a = av <= 1.0f ? av * 255.0f : av;
            }
        } else {
            a = 255;
        }
        float r, g, b;
        pc__hwb_to_rgb(h, w, blk, &r, &g, &b);
        out->r = pc__clamp_u8(r);
        out->g = pc__clamp_u8(g);
        out->b = pc__clamp_u8(b);
        out->a = pc__clamp_u8(a);
        return PC_TRUE;
    }
    if (strcmp(func, "cmy") == 0) {
        float cv = 0, m = 0, y = 0;
        if (n >= 1) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[0];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
                cv = (float) atof(tmp) / 100.0f;
            } else {
                cv = (float) atof(tmp);
            }
        }
        if (n >= 2) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[1];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
                m = (float) atof(tmp) / 100.0f;
            } else {
                m = (float) atof(tmp);
            }
        }
        if (n >= 3) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[2];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
                y = (float) atof(tmp) / 100.0f;
            } else {
                y = (float) atof(tmp);
            }
        }
        float r, g, b;
        pc__cmy_to_rgb(cv, m, y, &r, &g, &b);
        out->r = pc__clamp_u8(r);
        out->g = pc__clamp_u8(g);
        out->b = pc__clamp_u8(b);
        out->a = 255;
        return PC_TRUE;
    }
    if (strcmp(func, "cmyk") == 0) {
        float cv = 0, m = 0, y = 0, k = 0;
        float vals[4] = {0, 0, 0, 0};
        int i;
        for (i = 0; i < n && i < 4; i++) {
            char tmp[64];
            int ti = 0;
            const char* t = tokens[i];
            while (*t && ti < 63) {
                tmp[ti++] = *t++;
            }
            tmp[ti] = '\0';
            if (ti > 0 && tmp[ti - 1] == '%') {
                tmp[--ti] = '\0';
                vals[i] = (float) atof(tmp) / 100.0f;
            } else {
                vals[i] = (float) atof(tmp);
            }
        }
        cv = vals[0];
        m = vals[1];
        y = vals[2];
        k = vals[3];
        float r, g, b;
        pc__cmyk_to_rgb(cv, m, y, k, &r, &g, &b);
        out->r = pc__clamp_u8(r);
        out->g = pc__clamp_u8(g);
        out->b = pc__clamp_u8(b);
        out->a = 255;
        return PC_TRUE;
    }
    if (strcmp(func, "lab") == 0) {
        float L = 0, a_ = 0, b_ = 0, alpha = 255;
        if (n >= 1) {
            L = (float) atof(tokens[0]);
        }
        if (n >= 2) {
            a_ = (float) atof(tokens[1]);
        }
        if (n >= 3) {
            b_ = (float) atof(tokens[2]);
        }
        if (n >= 4) {
            float av = (float) atof(tokens[3]);
            alpha = av <= 1.0f ? av * 255.0f : av;
        }
        float r, g, b2;
        pc__lab_to_rgb(L, a_, b_, &r, &g, &b2);
        out->r = pc__clamp_u8(r);
        out->g = pc__clamp_u8(g);
        out->b = pc__clamp_u8(b2);
        out->a = pc__clamp_u8(alpha);
        return PC_TRUE;
    }
    if (strcmp(func, "lch") == 0) {
        float L = 0, C = 0, H = 0, alpha = 255;
        if (n >= 1) {
            L = (float) atof(tokens[0]);
        }
        if (n >= 2) {
            C = (float) atof(tokens[1]);
        }
        if (n >= 3) {
            H = (float) atof(tokens[2]);
        }
        if (n >= 4) {
            float av = (float) atof(tokens[3]);
            alpha = av <= 1.0f ? av * 255.0f : av;
        }
        float r, g, b2;
        pc__lch_to_rgb(L, C, H, &r, &g, &b2);
        out->r = pc__clamp_u8(r);
        out->g = pc__clamp_u8(g);
        out->b = pc__clamp_u8(b2);
        out->a = pc__clamp_u8(alpha);
        return PC_TRUE;
    }
    if (strcmp(func, "oklab") == 0) {
        float L = 0, a_ = 0, b_ = 0, alpha = 255;
        if (n >= 1) {
            L = (float) atof(tokens[0]);
        }
        if (n >= 2) {
            a_ = (float) atof(tokens[1]);
        }
        if (n >= 3) {
            b_ = (float) atof(tokens[2]);
        }
        if (n >= 4) {
            float av = (float) atof(tokens[3]);
            alpha = av <= 1.0f ? av * 255.0f : av;
        }
        float r, g, b2;
        pc__oklab_to_rgb(L, a_, b_, &r, &g, &b2);
        out->r = pc__clamp_u8(r);
        out->g = pc__clamp_u8(g);
        out->b = pc__clamp_u8(b2);
        out->a = pc__clamp_u8(alpha);
        return PC_TRUE;
    }
    if (strcmp(func, "oklch") == 0) {
        float L = 0, C = 0, H = 0, alpha = 255;
        if (n >= 1) {
            L = (float) atof(tokens[0]);
        }
        if (n >= 2) {
            C = (float) atof(tokens[1]);
        }
        if (n >= 3) {
            H = (float) atof(tokens[2]);
        }
        if (n >= 4) {
            float av = (float) atof(tokens[3]);
            alpha = av <= 1.0f ? av * 255.0f : av;
        }
        float r, g, b2;
        pc__oklch_to_rgb(L, C, H, &r, &g, &b2);
        out->r = pc__clamp_u8(r);
        out->g = pc__clamp_u8(g);
        out->b = pc__clamp_u8(b2);
        out->a = pc__clamp_u8(alpha);
        return PC_TRUE;
    }
    return PC_FALSE;
}

/* color(srgb r g b) or color(display-p3 r g b) */
static pc_bool pc__parse_color_func(const char* s, SDL_Color* out) {
    if (strncasecmp(s, "color(", 6) != 0)
        return PC_FALSE;
    const char* inner = s + 6;
    const char* rp = strrchr(inner, ')');
    if (!rp)
        return PC_FALSE;
    char buf[128];
    int bl = (int) (rp - inner);
    if (bl < 0 || bl >= 128)
        return PC_FALSE;
    memcpy(buf, inner, bl);
    buf[bl] = '\0';

    /* Parse colorspace as first whitespace-delimited token */
    char cs[32];
    int ci = 0;
    const char* p = buf;
    while (*p == ' ')
        p++;
    while (*p && *p != ' ' && ci < 31) {
        cs[ci++] = *p++;
    }
    cs[ci] = '\0';
    while (*p == ' ')
        p++;

    pc__lower(cs);
    if (strcmp(cs, "srgb") == 0 || strcmp(cs, "display-p3") == 0 || strcmp(cs, "srgb-linear") == 0) {
        float rv = 0, gv = 0, bv = 0, av = 1.0f;
        /* parse up to 3 channel values, then optional / alpha */
        char rest[96];
        int ri = 0;
        while (*p && ri < 95) {
            rest[ri++] = *p++;
        }
        rest[ri] = '\0';

        /* strip alpha after slash */
        char* sl = strchr(rest, '/');
        if (sl) {
            *sl = '\0';
            char atmp[32];
            int ai = 0;
            const char* ap = sl + 1;
            while (*ap == ' ')
                ap++;
            while (*ap && ai < 31) {
                atmp[ai++] = *ap++;
            }
            atmp[ai] = '\0';
            if (ai > 0 && atmp[ai - 1] == '%') {
                atmp[--ai] = '\0';
                av = (float) atof(atmp) / 100.0f;
            } else {
                av = (float) atof(atmp);
            }
        }
        sscanf(rest, "%f %f %f", &rv, &gv, &bv);
        if (strcmp(cs, "srgb-linear") == 0) {
            rv = pc__srgb(rv);
            gv = pc__srgb(gv);
            bv = pc__srgb(bv);
        }
        out->r = pc__f01_to_u8(rv);
        out->g = pc__f01_to_u8(gv);
        out->b = pc__f01_to_u8(bv);
        out->a = pc__f01_to_u8(av);
        return PC_TRUE;
    }
    return PC_FALSE;
}

/* JSON-ish: {"r":255,"g":0,"b":0} or {r:255,g:0,b:0} */
static pc_bool pc__parse_json(const char* s, SDL_Color* out) {
    const char* p = s;
    int r = -1, g = -1, b = -1, a = 255;
    if (*p != '{')
        return PC_FALSE;
    p++;
    while (*p && *p != '}') {
        while (*p == ' ' || *p == ',' || *p == '"' || *p == '\'')
            p++;
        if (*p == '}')
            break;
        char key[16];
        int ki = 0;
        while (*p && *p != '"' && *p != '\'' && *p != ':' && *p != ' ' && ki < 15) {
            key[ki++] = *p++;
        }
        key[ki] = '\0';
        while (*p == ' ' || *p == '"' || *p == '\'')
            p++;
        if (*p == ':')
            p++;
        while (*p == ' ' || *p == '"' || *p == '\'')
            p++;
        char val[16];
        int vi = 0;
        while (*p && *p != ',' && *p != '}' && *p != '"' && *p != '\'' && vi < 15) {
            val[vi++] = *p++;
        }
        val[vi] = '\0';
        int ival = atoi(val);
        if (strcmp(key, "r") == 0 || strcmp(key, "red") == 0)
            r = ival;
        else if (strcmp(key, "g") == 0 || strcmp(key, "green") == 0)
            g = ival;
        else if (strcmp(key, "b") == 0 || strcmp(key, "blue") == 0)
            b = ival;
        else if (strcmp(key, "a") == 0 || strcmp(key, "alpha") == 0)
            a = ival;
    }
    if (r < 0 || g < 0 || b < 0)
        return PC_FALSE;
    out->r = (uint8_t) r;
    out->g = (uint8_t) g;
    out->b = (uint8_t) b;
    out->a = (uint8_t) a;
    return PC_TRUE;
}

/* key=value: r=255 g=0 b=0 a=255 */
static pc_bool pc__parse_keyvalue(const char* s, SDL_Color* out) {
    int r = -1, g = -1, b = -1, a = 255;
    const char* p = s;

    if (strncasecmp(p, "rgba=", 5) == 0) {
        char tmp[64];
        int i = 0;
        p += 5;
        while (*p && i < 63) {
            tmp[i++] = *p++;
        }
        tmp[i] = '\0';
        char tokens[5][64];
        int n = pc__split_sep(tmp, ',', tokens, 4);
        if (n < 3)
            n = pc__split_sep(tmp, ' ', tokens, 4);
        if (n < 3)
            return PC_FALSE;
        r = atoi(tokens[0]);
        g = atoi(tokens[1]);
        b = atoi(tokens[2]);
        if (n >= 4)
            a = atoi(tokens[3]);
        out->r = (uint8_t) r;
        out->g = (uint8_t) g;
        out->b = (uint8_t) b;
        out->a = (uint8_t) a;
        return PC_TRUE;
    }
    if (strncasecmp(p, "rgb=", 4) == 0) {
        char tmp[64];
        int i = 0;
        p += 4;
        while (*p && i < 63) {
            tmp[i++] = *p++;
        }
        tmp[i] = '\0';
        char tokens[5][64];
        int n = pc__split_sep(tmp, ',', tokens, 4);
        if (n < 3)
            n = pc__split_sep(tmp, ' ', tokens, 4);
        if (n < 3)
            return PC_FALSE;
        r = atoi(tokens[0]);
        g = atoi(tokens[1]);
        b = atoi(tokens[2]);
        if (n >= 4)
            a = atoi(tokens[3]);
        out->r = (uint8_t) r;
        out->g = (uint8_t) g;
        out->b = (uint8_t) b;
        out->a = (uint8_t) a;
        return PC_TRUE;
    }

    if (!strchr(s, '='))
        return PC_FALSE;
    char buf[256];
    strncpy(buf, s, 255);
    buf[255] = '\0';
    int i;
    for (i = 0; buf[i]; i++) {
        if (buf[i] == ',')
            buf[i] = ' ';
    }

    char *tok, *saveptr = NULL;
#ifdef _WIN32
    tok = strtok_s(buf, " ", &saveptr);
#else
    tok = strtok_r(buf, " ", &saveptr);
#endif
    while (tok) {
        char* eq = strchr(tok, '=');
        if (eq) {
            char key[8];
            int kl = (int) (eq - tok);
            if (kl > 7)
                kl = 7;
            memcpy(key, tok, kl);
            key[kl] = '\0';
            pc__lower(key);
            int val = atoi(eq + 1);
            if (strcmp(key, "r") == 0)
                r = val;
            else if (strcmp(key, "g") == 0)
                g = val;
            else if (strcmp(key, "b") == 0)
                b = val;
            else if (strcmp(key, "a") == 0)
                a = val;
        }
#ifdef _WIN32
        tok = strtok_s(NULL, " ", &saveptr);
#else
        tok = strtok_r(NULL, " ", &saveptr);
#endif
    }
    if (r < 0 || g < 0 || b < 0)
        return PC_FALSE;
    out->r = (uint8_t) r;
    out->g = (uint8_t) g;
    out->b = (uint8_t) b;
    out->a = (uint8_t) a;
    return PC_TRUE;
}

/* bracket wrappers: (r,g,b) [r,g,b] {r,g,b} SDL_Color{...} SDL_Color(...) */
static pc_bool pc__parse_bracketed(const char* s, SDL_Color* out) {
    const char* p = s;
    char inner[128];
    int il;

    if (strncasecmp(p, "sdl_color", 9) == 0)
        p += 9;
    while (*p == ' ')
        p++;

    char open = *p, close = 0;
    if (open == '(')
        close = ')';
    else if (open == '[')
        close = ']';
    else if (open == '{')
        close = '}';
    else
        return PC_FALSE;

    p++;
    const char* ep = strrchr(p, close);
    if (!ep)
        return PC_FALSE;
    il = (int) (ep - p);
    if (il < 0 || il >= 128)
        return PC_FALSE;
    memcpy(inner, p, il);
    inner[il] = '\0';

    char tokens[5][64];
    int n = pc__split_sep(inner, ',', tokens, 5);
    if (n < 3)
        n = pc__split_sep(inner, ' ', tokens, 5);
    if (n < 3)
        return PC_FALSE;

    float v[4] = {0, 0, 0, 255};
    int i;
    pc_bool all_byte = PC_TRUE;
    for (i = 0; i < n && i < 4; i++) {
        v[i] = (float) atof(tokens[i]);
        if (v[i] > 1.0f)
            all_byte = PC_TRUE;
        if (strchr(tokens[i], '.') != NULL && v[i] <= 1.0f)
            all_byte = PC_FALSE;
    }
    if (!all_byte) {
        out->r = pc__f01_to_u8(v[0]);
        out->g = pc__f01_to_u8(v[1]);
        out->b = pc__f01_to_u8(v[2]);
        out->a = pc__f01_to_u8(n >= 4 ? v[3] : 1.0f);
    } else {
        out->r = pc__clamp_u8(v[0]);
        out->g = pc__clamp_u8(v[1]);
        out->b = pc__clamp_u8(v[2]);
        out->a = pc__clamp_u8(n >= 4 ? v[3] : 255);
    }
    return PC_TRUE;
}

/* percentage CSV: 100%,0%,0% */
static pc_bool pc__parse_percent_csv(const char* s, SDL_Color* out) {
    if (!strchr(s, '%'))
        return PC_FALSE;
    char buf[128];
    strncpy(buf, s, 127);
    buf[127] = '\0';
    char tokens[5][64];
    int n = pc__split_sep(buf, ',', tokens, 5);
    if (n < 3)
        n = pc__split_sep(buf, ' ', tokens, 5);
    if (n < 3)
        return PC_FALSE;
    float v[4] = {0, 0, 0, 255};
    int i;
    for (i = 0; i < n && i < 4; i++) {
        char tmp[64];
        int ti = 0;
        const char* t = tokens[i];
        while (*t && ti < 63) {
            tmp[ti++] = *t++;
        }
        tmp[ti] = '\0';
        if (ti > 0 && tmp[ti - 1] == '%') {
            tmp[--ti] = '\0';
            v[i] = (float) atof(tmp) / 100.0f * 255.0f;
        } else if (ti > 0) {
            v[i] = (float) atof(tmp);
            if (i == 3 && v[i] <= 1.0f)
                v[i] *= 255.0f;
        }
    }
    if (n < 4)
        v[3] = 255;
    out->r = pc__clamp_u8(v[0]);
    out->g = pc__clamp_u8(v[1]);
    out->b = pc__clamp_u8(v[2]);
    out->a = pc__clamp_u8(v[3]);
    return PC_TRUE;
}

/* float CSV / space-sep: 1.0,0.0,0.0 or 1.0 0.0 0.0 */
static pc_bool pc__parse_float_tuple(const char* s, SDL_Color* out) {
    if (!strchr(s, '.'))
        return PC_FALSE;
    char buf[128];
    strncpy(buf, s, 127);
    buf[127] = '\0';
    char tokens[5][64];
    int n = pc__split_sep(buf, ',', tokens, 5);
    if (n < 3)
        n = pc__split_sep(buf, ' ', tokens, 5);
    if (n < 3)
        return PC_FALSE;
    float v[4] = {0, 0, 0, 1};
    int i;
    pc_bool any_gt1 = PC_FALSE;
    for (i = 0; i < n && i < 4; i++) {
        v[i] = (float) atof(tokens[i]);
        if (v[i] > 1.0f)
            any_gt1 = PC_TRUE;
    }
    if (any_gt1) {
        out->r = pc__clamp_u8(v[0]);
        out->g = pc__clamp_u8(v[1]);
        out->b = pc__clamp_u8(v[2]);
        out->a = pc__clamp_u8(n >= 4 ? v[3] : 255);
    } else {
        out->r = pc__f01_to_u8(v[0]);
        out->g = pc__f01_to_u8(v[1]);
        out->b = pc__f01_to_u8(v[2]);
        out->a = pc__f01_to_u8(n >= 4 ? v[3] : 1.0f);
    }
    return PC_TRUE;
}

/* integer CSV / space / semicolon */
static pc_bool pc__parse_int_tuple(const char* s, SDL_Color* out) {
    char buf[128];
    strncpy(buf, s, 127);
    buf[127] = '\0';
    int i;
    for (i = 0; buf[i]; i++) {
        if (buf[i] == ';')
            buf[i] = ',';
    }
    char tokens[5][64];
    int n = pc__split_sep(buf, ',', tokens, 5);
    if (n < 3)
        n = pc__split_sep(buf, ' ', tokens, 5);
    if (n < 3)
        return PC_FALSE;
    for (i = 0; i < n; i++) {
        const char* t = tokens[i];
        while (*t == ' ')
            t++;
        if (!*t)
            return PC_FALSE;
        if (*t == '-')
            t++;
        if (!isdigit((unsigned char) *t))
            return PC_FALSE;
    }
    float v[4] = {0, 0, 0, 255};
    for (i = 0; i < n && i < 4; i++) {
        v[i] = (float) atoi(tokens[i]);
    }
    if (n < 4)
        v[3] = 255;
    out->r = pc__clamp_u8(v[0]);
    out->g = pc__clamp_u8(v[1]);
    out->b = pc__clamp_u8(v[2]);
    out->a = pc__clamp_u8(v[3]);
    return PC_TRUE;
}

/* packed integer: 16711680 -> 0xRRGGBB */
static pc_bool pc__parse_packed_int(const char* s, SDL_Color* out) {
    const char* p = s;
    while (*p == ' ')
        p++;
    if (!isdigit((unsigned char) *p))
        return PC_FALSE;
    const char* q = p;
    while (isdigit((unsigned char) *q))
        q++;
    if (*q != '\0')
        return PC_FALSE;
    unsigned long long v = (unsigned long long) strtoull(p, NULL, 10);
    if (v > 0xFFFFFFFFULL)
        return PC_FALSE;
    if (v > 0xFFFFFFULL) {
        out->r = (uint8_t) ((v >> 24) & 0xFF);
        out->g = (uint8_t) ((v >> 16) & 0xFF);
        out->b = (uint8_t) ((v >> 8) & 0xFF);
        out->a = (uint8_t) (v & 0xFF);
    } else {
        out->r = (uint8_t) ((v >> 16) & 0xFF);
        out->g = (uint8_t) ((v >> 8) & 0xFF);
        out->b = (uint8_t) (v & 0xFF);
        out->a = 255;
    }
    return PC_TRUE;
}

/* bare hex (no prefix, no #) - 6 or 8 hex chars */
static pc_bool pc__is_bare_hex(const char* s) {
    int len = 0;
    for (; s[len]; len++) {
        char c = s[len];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return PC_FALSE;
    }
    return (len == 6 || len == 8) ? PC_TRUE : PC_FALSE;
}

/* ====================================================================
 * NATURAL LANGUAGE COLOR PARSER
 *
 * Parses phrases like "bright red", "pastel pink", "dark vivid blue",
 * "slightly transparent orange", "warm grey", "neon green", etc.
 *
 * Strategy:
 *   1. Tokenize the phrase into words.
 *   2. Scan tokens for recognized adjectives (modifiers); collect them.
 *   3. The remaining token(s) form the base color name - look it up.
 *   4. Convert base color to HSL.
 *   5. Apply adjective effects as HSL/alpha deltas in a defined order:
 *      temperature -> lightness group -> saturation group -> alpha group.
 *   6. Convert back to RGB.
 *
 * This is compositional: "dark muted blue" = dark + muted + blue,
 * each modifier stacking on the result of the previous.
 * ==================================================================== */

/* Each adjective maps to HSL deltas (dH in degrees, dS and dL in pct-points)
 * and an alpha multiplier. dH is applied as a hue rotate for warm/cool words.
 */
typedef struct {
    const char* word;
    float dH; /* hue shift degrees          */
    float dS; /* saturation delta (0..100)  */
    float dL; /* lightness delta  (0..100)  */
    float dA; /* alpha delta (0..255)       */
} pc__NLAdj;

static const pc__NLAdj pc__nl_adjectives[] = {
    /* lightness modifiers */
    {"bright",               0,   10,  20,  0   },
    {"light",                0,   0,   25,  0   },
    {"lite",                 0,   0,   20,  0   },
    {"lighter",              0,   0,   20,  0   },
    {"slightly lighter",     0,   0,   10,  0   },
    {"much lighter",         0,   0,   35,  0   },
    {"dark",                 0,   0,   -25, 0   },
    {"darker",               0,   0,   -20, 0   },
    {"slightly darker",      0,   0,   -10, 0   },
    {"much darker",          0,   0,   -35, 0   },
    {"deep",                 0,   10,  -20, 0   },
    {"mid",                  0,   0,   -5,  0   },
    /* saturation / vividness modifiers */
    {"vivid",                0,   30,  5,   0   },
    {"vibrant",              0,   30,  5,   0   },
    {"rich",                 0,   20,  -5,  0   },
    {"saturated",            0,   25,  0,   0   },
    {"bold",                 0,   20,  -5,  0   },
    {"intense",              0,   25,  -10, 0   },
    {"strong",               0,   20,  -5,  0   },
    {"pure",                 0,   35,  5,   0   },
    {"neon",                 0,   50,  15,  0   },
    {"electric",             0,   45,  10,  0   },
    {"fluorescent",          0,   50,  15,  0   },
    {"muted",                0,   -30, 5,   0   },
    {"dull",                 0,   -30, -5,  0   },
    {"washed",               0,   -25, 15,  0   },
    {"faded",                0,   -25, 15,  0   },
    {"desaturated",          0,   -40, 0,   0   },
    {"grayish",              0,   -35, 0,   0   },
    {"greyish",              0,   -35, 0,   0   },
    /* combined lightness+saturation presets */
    {"pastel",               0,   -25, 25,  0   },
    {"pale",                 0,   -20, 22,  0   },
    {"soft",                 0,   -20, 18,  0   },
    {"gentle",               0,   -15, 15,  0   },
    {"subtle",               0,   -20, 12,  0   },
    {"dusty",                0,   -20, -5,  0   },
    {"earthy",               0,   -15, -10, 0   },
    {"mellow",               0,   -15, 10,  0   },
    {"smoky",                0,   -20, -10, 0   },
    {"shadowy",              0,   -5,  -20, 0   },
    {"brilliant",            0,   20,  15,  0   },
    {"luminous",             0,   15,  20,  0   },
    {"radiant",              0,   15,  18,  0   },
    /* temperature / hue bias */
    {"warm",                 15,  5,   0,   0   },
    {"cool",                 -15, 5,   0,   0   },
    {"cold",                 -20, 5,   -5,  0   },
    {"hot",                  10,  15,  0,   0   },
    {"icy",                  -20, -10, 20,  0   },
    {"fiery",                10,  20,  -5,  0   },
    {"sunny",                10,  15,  15,  0   },
    {"golden",               10,  20,  5,   0   },
    {"sandy",                5,   -5,  10,  0   },
    /* alpha modifiers */
    {"transparent",          0,   0,   0,   -255},
    {"semi-transparent",     0,   0,   0,   -127},
    {"translucent",          0,   0,   0,   -127},
    {"slightly transparent", 0,   0,   0,   -60 },
    {"mostly opaque",        0,   0,   0,   -40 },
    {"opaque",               0,   0,   0,   255 }, /* clamp will cap at 255 */
    {NULL,                   0,   0,   0,   0   }
};

/*
 * Greedy multi-word adjective match: try to match the longest adjective
 * phrase starting at position pos in the token array.
 * Returns the number of tokens consumed, or 0 if none matched.
 * Fills *adj_out with a pointer to the matching entry.
 */
static int pc__nl_match_adj(const char* const* tokens, int ntok, int pos, const pc__NLAdj** adj_out) {
    /* Try longest match first: build phrases of decreasing length */
    int max_words = 3; /* longest adjective phrase is 3 words */
    int try_len;
    for (try_len = max_words; try_len >= 1; try_len--) {
        if (pos + try_len > ntok)
            continue;
        /* build candidate phrase */
        char phrase[64];
        int pi = 0;
        int w;
        for (w = 0; w < try_len && pi < 62; w++) {
            if (w > 0 && pi < 62) {
                phrase[pi++] = ' ';
            }
            const char* t = tokens[pos + w];
            while (*t && pi < 62) {
                phrase[pi++] = *t++;
            }
        }
        phrase[pi] = '\0';
        /* search adjective table */
        int i;
        for (i = 0; pc__nl_adjectives[i].word; i++) {
            if (strcmp(phrase, pc__nl_adjectives[i].word) == 0) {
                *adj_out = &pc__nl_adjectives[i];
                return try_len;
            }
        }
    }
    return 0;
}

static pc_bool pc__parse_natural(const char* s, SDL_Color* out) {
    /* Tokenize */
    char buf[128];
    int bi = 0;
    const char* p = s;
    while (*p && bi < 127) {
        buf[bi++] = (char) tolower((unsigned char) *p++);
    }
    buf[bi] = '\0';

    /* split on spaces, hyphens (treat as spaces for multi-word adj matching) */
    /* we keep hyphens as-is and let phrase matching handle "semi-transparent" */
    const char* tokens_raw[16];
    int ntok = 0;
    char tokbuf[128];
    memcpy(tokbuf, buf, 128);
    char* t = tokbuf;
    char* tok;
    /* simple space tokenize */
    tok = strtok(t, " ");
    while (tok && ntok < 16) {
        tokens_raw[ntok++] = tok;
        tok = strtok(NULL, " ");
    }

    if (ntok == 0)
        return PC_FALSE;

    /* Collect adjectives: scan left-to-right, greedy */
    const pc__NLAdj* adjs[16];
    int nadj = 0;
    pc_bool used[16];
    int i;
    for (i = 0; i < ntok; i++)
        used[i] = PC_FALSE;

    i = 0;
    while (i < ntok) {
        const pc__NLAdj* match = NULL;
        int consumed = pc__nl_match_adj(tokens_raw, ntok, i, &match);
        if (consumed > 0 && nadj < 16) {
            adjs[nadj++] = match;
            int j;
            for (j = i; j < i + consumed; j++)
                used[j] = PC_TRUE;
            i += consumed;
        } else {
            i++;
        }
    }

    /* Remaining unused tokens form the base color name */
    char base[64];
    int blen = 0;
    for (i = 0; i < ntok; i++) {
        if (!used[i]) {
            if (blen > 0 && blen < 63) {
                base[blen++] = ' ';
            }
            const char* w = tokens_raw[i];
            while (*w && blen < 63) {
                base[blen++] = *w++;
            }
        }
    }
    base[blen] = '\0';
    if (blen == 0)
        return PC_FALSE;

    /* Look up base color */
    SDL_Color basecolor;
    if (!pc__lookup_named(base, &basecolor))
        return PC_FALSE;

    /* No adjectives? Already handled by named lookup above, but just in case */
    if (nadj == 0) {
        *out = basecolor;
        return PC_TRUE;
    }

    /* Convert base to HSL */
    float h, s_, l_;
    pc__rgb_to_hsl((float) basecolor.r, (float) basecolor.g, (float) basecolor.b, &h, &s_, &l_);
    float a = (float) basecolor.a;

    /* Apply adjectives in order */
    int j;
    for (j = 0; j < nadj; j++) {
        const pc__NLAdj* adj = adjs[j];
        h += adj->dH;
        s_ += adj->dS;

        // Scale lightness delta: if pushing lighter on an already-light color,
        // reduce the effect proportionally to avoid blowing out to white
        float dl = adj->dL;
        if (dl > 0 && l_ > 70.0f) {
            dl *= (100.0f - l_) / 30.0f; // fade out as we approach 100
        }
        l_ += dl;

        a += adj->dA;
        h = fmodf(h, 360.0f);
        if (h < 0)
            h += 360.0f;
        if (s_ < 0)
            s_ = 0;
        if (s_ > 100)
            s_ = 100;
        if (l_ < 0)
            l_ = 0;
        if (l_ > 96)
            l_ = 96; // hard cap at 96 not 100
        if (a < 0)
            a = 0;
        if (a > 255)
            a = 255;
    }

    /* Convert back to RGB */
    float r, g2, b2;
    pc__hsl_to_rgb(h, s_, l_, &r, &g2, &b2);
    out->r = pc__clamp_u8(r);
    out->g = pc__clamp_u8(g2);
    out->b = pc__clamp_u8(b2);
    out->a = pc__clamp_u8(a);
    return PC_TRUE;
}

/* ---- alpha modifier: red@50%, red/0.5 ---- */
static pc_bool pc__check_alpha_modifier(const char* s, char* base_out, float* alpha_out) {
    const char* p;
    char sep = 0;
    int depth = 0;
    for (p = s; *p; p++) {
        if (*p == '(' || *p == '[' || *p == '{')
            depth++;
        else if (*p == ')' || *p == ']' || *p == '}')
            depth--;
        else if (depth == 0 && (*p == '@' || *p == '/')) {
            sep = *p;
            break;
        }
    }
    if (!sep)
        return PC_FALSE;
    int bl = (int) (p - s);
    if (bl <= 0 || bl >= 128)
        return PC_FALSE;
    memcpy(base_out, s, bl);
    base_out[bl] = '\0';
    while (bl > 0 && base_out[bl - 1] == ' ') {
        base_out[--bl] = '\0';
    }

    const char* ap = p + 1;
    while (*ap == ' ')
        ap++;
    char atmp[32];
    int ai = 0;
    while (*ap && ai < 31) {
        atmp[ai++] = *ap++;
    }
    atmp[ai] = '\0';
    if (ai > 0 && atmp[ai - 1] == '%') {
        atmp[--ai] = '\0';
        *alpha_out = (float) atof(atmp) / 100.0f;
    } else {
        *alpha_out = (float) atof(atmp);
    }
    return PC_TRUE;
}

/* ================================================================
 * MAIN API
 * ================================================================ */

pc_bool parse_color(const char* s, SDL_Color* out) {
    if (!s || !out)
        return PC_FALSE;

    /* 1. trim whitespace, tabs, newlines, collapse spaces */
    char buf[512];
    pc__trim(s, buf, sizeof(buf));

    /* 2. strip outer quotes */
    char qbuf[512];
    if (pc__strip_quotes(buf, qbuf, sizeof(qbuf))) {
        pc__trim(qbuf, buf, sizeof(buf));
    }

    if (!buf[0])
        return PC_FALSE;

    /* 3. check for alpha modifier (red@0.5, red/50%) */
    {
        char base[256];
        float alpha_f;
        if (pc__check_alpha_modifier(buf, base, &alpha_f)) {
            SDL_Color tmp;
            if (parse_color(base, &tmp)) {
                float av = alpha_f > 1.0f ? alpha_f : alpha_f * 255.0f;
                tmp.a = pc__clamp_u8(av);
                *out = tmp;
                return PC_TRUE;
            }
        }
    }

    /* 4. SDL_Color prefix */
    {
        const char* p = buf;
        if (strncasecmp(p, "sdl_color", 9) == 0) {
            char sub[256];
            snprintf(sub, sizeof(sub), "%s", p + 9);
            pc__trim(sub, buf, sizeof(buf));
        }
    }

    /* 5. named color (includes 'transparent') */
    if (pc__lookup_named(buf, out))
        return PC_TRUE;

    /* 6. natural language ("pastel pink", "dark vivid blue", ...) */
    if (pc__parse_natural(buf, out))
        return PC_TRUE;

    /* 7. hex: #... or 0x... */
    if (buf[0] == '#' || (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X'))) {
        return pc__parse_hex(buf, out);
    }

    /* 8. color(...) */
    if (strncasecmp(buf, "color(", 6) == 0) {
        return pc__parse_color_func(buf, out);
    }

    /* 9. functional: rgb/rgba/hsl/... */
    if (strchr(buf, '(')) {
        const char* lp = strchr(buf, '(');
        char fn[16];
        int fl = (int) (lp - buf);
        if (fl < 16) {
            memcpy(fn, buf, fl);
            fn[fl] = '\0';
            pc__lower(fn);
            if (strcmp(fn, "rgb") == 0 || strcmp(fn, "rgba") == 0 || strcmp(fn, "hsl") == 0 ||
                strcmp(fn, "hsla") == 0 || strcmp(fn, "hsv") == 0 || strcmp(fn, "hsva") == 0 ||
                strcmp(fn, "hsb") == 0 || strcmp(fn, "hsba") == 0 || strcmp(fn, "hwb") == 0 || strcmp(fn, "cmy") == 0 ||
                strcmp(fn, "cmyk") == 0 || strcmp(fn, "lab") == 0 || strcmp(fn, "lch") == 0 ||
                strcmp(fn, "oklab") == 0 || strcmp(fn, "oklch") == 0) {
                return pc__parse_functional(buf, out);
            }
        }
        return pc__parse_bracketed(buf, out);
    }

    /* 10. brackets [ ] { } */
    if (buf[0] == '[' || buf[0] == '{' || buf[0] == '(') {
        if (buf[0] == '{') {
            if (strchr(buf, ':')) {
                if (pc__parse_json(buf, out))
                    return PC_TRUE;
            }
        }
        return pc__parse_bracketed(buf, out);
    }

    /* 11. key=value */
    if (strchr(buf, '=')) {
        if (pc__parse_keyvalue(buf, out))
            return PC_TRUE;
    }

    /* 12. bare hex (ff0000 / ff0000ff) */
    if (pc__is_bare_hex(buf)) {
        return pc__parse_hex(buf, out);
    }

    /* 13. percentage CSV */
    if (strchr(buf, '%')) {
        if (pc__parse_percent_csv(buf, out))
            return PC_TRUE;
    }

    /* 14. float tuple */
    if (strchr(buf, '.')) {
        if (pc__parse_float_tuple(buf, out))
            return PC_TRUE;
    }

    /* 15. int tuple (comma / space / semicolon) */
    if (strchr(buf, ',') || strchr(buf, ' ') || strchr(buf, ';')) {
        if (pc__parse_int_tuple(buf, out))
            return PC_TRUE;
    }

    /* 16. packed integer */
    if (pc__parse_packed_int(buf, out))
        return PC_TRUE;

    return PC_FALSE;
}

#endif /* PARSE_COLOR_IMPLEMENTATION */
#endif /* PARSE_COLOR_H */
