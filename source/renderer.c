#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../thirdparty/SDL2/SDL.h"
#include "../thirdparty/libavformat/avformat.h"
#include "../thirdparty/libavcodec/avcodec.h"
#include "../thirdparty/libswscale/swscale.h"
#include "../thirdparty/libavutil/imgutils.h"
#include "../thirdparty/libswresample/swresample.h"
#include "../thirdparty/libavutil/opt.h"
#include "../thirdparty/libavutil/channel_layout.h"
#include "../thirdparty/libavutil/time.h"
#include "../thirdparty/ass/ass.h"

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_Texture* subtitle_texture;
    int width;
    int height;

    AVFormatContext* fmt_ctx;
    AVCodecContext* video_ctx;
    int video_stream_index;
    struct SwsContext* sws_ctx;

    AVCodecContext* audio_ctx;
    int audio_stream_index;
    SwrContext* swr_ctx;
    SDL_AudioDeviceID audio_dev;
    SDL_AudioSpec audio_spec;
    AVFrame* audio_frame;
    uint8_t* audio_buf;
    int audio_buf_size;
    float audio_volume;

    AVCodecContext* subtitle_ctx;
    int subtitle_stream_index;
    ASS_Library* ass_lib;
    ASS_Renderer* ass_renderer;
    ASS_Track* ass_track;

    double playback_speed;
    double current_time;

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
} VideoRenderer;

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

    if (vr->fmt_ctx) {
        avformat_close_input(&vr->fmt_ctx);
        vr->fmt_ctx = NULL;
    }

    vr_free_track_lists(vr);
    vr->video_stream_index = -1;
    vr->audio_stream_index = -1;
    vr->subtitle_stream_index = -1;
    vr->width = 0;
    vr->height = 0;
    vr->current_time = 0.0;
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
}

static void vr_process_subtitle(VideoRenderer* vr, const AVPacket* pkt) {
    if (!vr || !vr->subtitle_ctx || !vr->ass_track) return;
    AVSubtitle sub;
    int got = 0;
    int ret = avcodec_decode_subtitle2(vr->subtitle_ctx, &sub, &got, pkt);
    if (ret < 0 || !got) return;

    for (unsigned i = 0; i < sub.num_rects; i++) {
        AVSubtitleRect* r = sub.rects[i];
        if (r->ass && r->ass[0]) {
            ass_process_chunk(vr->ass_track, r->ass, strlen(r->ass), sub.start_display_time, sub.end_display_time - sub.start_display_time);
        } else if (r->text && r->text[0]) {
            char buf[1024];
            snprintf(buf, sizeof(buf), "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,%s", r->text);
            ass_process_chunk(vr->ass_track, buf, strlen(buf), sub.start_display_time, sub.end_display_time - sub.start_display_time);
        }
    }
    avsubtitle_free(&sub);
}

// --- create renderer ---
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
    return vr;
}

// --- load video ---
int vr_load(VideoRenderer* vr, const char* filename) {
    if (!vr || !filename) return 0;
    vr_reset_stream(vr);
    
    // Suppress FFmpeg warnings about attachment streams
    av_log_set_level(AV_LOG_ERROR);
    
    if (avformat_open_input(&vr->fmt_ctx, filename, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to open video: %s\n", filename);
        return 0;
    }

    if (avformat_find_stream_info(vr->fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Failed to find stream info\n");
        vr_reset_stream(vr);
        return 0;
    }

    for (unsigned i = 0; i < vr->fmt_ctx->nb_streams; i++) {
        AVStream* stream = vr->fmt_ctx->streams[i];

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && vr->video_stream_index < 0) {
            vr->video_stream_index = (int)i;
            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec) {
                fprintf(stderr, "Failed to find video decoder\n");
                continue;
            }
            vr->video_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(vr->video_ctx, stream->codecpar);
            if (avcodec_open2(vr->video_ctx, (AVCodec*)codec, NULL) < 0) {
                fprintf(stderr, "Failed to open video decoder\n");
                avcodec_free_context(&vr->video_ctx);
                vr->video_ctx = NULL;
                continue;
            }

            vr->width = vr->video_ctx->width;
            vr->height = vr->video_ctx->height;

            vr->texture = SDL_CreateTexture(vr->renderer,
                                            SDL_PIXELFORMAT_YV12,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            vr->width, vr->height);

            vr->sws_ctx = sws_getContext(
                vr->width, vr->height, vr->video_ctx->pix_fmt,
                vr->width, vr->height, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, NULL, NULL, NULL
            );

            vr->frame = av_frame_alloc();
            vr->yuv_frame = av_frame_alloc();
            vr->yuv_buffer = (uint8_t*)malloc(
                av_image_get_buffer_size(AV_PIX_FMT_YUV420P, vr->width, vr->height, 1)
            );
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

    if (vr->subtitle_count > 0) {
        vr->current_subtitle = 0;
        vr->subtitle_stream_index = vr->subtitle_streams[0];
    }

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
            SDL_AudioSpec want;
            SDL_zero(want);
            want.freq = vr->audio_ctx->sample_rate;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = 4096;
            want.callback = NULL;
            vr->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &vr->audio_spec, 0);
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

    if (vr->subtitle_stream_index >= 0) {
        AVStream* stream = vr->fmt_ctx->streams[vr->subtitle_stream_index];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            vr->subtitle_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(vr->subtitle_ctx, stream->codecpar);
            if (avcodec_open2(vr->subtitle_ctx, (AVCodec*)codec, NULL) < 0) {
                avcodec_free_context(&vr->subtitle_ctx);
                vr->subtitle_ctx = NULL;
            }
        }

        if (!vr->ass_lib) {
            vr->ass_lib = ass_library_init();
            vr->ass_renderer = ass_renderer_init(vr->ass_lib);
            ass_set_frame_size(vr->ass_renderer, vr->width, vr->height);
            // Use default fonts without scanning directories
            ass_set_fonts(vr->ass_renderer, NULL, "Arial", 1, NULL, 1);
        }
        
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

    return 1;
}

// --- decode one video frame, process audio/subtitles ---
int vr_render_frame(VideoRenderer* vr) {
    if (!vr || !vr->video_ctx) return 0;

    AVPacket pkt;
    while (av_read_frame(vr->fmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == vr->video_stream_index) {
            if (avcodec_send_packet(vr->video_ctx, &pkt) < 0) { av_packet_unref(&pkt); continue; }
            if (avcodec_receive_frame(vr->video_ctx, vr->frame) == 0) {
                sws_scale(vr->sws_ctx,
                          (const uint8_t* const*)vr->frame->data,
                          vr->frame->linesize, 0, vr->height,
                          vr->yuv_frame->data, vr->yuv_frame->linesize);

                SDL_UpdateYUVTexture(vr->texture, NULL,
                                     vr->yuv_frame->data[0], vr->yuv_frame->linesize[0],
                                     vr->yuv_frame->data[1], vr->yuv_frame->linesize[1],
                                     vr->yuv_frame->data[2], vr->yuv_frame->linesize[2]);

                if (vr->frame->pts != AV_NOPTS_VALUE) {
                    vr->current_time = vr->frame->pts * av_q2d(vr->fmt_ctx->streams[vr->video_stream_index]->time_base);
                }
                av_packet_unref(&pkt);
                vr->playback_speed = vr->playback_speed > 0 ? vr->playback_speed : 1.0f;
                return 1;
            }
        } else if (vr->audio_ctx && pkt.stream_index == vr->audio_stream_index) {
            if (avcodec_send_packet(vr->audio_ctx, &pkt) == 0) {
                while (avcodec_receive_frame(vr->audio_ctx, vr->audio_frame) == 0) {
                    vr_queue_audio(vr, vr->audio_frame);
                }
            }
        } else if (vr->subtitle_ctx && pkt.stream_index == vr->subtitle_stream_index) {
            vr_process_subtitle(vr, &pkt);
        }
        av_packet_unref(&pkt);
    }
    return 0;
}

SDL_Texture* vr_get_texture(VideoRenderer* vr) {
    return vr ? vr->texture : NULL;
}

SDL_Texture* vr_get_subtitle_texture(VideoRenderer* vr) {
    return vr ? vr->subtitle_texture : NULL;
}

int vr_render_subtitles(VideoRenderer* vr, double seconds) {
    if (!vr || !vr->ass_renderer || !vr->ass_track) return 0;
    int changed = 0;
    ASS_Image* img = ass_render_frame(vr->ass_renderer, vr->ass_track, (int)(seconds * 1000.0), &changed);
    if (!img) return 0;

    if (!vr->subtitle_texture) {
        vr->subtitle_texture = SDL_CreateTexture(vr->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, vr->width, vr->height);
        SDL_SetTextureBlendMode(vr->subtitle_texture, SDL_BLENDMODE_BLEND);
    }

    void* pixels = NULL;
    int pitch = 0;
    if (SDL_LockTexture(vr->subtitle_texture, NULL, &pixels, &pitch) != 0) return 0;
    memset(pixels, 0, pitch * vr->height);

    for (ASS_Image* p = img; p; p = p->next) {
        int x = p->dst_x;
        int y = p->dst_y;
        int w = p->w;
        int h = p->h;
        
        // Bounds check to prevent crashes
        if (x < 0 || y < 0 || x + w > vr->width || y + h > vr->height) {
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x + w > vr->width) w = vr->width - x;
            if (y + h > vr->height) h = vr->height - y;
            if (w <= 0 || h <= 0) continue;
        }
        
        uint32_t color = p->color;
        uint8_t a = 255 - ((color >> 24) & 0xFF);
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;

        for (int j = 0; j < h; j++) {
            uint8_t* src = p->bitmap + j * p->stride;
            uint8_t* dst = (uint8_t*)pixels + (y + j) * pitch + x * 4;
            for (int i = 0; i < w; i++) {
                uint8_t alpha = (uint8_t)((src[i] * a) / 255);
                dst[i * 4 + 0] = b;  // RGBA order
                dst[i * 4 + 1] = g;
                dst[i * 4 + 2] = r;
                dst[i * 4 + 3] = alpha;
            }
        }
    }

    SDL_UnlockTexture(vr->subtitle_texture);
    return 1;
}

// --- seek ---
void vr_seek(VideoRenderer* vr, double seconds) {
    if (!vr) return;
    int64_t ts = seconds / av_q2d(vr->fmt_ctx->streams[vr->video_stream_index]->time_base);
    av_seek_frame(vr->fmt_ctx, vr->video_stream_index, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(vr->video_ctx);
    if (vr->audio_ctx) avcodec_flush_buffers(vr->audio_ctx);
    // Clear audio queue to sync immediately
    if (vr->audio_dev) {
        SDL_ClearQueuedAudio(vr->audio_dev);
    }
    vr->current_time = seconds;
}

// --- playback speed ---
void vr_set_speed(VideoRenderer* vr, double speed) {
    if (vr) vr->playback_speed = speed;
}

void vr_set_volume(VideoRenderer* vr, float volume) {
    if (!vr) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 2.0f) volume = 2.0f;
    vr->audio_volume = volume;
    // Clear queued audio to apply volume change immediately
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
    
    // If audio is playing, use audio time for sync
    if (vr->audio_dev) {
        uint32_t queued = SDL_GetQueuedAudioSize(vr->audio_dev);
        // Each sample is 4 bytes (2 channels, 2 bytes each)
        int queued_samples = queued / 4;
        double audio_buffer_time = (double)queued_samples / (double)vr->audio_spec.freq;
        // Audio time is current_time minus the buffered audio ahead
        double audio_time = vr->current_time - audio_buffer_time;
        return audio_time > 0 ? audio_time : 0.0;
    }
    
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

    if (vr->audio_dev) {
        SDL_CloseAudioDevice(vr->audio_dev);
        vr->audio_dev = 0;
    }
    if (vr->audio_ctx) {
        avcodec_free_context(&vr->audio_ctx);
        vr->audio_ctx = NULL;
    }
    if (vr->swr_ctx) {
        swr_free(&vr->swr_ctx);
        vr->swr_ctx = NULL;
    }
    if (vr->audio_frame) {
        av_frame_free(&vr->audio_frame);
        vr->audio_frame = NULL;
    }

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

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = vr->audio_ctx->sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 4096;
    want.callback = NULL;
    vr->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &vr->audio_spec, 0);
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
    if (idx < 0 || idx >= vr->subtitle_count) {
        vr->current_subtitle = -1;
        vr->subtitle_stream_index = -1;
        return;
    }
    vr->subtitle_stream_index = vr->subtitle_streams[idx];
    vr->current_subtitle = idx;

    if (vr->subtitle_ctx) {
        avcodec_free_context(&vr->subtitle_ctx);
        vr->subtitle_ctx = NULL;
    }
    if (vr->ass_track) {
        ass_free_track(vr->ass_track);
        vr->ass_track = NULL;
    }

    AVStream* stream = vr->fmt_ctx->streams[vr->subtitle_stream_index];
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
}

void vr_set_paused(VideoRenderer* vr, int paused) {
    if (!vr || !vr->audio_dev) return;
    SDL_PauseAudioDevice(vr->audio_dev, paused ? 1 : 0);
}

// --- cleanup ---
void vr_free(VideoRenderer* vr) {
    if (!vr) return;
    vr_reset_stream(vr);
    free(vr);
}