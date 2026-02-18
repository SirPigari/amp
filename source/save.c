#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

typedef struct {
    int32_t size;
    int32_t outline_size;
    uint32_t color;
    uint32_t outline_color;
    char* font_path;
} FontSettings;

typedef struct {
    char* video_path;
    uint8_t file_hash[HASH_SIZE];
    double last_position;
    uint32_t volume_percent;
    float playback_speed;
    int32_t audio_track;
    int32_t subtitle_track;
    int audio_track_index;
    int subtitle_track_index;
} FileConfig;

typedef struct {
    char* recent_files[MAX_RECENT];
    uint64_t recent_files_count;

    FileConfig* remembered_files;
    uint64_t remembered_count;

    FontSettings font_settings;
} SaveState;

typedef struct {
    uint64_t magic;
    uint64_t version;
    SaveState state;
} __SaveFile;

void hash_file(const char* path, uint8_t out[HASH_SIZE]) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror("fopen"); return; }

    for (int i = 0; i < HASH_SIZE; i++) out[i] = (uint8_t)i;

    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            out[i % HASH_SIZE] = ((out[i % HASH_SIZE] << 5) | (out[i % HASH_SIZE] >> 3)) ^ buf[i];
        }
    }

    fclose(f);
}

static int write_save_state(const char* path, SaveState* state) {
    if (!path || !state) return 0;

    size_t total = sizeof(uint64_t) * 2;
    total += sizeof(FontSettings) - sizeof(char*);
    uint64_t font_len = state->font_settings.font_path ? strlen(state->font_settings.font_path) : 0;
    total += sizeof(font_len) + font_len;

    total += sizeof(state->recent_files_count);
    for (uint64_t i = 0; i < state->recent_files_count; i++)
        total += sizeof(uint64_t) + (state->recent_files[i] ? strlen(state->recent_files[i]) : 0);

    total += sizeof(state->remembered_count);
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        FileConfig* c = &state->remembered_files[i];
        total += sizeof(c->last_position) + sizeof(c->volume_percent) + sizeof(c->playback_speed)
               + sizeof(c->audio_track) + sizeof(c->subtitle_track)
               + sizeof(c->audio_track_index) + sizeof(c->subtitle_track_index)
               + HASH_SIZE;
        total += sizeof(uint64_t) + (c->video_path ? strlen(c->video_path) : 0);
    }

    uint8_t* buf = malloc(total);
    if (!buf) return 0;

    uint8_t* ptr = buf;

    uint64_t magic = SAVE_FILE_MAGIC, version = AMP_VERSION;
    memcpy(ptr, &magic, sizeof(magic)); ptr += sizeof(magic);
    memcpy(ptr, &version, sizeof(version)); ptr += sizeof(version);

    memcpy(ptr, &state->font_settings, sizeof(FontSettings) - sizeof(char*)); 
    ptr += sizeof(FontSettings) - sizeof(char*);
    memcpy(ptr, &font_len, sizeof(font_len)); ptr += sizeof(font_len);
    if (font_len) { memcpy(ptr, state->font_settings.font_path, font_len); ptr += font_len; }

    memcpy(ptr, &state->recent_files_count, sizeof(state->recent_files_count)); ptr += sizeof(state->recent_files_count);
    for (uint64_t i = 0; i < state->recent_files_count; i++) {
        uint64_t len = state->recent_files[i] ? strlen(state->recent_files[i]) : 0;
        memcpy(ptr, &len, sizeof(len)); ptr += sizeof(len);
        if (len) { memcpy(ptr, state->recent_files[i], len); ptr += len; }
    }

    memcpy(ptr, &state->remembered_count, sizeof(state->remembered_count)); ptr += sizeof(state->remembered_count);
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        FileConfig* c = &state->remembered_files[i];
        memcpy(ptr, &c->last_position, sizeof(c->last_position)); ptr += sizeof(c->last_position);
        memcpy(ptr, &c->volume_percent, sizeof(c->volume_percent)); ptr += sizeof(c->volume_percent);
        memcpy(ptr, &c->playback_speed, sizeof(c->playback_speed)); ptr += sizeof(c->playback_speed);
        memcpy(ptr, &c->audio_track, sizeof(c->audio_track)); ptr += sizeof(c->audio_track);
        memcpy(ptr, &c->subtitle_track, sizeof(c->subtitle_track)); ptr += sizeof(c->subtitle_track);
        memcpy(ptr, &c->audio_track_index, sizeof(c->audio_track_index)); ptr += sizeof(c->audio_track_index);
        memcpy(ptr, &c->subtitle_track_index, sizeof(c->subtitle_track_index)); ptr += sizeof(c->subtitle_track_index);
        memcpy(ptr, c->file_hash, HASH_SIZE); ptr += HASH_SIZE;

        uint64_t len = c->video_path ? strlen(c->video_path) : 0;
        memcpy(ptr, &len, sizeof(len)); ptr += sizeof(len);
        if (len) { memcpy(ptr, c->video_path, len); ptr += len; }
    }

    FILE* f = fopen(path, "wb");
    if (!f) { free(buf); return 0; }
    size_t written = fwrite(buf, 1, total, f);
    fclose(f);
    free(buf);
    return written == total;
}

static int load_save_state(const char* path, SaveState* s) {
    if (!path || !s) return 0;

    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* buf = malloc(size);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, size, f);
    fclose(f);

    uint8_t* ptr = buf;

    uint64_t magic, version;
    memcpy(&magic, ptr, sizeof(magic)); ptr += sizeof(magic);
    memcpy(&version, ptr, sizeof(version)); ptr += sizeof(version);
    if (magic != SAVE_FILE_MAGIC) { free(buf); return 0; }

    memcpy(&s->font_settings, ptr, sizeof(FontSettings) - sizeof(char*)); 
    ptr += sizeof(FontSettings) - sizeof(char*);
    uint64_t font_len;
    memcpy(&font_len, ptr, sizeof(font_len)); ptr += sizeof(font_len);
    if (font_len) {
        s->font_settings.font_path = malloc(font_len + 1);
        memcpy(s->font_settings.font_path, ptr, font_len);
        s->font_settings.font_path[font_len] = 0;
        ptr += font_len;
    } else s->font_settings.font_path = NULL;

    memcpy(&s->recent_files_count, ptr, sizeof(s->recent_files_count)); ptr += sizeof(s->recent_files_count);
    for (uint64_t i = 0; i < s->recent_files_count; i++) {
        uint64_t len;
        memcpy(&len, ptr, sizeof(len)); ptr += sizeof(len);
        if (len) {
            s->recent_files[i] = malloc(len + 1);
            memcpy(s->recent_files[i], ptr, len);
            s->recent_files[i][len] = 0;
            ptr += len;
        } else s->recent_files[i] = NULL;
    }

    memcpy(&s->remembered_count, ptr, sizeof(s->remembered_count)); ptr += sizeof(s->remembered_count);
    s->remembered_files = malloc(sizeof(FileConfig) * s->remembered_count);
    for (uint64_t i = 0; i < s->remembered_count; i++) {
        FileConfig* c = &s->remembered_files[i];
        memset(c, 0, sizeof(*c));
        memcpy(&c->last_position, ptr, sizeof(c->last_position)); ptr += sizeof(c->last_position);
        memcpy(&c->volume_percent, ptr, sizeof(c->volume_percent)); ptr += sizeof(c->volume_percent);
        memcpy(&c->playback_speed, ptr, sizeof(c->playback_speed)); ptr += sizeof(c->playback_speed);
        memcpy(&c->audio_track, ptr, sizeof(c->audio_track)); ptr += sizeof(c->audio_track);
        memcpy(&c->subtitle_track, ptr, sizeof(c->subtitle_track)); ptr += sizeof(c->subtitle_track);
        memcpy(&c->audio_track_index, ptr, sizeof(c->audio_track_index)); ptr += sizeof(c->audio_track_index);
        memcpy(&c->subtitle_track_index, ptr, sizeof(c->subtitle_track_index)); ptr += sizeof(c->subtitle_track_index);
        memcpy(c->file_hash, ptr, HASH_SIZE); ptr += HASH_SIZE;

        uint64_t len;
        memcpy(&len, ptr, sizeof(len)); ptr += sizeof(len);
        if (len) {
            c->video_path = malloc(len + 1);
            memcpy(c->video_path, ptr, len);
            c->video_path[len] = 0;
            ptr += len;
        } else c->video_path = NULL;
    }

    free(buf);
    return 1;
}
static int64_t get_remembered_file_index(SaveState* state, const char* video_path, uint8_t video_hash[HASH_SIZE]) {
    if (!state || (!video_path && !video_hash)) return -1;
    uint8_t hash[HASH_SIZE];
    if (video_path) {
        hash_file(video_path, hash);
    } else if (video_hash) {
        memcpy(hash, video_hash, HASH_SIZE);
    } else {
        return -1;
    }
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        if (memcmp(state->remembered_files[i].file_hash, hash, HASH_SIZE) == 0) {
            return i;
        }
    }
    return -1;
}

static void fill_save_state_from_vr_idx(VideoRenderer* vr, SaveState* state, int idx) {
    if (!vr || !state || idx < 0 || idx >= (int)state->remembered_count) return;
    
    if (idx >= 0) {
        FileConfig* existing = &state->remembered_files[idx];
        existing->last_position = vr->current_time;
        existing->volume_percent = (uint32_t)(vr->audio_volume * 100.0f);
        existing->playback_speed = vr->playback_speed;
        existing->audio_track = vr->current_audio;
        existing->subtitle_track = vr->current_subtitle;
        existing->audio_track_index = vr->audio_stream_index;
        existing->subtitle_track_index = vr->subtitle_stream_index;
    }
}

static void fill_save_state_from_vr(VideoRenderer* vr, SaveState* state, const char* video_path) {
    if (!vr || !state || !video_path) return;

    uint8_t hash[HASH_SIZE];
    hash_file(video_path, hash);

    int64_t idx = get_remembered_file_index(state, NULL, hash);

    if (idx >= 0) {
        fill_save_state_from_vr_idx(vr, state, idx);
    } else {
        FileConfig config = {0};
        config.video_path = strdup(video_path);
        memcpy(config.file_hash, hash, HASH_SIZE);
        config.last_position = vr->current_time;
        config.volume_percent = (uint32_t)(vr->audio_volume * 100.0f);
        config.playback_speed = vr->playback_speed;
        config.audio_track = vr->current_audio;
        config.subtitle_track = vr->current_subtitle;
        config.audio_track_index = vr->audio_stream_index;
        config.subtitle_track_index = vr->subtitle_stream_index;

        FileConfig* tmp = realloc(state->remembered_files, (state->remembered_count + 1) * sizeof(FileConfig));
        if (tmp) {
            state->remembered_files = tmp;
            state->remembered_files[state->remembered_count] = config;
            state->remembered_count += 1;
        } else {
            free(config.video_path);
        }
    }
}

static void apply_save_state_to_vr(VideoRenderer* vr, SaveState* state, const char* video_path) {
    if (!vr || !state) return;
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        if (state->remembered_files[i].video_path && video_path && strcmp(state->remembered_files[i].video_path, video_path) == 0) {
            if (state->remembered_files[i].audio_track >= 0)
                vr_select_audio_track(vr, state->remembered_files[i].audio_track);
            if (state->remembered_files[i].subtitle_track >= -1)
                vr_select_subtitle_track(vr, state->remembered_files[i].subtitle_track);

            vr_set_speed(vr, state->remembered_files[i].playback_speed);
            vr_set_volume(vr, state->remembered_files[i].volume_percent / 100.0f);

            vr_seek(vr, state->remembered_files[i].last_position);
            vr->last_time = state->remembered_files[i].last_position;
            break;
        }
    }
}

static void free_save_state(SaveState* state) {
    if (!state) return;
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        if (state->remembered_files[i].video_path) free(state->remembered_files[i].video_path);
    }
    if (state->remembered_files) free(state->remembered_files);
    state->remembered_files = NULL;
    state->remembered_count = 0;
    return;
}


static void debug_save_state(const SaveState* state) {
    if (!state) return;
    printf("Recent Files (%zu):\n", state->recent_files_count);
    for (uint64_t i = 0; i < state->recent_files_count; i++) {
        printf("  - %s\n", state->recent_files[i] ? state->recent_files[i] : "NULL");
    }
    printf("SaveState:\n");
    printf("  Remembered Files (%zu):\n", state->remembered_count);
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        const FileConfig* cfg = &state->remembered_files[i];
        printf("    - Video Path: %s\n", cfg->video_path ? cfg->video_path : "NULL");
        printf("      File Hash: %02X%02X%02X%02X%02X%02X%02X%02X\n", cfg->file_hash[0], cfg->file_hash[1], cfg->file_hash[2], cfg->file_hash[3], cfg->file_hash[4], cfg->file_hash[5], cfg->file_hash[6], cfg->file_hash[7]);
        printf("      Last Position: %.2f\n", cfg->last_position);
        printf("      Volume Percent: %u\n", cfg->volume_percent);
        printf("      Playback Speed: %.2f\n", cfg->playback_speed);
        printf("      Audio Track: %d\n", cfg->audio_track);
        printf("      Subtitle Track: %d\n", cfg->subtitle_track);
        printf("      Audio Track Index: %d\n", cfg->audio_track_index);
        printf("      Subtitle Track Index: %d\n", cfg->subtitle_track_index);
    }
    printf("Font Settings:\n");
    printf("  Size: %d\n", state->font_settings.size);
    printf("  Outline Size: %d\n", state->font_settings.outline_size);
    printf("  Color: 0x%08X\n", state->font_settings.color);
    printf("  Outline Color: 0x%08X\n", state->font_settings.outline_color);
    printf("  Font Path: %s\n", state->font_settings.font_path ? state->font_settings.font_path : "NULL");
}
