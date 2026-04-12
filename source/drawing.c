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
    Stroke strokes[MAX_DRAW_STROKES];
    int stroke_count;
    int last_rendered_stroke_count;
    int needs_full_redraw;
    
    int undo_stack[MAX_DRAW_UNDO_STACK];
    int undo_count;
    int redo_stack[MAX_DRAW_UNDO_STACK];
    int redo_count;
    
    DrawTool current_tool;
    SDL_Color current_color;
    SDL_Color custom_color;
    int brush_size;
    
    int is_drawing;
    Point last_point;
    Point smooth_point;
    Point shape_start;
    
    int show_palette;
    int zoom_percent;
    int pan_x;
    int pan_y;
} DrawingState;

static SDL_Color draw_palette[7];
static const int draw_palette_count = 7;

static void draw_init(DrawingState* ds) {
    if (!ds) return;
    
    draw_palette[0] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_1[0], THEME_DRAW_PALETTE_COLOR_1[1], THEME_DRAW_PALETTE_COLOR_1[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[1] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_2[0], THEME_DRAW_PALETTE_COLOR_2[1], THEME_DRAW_PALETTE_COLOR_2[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[2] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_3[0], THEME_DRAW_PALETTE_COLOR_3[1], THEME_DRAW_PALETTE_COLOR_3[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[3] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_4[0], THEME_DRAW_PALETTE_COLOR_4[1], THEME_DRAW_PALETTE_COLOR_4[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[4] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_5[0], THEME_DRAW_PALETTE_COLOR_5[1], THEME_DRAW_PALETTE_COLOR_5[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[5] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_6[0], THEME_DRAW_PALETTE_COLOR_6[1], THEME_DRAW_PALETTE_COLOR_6[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    draw_palette[6] = (SDL_Color){THEME_DRAW_PALETTE_COLOR_7[0], THEME_DRAW_PALETTE_COLOR_7[1], THEME_DRAW_PALETTE_COLOR_7[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    
    memset(ds, 0, sizeof(DrawingState));
    ds->current_tool = TOOL_PEN;
    ds->current_color = (SDL_Color){THEME_DRAW_PALETTE_COLOR_1[0], THEME_DRAW_PALETTE_COLOR_1[1], THEME_DRAW_PALETTE_COLOR_1[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    ds->custom_color = (SDL_Color){THEME_DRAW_PALETTE_COLOR_1[0], THEME_DRAW_PALETTE_COLOR_1[1], THEME_DRAW_PALETTE_COLOR_1[2], THEME_DRAW_PALETTE_COLOR_ALPHA};
    ds->brush_size = DRAW_BRUSH_SIZE_DEFAULT;
    ds->zoom_percent = 100;
    ds->show_palette = 1;
    ds->last_rendered_stroke_count = 0;
    ds->needs_full_redraw = 1;
}

static void draw_clear(DrawingState* ds) {
    if (!ds) return;
    for (int i = 0; i < ds->stroke_count; i++) {
        if (ds->strokes[i].points) {
            free(ds->strokes[i].points);
            ds->strokes[i].points = NULL;
        }
    }
    ds->stroke_count = 0;
    ds->undo_count = 0;
    ds->redo_count = 0;
    ds->is_drawing = 0;
    ds->last_rendered_stroke_count = 0;
    ds->needs_full_redraw = 1;
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
    int prev_count = ds->undo_stack[--ds->undo_count];
    if (ds->redo_count < MAX_DRAW_UNDO_STACK) {
        ds->redo_stack[ds->redo_count++] = ds->stroke_count;
    }
    ds->stroke_count = prev_count;
    ds->needs_full_redraw = 1;
    return 1;
}

static int draw_redo(DrawingState* ds) {
    if (!ds || ds->redo_count == 0) return 0;
    int next_count = ds->redo_stack[--ds->redo_count];
    if (ds->undo_count < MAX_DRAW_UNDO_STACK) {
        ds->undo_stack[ds->undo_count++] = ds->stroke_count;
    }
    ds->stroke_count = next_count;
    ds->needs_full_redraw = 1;
    return 1;
}

static void draw_add_point_to_current_stroke(DrawingState* ds, int x, int y) {
    if (!ds || ds->stroke_count >= MAX_DRAW_STROKES) return;
    Stroke* stroke = &ds->strokes[ds->stroke_count];
    
    if (stroke->point_count >= stroke->point_capacity) {
        int new_capacity = stroke->point_capacity == 0 ? 256 : stroke->point_capacity * 2;
        Point* new_points = (Point*)realloc(stroke->points, sizeof(Point) * new_capacity);
        if (!new_points) return;
        stroke->points = new_points;
        stroke->point_capacity = new_capacity;
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

static void draw_begin_stroke(DrawingState* ds, int x, int y) {
    if (!ds || ds->stroke_count >= MAX_DRAW_STROKES) return;
    
    draw_push_undo(ds);
    
    Stroke* stroke = &ds->strokes[ds->stroke_count];
    memset(stroke, 0, sizeof(Stroke));
    stroke->color = ds->current_color;
    stroke->size = ds->brush_size;
    stroke->tool = ds->current_tool;
    stroke->start.x = x;
    stroke->start.y = y;
    
    if (ds->current_tool == TOOL_PEN || ds->current_tool == TOOL_ERASER || ds->current_tool == TOOL_MARKER) {
        draw_add_point_to_current_stroke(ds, x, y);
    }
    
    ds->is_drawing = 1;
    ds->last_point.x = x;
    ds->last_point.y = y;
    ds->shape_start.x = x;
    ds->shape_start.y = y;
}

static void draw_continue_stroke(DrawingState* ds, int x, int y) {
    if (!ds || !ds->is_drawing || ds->stroke_count >= MAX_DRAW_STROKES) return;
    
    Stroke* stroke = &ds->strokes[ds->stroke_count];
    
    if (ds->current_tool == TOOL_PEN || ds->current_tool == TOOL_ERASER || ds->current_tool == TOOL_MARKER) {
        int dx = x - ds->last_point.x;
        int dy = y - ds->last_point.y;
        float dist = sqrtf((float)(dx * dx + dy * dy));
        int steps = (int)(dist / 2.0f) + 1;
        
        for (int i = 1; i <= steps; i++) {
            float t = (float)i / (float)steps;
            int px = ds->last_point.x + (int)(dx * t);
            int py = ds->last_point.y + (int)(dy * t);
            draw_add_point_to_current_stroke(ds, px, py);
        }
    } else {
        stroke->end.x = x;
        stroke->end.y = y;
    }
    
    ds->last_point.x = x;
    ds->last_point.y = y;
}

static void draw_end_stroke(DrawingState* ds, int x, int y) {
    if (!ds || !ds->is_drawing) return;
    
    Stroke* stroke = &ds->strokes[ds->stroke_count];
    
    if (ds->current_tool != TOOL_PEN && ds->current_tool != TOOL_ERASER && ds->current_tool != TOOL_MARKER) {
        stroke->start = ds->shape_start;
        stroke->end.x = x;
        stroke->end.y = y;
    }
    
    ds->stroke_count++;
    ds->is_drawing = 0;
}

static void draw_circle_filled(SDL_Renderer* ren, int cx, int cy, int radius, SDL_Color color) {
    SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius) {
                SDL_RenderDrawPoint(ren, cx + dx, cy + dy);
            }
        }
    }
}

static void draw_circle_outline(SDL_Renderer* ren, int cx, int cy, int radius, SDL_Color color, int thickness) {
    SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
    for (int w = 0; w < thickness; w++) {
        int r = radius - w;
        int x = r;
        int y = 0;
        int err = 0;
        
        while (x >= y) {
            SDL_RenderDrawPoint(ren, cx + x, cy + y);
            SDL_RenderDrawPoint(ren, cx + y, cy + x);
            SDL_RenderDrawPoint(ren, cx - y, cy + x);
            SDL_RenderDrawPoint(ren, cx - x, cy + y);
            SDL_RenderDrawPoint(ren, cx - x, cy - y);
            SDL_RenderDrawPoint(ren, cx - y, cy - x);
            SDL_RenderDrawPoint(ren, cx + y, cy - x);
            SDL_RenderDrawPoint(ren, cx + x, cy - y);
            
            if (err <= 0) {
                y += 1;
                err += 2 * y + 1;
            }
            if (err > 0) {
                x -= 1;
                err -= 2 * x + 1;
            }
        }
    }
}

static void draw_line_thick(SDL_Renderer* ren, int x1, int y1, int x2, int y2, SDL_Color color, int thickness) {
    SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
    
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        for (int ty = -thickness / 2; ty <= thickness / 2; ty++) {
            for (int tx = -thickness / 2; tx <= thickness / 2; tx++) {
                SDL_RenderDrawPoint(ren, x1 + tx, y1 + ty);
            }
        }
        
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

static void draw_marker_circle(SDL_Renderer* ren, int cx, int cy, int radius, int sample_from_screen) {
    int diameter = radius * 2 + 1;
    int rect_x = cx - radius;
    int rect_y = cy - radius;
    
    int total_pixels = diameter * diameter * 4;
    
#ifdef USE_SSE2_SIMD
    /* x86_64: Allocate aligned buffer for SIMD operations */
    Uint8* pixels = (Uint8*)_mm_malloc(total_pixels, 16);
#else
    /* Non-x86_64: Standard allocation */
    Uint8* pixels = (Uint8*)malloc(total_pixels);
#endif
    if (!pixels) return;
    
    SDL_Rect read_rect = {rect_x, rect_y, diameter, diameter};
    
    /* Read all pixels at once */
    if (sample_from_screen) {
        SDL_Texture* old_target = SDL_GetRenderTarget(ren);
        SDL_SetRenderTarget(ren, NULL);
        SDL_RenderReadPixels(ren, &read_rect, SDL_PIXELFORMAT_RGBA8888, pixels, diameter * 4);
        SDL_SetRenderTarget(ren, old_target);
    } else {
        SDL_RenderReadPixels(ren, &read_rect, SDL_PIXELFORMAT_RGBA8888, pixels, diameter * 4);
    }
    
#ifdef USE_SSE2_SIMD
    /* x86_64: SIMD RGB inversion - process 16 bytes (4 RGBA pixels) at once */
    __m128i all_255 = _mm_set1_epi8((char)255);
    int simd_count = total_pixels / 16;
    __m128i* simd_ptr = (__m128i*)pixels;
    
    for (int i = 0; i < simd_count; i++) {
        __m128i pixel_data = _mm_load_si128(simd_ptr + i);
        __m128i inverted = _mm_sub_epi8(all_255, pixel_data);
        __m128i alpha_mask = _mm_set_epi8(0,0xFF,0xFF,0xFF, 0,0xFF,0xFF,0xFF, 0,0xFF,0xFF,0xFF, 0,0xFF,0xFF,0xFF);
        __m128i result = _mm_or_si128(
            _mm_and_si128(inverted, alpha_mask),
            _mm_andnot_si128(alpha_mask, pixel_data)
        );
        _mm_store_si128(simd_ptr + i, result);
    }
    
    for (int i = simd_count * 16; i < total_pixels; i += 4) {
        pixels[i + 0] = 255 - pixels[i + 0];
        pixels[i + 1] = 255 - pixels[i + 1];
        pixels[i + 2] = 255 - pixels[i + 2];
    }
#else
    /* Non-x86_64: Scalar RGB inversion fallback */
    for (int i = 0; i < total_pixels; i += 4) {
        pixels[i + 0] = 255 - pixels[i + 0];
        pixels[i + 1] = 255 - pixels[i + 1];
        pixels[i + 2] = 255 - pixels[i + 2];
    }
#endif
    
    SDL_Surface* temp_surf = SDL_CreateRGBSurfaceFrom(
        pixels, diameter, diameter, 32, diameter * 4,
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
    );
    
    if (temp_surf) {
        SDL_Texture* temp_tex = SDL_CreateTextureFromSurface(ren, temp_surf);
        if (temp_tex) {
            if (radius > 10) {
                SDL_Rect dst = {rect_x, rect_y, diameter, diameter};
                SDL_RenderCopy(ren, temp_tex, NULL, &dst);
            } else {
                for (int y = 0; y < diameter; y++) {
                    for (int x = 0; x < diameter; x++) {
                        int dx = x - radius;
                        int dy = y - radius;
                        if (dx*dx + dy*dy <= radius*radius) {
                            int idx = (y * diameter + x) * 4;
                            SDL_SetRenderDrawColor(ren, pixels[idx+0], pixels[idx+1], pixels[idx+2], 255);
                            SDL_RenderDrawPoint(ren, rect_x + x, rect_y + y);
                        }
                    }
                }
            }
            SDL_DestroyTexture(temp_tex);
        }
        SDL_FreeSurface(temp_surf);
    }
    
#ifdef USE_SSE2_SIMD
    _mm_free(pixels);
#else
    free(pixels);
#endif
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
            for (int i = 0; i < stroke->point_count; i++) {
                draw_circle_filled(ren, stroke->points[i].x, stroke->points[i].y, stroke->size / 2, color);
            }
            break;
            
        case TOOL_MARKER:
            for (int i = 0; i < stroke->point_count; i++) {
                draw_marker_circle(ren, stroke->points[i].x, stroke->points[i].y, stroke->size / 2, 1);
            }
            break;
            
        case TOOL_LINE:
            draw_line_thick(ren, stroke->start.x, stroke->start.y, stroke->end.x, stroke->end.y, color, stroke->size);
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
                SDL_Rect outline = {r.x - i, r.y - i, r.w + i * 2, r.h + i * 2};
                SDL_RenderDrawRect(ren, &outline);
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
            int dx = stroke->end.x - stroke->start.x;
            int dy = stroke->end.y - stroke->start.y;
            int radius = (int)(sqrtf((float)(dx * dx + dy * dy)) / 2.0f);
            draw_circle_outline(ren, cx, cy, radius, color, stroke->size);
            break;
        }
            
        case TOOL_FILLED_CIRCLE: {
            int cx = (stroke->start.x + stroke->end.x) / 2;
            int cy = (stroke->start.y + stroke->end.y) / 2;
            int dx = stroke->end.x - stroke->start.x;
            int dy = stroke->end.y - stroke->start.y;
            int radius = (int)(sqrtf((float)(dx * dx + dy * dy)) / 2.0f);
            draw_circle_filled(ren, cx, cy, radius, color);
            break;
        }
    }
    
    if (restore_blend) {
        SDL_SetRenderDrawBlendMode(ren, old_blend);
    }
}

static void draw_render_preview(SDL_Renderer* ren, DrawingState* ds, int mouse_x, int mouse_y) {
    if (!ren || !ds) return;
    
    if (ds->is_drawing) {
        Stroke preview = {0};
        preview.color = ds->current_color;
        preview.size = ds->brush_size;
        preview.tool = ds->current_tool;
        preview.start = ds->shape_start;
        preview.end.x = mouse_x;
        preview.end.y = mouse_y;
        
        if (ds->current_tool != TOOL_PEN && ds->current_tool != TOOL_ERASER && ds->current_tool != TOOL_MARKER) {
            draw_render_stroke(ren, &preview, ds);
        }
    }
    
    SDL_Color cursor_color = ds->current_color;
    if (ds->current_tool == TOOL_ERASER) {
        cursor_color = (SDL_Color){255, 255, 255, 255};
    }
    
    draw_circle_outline(ren, mouse_x, mouse_y, ds->brush_size / 2 + 2, (SDL_Color){0, 0, 0, 180}, 1);
    draw_circle_outline(ren, mouse_x, mouse_y, ds->brush_size / 2, cursor_color, 1);
    SDL_SetRenderDrawColor(ren, cursor_color.r, cursor_color.g, cursor_color.b, 255);
    SDL_RenderDrawLine(ren, mouse_x - 4, mouse_y, mouse_x + 4, mouse_y);
    SDL_RenderDrawLine(ren, mouse_x, mouse_y - 4, mouse_x, mouse_y + 4);
}

static void draw_render_all(SDL_Renderer* ren, SDL_Texture* canvas_tex, DrawingState* ds, SDL_Rect dst) {
    if (!ren || !canvas_tex || !ds) return;
    
    SDL_SetRenderTarget(ren, canvas_tex);
    
    if (ds->needs_full_redraw) {
        SDL_SetRenderDrawColor(ren, THEME_DRAW_CANVAS_CLEAR_COLOR[0], THEME_DRAW_CANVAS_CLEAR_COLOR[1], THEME_DRAW_CANVAS_CLEAR_COLOR[2], 0);
        SDL_RenderClear(ren);
        
        for (int i = 0; i < ds->stroke_count; i++) {
            draw_render_stroke(ren, &ds->strokes[i], ds);
        }
        
        ds->last_rendered_stroke_count = ds->stroke_count;
        ds->needs_full_redraw = 0;
    } else {
        for (int i = ds->last_rendered_stroke_count; i < ds->stroke_count; i++) {
            draw_render_stroke(ren, &ds->strokes[i], ds);
        }
        ds->last_rendered_stroke_count = ds->stroke_count;
    }
    
    if (ds->is_drawing && ds->stroke_count < MAX_DRAW_STROKES) {
        Stroke* current = &ds->strokes[ds->stroke_count];
        if (current->tool == TOOL_PEN || current->tool == TOOL_ERASER || current->tool == TOOL_MARKER) {
            draw_render_stroke(ren, current, ds);
        }
    }
    
    SDL_SetRenderTarget(ren, NULL);
    
    SDL_RenderCopy(ren, canvas_tex, NULL, &dst);
}

static void draw_render_palette(SDL_Renderer* ren, DrawingState* ds, int win_w, int win_h) {
    if (!ren || !ds || !ds->show_palette) return;
    
    (void)win_w;
    
    int swatch_size = 32;
    int padding = 6;
    int cols = 4;
    int rows = 2;
    int palette_w = cols * (swatch_size + padding) + padding * 2;
    int palette_h = rows * (swatch_size + padding) + padding * 2;
    
    int palette_x = 20;
    int palette_y = win_h - palette_h - 90;
    
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect bg = {palette_x, palette_y, palette_w, palette_h};
    SDL_SetRenderDrawColor(ren, THEME_TEXT_INPUT_BG_COLOR[0], THEME_TEXT_INPUT_BG_COLOR[1], THEME_TEXT_INPUT_BG_COLOR[2], 200);
    SDL_RenderFillRect(ren, &bg);
    
    for (int i = 0; i < 7; i++) {
        int col = i % cols;
        int row = i / cols;
        SDL_Rect swatch = {
            palette_x + padding + col * (swatch_size + padding),
            palette_y + padding + row * (swatch_size + padding),
            swatch_size,
            swatch_size
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
        swatch_size,
        swatch_size
    };
    
    SDL_SetRenderDrawColor(ren, ds->custom_color.r, ds->custom_color.g, ds->custom_color.b, 255);
    SDL_RenderFillRect(ren, &custom_btn);
    
    float lum = (0.299f * ds->custom_color.r + 0.587f * ds->custom_color.g + 0.114f * ds->custom_color.b) / 255.0f;
    SDL_Color plus_color = lum > 0.5f ? (SDL_Color){0, 0, 0, 255} : (SDL_Color){255, 255, 255, 255};
    
    int cx = custom_btn.x + custom_btn.w / 2;
    int cy = custom_btn.y + custom_btn.h / 2;
    int plus_size = 12;
    SDL_SetRenderDrawColor(ren, plus_color.r, plus_color.g, plus_color.b, 255);
    SDL_RenderDrawLine(ren, cx - plus_size/2, cy, cx + plus_size/2, cy);
    SDL_RenderDrawLine(ren, cx - plus_size/2, cy + 1, cx + plus_size/2, cy + 1);
    SDL_RenderDrawLine(ren, cx, cy - plus_size/2, cx, cy + plus_size/2);
    SDL_RenderDrawLine(ren, cx + 1, cy - plus_size/2, cx + 1, cy + plus_size/2);
    
    if (ds->current_color.r == ds->custom_color.r &&
        ds->current_color.g == ds->custom_color.g &&
        ds->current_color.b == ds->custom_color.b) {
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_RenderDrawRect(ren, &custom_btn);
    }
}

static int draw_palette_click(DrawingState* ds, int mouse_x, int mouse_y, int win_w, int win_h) {
    if (!ds || !ds->show_palette) return 0;
    
    int swatch_size = 32;
    int padding = 6;
    int cols = 4;
    int rows = 2;
    
    int palette_x = 20;
    int palette_h = rows * (swatch_size + padding) + padding * 2;
    int palette_y = win_h - palette_h - 90;
    
    (void)win_w;
    
    for (int i = 0; i < 7; i++) {
        int col = i % cols;
        int row = i / cols;
        SDL_Rect swatch = {
            palette_x + padding + col * (swatch_size + padding),
            palette_y + padding + row * (swatch_size + padding),
            swatch_size,
            swatch_size
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
        swatch_size,
        swatch_size
    };
    
    if (mouse_x >= custom_btn.x && mouse_x < custom_btn.x + custom_btn.w &&
        mouse_y >= custom_btn.y && mouse_y < custom_btn.y + custom_btn.h) {
        unsigned char rgb[3] = {ds->custom_color.r, ds->custom_color.g, ds->custom_color.b};
        const char* hex_color = tinyfd_colorChooser(
            "Choose Color",
            NULL,
            rgb,
            rgb
        );
        if (hex_color) {
            unsigned int r, g, b;
            if (sscanf(hex_color, "#%02x%02x%02x", &r, &g, &b) == 3) {
                ds->custom_color.r = (unsigned char)r;
                ds->custom_color.g = (unsigned char)g;
                ds->custom_color.b = (unsigned char)b;
                ds->custom_color.a = 255;
                ds->current_color = ds->custom_color;
            }
        }
        return 1;
    }
    
    return 0;
}

static int export_save_dialog(char* out_path, size_t out_size, char* out_ext, char* default_filename) {
#ifdef _WIN32
    char filebuf[PATH_MAX];
    snprintf(filebuf, sizeof(filebuf), "%s.png", default_filename);

    const char filter[] =
        "PNG (*.png)\0*.png\0"
        "JPEG (*.jpg)\0*.jpg\0"
        "BMP (*.bmp)\0*.bmp\0"
        "TGA (*.tga)\0*.tga\0"
        "\0";

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filebuf;
    ofn.nMaxFile = sizeof(filebuf);
    ofn.lpstrDefExt = "png";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameA(&ofn))
        return 0;

    strncpy(out_path, filebuf, out_size - 1);
    out_path[out_size - 1] = 0;

    switch (ofn.nFilterIndex) {
        case 1: strcpy(out_ext, "png"); break;
        case 2: strcpy(out_ext, "jpg"); break;
        case 3: strcpy(out_ext, "bmp"); break;
        case 4: strcpy(out_ext, "tga"); break;
        default: strcpy(out_ext, "png"); break;
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

    const char* filters[] = {
        "*.png", "*.jpg", "*.bmp", "*.tga"
    };

    const char* path = tinyfd_saveFileDialog(
        "Export Drawing",
        filename,
        4,
        filters,
        "PNG, JPG, BMP, TGA"
    );

    if (!path) return 0;

    strncpy(out_path, path, out_size - 1);
    out_path[out_size - 1] = 0;

    const char* dot = strrchr(out_path, '.');
    if (dot) {
        dot++;
        if (!strcasecmp(dot, "png")) strcpy(out_ext, "png");
        else if (!strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg")) strcpy(out_ext, "jpg");
        else if (!strcasecmp(dot, "bmp")) strcpy(out_ext, "bmp");
        else if (!strcasecmp(dot, "tga")) strcpy(out_ext, "tga");
        else strcpy(out_ext, "png");
    } else {
        strcpy(out_ext, "png");
        strncat(out_path, ".png", out_size - strlen(out_path) - 1);
    }

    return 1;
#endif
}

static void draw_export(SDL_Renderer* ren,
                        SDL_Texture* canvas_tex,
                        SDL_Texture* video_tex,
                        SDL_Rect video_dst,
                        int win_w,
                        int win_h,
                        int include_video)
{
    if (!ren || !canvas_tex) return;

    char path[1024];
    char ext[8];

    if (!export_save_dialog(path, sizeof(path), ext, "drawing"))
        return;

    SDL_Surface* export_surf = SDL_CreateRGBSurface(
        0, win_w, win_h, 32,
        0x000000FF,
        0x0000FF00,
        0x00FF0000,
        0xFF000000
    );
    if (!export_surf) return;

    SDL_Texture* export_tex = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET,
        win_w,
        win_h
    );

    if (!export_tex) {
        SDL_FreeSurface(export_surf);
        return;
    }

    SDL_SetRenderTarget(ren, export_tex);
    SDL_SetRenderDrawColor(ren, THEME_LETTERBOX_COLOR[0], THEME_LETTERBOX_COLOR[1], THEME_LETTERBOX_COLOR[2], 0);
    SDL_RenderClear(ren);

    if (include_video && video_tex) {
        SDL_RenderCopy(ren, video_tex, NULL, &video_dst);
    }

    SDL_RenderCopy(ren, canvas_tex, NULL, NULL);

    SDL_RenderReadPixels(
        ren,
        NULL,
        SDL_PIXELFORMAT_RGBA32,
        export_surf->pixels,
        export_surf->pitch
    );

    SDL_SetRenderTarget(ren, NULL);

    if (strcmp(ext, "png") == 0) {
        stbi_write_png(path, win_w, win_h, 4,
                       export_surf->pixels, export_surf->pitch);

    } else if (strcmp(ext, "jpg") == 0) {
        stbi_write_jpg(path, win_w, win_h, 4,
                       export_surf->pixels, 90);

    } else if (strcmp(ext, "bmp") == 0) {
        SDL_SaveBMP(export_surf, path);

    } else if (strcmp(ext, "tga") == 0) {
        stbi_write_tga(path, win_w, win_h, 4,
                       export_surf->pixels);
    }

    SDL_DestroyTexture(export_tex);
    SDL_FreeSurface(export_surf);
}
