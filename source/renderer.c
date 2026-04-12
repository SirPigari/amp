#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include "config.h"
#include "../thirdparty/nob.h"
#include "../thirdparty/SDL2/SDL.h"
#include "../thirdparty/libavformat/avformat.h"
#include "../thirdparty/libavcodec/avcodec.h"
#include "../thirdparty/libswscale/swscale.h"
#include "../thirdparty/libavutil/imgutils.h"
#include "../thirdparty/libavutil/frame.h"
#include "../thirdparty/libavutil/hwcontext.h"
#include "../thirdparty/libswresample/swresample.h"
#include "../thirdparty/libavutil/opt.h"
#include "../thirdparty/libavutil/channel_layout.h"
#include "../thirdparty/libavutil/time.h"
#include "../thirdparty/libavfilter/avfilter.h"
#include "../thirdparty/libavfilter/buffersrc.h"
#include "../thirdparty/libavfilter/buffersink.h"
#include "../thirdparty/ass/ass.h"

#define VIDEO_PKT_QUEUE_CAP 128
#define AUDIO_PKT_QUEUE_CAP 256
#define AUDIO_QUEUE_TARGET_SEC 0.25

typedef struct {
    AVPacket* pkts;
    int capacity;
    int size;
    int r;
    int w;
} PacketQueue;

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_Texture* subtitle_texture;
    int width;
    int height;
    int video_ready;

    AVFormatContext* fmt_ctx;
    AVCodecContext* video_ctx;
    int video_stream_index;
    struct SwsContext* sws_ctx;
    enum AVPixelFormat sws_src_pix_fmt;
    AVRational video_time_base;

    AVCodecContext* audio_ctx;
    int audio_stream_index;
    SwrContext* swr_ctx;
    SDL_AudioDeviceID audio_dev;
    SDL_AudioSpec audio_spec;
    AVFrame* audio_frame;
    uint8_t* audio_buf;
    int audio_buf_size;
    float audio_volume;
    AVRational audio_time_base;
    double audio_clock_base;
    double audio_clock_pts;
    double start_time;
    int start_time_set;
    int64_t audio_base_samples;
    int64_t audio_samples_written;
    int audio_clock_valid;
    double last_time;

    AVCodecContext* subtitle_ctx;
    int subtitle_stream_index;
    ASS_Library* ass_lib;
    ASS_Renderer* ass_renderer;
    ASS_Track* ass_track;

    double playback_speed;
    double current_time;
    Uint32 clock_start_ticks;
    Uint32 clock_pause_ticks;
    Uint32 clock_pause_accum;
    int clock_paused;
    double clock_start_time;

    AVFrame* frame;
    AVFrame* yuv_frame;
    uint8_t* yuv_buffer;

    int* audio_streams;
    char** audio_names;
    int audio_count;
    int current_audio;

    int* subtitle_streams;
    char** subtitle_names;
    int subtitle_count;
    int current_subtitle;

    PacketQueue video_pktq;
    PacketQueue audio_pktq;
    AVPacket pending_pkt;
    int pending_valid;

    double frame_history[32];
    int frame_history_size;
    int frame_history_pos;

    char subtitle_override_font[128];
    uint32_t subtitle_override_color_rgb;
    int subtitle_override_size;
    int subtitle_override_margin_bottom;
    int64_t subtitle_offset_ms;

    AVBufferRef* hw_device_ctx;
    enum AVPixelFormat hw_pix_fmt;
    enum AVHWDeviceType hw_device_type;
    char hw_device_name[32];
    char current_filename[512];
    int hw_bad_frame_count;
    int hw_disabled;
    int hw_backend_marked;

    AVFilterGraph* filter_graph;
    AVFilterContext* buffersrc_ctx;
    AVFilterContext* buffersink_ctx;
    AVFrame* filtered_frame;

    unsigned int ar_x, ar_y;
    int zoom_percent;
    int desired_win_w, desired_win_h;
} VideoRenderer;

static void pkt_queue_init(PacketQueue* q, int capacity) {
    q->pkts = (AVPacket*)calloc((size_t)capacity, sizeof(AVPacket));
    q->capacity = capacity;
    q->size = 0;
    q->r = 0;
    q->w = 0;
}

static void pkt_queue_clear(PacketQueue* q) {
    if (!q || !q->pkts) return;
    for (int i = 0; i < q->capacity; i++) {
        av_packet_unref(&q->pkts[i]);
    }
    q->size = 0;
    q->r = 0;
    q->w = 0;
}

static void pkt_queue_free(PacketQueue* q) {
    if (!q || !q->pkts) return;
    pkt_queue_clear(q);
    free(q->pkts);
    q->pkts = NULL;
    q->capacity = 0;
}

extern int hw_cache_has(const char* name);
extern void hw_cache_mark_success(const char* name);

static int pkt_queue_is_full(PacketQueue* q) {
    return q && q->size >= q->capacity;
}

static int pkt_queue_is_empty(PacketQueue* q) {
    return !q || q->size == 0;
}

static int pkt_queue_push(PacketQueue* q, const AVPacket* pkt) {
    if (!q || !pkt || pkt_queue_is_full(q)) return 0;
    av_packet_ref(&q->pkts[q->w], pkt);
    q->w = (q->w + 1) % q->capacity;
    q->size++;
    return 1;
}

static int pkt_queue_pop(PacketQueue* q, AVPacket* out) {
    if (!q || !out || pkt_queue_is_empty(q)) return 0;
    av_packet_move_ref(out, &q->pkts[q->r]);
    q->r = (q->r + 1) % q->capacity;
    q->size--;
    return 1;
}

static double vr_get_audio_queue_seconds(VideoRenderer* vr) {
    if (!vr || !vr->audio_dev || vr->audio_spec.freq <= 0) return 0.0;
    uint32_t queued = SDL_GetQueuedAudioSize(vr->audio_dev);
    int bytes_per_sample = (SDL_AUDIO_BITSIZE(vr->audio_spec.format) / 8) * (int)vr->audio_spec.channels;
    if (bytes_per_sample <= 0) return 0.0;
    int queued_samples = (int)(queued / bytes_per_sample);
    return (double)queued_samples / (double)vr->audio_spec.freq;
}

static double vr_get_audio_clock(VideoRenderer* vr) {
    if (!vr || !vr->audio_dev || !vr->audio_clock_valid) return 0.0;
    double queued_seconds = vr_get_audio_queue_seconds(vr);
    double t = vr->audio_clock_pts - queued_seconds;
    return t < 0.0 ? 0.0 : t;
}

double vr_get_master_time(VideoRenderer* vr) {
    if (!vr) return 0.0;
    Uint32 now_ticks = vr->clock_paused ? vr->clock_pause_ticks : SDL_GetTicks();
    Uint32 elapsed = now_ticks - vr->clock_start_ticks;
    Uint32 active_ms = elapsed > vr->clock_pause_accum ? (elapsed - vr->clock_pause_accum) : 0;
    double t = vr->clock_start_time + ((double)active_ms / 1000.0) * vr->playback_speed;
    return t < 0.0 ? 0.0 : t;
}

static void vr_free_track_lists(VideoRenderer* vr) {
    if (!vr) return;
    for (int i = 0; i < vr->audio_count; i++) free(vr->audio_names[i]);
    free(vr->audio_names);
    free(vr->audio_streams);
    vr->audio_names = NULL;
    vr->audio_streams = NULL;
    vr->audio_count = 0;
    vr->current_audio = -1;

    for (int i = 0; i < vr->subtitle_count; i++) free(vr->subtitle_names[i]);
    free(vr->subtitle_names);
    free(vr->subtitle_streams);
    vr->subtitle_names = NULL;
    vr->subtitle_streams = NULL;
    vr->subtitle_count = 0;
    vr->current_subtitle = -1;
}

static void vr_reset_stream(VideoRenderer* vr) {
    if (!vr) return;
    if (vr->subtitle_texture) {
        SDL_DestroyTexture(vr->subtitle_texture);
        vr->subtitle_texture = NULL;
    }
    if (vr->texture) {
        SDL_DestroyTexture(vr->texture);
        vr->texture = NULL;
    }
    if (vr->frame) {
        av_frame_free(&vr->frame);
        vr->frame = NULL;
    }
    if (vr->yuv_frame) {
        av_frame_free(&vr->yuv_frame);
        vr->yuv_frame = NULL;
    }
    if (vr->yuv_buffer) {
        free(vr->yuv_buffer);
        vr->yuv_buffer = NULL;
    }
    if (vr->sws_ctx) {
        sws_freeContext(vr->sws_ctx);
        vr->sws_ctx = NULL;
        vr->sws_src_pix_fmt = AV_PIX_FMT_NONE;
    }
    if (vr->video_ctx) {
        avcodec_free_context(&vr->video_ctx);
        vr->video_ctx = NULL;
    }

    if (vr->audio_dev) {
        SDL_PauseAudioDevice(vr->audio_dev, 1);
        Uint32 drain_start = SDL_GetTicks();
        while (SDL_GetQueuedAudioSize(vr->audio_dev) > 0 && SDL_GetTicks() - drain_start < 200)
            SDL_Delay(5);
        SDL_ClearQueuedAudio(vr->audio_dev);
    }
    if (vr->audio_frame) {
        av_frame_free(&vr->audio_frame);
        vr->audio_frame = NULL;
    }
    if (vr->audio_buf) {
        free(vr->audio_buf);
        vr->audio_buf = NULL;
        vr->audio_buf_size = 0;
    }
    vr->audio_clock_base = 0.0;
    vr->audio_clock_pts = 0.0;
    vr->start_time = 0.0;
    vr->start_time_set = 0;
    vr->audio_base_samples = 0;
    vr->audio_samples_written = 0;
    vr->audio_clock_valid = 0;
    if (vr->swr_ctx) {
        swr_free(&vr->swr_ctx);
        vr->swr_ctx = NULL;
    }
    if (vr->audio_ctx) {
        avcodec_free_context(&vr->audio_ctx);
        vr->audio_ctx = NULL;
    }

    if (vr->subtitle_ctx) {
        avcodec_free_context(&vr->subtitle_ctx);
        vr->subtitle_ctx = NULL;
    }
    if (vr->ass_track) {
        ass_free_track(vr->ass_track);
        vr->ass_track = NULL;
    }
    if (vr->ass_renderer) {
        ass_renderer_done(vr->ass_renderer);
        vr->ass_renderer = NULL;
    }
    if (vr->ass_lib) {
        ass_library_done(vr->ass_lib);
        vr->ass_lib = NULL;
    }

    if (vr->hw_device_ctx) {
        av_buffer_unref(&vr->hw_device_ctx);
        vr->hw_device_ctx = NULL;
        vr->hw_pix_fmt = AV_PIX_FMT_NONE;
        vr->hw_device_type = AV_HWDEVICE_TYPE_NONE;
        vr->hw_device_name[0] = '\0';
    }

    if (vr->filtered_frame) {
        av_frame_free(&vr->filtered_frame);
        vr->filtered_frame = NULL;
    }
    if (vr->filter_graph) {
        avfilter_graph_free(&vr->filter_graph);
        vr->filter_graph = NULL;
        vr->buffersrc_ctx = NULL;
        vr->buffersink_ctx = NULL;
    }

    if (vr->pending_valid) {
        av_packet_unref(&vr->pending_pkt);
        vr->pending_valid = 0;
    }
    pkt_queue_clear(&vr->video_pktq);
    pkt_queue_clear(&vr->audio_pktq);

    if (vr->fmt_ctx) {
        avformat_close_input(&vr->fmt_ctx);
        vr->fmt_ctx = NULL;
    }

    vr_free_track_lists(vr);
    vr->video_stream_index = -1;
    vr->audio_stream_index = -1;
    vr->subtitle_stream_index = -1;
    vr->video_time_base = (AVRational){0, 1};
    vr->audio_time_base = (AVRational){0, 1};
    vr->width = 0;
    vr->height = 0;
    vr->video_ready = 0;
    vr->current_time = 0.0;
    vr->last_time = 0.0;
    vr->subtitle_offset_ms = 0;
    vr->zoom_percent = 100;
    vr->clock_start_ticks = SDL_GetTicks();
    vr->clock_pause_ticks = 0;
    vr->clock_pause_accum = 0;
    vr->clock_paused = 0;
    vr->clock_start_time = 0.0;
}

static char* vr_dup_stream_name(const AVStream* stream, const char* kind) {
    const AVDictionaryEntry* lang = av_dict_get(stream->metadata, "language", NULL, 0);
    const AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", NULL, 0);
    const char* codec = avcodec_get_name(stream->codecpar->codec_id);
    char buf[256];
    if (title && title->value && title->value[0]) {
        snprintf(buf, sizeof(buf), "%s", title->value);
    } else if (lang && lang->value && lang->value[0]) {
        snprintf(buf, sizeof(buf), "%s (%s)", lang->value, codec);
    } else {
        snprintf(buf, sizeof(buf), "%s (%s)", kind, codec);
    }
    return strdup(buf);
}

static void vr_add_track(VideoRenderer* vr, int is_audio, int stream_index, const char* name) {
    if (is_audio) {
        int*   s = (int*)  realloc(vr->audio_streams, sizeof(int)   * (vr->audio_count + 1));
        if (!s) return;
        vr->audio_streams = s;
        char** n = (char**)realloc(vr->audio_names,   sizeof(char*) * (vr->audio_count + 1));
        if (!n) return;
        vr->audio_names = n;
        vr->audio_streams[vr->audio_count] = stream_index;
        vr->audio_names[vr->audio_count]   = strdup(name);
        vr->audio_count++;
    } else {
        int*   s = (int*)  realloc(vr->subtitle_streams, sizeof(int)   * (vr->subtitle_count + 1));
        if (!s) return;
        vr->subtitle_streams = s;
        char** n = (char**)realloc(vr->subtitle_names,   sizeof(char*) * (vr->subtitle_count + 1));
        if (!n) return;
        vr->subtitle_names = n;
        vr->subtitle_streams[vr->subtitle_count] = stream_index;
        vr->subtitle_names[vr->subtitle_count]   = strdup(name);
        vr->subtitle_count++;
    }
}

static int vr_subtitle_style_mode(VideoRenderer* vr) {
    if (!vr || vr->current_subtitle < 0 || vr->subtitle_stream_index < 0 || !vr->fmt_ctx) return 0;
    enum AVCodecID codec_id = vr->fmt_ctx->streams[vr->subtitle_stream_index]->codecpar->codec_id;
    if (codec_id == AV_CODEC_ID_ASS || codec_id == AV_CODEC_ID_SSA) return 2;
    return 1;
}

static void subtitle_build_override_tags(VideoRenderer* vr, char* out, size_t out_size, int include_position) {
    if (!vr || !out || out_size == 0) return;
    int y = vr->height - vr->subtitle_override_margin_bottom;
    if (y < 0) y = 0;
    unsigned r = (vr->subtitle_override_color_rgb >> 16) & 0xFF;
    unsigned g = (vr->subtitle_override_color_rgb >> 8) & 0xFF;
    unsigned b = vr->subtitle_override_color_rgb & 0xFF;

    if (include_position) {
        snprintf(out, out_size,
                 "{\\fn%s\\fs%d\\1c&H%02X%02X%02X&\\an2\\pos(%d,%d)}",
                 vr->subtitle_override_font,
                 vr->subtitle_override_size,
                 b, g, r,
                 vr->width / 2,
                 y);
    } else {
        snprintf(out, out_size,
                 "{\\fn%s\\fs%d\\1c&H%02X%02X%02X&}",
                 vr->subtitle_override_font,
                 vr->subtitle_override_size,
                 b, g, r);
    }
}

static void subtitle_inject_tags_into_dialogue(const char* ass_line, const char* tags, char* out, size_t out_size) {
    if (!ass_line || !tags || !out || out_size == 0) return;

    const char* dialogue = strstr(ass_line, "Dialogue:");
    if (!dialogue) {
        snprintf(out, out_size, "%s", ass_line);
        return;
    }

    const char* colon = strchr(dialogue, ':');
    if (!colon) {
        snprintf(out, out_size, "%s", ass_line);
        return;
    }

    const char* text_start = colon + 1;
    int commas = 0;
    while (*text_start && commas < 9) {
        if (*text_start == ',') commas++;
        text_start++;
    }

    if (commas < 9) {
        snprintf(out, out_size, "%s", ass_line);
        return;
    }

    const char* text_insert = text_start;
    while (*text_insert == ' ' || *text_insert == '\t') text_insert++;
    while (*text_insert == '{') {
        const char* close = strchr(text_insert, '}');
        if (!close) break;
        text_insert = close + 1;
        while (*text_insert == ' ' || *text_insert == '\t') text_insert++;
    }

    int head_len = (int)(text_insert - ass_line);
    snprintf(out, out_size, "%.*s%s%s", head_len, ass_line, tags, text_insert);
}

static void vr_queue_audio(VideoRenderer* vr, AVFrame* frame) {
    if (!vr || !vr->audio_dev || !vr->swr_ctx) return;
    int out_samples = (int)av_rescale_rnd(
        swr_get_delay(vr->swr_ctx, vr->audio_ctx->sample_rate) + frame->nb_samples,
        vr->audio_spec.freq, vr->audio_ctx->sample_rate, AV_ROUND_UP);

    int out_channels = 2;
    int out_linesize = 0;
    int out_buf_size = av_samples_get_buffer_size(&out_linesize, out_channels, out_samples, AV_SAMPLE_FMT_S16, 1);
    if (out_buf_size <= 0) return;

    if (out_buf_size > vr->audio_buf_size) {
        uint8_t* tmp = (uint8_t*)realloc(vr->audio_buf, out_buf_size);
        if (!tmp) return;
        vr->audio_buf = tmp;
        vr->audio_buf_size = out_buf_size;
    }

    uint8_t* out_planes[2] = { vr->audio_buf, NULL };
    int converted = swr_convert(vr->swr_ctx, out_planes, out_samples,
                                (const uint8_t**)frame->data, frame->nb_samples);
    if (converted <= 0) return;

    int bytes = av_samples_get_buffer_size(NULL, out_channels, converted, AV_SAMPLE_FMT_S16, 1);
    if (bytes <= 0) return;

    int16_t* samples = (int16_t*)vr->audio_buf;
    int sample_count = bytes / sizeof(int16_t);
    float gain = vr->audio_volume;
    for (int i = 0; i < sample_count; i++) {
        int v = (int)(samples[i] * gain);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        samples[i] = (int16_t)v;
    }

    SDL_QueueAudio(vr->audio_dev, vr->audio_buf, bytes);
    SDL_PauseAudioDevice(vr->audio_dev, 0);
    if (vr->audio_time_base.num != 0 && vr->audio_time_base.den != 0) {
        int64_t pts = frame->best_effort_timestamp;
        if (pts != AV_NOPTS_VALUE) {
            double pts_sec = pts * av_q2d(vr->audio_time_base);
            if (!vr->start_time_set) {
                vr->start_time = pts_sec;
                vr->start_time_set = 1;
            }
            pts_sec -= vr->start_time;
            if (!vr->audio_clock_valid) {
                vr->audio_clock_base = pts_sec;
                vr->audio_base_samples = vr->audio_samples_written;
                vr->audio_clock_valid = 1;
            }
            double delay_sec = 0.0;
            int64_t delay = swr_get_delay(vr->swr_ctx, vr->audio_ctx->sample_rate);
            if (delay > 0) delay_sec = (double)delay / (double)vr->audio_ctx->sample_rate;
            vr->audio_clock_pts = pts_sec + (double)converted / (double)vr->audio_spec.freq - delay_sec;
        }
    }
    vr->audio_samples_written += converted;
}

static void vr_process_subtitle(VideoRenderer* vr, const AVPacket* pkt) {
    if (!vr || !vr->subtitle_ctx || !vr->ass_track) return;

    AVSubtitle sub;
    memset(&sub, 0, sizeof(sub));
    int got = 0;
    int ret = avcodec_decode_subtitle2(vr->subtitle_ctx, &sub, &got, (AVPacket*)pkt);
    if (ret < 0 || !got) return;

    int64_t pts_ms = AV_NOPTS_VALUE;
    if (sub.pts != AV_NOPTS_VALUE) {
        pts_ms = sub.pts / 1000;
    } else if (pkt->pts != AV_NOPTS_VALUE) {
        AVRational tb = vr->fmt_ctx->streams[pkt->stream_index]->time_base;
        pts_ms = av_rescale_q(pkt->pts, tb, (AVRational){1, 1000});
    }

    if (pts_ms == AV_NOPTS_VALUE) {
        avsubtitle_free(&sub);
        return;
    }

    pts_ms -= (int64_t)(vr->start_time * 1000.0);
    if (pts_ms < 0) pts_ms = 0;

    int64_t start_ms = pts_ms + (int64_t)sub.start_display_time;
    if (start_ms < 0) start_ms = 0;

    int64_t duration_ms = (int64_t)sub.end_display_time - (int64_t)sub.start_display_time;
    if (duration_ms <= 0 && pkt->duration > 0) {
        AVRational tb = vr->fmt_ctx->streams[pkt->stream_index]->time_base;
        duration_ms = av_rescale_q(pkt->duration, tb, (AVRational){1, 1000});
    }
    if (duration_ms <= 0) duration_ms = 2000;

    int subtitle_mode = vr_subtitle_style_mode(vr);

    for (unsigned i = 0; i < sub.num_rects; i++) {
        AVSubtitleRect* r = sub.rects[i];
        if (r->ass && r->ass[0]) {
            if (subtitle_mode == 2) {
                ass_process_chunk(vr->ass_track, r->ass, (int)strlen(r->ass),
                                  start_ms, duration_ms);
            } else {
                char tags[320];
                subtitle_build_override_tags(vr, tags, sizeof(tags), 1);
                char overridden[8192];
                subtitle_inject_tags_into_dialogue(r->ass, tags, overridden, sizeof(overridden));
                ass_process_chunk(vr->ass_track, overridden, (int)strlen(overridden),
                                  start_ms, duration_ms);
            }
        } else if (r->text && r->text[0]) {
            char utf8_buf[2048];
            const char* encoding = "unknown";
            const char* text_utf8 = subtitle_normalize_to_utf8(r->text, utf8_buf, sizeof(utf8_buf), &encoding);
            char escaped[4096];
            const char* src = text_utf8;
            char* dst = escaped;
            while (*src && dst < escaped + sizeof(escaped) - 2) {
                if (*src == '{') { *dst++ = '\\'; *dst++ = '{'; }
                else *dst++ = *src;
                src++;
            }
            *dst = '\0';

            char tags[320];
            subtitle_build_override_tags(vr, tags, sizeof(tags), 1);

            char buf[8192];
            snprintf(buf, sizeof(buf),
                "0,0,Default,,0,0,0,,%s%s", tags, escaped);
            ass_process_chunk(vr->ass_track, buf, (int)strlen(buf),
                              start_ms, duration_ms);
        }
    }

    avsubtitle_free(&sub);
}

VideoRenderer* vr_create(SDL_Window* window, SDL_Renderer* renderer) {
    avformat_network_init();

    VideoRenderer* vr = (VideoRenderer*)malloc(sizeof(VideoRenderer));
    memset(vr, 0, sizeof(VideoRenderer));
    vr->window = window;
    vr->renderer = renderer;
    vr->playback_speed = 1.0;
    vr->current_time = 0.0;
    vr->audio_volume = 1.0f;
    vr->current_audio = -1;
    vr->current_subtitle = -1;
    vr->audio_clock_pts = 0.0;
    vr->start_time = 0.0;
    vr->start_time_set = 0;
    vr->clock_start_ticks = SDL_GetTicks();
    vr->clock_pause_ticks = 0;
    vr->clock_pause_accum = 0;
    vr->clock_paused = 0;
    vr->clock_start_time = 0.0;
    pkt_queue_init(&vr->video_pktq, VIDEO_PKT_QUEUE_CAP);
    pkt_queue_init(&vr->audio_pktq, AUDIO_PKT_QUEUE_CAP);
    vr->pending_valid = 0;
    vr->frame_history_size = 0;
    vr->frame_history_pos = 0;
    memset(vr->frame_history, 0, sizeof(vr->frame_history));
    snprintf(vr->subtitle_override_font, sizeof(vr->subtitle_override_font), "%s", "Arial");
    vr->subtitle_override_color_rgb = 0xFFFFFF;
    vr->subtitle_override_size = 30;
    vr->subtitle_override_margin_bottom = 64;
    vr->subtitle_offset_ms = 0;
    vr->hw_device_ctx = NULL;
    vr->hw_pix_fmt = AV_PIX_FMT_NONE;
    vr->hw_device_type = AV_HWDEVICE_TYPE_NONE;
    vr->hw_device_name[0] = '\0';
    vr->current_filename[0] = '\0';
    vr->hw_bad_frame_count = 0;
    vr->hw_disabled = 0;
    vr->ar_x = 0;
    vr->ar_y = 0;
    vr->zoom_percent = 100;
    vr->desired_win_w = 0;
    vr->desired_win_h = 0;
    return vr;
}

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
    if (!ctx || !pix_fmts) return AV_PIX_FMT_NONE;
    VideoRenderer* vr = (VideoRenderer*)ctx->opaque;
    if (!vr) return AV_PIX_FMT_NONE;
    for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == vr->hw_pix_fmt) return *p;
    }
    return AV_PIX_FMT_NONE;
}

static int try_init_hw_decoder(VideoRenderer* vr, AVCodecContext* ctx, const AVCodec* codec, const char* hw_requested) {
    if (!vr || !ctx || !codec) return -1;
    if (!hw_requested || !hw_requested[0]) return -1;

    char req[32];
    snprintf(req, sizeof(req), "%s", hw_requested);
#ifdef _WIN32
    for (char* p = req; *p; ++p) *p = (char)tolower(*p);
#else
    for (char* p = req; *p; ++p) *p = (char)tolower(*p);
#endif

#define MAX_TRY 8
    const char* try_list[MAX_TRY];
    int try_count = 0;
    if (strcmp(req, "none") == 0) {
        nob_log(NOB_INFO, "Hardware decode disabled");
        return -1;
    }
    int is_auto  = strcmp(req, "auto")  == 0;
    int is_accel = strcmp(req, "accel") == 0;

    if (is_auto || is_accel) {
#ifdef _WIN32
        const char* defaults[] = {"d3d11va", "dxva2", "vaapi"};
        for (int i = 0; i < (int)(sizeof(defaults)/sizeof(defaults[0])) && try_count < MAX_TRY; i++) {
            if (hw_cache_has(defaults[i])) try_list[try_count++] = defaults[i];
        }
        for (int i = 0; i < (int)(sizeof(defaults)/sizeof(defaults[0])) && try_count < MAX_TRY; i++) {
            int dup = 0; for (int j = 0; j < try_count; j++) if (strcmp(try_list[j], defaults[i]) == 0) { dup = 1; break; }
            if (!dup) try_list[try_count++] = defaults[i];
        }
        try_count = try_count > 0 ? try_count : 3;
#else
        const char* defaults[] = {"vaapi", "d3d11va", "dxva2"};
        for (int i = 0; i < (int)(sizeof(defaults)/sizeof(defaults[0])) && try_count < MAX_TRY; i++) {
            if (hw_cache_has(defaults[i])) try_list[try_count++] = defaults[i];
        }
        for (int i = 0; i < (int)(sizeof(defaults)/sizeof(defaults[0])) && try_count < MAX_TRY; i++) {
            int dup = 0; for (int j = 0; j < try_count; j++) if (strcmp(try_list[j], defaults[i]) == 0) { dup = 1; break; }
            if (!dup) try_list[try_count++] = defaults[i];
        }
        try_count = try_count > 0 ? try_count : 3;
#endif
    } else {
        try_list[0] = req; try_count = 1;
    }

    for (int ti = 0; ti < try_count; ti++) {
        const char* name = try_list[ti];
        enum AVHWDeviceType type = av_hwdevice_find_type_by_name(name);
        if (type == AV_HWDEVICE_TYPE_NONE) continue;

        if (ctx->codec_id == AV_CODEC_ID_HEVC && ctx->profile == 2 && strcmp(name, "vaapi") == 0) {
            nob_log(NOB_INFO, "HEVC profile 2 detected (10-bit), skipping hardware acceleration");
            return -1;
        }
        if (ctx->codec_id == AV_CODEC_ID_H264) {
            nob_log(NOB_INFO, "H.264 detected, skipping hardware acceleration");
            return -1;
        }

        enum AVPixelFormat pix = AV_PIX_FMT_NONE;
        for (int i = 0; ; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
            if (!config) break;
            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == type) {
                pix = config->pix_fmt;
                break;
            }
        }
        if (pix == AV_PIX_FMT_NONE) continue;

        AVBufferRef* dev_ctx = NULL;
        int err = -1;
        for (int attempt = 0; attempt < 2; attempt++) {
            err = av_hwdevice_ctx_create(&dev_ctx, type, NULL, NULL, 0);
            if (err == 0 && dev_ctx) break;
            if (dev_ctx) { av_buffer_unref(&dev_ctx); dev_ctx = NULL; }
        }
        if (err < 0 || !dev_ctx) {
            continue;
        }

        vr->hw_device_ctx = dev_ctx;
        vr->hw_pix_fmt = pix;
        vr->hw_device_type = type;
        snprintf(vr->hw_device_name, sizeof(vr->hw_device_name), "%s", name);

        ctx->hw_device_ctx = av_buffer_ref(vr->hw_device_ctx);
        ctx->opaque = vr;
        ctx->get_format = get_hw_format;
        nob_log(NOB_INFO, "HW decode enabled: %s (pixfmt %d)", name, pix);
        if (!vr->hw_backend_marked) {
            hw_cache_mark_success(name);
            vr->hw_backend_marked = 1;
        }
        return 0;
    }

    if (is_accel) {
        nob_log(NOB_WARNING, "HW accel requested but none available");
    }
    return -1;
}

int vr_load(VideoRenderer* vr, const char* filename, const char* hw_opt) {
    if (!vr || !filename) return 0;
    vr_reset_stream(vr);
    snprintf(vr->current_filename, sizeof(vr->current_filename), "%s", filename);
    vr->hw_bad_frame_count = 0;
    vr->hw_disabled = 0;

    av_log_set_level(AV_LOG_QUIET);

    Uint32 load_start = SDL_GetTicks();

    AVDictionary* open_opts = NULL;
    av_dict_set(&open_opts, "probesize", AMP_FF_PROBE_SIZE, 0);
    av_dict_set(&open_opts, "analyzeduration", AMP_FF_ANALYZE_DURATION_US, 0);

    if (avformat_open_input(&vr->fmt_ctx, filename, NULL, &open_opts) != 0) {
        av_dict_free(&open_opts);
        nob_log(NOB_ERROR, "Failed to open video: %s", filename);
        return 0;
    }
    av_dict_free(&open_opts);

    if (avformat_find_stream_info(vr->fmt_ctx, NULL) < 0) {
        nob_log(NOB_ERROR, "Failed to find stream info");
        vr_reset_stream(vr);
        return 0;
    }

    if (vr->fmt_ctx->start_time != AV_NOPTS_VALUE) {
        vr->start_time = vr->fmt_ctx->start_time * av_q2d(AV_TIME_BASE_Q);
        vr->start_time_set = 1;
    }

    for (unsigned i = 0; i < vr->fmt_ctx->nb_streams; i++) {
        AVStream* stream = vr->fmt_ctx->streams[i];

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && vr->video_stream_index < 0) {
            vr->video_stream_index = (int)i;
            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec) { nob_log(NOB_ERROR, "Failed to find video decoder"); continue; }
            
            int skip_hw = 0;
            if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
                nob_log(NOB_INFO, "H.264 detected, using software decoding");
                skip_hw = 1;
            } else if (stream->codecpar->codec_id == AV_CODEC_ID_HEVC && stream->codecpar->profile == 2 && hw_opt && strcmp(hw_opt, "vaapi") == 0) {
                nob_log(NOB_INFO, "HEVC profile 2 detected (10-bit), forcing software decoding");
                skip_hw = 1;
            }
            
            vr->video_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(vr->video_ctx, stream->codecpar);
            
            int hw_tried = 0;
            if (!skip_hw && hw_opt && strcmp(hw_opt, "none") != 0) {
                hw_tried = (try_init_hw_decoder(vr, vr->video_ctx, codec, hw_opt) == 0);
            }
            
            if (avcodec_open2(vr->video_ctx, (AVCodec*)codec, NULL) < 0) {
                if (hw_tried) {
                    nob_log(NOB_WARNING, "Hardware decoder failed to open, falling back to software decoding");
                    avcodec_free_context(&vr->video_ctx);
                    
                    if (vr->hw_device_ctx) {
                        av_buffer_unref(&vr->hw_device_ctx);
                        vr->hw_device_ctx = NULL;
                    }
                    vr->hw_pix_fmt = AV_PIX_FMT_NONE;
                    vr->hw_device_type = AV_HWDEVICE_TYPE_NONE;
                    vr->hw_device_name[0] = '\0';
                    
                    vr->video_ctx = avcodec_alloc_context3(codec);
                    avcodec_parameters_to_context(vr->video_ctx, stream->codecpar);
                    
                    if (avcodec_open2(vr->video_ctx, (AVCodec*)codec, NULL) < 0) {
                        nob_log(NOB_ERROR, "Failed to open video decoder (software fallback also failed)");
                        avcodec_free_context(&vr->video_ctx);
                        vr->video_ctx = NULL;
                        continue;
                    } else {
                        nob_log(NOB_INFO, "Successfully opened software decoder");
                    }
                } else {
                    nob_log(NOB_ERROR, "Failed to open video decoder");
                    avcodec_free_context(&vr->video_ctx);
                    vr->video_ctx = NULL;
                    continue;
                }
            } else {
                if (hw_tried && vr->hw_device_ctx) {
                    nob_log(NOB_INFO, "Successfully opened hardware decoder: %s", vr->hw_device_name);
                } else {
                    nob_log(NOB_INFO, "Successfully opened software decoder");
                }
            }

            av_log_set_level(AV_LOG_ERROR);

            vr->video_time_base = stream->time_base;
            vr->width = vr->video_ctx->width;
            vr->height = vr->video_ctx->height;

            vr->texture = SDL_CreateTexture(vr->renderer,
                SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
                vr->width, vr->height);
            
            SDL_SetTextureScaleMode(vr->texture, SDL_ScaleModeLinear);

            vr->sws_ctx = sws_getContext(
                vr->width, vr->height, vr->video_ctx->pix_fmt,
                vr->width, vr->height, AV_PIX_FMT_YUV420P,
                SWS_LANCZOS | SWS_ACCURATE_RND, NULL, NULL, NULL);
            vr->sws_src_pix_fmt = vr->video_ctx->pix_fmt;

            vr->frame = av_frame_alloc();
            vr->yuv_frame = av_frame_alloc();
            vr->yuv_buffer = (uint8_t*)malloc(
                av_image_get_buffer_size(AV_PIX_FMT_YUV420P, vr->width, vr->height, 1));
            av_image_fill_arrays(vr->yuv_frame->data, vr->yuv_frame->linesize,
                vr->yuv_buffer, AV_PIX_FMT_YUV420P, vr->width, vr->height, 1);

        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            char* name = vr_dup_stream_name(stream, "Audio");
            vr_add_track(vr, 1, (int)i, name);
            free(name);
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            char* name = vr_dup_stream_name(stream, "Subtitles");
            vr_add_track(vr, 0, (int)i, name);
            free(name);
        }
    }

    if (vr->video_stream_index < 0 || !vr->video_ctx || !vr->fmt_ctx) {
        vr_reset_stream(vr);
        return 0;
    }

    if (vr->audio_count > 0) {
        vr->current_audio = 0;
        vr->audio_stream_index = vr->audio_streams[0];
    }

    vr->current_subtitle = -1;
    vr->subtitle_stream_index = -1;

    if (vr->audio_stream_index >= 0) {
        AVStream* stream = vr->fmt_ctx->streams[vr->audio_stream_index];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            vr->audio_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(vr->audio_ctx, stream->codecpar);
            if (avcodec_open2(vr->audio_ctx, (AVCodec*)codec, NULL) < 0) {
                avcodec_free_context(&vr->audio_ctx);
                vr->audio_ctx = NULL;
            }
        }

        if (vr->audio_ctx) {
            vr->audio_time_base = stream->time_base;
            
            SDL_AudioSpec want;
            SDL_zero(want);
            want.freq = vr->audio_ctx->sample_rate;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = 1024;
            want.callback = NULL;
            if (vr->audio_dev && vr->audio_spec.freq == want.freq) {
                SDL_ClearQueuedAudio(vr->audio_dev);
            } else {
                if (vr->audio_dev) {
                    SDL_PauseAudioDevice(vr->audio_dev, 1);
                    SDL_CloseAudioDevice(vr->audio_dev);
                    vr->audio_dev = 0;
                }
                vr->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &vr->audio_spec,
                                                     SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
                if (!vr->audio_dev) {
                    nob_log(NOB_ERROR, "SDL_OpenAudioDevice failed: %s", SDL_GetError());
                }
            }
            if (vr->audio_dev) SDL_PauseAudioDevice(vr->audio_dev, 0);

            AVChannelLayout in_layout = vr->audio_ctx->ch_layout;
            AVChannelLayout out_layout;
            av_channel_layout_default(&out_layout, 2);
            swr_alloc_set_opts2(&vr->swr_ctx,
                &out_layout, AV_SAMPLE_FMT_S16, vr->audio_spec.freq,
                &in_layout, vr->audio_ctx->sample_fmt, vr->audio_ctx->sample_rate,
                0, NULL);
            swr_init(vr->swr_ctx);
            vr->audio_frame = av_frame_alloc();
        }
    }

    if (!vr->ass_lib) {
        vr->ass_lib = ass_library_init();
    }
    if (!vr->ass_renderer && vr->ass_lib) {
        vr->ass_renderer = ass_renderer_init(vr->ass_lib);
        ass_set_frame_size(vr->ass_renderer, vr->width, vr->height);
        ass_set_fonts(vr->ass_renderer, NULL, "Arial", 1, NULL, 1);
    }

    fprintf(stderr, "[AUDIO TRACKS] Found %d audio track(s):\n", vr->audio_count);
    for (int i = 0; i < vr->audio_count; i++) {
        fprintf(stderr, "  [%02d] %s (stream %d)%s\n",
            i, vr->audio_names[i], vr->audio_streams[i],
            i == vr->current_audio ? " <- SELECTED" : "");
    }
    fprintf(stderr, "[SUBTITLE TRACKS] Found %d subtitle track(s):\n", vr->subtitle_count);
    for (int i = 0; i < vr->subtitle_count; i++) {
        fprintf(stderr, "  [%02d] %s (stream %d)\n",
            i, vr->subtitle_names[i], vr->subtitle_streams[i]);
    }

    nob_log(NOB_INFO, "vr_load total: %u ms", SDL_GetTicks() - load_start);
    return 1;
}

static void vr_demux_packets(VideoRenderer* vr) {
    if (!vr || !vr->fmt_ctx) return;

    if (pkt_queue_is_full(&vr->video_pktq)
        && (!vr->audio_ctx || pkt_queue_is_full(&vr->audio_pktq))
        && !vr->pending_valid) {
        return;
    }

    int reads = 0;
    const Uint64 perf_freq = SDL_GetPerformanceFrequency();
    const Uint64 start_counter = SDL_GetPerformanceCounter();

    while (reads < DEMUX_MAX_READS_PER_TICK) {
        Uint64 now_counter = SDL_GetPerformanceCounter();
        double elapsed_ms = perf_freq > 0
            ? ((double)(now_counter - start_counter) * 1000.0 / (double)perf_freq)
            : 0.0;
        if (reads >= DEMUX_MIN_READS_PER_TICK && elapsed_ms >= DEMUX_TIME_BUDGET_MS) {
            break;
        }

        if (vr->pending_valid) {
            int stream_index = vr->pending_pkt.stream_index;
            if (stream_index == vr->video_stream_index) {
                if (pkt_queue_is_full(&vr->video_pktq)) return;
                pkt_queue_push(&vr->video_pktq, &vr->pending_pkt);
            } else if (vr->audio_ctx && stream_index == vr->audio_stream_index) {
                if (pkt_queue_is_full(&vr->audio_pktq)) return;
                pkt_queue_push(&vr->audio_pktq, &vr->pending_pkt);
            } else if (vr->subtitle_ctx
                       && vr->subtitle_stream_index >= 0
                       && stream_index == vr->subtitle_stream_index) {
                vr_process_subtitle(vr, &vr->pending_pkt);
            }
            av_packet_unref(&vr->pending_pkt);
            vr->pending_valid = 0;
            reads++;
            continue;
        }

        AVPacket pkt;
        if (av_read_frame(vr->fmt_ctx, &pkt) < 0) return;

        if (pkt.stream_index == vr->video_stream_index) {
            if (pkt_queue_is_full(&vr->video_pktq)) {
                av_packet_move_ref(&vr->pending_pkt, &pkt);
                vr->pending_valid = 1;
                return;
            }
            pkt_queue_push(&vr->video_pktq, &pkt);
        } else if (vr->audio_ctx && pkt.stream_index == vr->audio_stream_index) {
            if (pkt_queue_is_full(&vr->audio_pktq)) {
                av_packet_move_ref(&vr->pending_pkt, &pkt);
                vr->pending_valid = 1;
                return;
            }
            pkt_queue_push(&vr->audio_pktq, &pkt);
        } else if (vr->subtitle_ctx
                   && vr->subtitle_stream_index >= 0
                   && pkt.stream_index == vr->subtitle_stream_index) {
            vr_process_subtitle(vr, &pkt);
        }
        av_packet_unref(&pkt);
        reads++;
    }
}

static void vr_decode_audio(VideoRenderer* vr) {
    if (!vr || !vr->audio_ctx || !vr->audio_dev) return;
    double queued = vr_get_audio_queue_seconds(vr);
    if (queued >= AUDIO_QUEUE_TARGET_SEC) return;

    while (queued < AUDIO_QUEUE_TARGET_SEC) {
        int recv = avcodec_receive_frame(vr->audio_ctx, vr->audio_frame);
        if (recv == 0) {
            vr_queue_audio(vr, vr->audio_frame);
            queued = vr_get_audio_queue_seconds(vr);
            continue;
        }
        if (recv != AVERROR(EAGAIN)) break;
        if (pkt_queue_is_empty(&vr->audio_pktq)) break;
        AVPacket pkt;
        if (!pkt_queue_pop(&vr->audio_pktq, &pkt)) break;
        int send_ret = avcodec_send_packet(vr->audio_ctx, &pkt);
        av_packet_unref(&pkt);
        (void)send_ret;
    }
}

static int vr_init_deinterlace_filter(VideoRenderer* vr, AVFrame* frame) {
    if (!vr || !frame) return -1;

    char args[512];
    int ret = 0;
    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();

    vr->filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !vr->filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame->width, frame->height, frame->format,
             vr->video_time_base.num, vr->video_time_base.den,
             frame->sample_aspect_ratio.num, 
             frame->sample_aspect_ratio.den > 0 ? frame->sample_aspect_ratio.den : 1);

    ret = avfilter_graph_create_filter(&vr->buffersrc_ctx, buffersrc, "in",
                                        args, NULL, vr->filter_graph);
    if (ret < 0) {
        nob_log(NOB_ERROR, "Cannot create buffer source");
        goto end;
    }

    ret = avfilter_graph_create_filter(&vr->buffersink_ctx, buffersink, "out",
                                        NULL, NULL, vr->filter_graph);
    if (ret < 0) {
        nob_log(NOB_ERROR, "Cannot create buffer sink");
        goto end;
    }

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = vr->buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = vr->buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    const char* filter_descr = "bwdif=mode=send_frame:parity=auto:deint=all";
    
    ret = avfilter_graph_parse_ptr(vr->filter_graph, filter_descr,
                                     &inputs, &outputs, NULL);
    if (ret < 0) {
        nob_log(NOB_ERROR, "Failed to parse filter graph");
        goto end;
    }

    ret = avfilter_graph_config(vr->filter_graph, NULL);
    if (ret < 0) {
        nob_log(NOB_ERROR, "Failed to configure filter graph");
        goto end;
    }

    vr->filtered_frame = av_frame_alloc();
    if (!vr->filtered_frame) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    if (ret < 0 && vr->filter_graph) {
        avfilter_graph_free(&vr->filter_graph);
        vr->filter_graph = NULL;
        vr->buffersrc_ctx = NULL;
        vr->buffersink_ctx = NULL;
        if (vr->filtered_frame) {
            av_frame_free(&vr->filtered_frame);
            vr->filtered_frame = NULL;
        }
    }

    return ret;
}

int vr_render_frame(VideoRenderer* vr) {
    if (!vr || !vr->video_ctx) return 0;

    for (;;) {
        int recv = avcodec_receive_frame(vr->video_ctx, vr->frame);
        if (recv == 0) {
            AVFrame* use_frame = vr->frame;
            AVFrame* tmp_sw = NULL;

            if (vr->hw_device_ctx) {
                tmp_sw = av_frame_alloc();
                if (tmp_sw) {
                    int transfer_err = av_hwframe_transfer_data(tmp_sw, vr->frame, 0);
                    if (transfer_err == 0) {
                        int expected_ls0 = av_image_get_linesize(tmp_sw->format, tmp_sw->width, 0);
                        int expected_ls1 = av_image_get_linesize(tmp_sw->format, tmp_sw->width, 1);
                        int bad = 0;
                        if (expected_ls0 <= 0 || tmp_sw->linesize[0] <= 0 || tmp_sw->data[0] == NULL) bad = 1;
                        if (expected_ls1 > 0) {
                            if (tmp_sw->linesize[1] <= 0 || tmp_sw->data[1] == NULL) bad = 1;
                        }
                        if (bad) {
                            nob_log(NOB_WARNING, "HW->SW transfer produced invalid software frame (format=%d w=%d h=%d ls0=%d ls1=%d)",
                                tmp_sw->format, tmp_sw->width, tmp_sw->height, tmp_sw->linesize[0], tmp_sw->linesize[1]);
                            av_frame_free(&tmp_sw);
                            tmp_sw = NULL;
                        } else {
                            use_frame = tmp_sw;
                        }
                    } else {
                        nob_log(NOB_WARNING, "HW->SW transfer failed (err=%d)", transfer_err);
                        av_frame_free(&tmp_sw);
                        tmp_sw = NULL;
                    }
                } else {
                    nob_log(NOB_WARNING, "Failed to allocate tmp_sw for HW transfer");
                }
            }

            if (!use_frame->data[0] || use_frame->linesize[0] <= 0 || use_frame->width <= 0 || use_frame->height <= 0) {
                nob_log(NOB_WARNING, "Dropped frame: invalid source pointers/linesize (format=%d, w=%d, h=%d, ls0=%d)",
                    use_frame->format, use_frame->width, use_frame->height, use_frame->linesize[0]);
                if (tmp_sw) av_frame_free(&tmp_sw);
                av_frame_unref(vr->frame);
                if (vr->hw_device_ctx && !vr->hw_disabled) {
                    vr->hw_bad_frame_count++;
                    if (vr->hw_bad_frame_count >= 5) {
                        nob_log(NOB_ERROR, "Too many bad HW->SW frames, disabling HW and reloading as software decode");
                        char fname[512];
                        snprintf(fname, sizeof(fname), "%s", vr->current_filename[0] ? vr->current_filename : "");
                        if (vr->hw_device_ctx) {
                            av_buffer_unref(&vr->hw_device_ctx);
                            vr->hw_device_ctx = NULL;
                        }
                        vr->hw_pix_fmt = AV_PIX_FMT_NONE;
                        vr->hw_device_type = AV_HWDEVICE_TYPE_NONE;
                        vr->hw_device_name[0] = '\0';
                        vr_reset_stream(vr);
                        if (fname[0]) {
                            if (!vr_load(vr, fname, "none")) {
                                nob_log(NOB_ERROR, "Failed to reload file without HW: %s", fname);
                            }
                        }
                        vr->hw_disabled = 1;
                        return 0;
                    }
                }
                continue;
            }

            if (!vr->filter_graph) {
                if (vr_init_deinterlace_filter(vr, use_frame) < 0) {
                    nob_log(NOB_WARNING, "Failed to initialize deinterlace filter, continuing without it");
                }
            }

            AVFrame* final_frame = use_frame;

            if (vr->filter_graph) {
                if (av_buffersrc_add_frame_flags(vr->buffersrc_ctx, use_frame, AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                    if (av_buffersink_get_frame(vr->buffersink_ctx, vr->filtered_frame) >= 0) {
                        final_frame = vr->filtered_frame;
                        static int logged_filter_use = 0;
                        if (!logged_filter_use) {
                            logged_filter_use = 1;
                        }
                    }
                }
            }

            if (final_frame->format != vr->sws_src_pix_fmt) {
                if (vr->sws_ctx) sws_freeContext(vr->sws_ctx);
                vr->sws_ctx = sws_getContext(
                    final_frame->width, final_frame->height, final_frame->format,
                    final_frame->width, final_frame->height, AV_PIX_FMT_YUV420P,
                    SWS_LANCZOS | SWS_ACCURATE_RND, NULL, NULL, NULL);
                vr->sws_src_pix_fmt = final_frame->format;
                if (!vr->sws_ctx) {
                    nob_log(NOB_ERROR, "Failed to create sws_ctx for format %d", final_frame->format);
                    if (final_frame == vr->filtered_frame) av_frame_unref(vr->filtered_frame);
                    if (tmp_sw) av_frame_free(&tmp_sw);
                    av_frame_unref(vr->frame);
                    continue;
                }
            }

            sws_scale(vr->sws_ctx,
                (const uint8_t* const*)final_frame->data,
                final_frame->linesize, 0, final_frame->height,
                vr->yuv_frame->data, vr->yuv_frame->linesize);

            if (final_frame == vr->filtered_frame) {
                av_frame_unref(vr->filtered_frame);
            }
            if (tmp_sw) av_frame_free(&tmp_sw);

            SDL_UpdateYUVTexture(vr->texture, NULL,
                vr->yuv_frame->data[0], vr->yuv_frame->linesize[0],
                vr->yuv_frame->data[1], vr->yuv_frame->linesize[1],
                vr->yuv_frame->data[2], vr->yuv_frame->linesize[2]);

            vr->hw_bad_frame_count = 0;
            vr->video_ready = 1;

            int64_t vts = vr->frame->best_effort_timestamp;
            if (vts != AV_NOPTS_VALUE) {
                double vts_sec = vts * av_q2d(vr->video_time_base);
                if (!vr->start_time_set) {
                    vr->start_time = vts_sec;
                    vr->start_time_set = 1;
                }
                vr->current_time = vts_sec - vr->start_time;

                if (vr->frame_history_size < 32) {
                    vr->frame_history[vr->frame_history_size++] = vr->current_time;
                } else {
                    for (int i = 1; i < 32; i++) vr->frame_history[i-1] = vr->frame_history[i];
                    vr->frame_history[31] = vr->current_time;
                }
                vr->frame_history_pos = vr->frame_history_size - 1;
            }
            vr->playback_speed = vr->playback_speed > 0 ? vr->playback_speed : 1.0f;
            return 1;
        }

        if (recv != AVERROR(EAGAIN)) break;
        if (pkt_queue_is_empty(&vr->video_pktq)) break;
        AVPacket pkt;
        if (!pkt_queue_pop(&vr->video_pktq, &pkt)) break;
        int send_ret = avcodec_send_packet(vr->video_ctx, &pkt);
        av_packet_unref(&pkt);
        (void)send_ret;
    }
    return 0;
}

SDL_Texture* vr_get_texture(VideoRenderer* vr) {
    return (vr && vr->video_ready) ? vr->texture : NULL;
}

SDL_Texture* vr_get_subtitle_texture(VideoRenderer* vr) {
    return vr ? vr->subtitle_texture : NULL;
}

double vr_get_video_time(VideoRenderer* vr) {
    return vr ? vr->current_time : 0.0;
}

double vr_get_audio_time(VideoRenderer* vr) {
    if (!vr || !vr->audio_dev || !vr->audio_clock_valid) return vr ? vr->current_time : 0.0;
    return vr_get_audio_clock(vr);
}

void vr_resync_audio(VideoRenderer* vr, double target_time) {
    if (!vr || !vr->audio_dev) return;
    SDL_ClearQueuedAudio(vr->audio_dev);
    if (vr->audio_ctx) avcodec_flush_buffers(vr->audio_ctx);
    vr->audio_clock_base = target_time;
    vr->audio_base_samples = 0;
    vr->audio_samples_written = 0;
    vr->audio_clock_valid = 1;
}

int vr_render_subtitles(VideoRenderer* vr, double seconds) {
    if (!vr || !vr->ass_renderer) return 0;
    if (vr->current_subtitle < 0) return 0;
    if (!vr->ass_track) return 0;

    int changed = 0;
    ASS_Image* img = ass_render_frame(vr->ass_renderer, vr->ass_track,
                                      (long long)(seconds * 1000.0 + vr->subtitle_offset_ms), &changed);
    if (!img) return 0;

    if (!vr->subtitle_texture) {
        vr->subtitle_texture = SDL_CreateTexture(vr->renderer,
            SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
            vr->width, vr->height);
        SDL_SetTextureBlendMode(vr->subtitle_texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(vr->subtitle_texture, SDL_ScaleModeLinear);
    }

    void* pixels = NULL;
    int pitch = 0;
    if (SDL_LockTexture(vr->subtitle_texture, NULL, &pixels, &pitch) != 0) return 0;
    memset(pixels, 0, (size_t)pitch * vr->height);

    for (ASS_Image* p = img; p; p = p->next) {
        int x = p->dst_x;
        int y = p->dst_y;
        int w = p->w;
        int h = p->h;

        int bmp_ox = 0, bmp_oy = 0;
        if (x < 0) { bmp_ox = -x; w += x; x = 0; }
        if (y < 0) { bmp_oy = -y; h += y; y = 0; }
        if (x + w > vr->width)  w = vr->width  - x;
        if (y + h > vr->height) h = vr->height - y;
        if (w <= 0 || h <= 0) continue;

        uint32_t color = p->color;
        uint8_t cr    = (color >> 24) & 0xFF;
        uint8_t cg    = (color >> 16) & 0xFF;
        uint8_t cb    = (color >>  8) & 0xFF;
        uint8_t a_inv =  color        & 0xFF;

        for (int j = 0; j < h; j++) {
            const uint8_t* src = p->bitmap + (bmp_oy + j) * p->stride + bmp_ox;
            uint8_t* dst = (uint8_t*)pixels + (y + j) * pitch + x * 4;
            for (int i = 0; i < w; i++) {
                int src_a = (src[i] * (255 - (int)a_inv)) / 255;
                if (src_a == 0) continue;

                uint8_t* d = dst + i * 4;
                int dst_a = d[3];

                if (dst_a == 0) {
                    d[0] = cr;
                    d[1] = cg;
                    d[2] = cb;
                    d[3] = (uint8_t)src_a;
                } else {
                    int out_a = src_a + (dst_a * (255 - src_a)) / 255;
                    if (out_a == 0) continue;
                    d[0] = (uint8_t)((cr * src_a + d[0] * dst_a * (255 - src_a) / 255) / out_a);
                    d[1] = (uint8_t)((cg * src_a + d[1] * dst_a * (255 - src_a) / 255) / out_a);
                    d[2] = (uint8_t)((cb * src_a + d[2] * dst_a * (255 - src_a) / 255) / out_a);
                    d[3] = (uint8_t)out_a;
                }
            }
        }
    }

    SDL_UnlockTexture(vr->subtitle_texture);
    return 1;
}

void vr_seek(VideoRenderer* vr, double seconds) {
    if (!vr || !vr->fmt_ctx || vr->video_stream_index < 0) return;

    AVStream* video_st = vr->fmt_ctx->streams[vr->video_stream_index];
    int64_t ts = av_rescale_q((int64_t)(seconds * AV_TIME_BASE), AV_TIME_BASE_Q, video_st->time_base);
    av_seek_frame(vr->fmt_ctx, vr->video_stream_index, ts, AVSEEK_FLAG_BACKWARD);

    avcodec_flush_buffers(vr->video_ctx);
    if (vr->audio_ctx) avcodec_flush_buffers(vr->audio_ctx);
    if (vr->subtitle_ctx) avcodec_flush_buffers(vr->subtitle_ctx);
    if (vr->audio_dev) SDL_ClearQueuedAudio(vr->audio_dev);

    if (vr->filter_graph) {
        avfilter_graph_free(&vr->filter_graph);
        vr->filter_graph = NULL;
        vr->buffersrc_ctx = NULL;
        vr->buffersink_ctx = NULL;
    }

    if (vr->ass_track && vr->ass_lib) {
        ass_free_track(vr->ass_track);
        vr->ass_track = ass_new_track(vr->ass_lib);
        if (vr->ass_track) {
            vr->ass_track->PlayResX = vr->width;
            vr->ass_track->PlayResY = vr->height;
            if (vr->subtitle_stream_index >= 0) {
                AVStream* st = vr->fmt_ctx->streams[vr->subtitle_stream_index];
                if (st->codecpar->extradata_size > 0) {
                    ass_process_codec_private(vr->ass_track,
                        (char*)st->codecpar->extradata,
                        st->codecpar->extradata_size);
                }
            }
        }
    }

    if (vr->pending_valid) {
        av_packet_unref(&vr->pending_pkt);
        vr->pending_valid = 0;
    }
    pkt_queue_clear(&vr->video_pktq);
    pkt_queue_clear(&vr->audio_pktq);

    vr->current_time = seconds;
    vr->last_time = seconds;
    vr->audio_clock_valid = 0;
    vr->audio_clock_pts = 0.0;
    vr->audio_base_samples = 0;
    vr->audio_samples_written = 0;

    int was_paused = vr->clock_paused;
    vr->clock_start_time = seconds;
    vr->clock_start_ticks = SDL_GetTicks();
    vr->clock_pause_accum = 0;
    vr->clock_paused = was_paused;
    if (was_paused) vr->clock_pause_ticks = vr->clock_start_ticks;
}

double vr_get_time(VideoRenderer* vr) {
    if (!vr) return 0.0;

    double master = vr_get_master_time(vr);

    if (vr->audio_dev && vr->audio_clock_valid) {
        double audio_time = vr_get_audio_clock(vr);
        double t = audio_time > master ? audio_time : master;
        if (t < vr->last_time) return vr->last_time;
        vr->last_time = t;
        return t;
    }

    double t = master > vr->current_time ? master : vr->current_time;
    if (t < vr->last_time) return vr->last_time;
    vr->last_time = t;
    return t;
}

void vr_next_frame(VideoRenderer* vr, int count) {
    if (!vr || count == 0) return;
    int step = (count > 0) ? 1 : -1;
    int n = abs(count);
    double frame_duration = 0.04;

    if (vr && vr->video_ctx && vr->video_time_base.num > 0 && vr->video_time_base.den > 0) {
        AVRational tb = vr->video_time_base;
        double fps = 0.0;
        if (vr->fmt_ctx && vr->video_stream_index >= 0) {
            AVStream* stream = vr->fmt_ctx->streams[vr->video_stream_index];
            if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
                fps = av_q2d(stream->avg_frame_rate);
            }
        }
        if (fps > 0.0) {
            frame_duration = 1.0 / fps;
        } else {
            frame_duration = av_q2d(tb);
        }
    }

    for (int i = 0; i < n; i++) {
        if (step > 0) {
            int rendered = 0;
            int tries = 0;
            while (!rendered && tries < 32) {
                vr_demux_packets(vr);
                rendered = vr_render_frame(vr);
                tries++;
            }
        } else {
            double target_time = vr->current_time - frame_duration;
            if (target_time < 0.0) target_time = 0.0;
            vr_seek(vr, target_time);

            int tries = 0;
            while (tries < 128) {
                vr_demux_packets(vr);
                if (vr_render_frame(vr)) {
                    if (vr->current_time >= target_time - 0.001) break;
                }
                tries++;
            }
        }

        vr->last_time = vr->current_time;
        vr->clock_start_time = vr->current_time;
        vr->clock_start_ticks = SDL_GetTicks();
        vr->clock_pause_accum = 0;
        if (vr->clock_paused) {
            vr->clock_pause_ticks = vr->clock_start_ticks;
        }
        vr_resync_audio(vr, vr->current_time);
    }
}

void vr_set_speed(VideoRenderer* vr, double speed) {
    if (!vr) return;
    if (speed <= 0.0) speed = 1.0;
    double now_time = vr_get_master_time(vr);
    vr->playback_speed = speed;
    vr->clock_start_time = now_time;
    vr->clock_start_ticks = SDL_GetTicks();
    vr->clock_pause_accum = 0;
    if (vr->clock_paused) {
        vr->clock_pause_ticks = vr->clock_start_ticks;
    }
}

void vr_set_volume(VideoRenderer* vr, float volume) {
    if (!vr) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 2.0f) volume = 2.0f;
    vr->audio_volume = volume;
}

void vr_set_subtitle_style_override(VideoRenderer* vr, uint32_t color_rgb, int size, int margin_bottom) {
    if (!vr) return;
    vr->subtitle_override_color_rgb = color_rgb & 0xFFFFFF;
    if (size < 8) size = 8;
    if (size > 96) size = 96;
    if (margin_bottom < 0) margin_bottom = 0;
    if (margin_bottom > 1000) margin_bottom = 1000;
    vr->subtitle_override_size = size;
    vr->subtitle_override_margin_bottom = margin_bottom;
}

int vr_get_subtitle_style_mode(VideoRenderer* vr) {
    return vr_subtitle_style_mode(vr);
}

void vr_set_aspect_ratio_mode(VideoRenderer* vr, unsigned int x, unsigned int y) {
    if (!vr) return;
    vr->ar_x = x;
    vr->ar_y = y;
}

void vr_set_zoom(VideoRenderer* vr, int zoom) {
    if (!vr) return;
    if (zoom < 1) zoom = 1;
    vr->zoom_percent = zoom;
}

int vr_get_zoom(VideoRenderer* vr) {
    return vr ? vr->zoom_percent : 100;
}

float vr_get_volume(VideoRenderer* vr) {
    return vr ? vr->audio_volume : 1.0f;
}

double vr_get_duration(VideoRenderer* vr) {
    if (!vr || !vr->fmt_ctx || vr->fmt_ctx->duration == AV_NOPTS_VALUE) return 0.0;
    return (double)vr->fmt_ctx->duration / AV_TIME_BASE;
}

int vr_get_audio_track_count(VideoRenderer* vr) {
    return vr ? vr->audio_count : 0;
}

const char* vr_get_audio_track_name(VideoRenderer* vr, int idx) {
    if (!vr || idx < 0 || idx >= vr->audio_count) return NULL;
    return vr->audio_names[idx];
}

int vr_get_subtitle_track_count(VideoRenderer* vr) {
    return vr ? vr->subtitle_count : 0;
}

const char* vr_get_subtitle_track_name(VideoRenderer* vr, int idx) {
    if (!vr || idx < 0 || idx >= vr->subtitle_count) return NULL;
    return vr->subtitle_names[idx];
}

void vr_select_audio_track(VideoRenderer* vr, int idx) {
    if (!vr || idx < 0 || idx >= vr->audio_count) return;

    double t = vr->current_time;

    vr->audio_stream_index = vr->audio_streams[idx];
    vr->current_audio = idx;

    vr->audio_clock_base = 0.0;
    vr->audio_base_samples = 0;
    vr->audio_samples_written = 0;
    vr->audio_clock_valid = 0;

    if (vr->pending_valid) {
        av_packet_unref(&vr->pending_pkt);
        vr->pending_valid = 0;
    }
    pkt_queue_clear(&vr->audio_pktq);

    if (vr->audio_dev) { SDL_CloseAudioDevice(vr->audio_dev); vr->audio_dev = 0; }
    if (vr->audio_ctx) { avcodec_free_context(&vr->audio_ctx); vr->audio_ctx = NULL; }
    if (vr->swr_ctx)   { swr_free(&vr->swr_ctx); vr->swr_ctx = NULL; }
    if (vr->audio_frame) { av_frame_free(&vr->audio_frame); vr->audio_frame = NULL; }

    AVStream* stream = vr->fmt_ctx->streams[vr->audio_stream_index];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return;
    vr->audio_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(vr->audio_ctx, stream->codecpar);
    if (avcodec_open2(vr->audio_ctx, (AVCodec*)codec, NULL) < 0) {
        avcodec_free_context(&vr->audio_ctx);
        vr->audio_ctx = NULL;
        return;
    }
    vr->audio_time_base = stream->time_base;

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = vr->audio_ctx->sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL;
    vr->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &vr->audio_spec,
                                         SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (!vr->audio_dev) {
        nob_log(NOB_ERROR, "SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(vr->audio_dev, 0);

    AVChannelLayout in_layout = vr->audio_ctx->ch_layout;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, 2);
    swr_alloc_set_opts2(&vr->swr_ctx,
        &out_layout, AV_SAMPLE_FMT_S16, vr->audio_spec.freq,
        &in_layout, vr->audio_ctx->sample_fmt, vr->audio_ctx->sample_rate,
        0, NULL);
    swr_init(vr->swr_ctx);
    vr->audio_frame = av_frame_alloc();

    if (vr->fmt_ctx && vr->video_stream_index >= 0) {
        AVStream* video_st = vr->fmt_ctx->streams[vr->video_stream_index];
        int64_t ts = av_rescale_q((int64_t)(t * AV_TIME_BASE), AV_TIME_BASE_Q, video_st->time_base);
        av_seek_frame(vr->fmt_ctx, vr->video_stream_index, ts, AVSEEK_FLAG_BACKWARD);

        if (vr->video_ctx)    avcodec_flush_buffers(vr->video_ctx);
        if (vr->audio_ctx)    avcodec_flush_buffers(vr->audio_ctx);
        if (vr->subtitle_ctx) avcodec_flush_buffers(vr->subtitle_ctx);
        if (vr->audio_dev)    SDL_ClearQueuedAudio(vr->audio_dev);

        if (vr->filter_graph) {
            avfilter_graph_free(&vr->filter_graph);
            vr->filter_graph = NULL;
            vr->buffersrc_ctx = NULL;
            vr->buffersink_ctx = NULL;
        }

        if (vr->ass_track && vr->ass_lib) {
            ass_free_track(vr->ass_track);
            vr->ass_track = ass_new_track(vr->ass_lib);
            if (vr->ass_track) {
                vr->ass_track->PlayResX = vr->width;
                vr->ass_track->PlayResY = vr->height;
                if (vr->subtitle_stream_index >= 0) {
                    AVStream* st = vr->fmt_ctx->streams[vr->subtitle_stream_index];
                    if (st->codecpar->extradata_size > 0) {
                        ass_process_codec_private(vr->ass_track,
                            (char*)st->codecpar->extradata,
                            st->codecpar->extradata_size);
                    }
                }
            }
        }

        pkt_queue_clear(&vr->video_pktq);
        pkt_queue_clear(&vr->audio_pktq);

        vr->current_time  = t;
        vr->last_time     = t;
        vr->audio_clock_pts = 0.0;

        int was_paused = vr->clock_paused;
        vr->clock_start_time  = t;
        vr->clock_start_ticks = SDL_GetTicks();
        vr->clock_pause_accum = 0;
        vr->clock_paused      = was_paused;
        if (was_paused) vr->clock_pause_ticks = vr->clock_start_ticks;
    }
}

void vr_select_subtitle_track(VideoRenderer* vr, int idx) {
    if (!vr) return;

    if (vr->subtitle_ctx) {
        avcodec_free_context(&vr->subtitle_ctx);
        vr->subtitle_ctx = NULL;
    }
    if (vr->ass_track) {
        ass_free_track(vr->ass_track);
        vr->ass_track = NULL;
    }
    if (vr->subtitle_texture) {
        SDL_DestroyTexture(vr->subtitle_texture);
        vr->subtitle_texture = NULL;
    }

    if (idx < 0 || idx >= vr->subtitle_count) {
        vr->current_subtitle = -1;
        vr->subtitle_stream_index = -1;
        return;
    }

    vr->subtitle_stream_index = vr->subtitle_streams[idx];
    vr->current_subtitle = idx;

    const AVStream* stream = vr->fmt_ctx->streams[vr->subtitle_stream_index];

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec) {
        vr->subtitle_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(vr->subtitle_ctx, stream->codecpar);
        if (avcodec_open2(vr->subtitle_ctx, (AVCodec*)codec, NULL) < 0) {
            avcodec_free_context(&vr->subtitle_ctx);
            vr->subtitle_ctx = NULL;
        }
    }

    if (vr->ass_lib && vr->ass_renderer) {
        vr->ass_track = ass_new_track(vr->ass_lib);
        if (vr->ass_track) {
            vr->ass_track->PlayResX = vr->width;
            vr->ass_track->PlayResY = vr->height;
            if (stream->codecpar->extradata_size > 0) {
                ass_process_codec_private(vr->ass_track,
                    (char*)stream->codecpar->extradata,
                    stream->codecpar->extradata_size);
            }
        }
    }

    double current_pos = vr_get_time(vr);

    if (vr->fmt_ctx && vr->video_stream_index >= 0) {
        AVStream* video_st = vr->fmt_ctx->streams[vr->video_stream_index];
        int64_t ts = av_rescale_q((int64_t)(current_pos * AV_TIME_BASE), AV_TIME_BASE_Q, video_st->time_base);
        av_seek_frame(vr->fmt_ctx, vr->video_stream_index, ts, AVSEEK_FLAG_BACKWARD);

        if (vr->video_ctx)    avcodec_flush_buffers(vr->video_ctx);
        if (vr->audio_ctx)    avcodec_flush_buffers(vr->audio_ctx);
        if (vr->subtitle_ctx) avcodec_flush_buffers(vr->subtitle_ctx);
        if (vr->audio_dev)    SDL_ClearQueuedAudio(vr->audio_dev);

        if (vr->filter_graph) {
            avfilter_graph_free(&vr->filter_graph);
            vr->filter_graph = NULL;
            vr->buffersrc_ctx = NULL;
            vr->buffersink_ctx = NULL;
        }

        if (vr->pending_valid) {
            av_packet_unref(&vr->pending_pkt);
            vr->pending_valid = 0;
        }
        pkt_queue_clear(&vr->video_pktq);
        pkt_queue_clear(&vr->audio_pktq);
    }

    AVPacket pkt;
    int reads = 0;
    while (reads < 200) {
        if (av_read_frame(vr->fmt_ctx, &pkt) < 0) break;
        if (pkt.stream_index == vr->subtitle_stream_index) {
            double pkt_time = 0.0;
            if (pkt.pts != AV_NOPTS_VALUE) {
                AVRational tb = vr->fmt_ctx->streams[pkt.stream_index]->time_base;
                pkt_time = av_rescale_q(pkt.pts, tb, AV_TIME_BASE_Q) * av_q2d(AV_TIME_BASE_Q);
                pkt_time -= vr->start_time;
            }
            vr_process_subtitle(vr, &pkt);
            av_packet_unref(&pkt);
            if (pkt_time > current_pos + 0.1) break;
        } else if (pkt.stream_index == vr->video_stream_index) {
            if (!pkt_queue_is_full(&vr->video_pktq)) {
                pkt_queue_push(&vr->video_pktq, &pkt);
            }
            av_packet_unref(&pkt);
        } else if (vr->audio_ctx && pkt.stream_index == vr->audio_stream_index) {
            if (!pkt_queue_is_full(&vr->audio_pktq)) {
                pkt_queue_push(&vr->audio_pktq, &pkt);
            }
            av_packet_unref(&pkt);
        } else {
            av_packet_unref(&pkt);
        }
        reads++;
    }
}

void vr_set_paused(VideoRenderer* vr, int paused) {
    if (!vr) return;
    if (vr->audio_dev) {
        SDL_PauseAudioDevice(vr->audio_dev, paused ? 1 : 0);
    }
    if (paused && !vr->clock_paused) {
        vr->clock_pause_ticks = SDL_GetTicks();
        vr->clock_paused = 1;
    } else if (!paused && vr->clock_paused) {
        Uint32 now = SDL_GetTicks();
        vr->clock_pause_accum += (now - vr->clock_pause_ticks);
        vr->clock_paused = 0;
    }
}

void vr_free(VideoRenderer* vr) {
    if (!vr) return;
    vr_reset_stream(vr);
    if (vr->audio_dev) {
        SDL_PauseAudioDevice(vr->audio_dev, 1);
        SDL_CloseAudioDevice(vr->audio_dev);
        vr->audio_dev = 0;
    }
    pkt_queue_free(&vr->video_pktq);
    pkt_queue_free(&vr->audio_pktq);
    free(vr);
}
