#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../thirdparty/nob.h"
#include "../thirdparty/SDL2/SDL.h"
#include "../thirdparty/libavformat/avformat.h"
#include "../thirdparty/libavcodec/avcodec.h"
#include "../thirdparty/libswscale/swscale.h"
#include "../thirdparty/libavutil/imgutils.h"
#include "../thirdparty/libavutil/frame.h"
#include "../thirdparty/libswresample/swresample.h"
#include "../thirdparty/libavutil/opt.h"
#include "../thirdparty/libavutil/channel_layout.h"
#include "../thirdparty/libavutil/time.h"
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
    }
    if (vr->video_ctx) {
        avcodec_free_context(&vr->video_ctx);
        vr->video_ctx = NULL;
    }

    if (vr->audio_dev) {
        SDL_CloseAudioDevice(vr->audio_dev);
        vr->audio_dev = 0;
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
        vr->audio_streams = (int*)realloc(vr->audio_streams, sizeof(int) * (vr->audio_count + 1));
        vr->audio_names = (char**)realloc(vr->audio_names, sizeof(char*) * (vr->audio_count + 1));
        vr->audio_streams[vr->audio_count] = stream_index;
        vr->audio_names[vr->audio_count] = strdup(name);
        vr->audio_count++;
    } else {
        vr->subtitle_streams = (int*)realloc(vr->subtitle_streams, sizeof(int) * (vr->subtitle_count + 1));
        vr->subtitle_names = (char**)realloc(vr->subtitle_names, sizeof(char*) * (vr->subtitle_count + 1));
        vr->subtitle_streams[vr->subtitle_count] = stream_index;
        vr->subtitle_names[vr->subtitle_count] = strdup(name);
        vr->subtitle_count++;
    }
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
        vr->audio_buf = (uint8_t*)realloc(vr->audio_buf, out_buf_size);
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

    int64_t start_ms;
    if (sub.pts != AV_NOPTS_VALUE) {
        start_ms = av_rescale_q(sub.pts, AV_TIME_BASE_Q, (AVRational){1, 1000});
        start_ms += (int64_t)sub.start_display_time;
    } else if (pkt->pts != AV_NOPTS_VALUE) {
        AVRational tb = vr->fmt_ctx->streams[pkt->stream_index]->time_base;
        start_ms = av_rescale_q(pkt->pts, tb, (AVRational){1, 1000});
        start_ms += (int64_t)sub.start_display_time;
    } else {
        avsubtitle_free(&sub);
        return;
    }

    int64_t duration_ms = (int64_t)sub.end_display_time - (int64_t)sub.start_display_time;
    if (duration_ms <= 0) duration_ms = 5000;

    for (unsigned i = 0; i < sub.num_rects; i++) {
        AVSubtitleRect* r = sub.rects[i];
        if (r->ass && r->ass[0]) {
            ass_process_chunk(vr->ass_track, r->ass, (int)strlen(r->ass),
                              start_ms, duration_ms);
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
            char buf[4500]; /* warning: '%s' directive output may be truncated writing up to 4095 bytes into a region of size 4046 */
            snprintf(buf, sizeof(buf),
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,%s", escaped);
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
    return vr;
}

int vr_load(VideoRenderer* vr, const char* filename) {
    if (!vr || !filename) return 0;
    vr_reset_stream(vr);

    av_log_set_level(AV_LOG_ERROR);

    if (avformat_open_input(&vr->fmt_ctx, filename, NULL, NULL) != 0) {
        nob_log(NOB_ERROR, "Failed to open video: %s", filename);
        return 0;
    }

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
            vr->video_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(vr->video_ctx, stream->codecpar);
            if (avcodec_open2(vr->video_ctx, (AVCodec*)codec, NULL) < 0) {
                nob_log(NOB_ERROR, "Failed to open video decoder");
                avcodec_free_context(&vr->video_ctx);
                vr->video_ctx = NULL;
                continue;
            }
            vr->video_time_base = stream->time_base;
            vr->width = vr->video_ctx->width;
            vr->height = vr->video_ctx->height;

            vr->texture = SDL_CreateTexture(vr->renderer,
                SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
                vr->width, vr->height);

            vr->sws_ctx = sws_getContext(
                vr->width, vr->height, vr->video_ctx->pix_fmt,
                vr->width, vr->height, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, NULL, NULL, NULL);

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
            vr->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &vr->audio_spec,
                                                 SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
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

    return 1;
}

static void vr_demux_packets(VideoRenderer* vr) {
    if (!vr || !vr->fmt_ctx) return;
    int reads = 0;
    const int max_reads = 32;

    while (reads < max_reads) {
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

    while (queued < AUDIO_QUEUE_TARGET_SEC && !pkt_queue_is_empty(&vr->audio_pktq)) {
        AVPacket pkt;
        if (!pkt_queue_pop(&vr->audio_pktq, &pkt)) break;
        if (avcodec_send_packet(vr->audio_ctx, &pkt) == 0) {
            while (avcodec_receive_frame(vr->audio_ctx, vr->audio_frame) == 0) {
                vr_queue_audio(vr, vr->audio_frame);
            }
        }
        av_packet_unref(&pkt);
        queued = vr_get_audio_queue_seconds(vr);
    }
}

int vr_render_frame(VideoRenderer* vr) {
    if (!vr || !vr->video_ctx) return 0;

    while (!pkt_queue_is_empty(&vr->video_pktq)) {
        AVPacket pkt;
        if (!pkt_queue_pop(&vr->video_pktq, &pkt)) break;
        if (avcodec_send_packet(vr->video_ctx, &pkt) < 0) {
            av_packet_unref(&pkt);
            continue;
        }
        av_packet_unref(&pkt);

        if (avcodec_receive_frame(vr->video_ctx, vr->frame) == 0) {
            sws_scale(vr->sws_ctx,
                (const uint8_t* const*)vr->frame->data,
                vr->frame->linesize, 0, vr->height,
                vr->yuv_frame->data, vr->yuv_frame->linesize);

            SDL_UpdateYUVTexture(vr->texture, NULL,
                vr->yuv_frame->data[0], vr->yuv_frame->linesize[0],
                vr->yuv_frame->data[1], vr->yuv_frame->linesize[1],
                vr->yuv_frame->data[2], vr->yuv_frame->linesize[2]);

            vr->video_ready = 1;

            int64_t vts = vr->frame->best_effort_timestamp;
            if (vts != AV_NOPTS_VALUE) {
                double vts_sec = vts * av_q2d(vr->video_time_base);
                if (!vr->start_time_set) {
                    vr->start_time = vts_sec;
                    vr->start_time_set = 1;
                }
                vr->current_time = vts_sec - vr->start_time;
            }
            vr->playback_speed = vr->playback_speed > 0 ? vr->playback_speed : 1.0f;
            return 1;
        }
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
                                      (long long)(seconds * 1000.0), &changed);
    if (!img) return 0;

    if (!vr->subtitle_texture) {
        vr->subtitle_texture = SDL_CreateTexture(vr->renderer,
            SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
            vr->width, vr->height);
        SDL_SetTextureBlendMode(vr->subtitle_texture, SDL_BLENDMODE_BLEND);
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
    if (!vr) return;
    int64_t ts = (int64_t)(seconds / av_q2d(vr->fmt_ctx->streams[vr->video_stream_index]->time_base));
    av_seek_frame(vr->fmt_ctx, vr->video_stream_index, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(vr->video_ctx);
    if (vr->audio_ctx) avcodec_flush_buffers(vr->audio_ctx);
    if (vr->subtitle_ctx) avcodec_flush_buffers(vr->subtitle_ctx);
    if (vr->audio_dev) SDL_ClearQueuedAudio(vr->audio_dev);

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

    vr->audio_clock_base = seconds;
    vr->audio_clock_pts = seconds;
    vr->audio_base_samples = 0;
    vr->audio_samples_written = 0;
    vr->audio_clock_valid = 1;
    vr->current_time = seconds;
    vr->last_time = seconds;
    vr->clock_start_ticks = SDL_GetTicks();
    vr->clock_pause_ticks = 0;
    vr->clock_pause_accum = 0;
    vr->clock_paused = 0;
    vr->clock_start_time = seconds;

    if (vr->pending_valid) {
        av_packet_unref(&vr->pending_pkt);
        vr->pending_valid = 0;
    }
    pkt_queue_clear(&vr->video_pktq);
    pkt_queue_clear(&vr->audio_pktq);
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
    if (vr->audio_dev) SDL_ClearQueuedAudio(vr->audio_dev);
}

float vr_get_volume(VideoRenderer* vr) {
    return vr ? vr->audio_volume : 1.0f;
}

double vr_get_duration(VideoRenderer* vr) {
    if (!vr || !vr->fmt_ctx || vr->fmt_ctx->duration == AV_NOPTS_VALUE) return 0.0;
    return (double)vr->fmt_ctx->duration / AV_TIME_BASE;
}

double vr_get_time(VideoRenderer* vr) {
    if (!vr) return 0.0;

    if (vr->audio_dev && vr->audio_clock_valid) {
        double audio_time = vr_get_audio_clock(vr);
        if (audio_time < vr->last_time) return vr->last_time;
        vr->last_time = audio_time;
        return audio_time;
    }

    if (vr->current_time < vr->last_time) return vr->last_time;
    vr->last_time = vr->current_time;
    return vr->current_time;
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
        nob_log(NOB_INFO, "[SUBTITLE] Disabled subtitles");
        return;
    }

    vr->subtitle_stream_index = vr->subtitle_streams[idx];
    vr->current_subtitle = idx;

    const AVStream* stream = vr->fmt_ctx->streams[vr->subtitle_stream_index];
    const char* codec_name = avcodec_get_name(stream->codecpar->codec_id);
    nob_log(NOB_INFO, "[SUBTITLE] Selected track %d: %s (stream %d, codec: %s)",
            idx, vr->subtitle_names[idx], vr->subtitle_stream_index, codec_name);

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
    if (current_pos > 0.0) {
        int64_t ts_us = (int64_t)(current_pos * AV_TIME_BASE);
        av_seek_frame(vr->fmt_ctx, -1, ts_us, AVSEEK_FLAG_BACKWARD);
        if (vr->subtitle_ctx) avcodec_flush_buffers(vr->subtitle_ctx);
        if (vr->pending_valid) {
            av_packet_unref(&vr->pending_pkt);
            vr->pending_valid = 0;
        }
    }
}

void vr_set_paused(VideoRenderer* vr, int paused) {
    if (!vr || !vr->audio_dev) return;
    SDL_PauseAudioDevice(vr->audio_dev, paused ? 1 : 0);
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
    pkt_queue_free(&vr->video_pktq);
    pkt_queue_free(&vr->audio_pktq);
    free(vr);
}