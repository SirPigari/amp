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
    uint64_t recent_files_sizes[MAX_RECENT];

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
    FILE* f = fopen(path, "wb");
    if (!f) return 0;

    uint64_t magic = SAVE_FILE_MAGIC;
    uint64_t version = AMP_VERSION;
    fwrite(&magic, sizeof(uint64_t), 1, f);
    fwrite(&version, sizeof(uint64_t), 1, f);

    fwrite(&state->font_settings, sizeof(FontSettings), 1, f);

    fwrite(&state->remembered_count, sizeof(uint64_t), 1, f);

    for (uint64_t i = 0; i < state->remembered_count; i++) {
        FileConfig* cfg = &state->remembered_files[i];

        fwrite(&cfg->last_position, sizeof(double), 1, f);
        fwrite(&cfg->volume_percent, sizeof(uint32_t), 1, f);
        fwrite(&cfg->playback_speed, sizeof(float), 1, f);
        fwrite(&cfg->audio_track, sizeof(int32_t), 1, f);
        fwrite(&cfg->subtitle_track, sizeof(int32_t), 1, f);
        fwrite(&cfg->audio_track_index, sizeof(int), 1, f);
        fwrite(&cfg->subtitle_track_index, sizeof(int), 1, f);
        fwrite(&cfg->file_hash, sizeof(cfg->file_hash), 1, f);

        if (cfg->video_path) {
            uint64_t len = strlen(cfg->video_path);
            fwrite(&len, sizeof(uint64_t), 1, f);
            fwrite(cfg->video_path, 1, len, f);
        } else {
            uint64_t len = 0;
            fwrite(&len, sizeof(uint64_t), 1, f);
        }
    }

    fclose(f);
    return 1;
}

static int load_save_state(const char* path, SaveState* out_state) {
    if (!path || !out_state) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    uint64_t magic = 0, version = 0;
    fread(&magic, sizeof(uint64_t), 1, f);
    fread(&version, sizeof(uint64_t), 1, f);

    if (magic != SAVE_FILE_MAGIC) { fclose(f); return 0; }

    fread(&out_state->font_settings, sizeof(FontSettings), 1, f);

    fread(&out_state->remembered_count, sizeof(uint64_t), 1, f);

    out_state->remembered_files = malloc(out_state->remembered_count * sizeof(FileConfig));
    if (!out_state->remembered_files) { fclose(f); return 0; }

    for (uint64_t i = 0; i < out_state->remembered_count; i++) {
        FileConfig* cfg = &out_state->remembered_files[i];
        memset(cfg, 0, sizeof(FileConfig));

        fread(&cfg->last_position, sizeof(double), 1, f);
        fread(&cfg->volume_percent, sizeof(uint32_t), 1, f);
        fread(&cfg->playback_speed, sizeof(float), 1, f);
        fread(&cfg->audio_track, sizeof(int32_t), 1, f);
        fread(&cfg->subtitle_track, sizeof(int32_t), 1, f);
        fread(&cfg->audio_track_index, sizeof(int), 1, f);
        fread(&cfg->subtitle_track_index, sizeof(int), 1, f);
        fread(&cfg->file_hash, sizeof(cfg->file_hash), 1, f);

        uint64_t len = 0;
        fread(&len, sizeof(uint64_t), 1, f);
        if (len > 0) {
            cfg->video_path = malloc(len + 1);
            fread(cfg->video_path, 1, len, f);
            cfg->video_path[len] = '\0';
        } else {
            cfg->video_path = NULL;
        }
    }

    fclose(f);
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
