#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif
#include <string.h>
#include <math.h>
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
#define USE_SSE2_SIMD 1
#include <emmintrin.h>
#endif
#include "../thirdparty/SDL2/SDL.h"
#include "../thirdparty/tinyfd.h"
#include "../thirdparty/nob.h"
#include "config.h"

typedef enum {
    TOOL_PEN,
    TOOL_ERASER,
    TOOL_MARKER,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_CIRCLE,
    TOOL_FILLED_RECT,
    TOOL_FILLED_CIRCLE,
} DrawTool;

typedef struct {
    int x, y;
} Point;

typedef struct {
    Point* points;
    int point_count;
    int point_capacity;
    SDL_Color color;
    int size;
    DrawTool tool;
    Point start;
    Point end;
} Stroke;

typedef struct {
    Uint8*    buf;
    Uint8*    cover;
    int       buf_w;
    int       buf_h;
    int       valid;
    int       dirty;
    SDL_Rect  dirty_rect;
    Uint32    fmt;
    int       alpha_offset;
} MarkerBatch;

typedef struct {
    Stroke strokes[MAX_DRAW_STROKES];
    int stroke_count;
    int last_rendered_stroke_count;
    int needs_full_redraw;

    int undo_stack[MAX_DRAW_UNDO_STACK];
    int undo_count;
    int redo_stack[MAX_DRAW_UNDO_STACK];
    int redo_count;

    DrawTool  current_tool;
    SDL_Color current_color;
    SDL_Color custom_color;
    int       brush_size;

    int   is_drawing;
    Point last_point;
    Point smooth_point;
    Point shape_start;

    int show_palette;
    int zoom_percent;
    int pan_x;
    int pan_y;

    MarkerBatch marker;

    SDL_Rect last_video_dst;
    double   last_video_time;
} DrawingState;

static SDL_Color draw_palette[7];
static const int draw_palette_count = 7;


static void marker_buf_free(MarkerBatch* m) {
    if (!m || !m->buf) return;
#ifdef USE_SSE2_SIMD
    _mm_free(m->buf);
#else
    free(m->buf);
#endif
    free(m->cover);
    m->buf   = NULL;
    m->cover = NULL;
    m->buf_w = 0;
    m->buf_h = 0;
    m->valid = 0;
    m->dirty = 0;
}

static int marker_buf_ensure(MarkerBatch* m, int w, int h, Uint32 fmt) {
    if (!m) return 0;
    if (m->buf && m->buf_w == w && m->buf_h == h && m->fmt == fmt) return 1;

    marker_buf_free(m);
    int total = w * h * 4;
#ifdef USE_SSE2_SIMD
    m->buf = (Uint8*)_mm_malloc(total, 16);
#else
    m->buf = (Uint8*)malloc(total);
#endif
    if (!m->buf) return 0;
    m->cover = (Uint8*)calloc(w * h, 1);
    if (!m->cover) {
#ifdef USE_SSE2_SIMD
        _mm_free(m->buf);
#else
        free(m->buf);
#endif
        m->buf = NULL;
        return 0;
    }
    m->buf_w        = w;
    m->buf_h        = h;
    m->fmt          = fmt;
    {
        SDL_PixelFormat* pf = SDL_AllocFormat(fmt);
        m->alpha_offset = pf ? (int)(pf->Ashift / 8) : 3;
        if (pf) SDL_FreeFormat(pf);
    }
    m->valid        = 0;
    m->dirty        = 0;
    return 1;
}

static void marker_expand_dirty(MarkerBatch* m, int cx, int cy, int r) {
    int x1 = cx - r; if (x1 < 0) x1 = 0;
    int y1 = cy - r; if (y1 < 0) y1 = 0;
    int x2 = cx + r; if (x2 >= m->buf_w) x2 = m->buf_w - 1;
    int y2 = cy + r; if (y2 >= m->buf_h) y2 = m->buf_h - 1;
    if (x2 < x1 || y2 < y1) return;

    if (!m->dirty) {
        m->dirty_rect = (SDL_Rect){x1, y1, x2 - x1 + 1, y2 - y1 + 1};
        m->dirty = 1;
    } else {
        int ox1 = m->dirty_rect.x;
        int oy1 = m->dirty_rect.y;
        int ox2 = ox1 + m->dirty_rect.w - 1;
        int oy2 = oy1 + m->dirty_rect.h - 1;
        if (x1 < ox1) ox1 = x1;
        if (y1 < oy1) oy1 = y1;
        if (x2 > ox2) ox2 = x2;
        if (y2 > oy2) oy2 = y2;
        m->dirty_rect = (SDL_Rect){ox1, oy1, ox2 - ox1 + 1, oy2 - oy1 + 1};
    }
}

static void marker_draw_circle(MarkerBatch* m, int cx, int cy, int radius, SDL_Color color) {
    if (!m || !m->cover) return;
    (void)color;
    int x1 = cx - radius; if (x1 < 0) x1 = 0;
    int y1 = cy - radius; if (y1 < 0) y1 = 0;
    int x2 = cx + radius; if (x2 >= m->buf_w) x2 = m->buf_w - 1;
    int y2 = cy + radius; if (y2 >= m->buf_h) y2 = m->buf_h - 1;

    int r2 = radius * radius;

    for (int py = y1; py <= y2; py++) {
        int dy  = py - cy;
        int dy2 = dy * dy;
        if (dy2 > r2) continue;
        int dx_max = (int)sqrtf((float)(r2 - dy2));
        int sx = cx - dx_max; if (sx < 0) sx = 0;
        int ex = cx + dx_max; if (ex >= m->buf_w) ex = m->buf_w - 1;
        if (sx > ex) continue;
        memset(m->cover + py * m->buf_w + sx, 1, ex - sx + 1);
    }
}

static void marker_read_screen(MarkerBatch* m, SDL_Renderer* ren, SDL_Texture* canvas_tex) {
    if (!m || !m->buf) return;
    SDL_RenderReadPixels(ren, NULL, m->fmt, m->buf, m->buf_w * 4);

    if (canvas_tex) {
        int total = m->buf_w * m->buf_h;
        Uint8* tmp = (Uint8*)malloc(total * 4);
        if (tmp) {
            SDL_BlendMode old;
            SDL_GetTextureBlendMode(canvas_tex, &old);
            SDL_SetTextureBlendMode(canvas_tex, SDL_BLENDMODE_NONE);
            SDL_SetRenderTarget(ren, canvas_tex);
            SDL_RenderReadPixels(ren, NULL, m->fmt, tmp, m->buf_w * 4);
            SDL_SetRenderTarget(ren, NULL);
            SDL_SetTextureBlendMode(canvas_tex, old);

            int ao = m->alpha_offset;
            for (int i = 0; i < total; i++) {
                Uint8* dst = m->buf + i * 4;
                Uint8* src = tmp    + i * 4;
                int sa = src[ao];
                if (sa == 0) continue;
                if (sa == 255) {
                    dst[0] = src[0]; dst[1] = src[1];
                    dst[2] = src[2]; dst[3] = src[3];
                    continue;
                }
                int inv = 255 - sa;
                for (int c = 0; c < 4; c++) {
                    if (c == ao) { dst[ao] = (Uint8)(sa + (dst[ao] * inv) / 255); continue; }
                    dst[c] = (Uint8)((src[c] * sa + dst[c] * inv) / 255);
                }
            }
            free(tmp);
        }
    }

    if (m->cover) memset(m->cover, 0, m->buf_w * m->buf_h);
    m->valid = 1;
    m->dirty = 0;
}

static void marker_flush(MarkerBatch* m, SDL_Renderer* ren, SDL_Texture* canvas_tex) {
    if (!m || !m->dirty || !m->valid || !m->buf || !m->cover) return;

    SDL_Rect* r = &m->dirty_rect;
    int dw = r->w, dh = r->h;

    Uint8* out = (Uint8*)malloc(dw * dh * 4);
    if (!out) return;

    int ao = m->alpha_offset;
    Uint8 xor_bytes[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    xor_bytes[ao] = 0x00;
    Uint8 or_bytes[4]  = {0x00, 0x00, 0x00, 0x00};
    or_bytes[ao]  = 0xFF;

    Uint32 xor32 =
        ((Uint32)xor_bytes[0])        |
        ((Uint32)xor_bytes[1] <<  8)  |
        ((Uint32)xor_bytes[2] << 16)  |
        ((Uint32)xor_bytes[3] << 24);
    Uint32 or32 =
        ((Uint32)or_bytes[0])         |
        ((Uint32)or_bytes[1] <<  8)   |
        ((Uint32)or_bytes[2] << 16)   |
        ((Uint32)or_bytes[3] << 24);

    int buf_stride = m->buf_w * 4;

    for (int ry = 0; ry < dh; ry++) {
        int sy = r->y + ry;
        const Uint8* src_row = m->buf   + sy * buf_stride + r->x * 4;
        const Uint8* cov_row = m->cover + sy * m->buf_w   + r->x;
        Uint8*       out_row = out      + ry * dw * 4;
        int px = 0;

#ifdef USE_SSE2_SIMD
        __m128i vxor  = _mm_set1_epi32((int)xor32);
        __m128i vor   = _mm_set1_epi32((int)or32);
        __m128i vzero = _mm_setzero_si128();

        for (; px + 3 < dw; px += 4) {
            __m128i cov4 = _mm_set_epi32(
                (int)(Uint32)cov_row[px+3],
                (int)(Uint32)cov_row[px+2],
                (int)(Uint32)cov_row[px+1],
                (int)(Uint32)cov_row[px+0]);
            __m128i mask = _mm_cmpgt_epi32(cov4, vzero);

            __m128i orig = _mm_loadu_si128((__m128i*)(src_row + px * 4));
            __m128i xord = _mm_or_si128(_mm_xor_si128(orig, vxor), vor);
            __m128i res  = _mm_or_si128(
                _mm_and_si128(mask, xord),
                _mm_andnot_si128(mask, orig));
            _mm_storeu_si128((__m128i*)(out_row + px * 4), res);
        }
#endif
        for (; px < dw; px++) {
            Uint32 orig;
            memcpy(&orig, src_row + px * 4, 4);
            Uint32 res = cov_row[px] ? ((orig ^ xor32) | or32) : orig;
            memcpy(out_row + px * 4, &res, 4);
        }
    }

    SDL_PixelFormat* pf = SDL_AllocFormat(m->fmt);
    Uint32 rmask = pf ? pf->Rmask : 0x000000FF;
    Uint32 gmask = pf ? pf->Gmask : 0x0000FF00;
    Uint32 bmask = pf ? pf->Bmask : 0x00FF0000;
    Uint32 amask = pf ? pf->Amask : 0xFF000000;
    if (pf) SDL_FreeFormat(pf);

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        out, dw, dh, 32, dw * 4,
        rmask, gmask, bmask, amask);
    if (!surf) { free(out); return; }

    SDL_Texture* patch = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (!patch) { free(out); return; }

    SDL_SetTextureBlendMode(patch, SDL_BLENDMODE_NONE);

    SDL_BlendMode old_tex;
    SDL_GetTextureBlendMode(canvas_tex, &old_tex);
    SDL_SetTextureBlendMode(canvas_tex, SDL_BLENDMODE_NONE);

    SDL_SetRenderTarget(ren, canvas_tex);

    SDL_BlendMode old_ren;
    SDL_GetRenderDrawBlendMode(ren, &old_ren);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    SDL_RenderCopy(ren, patch, NULL, r);

    SDL_SetRenderDrawBlendMode(ren, old_ren);
    SDL_SetRenderTarget(ren, NULL);
    SDL_SetTextureBlendMode(canvas_tex, old_tex);

    SDL_DestroyTexture(patch);
    free(out);

    m->dirty = 0;
    m->dirty_rect = (SDL_Rect){0, 0, 0, 0};
}

static void marker_replay_stroke(MarkerBatch* m, SDL_Renderer* ren,
                                  SDL_Texture* canvas_tex, Stroke* stroke)
{
    if (!m || !stroke || stroke->tool != TOOL_MARKER) return;
    if (!m->buf) return;

    marker_read_screen(m, ren, canvas_tex);

    int radius = stroke->size / 2;
    for (int i = 0; i < stroke->point_count; i++) {
        marker_draw_circle(m, stroke->points[i].x, stroke->points[i].y, radius, stroke->color);
        marker_expand_dirty(m, stroke->points[i].x, stroke->points[i].y, radius);
    }
    marker_flush(m, ren, canvas_tex);
    m->valid = 0;
}

static void draw_init(DrawingState* ds) {
    if (!ds) return;

    draw_palette[0] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_1[0], THEME_DRAW_PALETTE_COLOR_1[1], THEME_DRAW_PALETTE_COLOR_1[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[1] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_2[0], THEME_DRAW_PALETTE_COLOR_2[1], THEME_DRAW_PALETTE_COLOR_2[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[2] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_3[0], THEME_DRAW_PALETTE_COLOR_3[1], THEME_DRAW_PALETTE_COLOR_3[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[3] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_4[0], THEME_DRAW_PALETTE_COLOR_4[1], THEME_DRAW_PALETTE_COLOR_4[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[4] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_5[0], THEME_DRAW_PALETTE_COLOR_5[1], THEME_DRAW_PALETTE_COLOR_5[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[5] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_6[0], THEME_DRAW_PALETTE_COLOR_6[1], THEME_DRAW_PALETTE_COLOR_6[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[6] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_7[0], THEME_DRAW_PALETTE_COLOR_7[1], THEME_DRAW_PALETTE_COLOR_7[2], THEME_DRAW_PALETTE_COLOR_ALPHA};

    MarkerBatch saved_marker = ds->marker;

    memset(ds, 0, sizeof(DrawingState));
    ds->current_tool  = TOOL_PEN;
    ds->current_color = (SDL_Color){THEME_DRAW_PALETTE_COLOR_1[0], THEME_DRAW_PALETTE_COLOR_1[1], THEME_DRAW_PALETTE_COLOR_1[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    ds->custom_color  = ds->current_color;
    ds->brush_size    = DRAW_BRUSH_SIZE_DEFAULT;
    ds->zoom_percent  = 100;
    ds->show_palette  = 1;
    ds->needs_full_redraw = 1;

    ds->marker = saved_marker;
    ds->marker.valid = 0;
    ds->marker.dirty = 0;
}

static void draw_clear(DrawingState* ds) {
    if (!ds) return;
    for (int i = 0; i < ds->stroke_count; i++) {
        if (ds->strokes[i].points) {
            free(ds->strokes[i].points);
            ds->strokes[i].points = NULL;
        }
    }
    ds->stroke_count               = 0;
    ds->undo_count                 = 0;
    ds->redo_count                 = 0;
    ds->is_drawing                 = 0;
    ds->last_rendered_stroke_count = 0;
    ds->needs_full_redraw          = 1;
    ds->marker.valid               = 0;
    ds->marker.dirty               = 0;
}

static void draw_notify_canvas_resized(DrawingState* ds, int canvas_w, int canvas_h) {
    if (!ds) return;
    Uint32 fmt = ds->marker.fmt ? ds->marker.fmt : SDL_PIXELFORMAT_RGBA8888;
    if (ds->marker.buf_w == canvas_w && ds->marker.buf_h == canvas_h &&
        ds->marker.fmt == fmt) return;
    marker_buf_ensure(&ds->marker, canvas_w, canvas_h, fmt);
    ds->marker.valid = 0;
    ds->marker.dirty = 0;
    ds->needs_full_redraw = 1;
}

static void draw_free(DrawingState* ds) {
    if (!ds) return;
    for (int i = 0; i < MAX_DRAW_STROKES; i++) {
        if (ds->strokes[i].points) {
            free(ds->strokes[i].points);
            ds->strokes[i].points = NULL;
        }
    }
    marker_buf_free(&ds->marker);
}

static void draw_push_undo(DrawingState* ds) {
    if (!ds) return;
    if (ds->undo_count < MAX_DRAW_UNDO_STACK) {
        ds->undo_stack[ds->undo_count++] = ds->stroke_count;
    } else {
        memmove(ds->undo_stack, ds->undo_stack + 1, sizeof(int) * (MAX_DRAW_UNDO_STACK - 1));
        ds->undo_stack[MAX_DRAW_UNDO_STACK - 1] = ds->stroke_count;
    }
    ds->redo_count = 0;
}

static int draw_undo(DrawingState* ds) {
    if (!ds || ds->undo_count == 0) return 0;
    int prev = ds->undo_stack[--ds->undo_count];
    if (ds->redo_count < MAX_DRAW_UNDO_STACK)
        ds->redo_stack[ds->redo_count++] = ds->stroke_count;
    ds->stroke_count = prev;
    ds->needs_full_redraw = 1;
    ds->marker.valid = 0;
    return 1;
}

static int draw_redo(DrawingState* ds) {
    if (!ds || ds->redo_count == 0) return 0;
    int next = ds->redo_stack[--ds->redo_count];
    if (ds->undo_count < MAX_DRAW_UNDO_STACK)
        ds->undo_stack[ds->undo_count++] = ds->stroke_count;
    ds->stroke_count = next;
    ds->needs_full_redraw = 1;
    ds->marker.valid = 0;
    return 1;
}

static void draw_add_point_to_current_stroke(DrawingState* ds, int x, int y) {
    if (!ds || ds->stroke_count >= MAX_DRAW_STROKES) return;
    Stroke* stroke = &ds->strokes[ds->stroke_count];

    if (stroke->point_count >= stroke->point_capacity) {
        int cap = stroke->point_capacity == 0 ? 256 : stroke->point_capacity * 2;
        Point* np = (Point*)realloc(stroke->points, sizeof(Point) * cap);
        if (!np) return;
        stroke->points   = np;
        stroke->point_capacity = cap;
    }

    if (ds->current_tool == TOOL_PEN || ds->current_tool == TOOL_ERASER) {
        if (stroke->point_count == 0) {
            ds->smooth_point.x = x;
            ds->smooth_point.y = y;
        } else {
            ds->smooth_point.x = (int)(DRAW_SMOOTHING_ALPHA * x + (1.0f - DRAW_SMOOTHING_ALPHA) * ds->smooth_point.x);
            ds->smooth_point.y = (int)(DRAW_SMOOTHING_ALPHA * y + (1.0f - DRAW_SMOOTHING_ALPHA) * ds->smooth_point.y);
        }
        stroke->points[stroke->point_count].x = ds->smooth_point.x;
        stroke->points[stroke->point_count].y = ds->smooth_point.y;
    } else {
        stroke->points[stroke->point_count].x = x;
        stroke->points[stroke->point_count].y = y;
    }
    stroke->point_count++;
}

static void draw_begin_stroke(DrawingState* ds, int x, int y,
                               SDL_Renderer* ren, SDL_Texture* canvas_tex)
{
    if (!ds || ds->stroke_count >= MAX_DRAW_STROKES) return;

    draw_push_undo(ds);

    Stroke* stroke = &ds->strokes[ds->stroke_count];
    memset(stroke, 0, sizeof(Stroke));
    stroke->color = ds->current_color;
    stroke->size  = ds->brush_size;
    stroke->tool  = ds->current_tool;
    stroke->start.x = x;
    stroke->start.y = y;

    ds->is_drawing    = 1;
    ds->last_point.x  = x;
    ds->last_point.y  = y;
    ds->shape_start.x = x;
    ds->shape_start.y = y;

    if (ds->current_tool == TOOL_MARKER) {
        if (ren && canvas_tex) {
            int tw = 0, th = 0;
            Uint32 tex_fmt = 0;
            SDL_QueryTexture(canvas_tex, &tex_fmt, NULL, &tw, &th);
            marker_buf_ensure(&ds->marker, tw, th, tex_fmt);
            ds->marker.valid = 0;
        }
        draw_add_point_to_current_stroke(ds, x, y);
    } else if (ds->current_tool == TOOL_PEN ||
               ds->current_tool == TOOL_ERASER) {
        draw_add_point_to_current_stroke(ds, x, y);
    }
}

static void draw_continue_stroke(DrawingState* ds, int x, int y,
                                  SDL_Renderer* ren, SDL_Texture* canvas_tex)
{
    if (!ds || !ds->is_drawing || ds->stroke_count >= MAX_DRAW_STROKES) return;
    (void)ren; (void)canvas_tex;

    Stroke* stroke = &ds->strokes[ds->stroke_count];

    if (ds->current_tool == TOOL_MARKER) {
        int dx    = x - ds->last_point.x;
        int dy    = y - ds->last_point.y;
        float dist = sqrtf((float)(dx * dx + dy * dy));
        int steps = (int)(dist / 2.0f) + 1;

        for (int i = 1; i <= steps; i++) {
            float t  = (float)i / (float)steps;
            int px   = ds->last_point.x + (int)(dx * t);
            int py   = ds->last_point.y + (int)(dy * t);
            draw_add_point_to_current_stroke(ds, px, py);
            if (ds->marker.valid) {
                int radius = ds->brush_size / 2;
                marker_draw_circle(&ds->marker, px, py, radius, ds->current_color);
                marker_expand_dirty(&ds->marker, px, py, radius);
            }
        }
    } else if (ds->current_tool == TOOL_PEN ||
               ds->current_tool == TOOL_ERASER) {
        int dx    = x - ds->last_point.x;
        int dy    = y - ds->last_point.y;
        float dist = sqrtf((float)(dx * dx + dy * dy));
        int steps = (int)(dist / 2.0f) + 1;

        for (int i = 1; i <= steps; i++) {
            float t  = (float)i / (float)steps;
            int px   = ds->last_point.x + (int)(dx * t);
            int py   = ds->last_point.y + (int)(dy * t);
            draw_add_point_to_current_stroke(ds, px, py);
        }
    } else {
        stroke->end.x = x;
        stroke->end.y = y;
    }

    ds->last_point.x = x;
    ds->last_point.y = y;
}

static void draw_end_stroke(DrawingState* ds, int x, int y,
                             SDL_Renderer* ren, SDL_Texture* canvas_tex)
{
    if (!ds || !ds->is_drawing) return;

    Stroke* stroke = &ds->strokes[ds->stroke_count];

    int flushed_marker = 0;

    if (ds->current_tool == TOOL_MARKER) {
        if (ren && canvas_tex && ds->marker.valid) {
            marker_flush(&ds->marker, ren, canvas_tex);
            flushed_marker = 1;
        }
        ds->marker.valid = 0;
    } else if (ds->current_tool != TOOL_PEN &&
               ds->current_tool != TOOL_ERASER) {
        stroke->start = ds->shape_start;
        stroke->end.x = x;
        stroke->end.y = y;
    }

    ds->stroke_count++;
    ds->is_drawing = 0;

    if (ds->current_tool == TOOL_MARKER) {
        if (flushed_marker)
            ds->last_rendered_stroke_count = ds->stroke_count;
        else
            ds->needs_full_redraw = 1;
    }
}

static void draw_circle_filled(SDL_Renderer* ren, int cx, int cy, int radius, SDL_Color color) {
    SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius)
                SDL_RenderDrawPoint(ren, cx + dx, cy + dy);
        }
    }
}

static void draw_circle_outline(SDL_Renderer* ren, int cx, int cy, int radius, SDL_Color color, int thickness) {
    SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
    for (int w = 0; w < thickness; w++) {
        int r = radius - w;
        int x = r, y = 0, err = 0;
        while (x >= y) {
            SDL_RenderDrawPoint(ren, cx + x, cy + y);
            SDL_RenderDrawPoint(ren, cx + y, cy + x);
            SDL_RenderDrawPoint(ren, cx - y, cy + x);
            SDL_RenderDrawPoint(ren, cx - x, cy + y);
            SDL_RenderDrawPoint(ren, cx - x, cy - y);
            SDL_RenderDrawPoint(ren, cx - y, cy - x);
            SDL_RenderDrawPoint(ren, cx + y, cy - x);
            SDL_RenderDrawPoint(ren, cx + x, cy - y);
            if (err <= 0) { y++; err += 2 * y + 1; }
            if (err > 0)  { x--; err -= 2 * x + 1; }
        }
    }
}

static void draw_line_thick(SDL_Renderer* ren, int x1, int y1, int x2, int y2,
                             SDL_Color color, int thickness)
{
    SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
    int dx = abs(x2 - x1), dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        for (int ty = -thickness / 2; ty <= thickness / 2; ty++)
            for (int tx = -thickness / 2; tx <= thickness / 2; tx++)
                SDL_RenderDrawPoint(ren, x1 + tx, y1 + ty);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}

static void draw_render_stroke(SDL_Renderer* ren, Stroke* stroke, DrawingState* ds) {
    if (!ren || !stroke) return;
    NOB_UNUSED(ds);

    SDL_Color color = stroke->color;
    SDL_BlendMode old_blend;
    int restore_blend = 0;

    if (stroke->tool == TOOL_ERASER) {
        color = (SDL_Color){0, 0, 0, 0};
        SDL_GetRenderDrawBlendMode(ren, &old_blend);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        restore_blend = 1;
    }

    switch (stroke->tool) {
        case TOOL_PEN:
        case TOOL_ERASER:
            for (int i = 0; i < stroke->point_count; i++)
                draw_circle_filled(ren, stroke->points[i].x, stroke->points[i].y,
                                   stroke->size / 2, color);
            break;
        case TOOL_MARKER:
            break;
        case TOOL_LINE:
            draw_line_thick(ren, stroke->start.x, stroke->start.y,
                            stroke->end.x, stroke->end.y, color, stroke->size);
            break;
        case TOOL_RECT: {
            SDL_Rect r = {
                stroke->start.x < stroke->end.x ? stroke->start.x : stroke->end.x,
                stroke->start.y < stroke->end.y ? stroke->start.y : stroke->end.y,
                abs(stroke->end.x - stroke->start.x),
                abs(stroke->end.y - stroke->start.y)
            };
            SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
            for (int i = 0; i < stroke->size; i++) {
                SDL_Rect o = {r.x - i, r.y - i, r.w + i * 2, r.h + i * 2};
                SDL_RenderDrawRect(ren, &o);
            }
            break;
        }
        case TOOL_FILLED_RECT: {
            SDL_Rect r = {
                stroke->start.x < stroke->end.x ? stroke->start.x : stroke->end.x,
                stroke->start.y < stroke->end.y ? stroke->start.y : stroke->end.y,
                abs(stroke->end.x - stroke->start.x),
                abs(stroke->end.y - stroke->start.y)
            };
            SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(ren, &r);
            break;
        }
        case TOOL_CIRCLE: {
            int cx = (stroke->start.x + stroke->end.x) / 2;
            int cy = (stroke->start.y + stroke->end.y) / 2;
            int ddx = stroke->end.x - stroke->start.x;
            int ddy = stroke->end.y - stroke->start.y;
            int radius = (int)(sqrtf((float)(ddx * ddx + ddy * ddy)) / 2.0f);
            draw_circle_outline(ren, cx, cy, radius, color, stroke->size);
            break;
        }
        case TOOL_FILLED_CIRCLE: {
            int cx = (stroke->start.x + stroke->end.x) / 2;
            int cy = (stroke->start.y + stroke->end.y) / 2;
            int ddx = stroke->end.x - stroke->start.x;
            int ddy = stroke->end.y - stroke->start.y;
            int radius = (int)(sqrtf((float)(ddx * ddx + ddy * ddy)) / 2.0f);
            draw_circle_filled(ren, cx, cy, radius, color);
            break;
        }
    }

    if (restore_blend)
        SDL_SetRenderDrawBlendMode(ren, old_blend);
}

static void draw_render_preview(SDL_Renderer* ren, DrawingState* ds, int mouse_x, int mouse_y) {
    if (!ren || !ds) return;

    if (ds->is_drawing) {
        if (ds->current_tool != TOOL_PEN &&
            ds->current_tool != TOOL_ERASER &&
            ds->current_tool != TOOL_MARKER)
        {
            Stroke preview = {0};
            preview.color  = ds->current_color;
            preview.size   = ds->brush_size;
            preview.tool   = ds->current_tool;
            preview.start  = ds->shape_start;
            preview.end.x  = mouse_x;
            preview.end.y  = mouse_y;
            draw_render_stroke(ren, &preview, ds);
        }
    }

    SDL_Color cursor_color = ds->current_color;
    if (ds->current_tool == TOOL_ERASER)
        cursor_color = (SDL_Color){255, 255, 255, 255};

    draw_circle_outline(ren, mouse_x, mouse_y, ds->brush_size / 2 + 2,
                        (SDL_Color){0, 0, 0, 180}, 1);
    draw_circle_outline(ren, mouse_x, mouse_y, ds->brush_size / 2,
                        cursor_color, 1);
    SDL_SetRenderDrawColor(ren, cursor_color.r, cursor_color.g, cursor_color.b, 255);
    SDL_RenderDrawLine(ren, mouse_x - 4, mouse_y, mouse_x + 4, mouse_y);
    SDL_RenderDrawLine(ren, mouse_x, mouse_y - 4, mouse_x, mouse_y + 4);
}

static void draw_render_all(SDL_Renderer* ren, SDL_Texture* canvas_tex,
                             DrawingState* ds, SDL_Rect dst,
                             SDL_Rect video_dst, double video_time)
{
    if (!ren || !canvas_tex || !ds) return;

    {
        int has_marker = 0;
        for (int i = 0; i < ds->stroke_count; i++) {
            if (ds->strokes[i].tool == TOOL_MARKER) { has_marker = 1; break; }
        }
        if (has_marker) {
            if (memcmp(&video_dst, &ds->last_video_dst, sizeof(SDL_Rect)) != 0 ||
                video_time != ds->last_video_time)
                ds->needs_full_redraw = 1;
        }
        ds->last_video_dst  = video_dst;
        ds->last_video_time = video_time;
    }

    if (ds->is_drawing && ds->current_tool == TOOL_MARKER &&
        !ds->marker.valid && ds->marker.buf) {
        marker_read_screen(&ds->marker, ren, canvas_tex);
        Stroke* s = &ds->strokes[ds->stroke_count];
        int radius = s->size / 2;
        for (int i = 0; i < s->point_count; i++) {
            marker_draw_circle(&ds->marker, s->points[i].x, s->points[i].y, radius, s->color);
            marker_expand_dirty(&ds->marker, s->points[i].x, s->points[i].y, radius);
        }
    }

    SDL_SetRenderTarget(ren, canvas_tex);

    if (ds->needs_full_redraw) {
        SDL_BlendMode old_ren;
        SDL_GetRenderDrawBlendMode(ren, &old_ren);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren,
            THEME_DRAW_CANVAS_CLEAR_COLOR[0],
            THEME_DRAW_CANVAS_CLEAR_COLOR[1],
            THEME_DRAW_CANVAS_CLEAR_COLOR[2], 0);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawBlendMode(ren, old_ren);

        for (int i = 0; i < ds->stroke_count; i++) {
            if (ds->strokes[i].tool == TOOL_MARKER) {
                SDL_SetRenderTarget(ren, NULL);
                marker_replay_stroke(&ds->marker, ren, canvas_tex, &ds->strokes[i]);
                SDL_SetRenderTarget(ren, canvas_tex);
            } else {
                draw_render_stroke(ren, &ds->strokes[i], ds);
            }
        }

        ds->last_rendered_stroke_count = ds->stroke_count;
        ds->needs_full_redraw = 0;
    } else {
        for (int i = ds->last_rendered_stroke_count; i < ds->stroke_count; i++) {
            if (ds->strokes[i].tool == TOOL_MARKER) {
                SDL_SetRenderTarget(ren, NULL);
                marker_replay_stroke(&ds->marker, ren, canvas_tex, &ds->strokes[i]);
                SDL_SetRenderTarget(ren, canvas_tex);
            } else {
                draw_render_stroke(ren, &ds->strokes[i], ds);
            }
        }
        ds->last_rendered_stroke_count = ds->stroke_count;
    }

    if (ds->is_drawing && ds->current_tool == TOOL_MARKER && ds->marker.dirty) {
        SDL_SetRenderTarget(ren, NULL);
        marker_flush(&ds->marker, ren, canvas_tex);
    }

    if (ds->is_drawing && ds->stroke_count < MAX_DRAW_STROKES) {
        Stroke* current = &ds->strokes[ds->stroke_count];
        if (current->tool == TOOL_PEN || current->tool == TOOL_ERASER) {
            SDL_SetRenderTarget(ren, canvas_tex);
            draw_render_stroke(ren, current, ds);
        }
    }

    SDL_SetRenderTarget(ren, NULL);
    SDL_RenderCopy(ren, canvas_tex, NULL, &dst);
}

static void draw_render_palette(SDL_Renderer* ren, DrawingState* ds, int win_w, int win_h) {
    if (!ren || !ds || !ds->show_palette) return;
    (void)win_w;

    int swatch_size = 32, padding = 6, cols = 4, rows = 2;
    int palette_w = cols * (swatch_size + padding) + padding * 2;
    int palette_h = rows * (swatch_size + padding) + padding * 2;
    int palette_x = 20;
    int palette_y = win_h - palette_h - 90;

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect bg = {palette_x, palette_y, palette_w, palette_h};
    SDL_SetRenderDrawColor(ren, THEME_TEXT_INPUT_BG_COLOR[0], THEME_TEXT_INPUT_BG_COLOR[1],
                           THEME_TEXT_INPUT_BG_COLOR[2], 200);
    SDL_RenderFillRect(ren, &bg);

    for (int i = 0; i < 7; i++) {
        int col = i % cols, row = i / cols;
        SDL_Rect swatch = {
            palette_x + padding + col * (swatch_size + padding),
            palette_y + padding + row * (swatch_size + padding),
            swatch_size, swatch_size
        };
        SDL_SetRenderDrawColor(ren, draw_palette[i].r, draw_palette[i].g, draw_palette[i].b, 255);
        SDL_RenderFillRect(ren, &swatch);
        if (ds->current_color.r == draw_palette[i].r &&
            ds->current_color.g == draw_palette[i].g &&
            ds->current_color.b == draw_palette[i].b) {
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_RenderDrawRect(ren, &swatch);
        }
    }

    SDL_Rect custom_btn = {
        palette_x + padding + 3 * (swatch_size + padding),
        palette_y + padding + 1 * (swatch_size + padding),
        swatch_size, swatch_size
    };
    SDL_SetRenderDrawColor(ren, ds->custom_color.r, ds->custom_color.g, ds->custom_color.b, 255);
    SDL_RenderFillRect(ren, &custom_btn);

    float lum = (0.299f * ds->custom_color.r + 0.587f * ds->custom_color.g +
                 0.114f * ds->custom_color.b) / 255.0f;
    SDL_Color plus = lum > 0.5f ? (SDL_Color){0,0,0,255} : (SDL_Color){255,255,255,255};
    int cx = custom_btn.x + custom_btn.w / 2;
    int cy = custom_btn.y + custom_btn.h / 2;
    int ps = 12;
    SDL_SetRenderDrawColor(ren, plus.r, plus.g, plus.b, 255);
    SDL_RenderDrawLine(ren, cx - ps/2, cy,     cx + ps/2, cy);
    SDL_RenderDrawLine(ren, cx - ps/2, cy + 1, cx + ps/2, cy + 1);
    SDL_RenderDrawLine(ren, cx, cy - ps/2,     cx, cy + ps/2);
    SDL_RenderDrawLine(ren, cx + 1, cy - ps/2, cx + 1, cy + ps/2);

    if (ds->current_color.r == ds->custom_color.r &&
        ds->current_color.g == ds->custom_color.g &&
        ds->current_color.b == ds->custom_color.b) {
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_RenderDrawRect(ren, &custom_btn);
    }
}

static int draw_palette_click(DrawingState* ds, int mouse_x, int mouse_y,
                               int win_w, int win_h)
{
    if (!ds || !ds->show_palette) return 0;
    (void)win_w;

    int swatch_size = 32, padding = 6, cols = 4, rows = 2;
    int palette_x = 20;
    int palette_h = rows * (swatch_size + padding) + padding * 2;
    int palette_y = win_h - palette_h - 90;

    for (int i = 0; i < 7; i++) {
        int col = i % cols, row = i / cols;
        SDL_Rect swatch = {
            palette_x + padding + col * (swatch_size + padding),
            palette_y + padding + row * (swatch_size + padding),
            swatch_size, swatch_size
        };
        if (mouse_x >= swatch.x && mouse_x < swatch.x + swatch.w &&
            mouse_y >= swatch.y && mouse_y < swatch.y + swatch.h) {
            ds->current_color = draw_palette[i];
            return 1;
        }
    }

    SDL_Rect custom_btn = {
        palette_x + padding + 3 * (swatch_size + padding),
        palette_y + padding + 1 * (swatch_size + padding),
        swatch_size, swatch_size
    };
    if (mouse_x >= custom_btn.x && mouse_x < custom_btn.x + custom_btn.w &&
        mouse_y >= custom_btn.y && mouse_y < custom_btn.y + custom_btn.h) {
        unsigned char rgb[3] = {ds->custom_color.r, ds->custom_color.g, ds->custom_color.b};
        const char* hex = tinyfd_colorChooser("Choose Color", NULL, rgb, rgb);
        if (hex) {
            unsigned int r, g, b;
            if (sscanf(hex, "#%02x%02x%02x", &r, &g, &b) == 3) {
                ds->custom_color = (SDL_Color){(Uint8)r, (Uint8)g, (Uint8)b, 255};
                ds->current_color = ds->custom_color;
            }
        }
        return 1;
    }
    return 0;
}

static int export_save_dialog(char* out_path, size_t out_size,
                               char* out_ext, char* default_filename)
{
#ifdef _WIN32
    char filebuf[PATH_MAX];
    snprintf(filebuf, sizeof(filebuf), "%s.png", default_filename);
    const char filter[] =
        "PNG (*.png)\0*.png\0"
        "JPEG (*.jpg)\0*.jpg\0"
        "BMP (*.bmp)\0*.bmp\0"
        "TGA (*.tga)\0*.tga\0\0";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = filebuf;
    ofn.nMaxFile    = sizeof(filebuf);
    ofn.lpstrDefExt = "png";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameA(&ofn)) return 0;
    strncpy(out_path, filebuf, out_size - 1);
    out_path[out_size - 1] = 0;
    switch (ofn.nFilterIndex) {
        case 1: strcpy(out_ext, "png"); break;
        case 2: strcpy(out_ext, "jpg"); break;
        case 3: strcpy(out_ext, "bmp"); break;
        case 4: strcpy(out_ext, "tga"); break;
        default: strcpy(out_ext, "png");
    }
    char* dot = strrchr(out_path, '.');
    if (!dot || _stricmp(dot + 1, out_ext) != 0) {
        if (dot) *dot = '\0';
        strncat(out_path, ".", out_size - strlen(out_path) - 1);
        strncat(out_path, out_ext, out_size - strlen(out_path) - 1);
    }
    return 1;
#else
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s.png", default_filename);
    const char* filters[] = {"*.png", "*.jpg", "*.bmp", "*.tga"};
    const char* path = tinyfd_saveFileDialog("Export Drawing", filename, 4,
                                              filters, "PNG, JPG, BMP, TGA");
    if (!path) return 0;
    strncpy(out_path, path, out_size - 1);
    out_path[out_size - 1] = 0;
    const char* dot = strrchr(out_path, '.');
    if (dot) {
        dot++;
        if      (!strcasecmp(dot, "png"))  strcpy(out_ext, "png");
        else if (!strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg")) strcpy(out_ext, "jpg");
        else if (!strcasecmp(dot, "bmp"))  strcpy(out_ext, "bmp");
        else if (!strcasecmp(dot, "tga"))  strcpy(out_ext, "tga");
        else                               strcpy(out_ext, "png");
    } else {
        strcpy(out_ext, "png");
        strncat(out_path, ".png", out_size - strlen(out_path) - 1);
    }
    return 1;
#endif
}

static void draw_export(SDL_Renderer* ren,
                         SDL_Texture*  canvas_tex,
                         SDL_Texture*  video_tex,
                         SDL_Rect      video_dst,
                         int           win_w,
                         int           win_h,
                         int           include_video,
                         DrawingState* ds)
{
    if (!ren || !canvas_tex) return;

    if (ds) marker_flush(&ds->marker, ren, canvas_tex);

    char path[1024], ext[8];
    if (!export_save_dialog(path, sizeof(path), ext, "drawing")) return;

    SDL_Surface* export_surf = SDL_CreateRGBSurface(
        0, win_w, win_h, 32,
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (!export_surf) return;

    SDL_Texture* export_tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, win_w, win_h);
    if (!export_tex) { SDL_FreeSurface(export_surf); return; }

    SDL_SetRenderTarget(ren, export_tex);
    SDL_SetRenderDrawColor(ren, THEME_LETTERBOX_COLOR[0], THEME_LETTERBOX_COLOR[1],
                           THEME_LETTERBOX_COLOR[2], 0);
    SDL_RenderClear(ren);
    if (include_video && video_tex)
        SDL_RenderCopy(ren, video_tex, NULL, &video_dst);
    SDL_RenderCopy(ren, canvas_tex, NULL, NULL);

    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA32,
                         export_surf->pixels, export_surf->pitch);

    SDL_SetRenderTarget(ren, NULL);

    if (strcmp(ext, "png") == 0)
        stbi_write_png(path, win_w, win_h, 4, export_surf->pixels, export_surf->pitch);
    else if (strcmp(ext, "jpg") == 0)
        stbi_write_jpg(path, win_w, win_h, 4, export_surf->pixels, 90);
    else if (strcmp(ext, "bmp") == 0)
        SDL_SaveBMP(export_surf, path);
    else if (strcmp(ext, "tga") == 0)
        stbi_write_tga(path, win_w, win_h, 4, export_surf->pixels);

    SDL_DestroyTexture(export_tex);
    SDL_FreeSurface(export_surf);
}
