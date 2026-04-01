#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "config.h"
#include "../thirdparty/tinyfd.c"

typedef enum {
    /* FontSettings */
    FIELD_FONT_SIZE             = 0x0001,
    FIELD_FONT_OUTLINE_SIZE     = 0x0002,
    FIELD_FONT_COLOR            = 0x0003,
    FIELD_FONT_OUTLINE_COLOR    = 0x0004,
    FIELD_FONT_PATH             = 0x0005,   /* variable-length */

    /* recent files list */
    FIELD_RECENT_FILES          = 0x0010,   /* variable-length */

    /* remembered FileConfig array */
    FIELD_REMEMBERED_FILES      = 0x0020,   /* variable-length */

    /* per-file bookmarks */
    FIELD_BOOKMARKS             = 0x0021,   /* variable-length */

    /* hardware-decoder cache */
    FIELD_HW_CACHE              = 0x0030,   /* variable-length */

    /* global state */
    FIELD_GLOBAL                = 0x0040,   /* static-size */

    /* sentinel - must stay last */
    FIELD_SENTINEL              = 0xFFFF,
} FieldTag;

#define SIZE_VARIABLE  UINT32_MAX

typedef struct {
    uint32_t tag;
    uint32_t expected_size;
} LayoutField;

typedef enum {
    LAYOUT_MATCH         = 0,
    LAYOUT_RECOVERABLE   = 1,
    LAYOUT_UNRECOVERABLE = 2,
} LayoutDiffResult;

typedef struct {
    int32_t size;
    int32_t outline_size;
    uint32_t color;
    uint32_t outline_color;
    char* font_path;
} FontSettings;

typedef struct {
    double   time;
    char     name[BOOKMARK_NAME_MAX];
    uint32_t color_rgb;
    int      is_default_color;
} Bookmark;

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
    int bookmark_count;
    Bookmark bookmarks[MAX_BOOKMARKS_PER_FILE];
} FileConfig;

typedef struct {
    int32_t count;
    char entries[HW_CACHE_SIZE][256];
} HWCache;

static HWCache hw_cache = {0};

typedef struct {
    int paused;
} Global;

static const LayoutField CURRENT_LAYOUT[] = {
    { FIELD_FONT_SIZE,          sizeof(int32_t)  },
    { FIELD_FONT_OUTLINE_SIZE,  sizeof(int32_t)  },
    { FIELD_FONT_COLOR,         sizeof(uint32_t) },
    { FIELD_FONT_OUTLINE_COLOR, sizeof(uint32_t) },
    { FIELD_FONT_PATH,          SIZE_VARIABLE    },
    { FIELD_RECENT_FILES,       SIZE_VARIABLE    },
    { FIELD_REMEMBERED_FILES,   SIZE_VARIABLE    },
    { FIELD_BOOKMARKS,          SIZE_VARIABLE    },
    { FIELD_HW_CACHE,           SIZE_VARIABLE    },
    { FIELD_GLOBAL,             sizeof(Global)   },
};

#define CURRENT_LAYOUT_COUNT  (sizeof(CURRENT_LAYOUT) / sizeof(CURRENT_LAYOUT[0]))
#define LAYOUT_HEADER_FIXED_BYTES   4u
#define LAYOUT_ENTRY_BYTES          8u

static inline size_t layout_block_size(uint32_t field_count) {
    return LAYOUT_HEADER_FIXED_BYTES + (size_t)field_count * LAYOUT_ENTRY_BYTES;
}

typedef struct {
    char* recent_files[MAX_RECENT];
    uint64_t recent_files_count;

    FileConfig* remembered_files;
    uint64_t remembered_count;

    FontSettings font_settings;
    HWCache hw_cache;
    Global global;
} SaveState;

typedef struct {
    uint64_t magic;
    uint64_t version;
    SaveState state;
} __SaveFile;

static void free_save_state(SaveState* state);

#define TAG_TABLE_SIZE 128
#define TAG_TABLE_NONE UINT8_MAX
#define MAX_LAYOUT_FIELDS 32

typedef struct {
    uint8_t idx[TAG_TABLE_SIZE];
} TagTable;

static inline void tag_table_build(TagTable* t,
                                    const LayoutField* fields, uint32_t count) {
    memset(t->idx, TAG_TABLE_NONE, sizeof(t->idx));
    for (uint32_t i = 0; i < count && i < TAG_TABLE_NONE; i++) {
        if (fields[i].tag < TAG_TABLE_SIZE)
            t->idx[fields[i].tag] = (uint8_t)i;
    }
}

static inline int tag_table_has(const TagTable* t, uint32_t tag) {
    return tag < TAG_TABLE_SIZE && t->idx[tag] != TAG_TABLE_NONE;
}

typedef struct {
    LayoutField fields[MAX_LAYOUT_FIELDS];
    uint32_t    count;
} LayoutBlock;

void hash_file(const char* path, uint8_t out[HASH_SIZE]) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror("fopen"); return; }

    for (int i = 0; i < HASH_SIZE; i++) out[i] = (uint8_t)(i * 131u + 17u);

#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    int64_t file_size = _ftelli64(f);
#else
    fseeko(f, 0, SEEK_END);
    int64_t file_size = (int64_t)ftello(f);
#endif

    if (file_size < 0) file_size = 0;

    for (int i = 0; i < 8; i++) {
        out[i] ^= (uint8_t)((uint64_t)file_size >> (i * 8));
    }

    const int64_t chunk_size = 64 * 1024;
    int64_t offsets[3] = {
        0,
        file_size > 0 ? file_size / 2 : 0,
        file_size > chunk_size ? (file_size - chunk_size) : 0
    };

    uint8_t buf[4096];
    for (int s = 0; s < 3; s++) {
        int64_t off = offsets[s];
        if (off < 0) off = 0;
#ifdef _WIN32
        _fseeki64(f, off, SEEK_SET);
#else
        fseeko(f, off, SEEK_SET);
#endif

        int64_t remaining = chunk_size;
        while (remaining > 0) {
            size_t to_read = remaining > (int64_t)sizeof(buf) ? sizeof(buf) : (size_t)remaining;
            size_t n = fread(buf, 1, to_read, f);
            if (n == 0) break;
            for (size_t i = 0; i < n; i++) {
                uint8_t mix = (uint8_t)(buf[i] ^ (uint8_t)(off + (int64_t)i + s * 29));
                out[(i + (size_t)(s * 37)) % HASH_SIZE] = (uint8_t)((out[(i + (size_t)(s * 37)) % HASH_SIZE] << 5) | (out[(i + (size_t)(s * 37)) % HASH_SIZE] >> 3));
                out[(i + (size_t)(s * 37)) % HASH_SIZE] ^= mix;
            }
            remaining -= (int64_t)n;
        }
    }

    fclose(f);
}

static size_t write_layout_block(uint8_t* buf,
                                  const LayoutField* fields, uint32_t count) {
    uint8_t* p = buf;
    memcpy(p, &count, sizeof(count)); p += sizeof(count);
    for (uint32_t i = 0; i < count; i++) {
        memcpy(p, &fields[i].tag,           sizeof(uint32_t)); p += 4;
        memcpy(p, &fields[i].expected_size, sizeof(uint32_t)); p += 4;
    }
    return (size_t)(p - buf);
}

static int read_layout_block(const uint8_t* ptr, const uint8_t* buf_end,
                               LayoutBlock* out, size_t* out_bytes_consumed) {
    if (ptr + 4 > buf_end) return 0;

    uint32_t count = 0;
    memcpy(&count, ptr, 4);
    const uint8_t* p = ptr + 4;

    if (count > MAX_LAYOUT_FIELDS) return 0;
    if (p + (size_t)count * LAYOUT_ENTRY_BYTES > buf_end) return 0;

    for (uint32_t i = 0; i < count; i++) {
        memcpy(&out->fields[i].tag,           p, 4); p += 4;
        memcpy(&out->fields[i].expected_size, p, 4); p += 4;
    }

    out->count          = count;
    *out_bytes_consumed = (size_t)(p - ptr);
    return 1;
}

static LayoutDiffResult diff_layouts(const LayoutField* stored, uint32_t stored_count,
                                      const LayoutField* current, uint32_t current_count) {
    TagTable cur_table;
    tag_table_build(&cur_table, current, current_count);

    uint32_t last_cur_pos = UINT32_MAX;
    int      first        = 1;

    for (uint32_t si = 0; si < stored_count; si++) {
        uint32_t tag = stored[si].tag;

        if (!tag_table_has(&cur_table, tag)) {
            nob_log(NOB_ERROR,
                "[layout] stored field tag 0x%04X not found in current layout",
                stored[si].tag);
            return LAYOUT_UNRECOVERABLE;
        }

        uint32_t cur_pos = cur_table.idx[tag];

        if (current[cur_pos].expected_size != stored[si].expected_size) {
            nob_log(NOB_ERROR,
                "[layout] field tag 0x%04X: stored size %u vs current size %u",
                stored[si].tag, stored[si].expected_size,
                current[cur_pos].expected_size);
            return LAYOUT_UNRECOVERABLE;
        }

        if (!first && cur_pos <= last_cur_pos) {
            nob_log(NOB_ERROR,
                "[layout] field order mismatch: stored[%u] tag=0x%04X is at current "
                "position %u but previous stored field is at current position %u",
                si, stored[si].tag, cur_pos, last_cur_pos);
            return LAYOUT_UNRECOVERABLE;
        }

        last_cur_pos = cur_pos;
        first        = 0;
    }

    if (stored_count == current_count) return LAYOUT_MATCH;
    if (stored_count <  current_count) return LAYOUT_RECOVERABLE;
    return LAYOUT_UNRECOVERABLE;
}


static void init_default_save_state(SaveState* s) {
    memset(s, 0, sizeof(*s));
}

static int write_save_state(const char* path, SaveState* state) {
    if (!path || !state) return 0;

    size_t layout_bytes = layout_block_size((uint32_t)CURRENT_LAYOUT_COUNT);

    size_t total = sizeof(uint64_t) * 2
                 + layout_bytes;

    total += sizeof(FontSettings) - sizeof(char*);
    uint64_t font_len = state->font_settings.font_path
                      ? strlen(state->font_settings.font_path) : 0;
    total += sizeof(font_len) + font_len;

    uint64_t recent_count = state->recent_files_count;
    if (recent_count > MAX_RECENT) recent_count = MAX_RECENT;
    total += sizeof(recent_count);
    for (uint64_t i = 0; i < recent_count; i++)
        total += sizeof(uint64_t)
               + (state->recent_files[i] ? strlen(state->recent_files[i]) : 0);

    total += sizeof(state->remembered_count);
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        FileConfig* c = &state->remembered_files[i];
        total += sizeof(c->last_position) + sizeof(c->volume_percent)
               + sizeof(c->playback_speed) + sizeof(c->audio_track)
               + sizeof(c->subtitle_track)
               + sizeof(c->audio_track_index) + sizeof(c->subtitle_track_index)
               + HASH_SIZE
               + sizeof(uint64_t)
               + (c->video_path ? strlen(c->video_path) : 0);
    }

    total += sizeof(uint64_t);
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        FileConfig* c = &state->remembered_files[i];
        total += HASH_SIZE + sizeof(uint32_t);
        total += (size_t)c->bookmark_count
               * (sizeof(double) + BOOKMARK_NAME_MAX + sizeof(uint32_t) + sizeof(int));
    }

    total += sizeof(uint64_t);
    for (int i = 0; i < state->hw_cache.count; i++)
        total += sizeof(uint64_t) + strlen(state->hw_cache.entries[i]);
    total += sizeof(state->global);

    uint8_t* buf = malloc(total);
    if (!buf) return 0;
    uint8_t* ptr = buf;

    uint64_t magic = SAVE_FILE_MAGIC, version = AMP_VERSION;
    memcpy(ptr, &magic,   sizeof(magic));   ptr += sizeof(magic);
    memcpy(ptr, &version, sizeof(version)); ptr += sizeof(version);

    ptr += write_layout_block(ptr, CURRENT_LAYOUT, (uint32_t)CURRENT_LAYOUT_COUNT);

    memcpy(ptr, &state->font_settings, sizeof(FontSettings) - sizeof(char*));
    ptr += sizeof(FontSettings) - sizeof(char*);
    memcpy(ptr, &font_len, sizeof(font_len)); ptr += sizeof(font_len);
    if (font_len) { memcpy(ptr, state->font_settings.font_path, font_len); ptr += font_len; }

    memcpy(ptr, &recent_count, sizeof(recent_count)); ptr += sizeof(recent_count);
    for (uint64_t i = 0; i < recent_count; i++) {
        uint64_t len = state->recent_files[i] ? strlen(state->recent_files[i]) : 0;
        memcpy(ptr, &len, sizeof(len)); ptr += sizeof(len);
        if (len) { memcpy(ptr, state->recent_files[i], len); ptr += len; }
    }

    memcpy(ptr, &state->remembered_count, sizeof(state->remembered_count));
    ptr += sizeof(state->remembered_count);
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        FileConfig* c = &state->remembered_files[i];
        memcpy(ptr, &c->last_position,       sizeof(c->last_position));       ptr += sizeof(c->last_position);
        memcpy(ptr, &c->volume_percent,      sizeof(c->volume_percent));      ptr += sizeof(c->volume_percent);
        memcpy(ptr, &c->playback_speed,      sizeof(c->playback_speed));      ptr += sizeof(c->playback_speed);
        memcpy(ptr, &c->audio_track,         sizeof(c->audio_track));         ptr += sizeof(c->audio_track);
        memcpy(ptr, &c->subtitle_track,      sizeof(c->subtitle_track));      ptr += sizeof(c->subtitle_track);
        memcpy(ptr, &c->audio_track_index,   sizeof(c->audio_track_index));   ptr += sizeof(c->audio_track_index);
        memcpy(ptr, &c->subtitle_track_index,sizeof(c->subtitle_track_index));ptr += sizeof(c->subtitle_track_index);
        memcpy(ptr, c->file_hash, HASH_SIZE); ptr += HASH_SIZE;
        uint64_t len = c->video_path ? strlen(c->video_path) : 0;
        memcpy(ptr, &len, sizeof(len)); ptr += sizeof(len);
        if (len) { memcpy(ptr, c->video_path, len); ptr += len; }
    }

    {
        uint64_t bm_entry_count = state->remembered_count;
        memcpy(ptr, &bm_entry_count, sizeof(bm_entry_count)); ptr += sizeof(bm_entry_count);
        for (uint64_t i = 0; i < state->remembered_count; i++) {
            FileConfig* c = &state->remembered_files[i];
            memcpy(ptr, c->file_hash, HASH_SIZE); ptr += HASH_SIZE;
            uint32_t bm_count = (uint32_t)c->bookmark_count;
            memcpy(ptr, &bm_count, sizeof(bm_count)); ptr += sizeof(bm_count);
            for (int j = 0; j < c->bookmark_count; j++) {
                memcpy(ptr, &c->bookmarks[j].time, sizeof(double)); ptr += sizeof(double);
                memcpy(ptr, c->bookmarks[j].name, BOOKMARK_NAME_MAX); ptr += BOOKMARK_NAME_MAX;
                memcpy(ptr, &c->bookmarks[j].color_rgb, sizeof(uint32_t)); ptr += sizeof(uint32_t);
                memcpy(ptr, &c->bookmarks[j].is_default_color, sizeof(int)); ptr += sizeof(int);
            }
        }
    }

    uint64_t hw_count = (uint64_t)(state->hw_cache.count > 0 ? state->hw_cache.count : 0);
    memcpy(ptr, &hw_count, sizeof(hw_count)); ptr += sizeof(hw_count);
    for (uint64_t i = 0; i < hw_count; i++) {
        uint64_t elen = state->hw_cache.entries[i][0]
                      ? strlen(state->hw_cache.entries[i]) : 0;
        memcpy(ptr, &elen, sizeof(elen)); ptr += sizeof(elen);
        if (elen) { memcpy(ptr, state->hw_cache.entries[i], elen); ptr += elen; }
    }
    memcpy(ptr, &state->global, sizeof(state->global)); ptr += sizeof(state->global);

    FILE* f = fopen(path, "wb");
    if (!f) { free(buf); return 0; }
    size_t written = fwrite(buf, 1, total, f);
    fclose(f);
    free(buf);
    return written == total;
}

static int parse_payload(const uint8_t* ptr, const uint8_t* buf_end,
                          const LayoutField* stored_fields, uint32_t stored_count,
                          SaveState* s) {

    TagTable stored_table;
    tag_table_build(&stored_table, stored_fields, stored_count);

    for (uint32_t ci = 0; ci < (uint32_t)CURRENT_LAYOUT_COUNT; ci++) {
        uint32_t tag    = CURRENT_LAYOUT[ci].tag;
        int      present = tag_table_has(&stored_table, tag);

        if (!present) {
            const char* name = "unknown";
            switch (tag) {
                case FIELD_FONT_SIZE:          name = "font_size";          break;
                case FIELD_FONT_OUTLINE_SIZE:  name = "font_outline_size";  break;
                case FIELD_FONT_COLOR:         name = "font_color";         break;
                case FIELD_FONT_OUTLINE_COLOR: name = "font_outline_color"; break;
                case FIELD_FONT_PATH:          name = "font_path";          break;
                case FIELD_RECENT_FILES:       name = "recent_files";       break;
                case FIELD_REMEMBERED_FILES:   name = "remembered_files";   break;
                case FIELD_HW_CACHE:           name = "hw_cache";           break;
            }
            nob_log(NOB_WARNING,
                "[layout] field '%s' (tag 0x%04X) not found in save file - "
                "defaulting", name, tag);
            continue;
        }

        switch (tag) {

        case FIELD_FONT_SIZE:
            if (ptr + sizeof(int32_t) > buf_end) return 0;
            memcpy(&s->font_settings.size, ptr, sizeof(int32_t));
            ptr += sizeof(int32_t);
            break;

        case FIELD_FONT_OUTLINE_SIZE:
            if (ptr + sizeof(int32_t) > buf_end) return 0;
            memcpy(&s->font_settings.outline_size, ptr, sizeof(int32_t));
            ptr += sizeof(int32_t);
            break;

        case FIELD_FONT_COLOR:
            if (ptr + sizeof(uint32_t) > buf_end) return 0;
            memcpy(&s->font_settings.color, ptr, sizeof(uint32_t));
            ptr += sizeof(uint32_t);
            break;

        case FIELD_FONT_OUTLINE_COLOR:
            if (ptr + sizeof(uint32_t) > buf_end) return 0;
            memcpy(&s->font_settings.outline_color, ptr, sizeof(uint32_t));
            ptr += sizeof(uint32_t);
            break;

        case FIELD_FONT_PATH: {
            if (ptr + sizeof(uint64_t) > buf_end) return 0;
            uint64_t font_len;
            memcpy(&font_len, ptr, sizeof(font_len)); ptr += sizeof(font_len);
            if (font_len) {
                if (ptr + font_len > buf_end) return 0;
                s->font_settings.font_path = malloc(font_len + 1);
                if (!s->font_settings.font_path) return 0;
                memcpy(s->font_settings.font_path, ptr, font_len);
                s->font_settings.font_path[font_len] = '\0';
                ptr += font_len;
            } else {
                s->font_settings.font_path = NULL;
            }
            break;
        }

        case FIELD_RECENT_FILES: {
            if (ptr + sizeof(uint64_t) > buf_end) return 0;
            uint64_t loaded_recent_count = 0;
            memcpy(&loaded_recent_count, ptr, sizeof(loaded_recent_count));
            ptr += sizeof(loaded_recent_count);
            s->recent_files_count = loaded_recent_count > MAX_RECENT
                                  ? MAX_RECENT : loaded_recent_count;
            for (uint64_t i = 0; i < loaded_recent_count; i++) {
                if (ptr + sizeof(uint64_t) > buf_end) return 0;
                uint64_t len;
                memcpy(&len, ptr, sizeof(len)); ptr += sizeof(len);
                if (len) {
                    if (ptr + len > buf_end) return 0;
                    if (i < MAX_RECENT) {
                        s->recent_files[i] = malloc(len + 1);
                        if (!s->recent_files[i]) return 0;
                        memcpy(s->recent_files[i], ptr, len);
                        s->recent_files[i][len] = '\0';
                    }
                    ptr += len;
                } else if (i < MAX_RECENT) {
                    s->recent_files[i] = NULL;
                }
            }
            break;
        }

        case FIELD_REMEMBERED_FILES: {
            if (ptr + sizeof(uint64_t) > buf_end) return 0;
            memcpy(&s->remembered_count, ptr, sizeof(s->remembered_count));
            ptr += sizeof(s->remembered_count);
            s->remembered_files = malloc(sizeof(FileConfig) * (s->remembered_count ? s->remembered_count : 1));
            if (!s->remembered_files) return 0;
            for (uint64_t i = 0; i < s->remembered_count; i++) {
                FileConfig* c = &s->remembered_files[i];
                memset(c, 0, sizeof(*c));
                #define READ_FIELD(dst, T) \
                    do { if (ptr + sizeof(T) > buf_end) return 0; \
                         memcpy(dst, ptr, sizeof(T)); ptr += sizeof(T); } while(0)
                READ_FIELD(&c->last_position,        double);
                READ_FIELD(&c->volume_percent,       uint32_t);
                READ_FIELD(&c->playback_speed,       float);
                READ_FIELD(&c->audio_track,          int32_t);
                READ_FIELD(&c->subtitle_track,       int32_t);
                READ_FIELD(&c->audio_track_index,    int);
                READ_FIELD(&c->subtitle_track_index, int);
                #undef READ_FIELD
                if (ptr + HASH_SIZE > buf_end) return 0;
                memcpy(c->file_hash, ptr, HASH_SIZE); ptr += HASH_SIZE;
                if (ptr + sizeof(uint64_t) > buf_end) return 0;
                uint64_t len;
                memcpy(&len, ptr, sizeof(len)); ptr += sizeof(len);
                if (len) {
                    if (ptr + len > buf_end) return 0;
                    c->video_path = malloc(len + 1);
                    if (!c->video_path) return 0;
                    memcpy(c->video_path, ptr, len);
                    c->video_path[len] = '\0';
                    ptr += len;
                } else {
                    c->video_path = NULL;
                }
            }
            break;
        }

        case FIELD_HW_CACHE: {
            if (ptr + sizeof(uint64_t) > buf_end) break;
            uint64_t hw_count = 0;
            memcpy(&hw_count, ptr, sizeof(hw_count)); ptr += sizeof(hw_count);
            if (hw_count > HW_CACHE_SIZE) hw_count = HW_CACHE_SIZE;
            s->hw_cache.count = (int32_t)hw_count;
            for (uint64_t i = 0; i < hw_count; i++) {
                if (ptr + sizeof(uint64_t) > buf_end) break;
                uint64_t elen = 0;
                memcpy(&elen, ptr, sizeof(elen)); ptr += sizeof(elen);
                if (elen > 0 && ptr + elen <= buf_end) {
                    if (elen >= sizeof(s->hw_cache.entries[i]))
                        elen = sizeof(s->hw_cache.entries[i]) - 1;
                    memcpy(s->hw_cache.entries[i], ptr, elen);
                    s->hw_cache.entries[i][elen] = '\0';
                    ptr += elen;
                } else {
                    s->hw_cache.entries[i][0] = '\0';
                }
            }
            break;
        }

        case FIELD_SENTINEL:
            break;

        case FIELD_GLOBAL: {
            if (ptr + sizeof(Global) > buf_end) break;
            memcpy(&s->global, ptr, sizeof(Global)); ptr += sizeof(Global);
            break;
        }

        case FIELD_BOOKMARKS: {
            if (ptr + sizeof(uint64_t) > buf_end) break;
            uint64_t bm_entry_count = 0;
            memcpy(&bm_entry_count, ptr, sizeof(bm_entry_count)); ptr += sizeof(bm_entry_count);
            for (uint64_t ei = 0; ei < bm_entry_count; ei++) {
                if (ptr + HASH_SIZE + sizeof(uint32_t) > buf_end) break;
                uint8_t hash[HASH_SIZE];
                memcpy(hash, ptr, HASH_SIZE); ptr += HASH_SIZE;
                uint32_t bm_count = 0;
                memcpy(&bm_count, ptr, sizeof(bm_count)); ptr += sizeof(bm_count);
                if (bm_count > MAX_BOOKMARKS_PER_FILE) bm_count = MAX_BOOKMARKS_PER_FILE;
                FileConfig* target = NULL;
                for (uint64_t j = 0; j < s->remembered_count; j++) {
                    if (memcmp(s->remembered_files[j].file_hash, hash, HASH_SIZE) == 0) {
                        target = &s->remembered_files[j];
                        break;
                    }
                }
                for (uint32_t k = 0; k < bm_count; k++) {
                    if (ptr + sizeof(double) + BOOKMARK_NAME_MAX + sizeof(uint32_t) + sizeof(int) > buf_end) break;
                    double bm_time;
                    char bm_name[BOOKMARK_NAME_MAX];
                    uint32_t bm_color;
                    int bm_is_default_color;
                    memcpy(&bm_time,  ptr, sizeof(double));       ptr += sizeof(double);
                    memcpy(bm_name,   ptr, BOOKMARK_NAME_MAX);    ptr += BOOKMARK_NAME_MAX;
                    memcpy(&bm_color, ptr, sizeof(uint32_t));     ptr += sizeof(uint32_t);
                    memcpy(&bm_is_default_color, ptr, sizeof(int)); ptr += sizeof(int);
                    if (target && target->bookmark_count < MAX_BOOKMARKS_PER_FILE) {
                        Bookmark* bm = &target->bookmarks[target->bookmark_count];
                        bm->time      = bm_time;
                        bm->color_rgb = bm_color;
                        bm->is_default_color = bm_is_default_color;
                        memcpy(bm->name, bm_name, BOOKMARK_NAME_MAX);
                        bm->name[BOOKMARK_NAME_MAX - 1] = '\0';
                        target->bookmark_count++;
                    }
                }
            }
            break;
        }

        default:
            nob_log(NOB_ERROR, "[layout] unhandled tag 0x%04X in parse_payload", tag);
            break;
        }
    }

    return 1;
}

static int load_save_state(const char* path, SaveState* s) {
    if (!path || !s) return 0;

    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    uint64_t hdr[2];
    if (fread(hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return 0; }
    if (hdr[0] != SAVE_FILE_MAGIC) {
        fclose(f);
        nob_log(NOB_ERROR,
            "invalid save file magic: expected 0x%016" PRIX64 ", got 0x%016" PRIX64,
            (uint64_t)SAVE_FILE_MAGIC, hdr[0]);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* buf = malloc(size);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, size, f);
    fclose(f);

    const uint8_t* buf_end = buf + size;
    const uint8_t* ptr     = buf + sizeof(uint64_t) * 2;

    LayoutBlock      stored_block = {0};
    size_t           layout_bytes = 0;
    LayoutDiffResult diff         = LAYOUT_UNRECOVERABLE;

    if (read_layout_block(ptr, buf_end, &stored_block, &layout_bytes)) {
        ptr += layout_bytes;
        diff = diff_layouts(stored_block.fields, stored_block.count,
                            CURRENT_LAYOUT, (uint32_t)CURRENT_LAYOUT_COUNT);
    } else {
        nob_log(NOB_WARNING,
            "[layout] save file has no layout descriptor "
            "(predates layout versioning)");
        diff = LAYOUT_UNRECOVERABLE;
    }

    if (diff == LAYOUT_UNRECOVERABLE) {
        free(buf);

        nob_log(NOB_ERROR,
            "[layout] unrecoverable layout mismatch - prompting user");

        int reset = tinyfd_messageBox(
            "Save File Incompatible",
            "The save file layout is incompatible with this version of "
            "amp and cannot be recovered.\n\n"
            "Reset the save file to defaults?\n"
            "(Choosing No will exit the application.)",
            "yesno", "warning", 0 /* default = No */);

        if (reset) {
            nob_log(NOB_ERROR, "[layout] user chose reset - returning default state");
            init_default_save_state(s);
            hw_cache = s->hw_cache;
            write_save_state(path, s);
            return 1;
        } else {
            nob_log(NOB_ERROR,
                "[layout] user chose not to reset - signalling caller to quit");
            return -1;
        }
    }

    init_default_save_state(s);

    int ok = parse_payload(ptr, buf_end,
                            stored_block.fields, stored_block.count,
                            s);

    free(buf);

    if (!ok) {
        nob_log(NOB_ERROR, "[layout] payload parse failed");
        free_save_state(s);
        return 0;
    }

    hw_cache = s->hw_cache;

    if (diff == LAYOUT_RECOVERABLE) {
        nob_log(NOB_WARNING,
            "[layout] recoverable migration complete - rewriting '%s' "
            "with current layout", path);
        write_save_state(path, s);
    }

    return 1;
}

int hw_cache_has(const char* name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < hw_cache.count; i++) {
        if (strcmp(hw_cache.entries[i], name) == 0) return 1;
    }
    return 0;
}

void hw_cache_mark_success(const char* name) {
    if (!name || !name[0]) return;
    if (hw_cache_has(name)) return;
    if (hw_cache.count < HW_CACHE_SIZE) {
        snprintf(hw_cache.entries[hw_cache.count],
                 sizeof(hw_cache.entries[hw_cache.count]), "%s", name);
        hw_cache.count++;
    }
}

static int64_t get_remembered_file_index(SaveState* state,
                                          const char* video_path,
                                          uint8_t video_hash[HASH_SIZE]) {
    if (!state || (!video_path && !video_hash)) return -1;
    uint8_t hash[HASH_SIZE];
    if (video_path)       hash_file(video_path, hash);
    else if (video_hash)  memcpy(hash, video_hash, HASH_SIZE);
    else                  return -1;

    for (uint64_t i = 0; i < state->remembered_count; i++) {
        if (memcmp(state->remembered_files[i].file_hash, hash, HASH_SIZE) == 0)
            return (int64_t)i;
    }
    return -1;
}

static void fill_save_state_from_vr_idx(VideoRenderer* vr,
                                         SaveState* state, int idx) {
    if (!vr || !state || idx < 0 || idx >= (int)state->remembered_count) return;
    FileConfig* existing = &state->remembered_files[idx];
    existing->last_position        = vr->last_time;
    existing->volume_percent       = (uint32_t)(vr->audio_volume * 100.0f);
    existing->playback_speed       = vr->playback_speed;
    existing->audio_track          = vr->current_audio;
    existing->subtitle_track       = vr->current_subtitle;
    existing->audio_track_index    = vr->audio_stream_index;
    existing->subtitle_track_index = vr->subtitle_stream_index;
}

static void fill_save_state_from_vr(VideoRenderer* vr,
                                     SaveState* state,
                                     const char* video_path, bool paused) {
    if (!vr || !state || !video_path) return;

    uint8_t hash[HASH_SIZE];
    hash_file(video_path, hash);

    state->global.paused = paused;

    int64_t idx = get_remembered_file_index(state, NULL, hash);
    if (idx >= 0) {
        fill_save_state_from_vr_idx(vr, state, (int)idx);
    } else {
        FileConfig config = {0};
        config.video_path    = strdup(video_path);
        memcpy(config.file_hash, hash, HASH_SIZE);
        config.last_position       = vr->last_time;
        config.volume_percent      = (uint32_t)(vr->audio_volume * 100.0f);
        config.playback_speed      = vr->playback_speed;
        config.audio_track         = vr->current_audio;
        config.subtitle_track      = vr->current_subtitle;
        config.audio_track_index   = vr->audio_stream_index;
        config.subtitle_track_index= vr->subtitle_stream_index;

        FileConfig* tmp = realloc(state->remembered_files,
                                  (state->remembered_count + 1) * sizeof(FileConfig));
        if (tmp) {
            state->remembered_files = tmp;
            state->remembered_files[state->remembered_count] = config;
            state->remembered_count++;
        } else {
            free(config.video_path);
        }
    }
}

static void apply_save_state_to_vr(VideoRenderer* vr,
                                    SaveState* state,
                                    const char* video_path) {
    if (!vr || !state) return;
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        if (state->remembered_files[i].video_path && video_path &&
            strcmp(state->remembered_files[i].video_path, video_path) == 0) {
            if (state->remembered_files[i].audio_track >= 0)
                vr_select_audio_track(vr, state->remembered_files[i].audio_track);
            if (state->remembered_files[i].subtitle_track >= -1)
                vr_select_subtitle_track(vr, state->remembered_files[i].subtitle_track);
            vr_set_speed(vr, state->remembered_files[i].playback_speed);
            vr_set_volume(vr, state->remembered_files[i].volume_percent / 100.0f);
            double target_pos = state->remembered_files[i].last_position;
            vr_seek(vr, target_pos);
            for (int j = 0; j < 128 && vr->current_time < target_pos - 0.05; j++) {
                vr_demux_packets(vr);
                vr_render_frame(vr);
            }
            vr->last_time = target_pos;
            break;
        }
    }
}

static void free_save_state(SaveState* state) {
    if (!state) return;
    for (uint64_t i = 0;
         i < state->recent_files_count && i < MAX_RECENT; i++) {
        if (state->recent_files[i]) free(state->recent_files[i]);
        state->recent_files[i] = NULL;
    }
    state->recent_files_count = 0;
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        if (state->remembered_files[i].video_path)
            free(state->remembered_files[i].video_path);
    }
    if (state->remembered_files) free(state->remembered_files);
    state->remembered_files = NULL;
    state->remembered_count = 0;
    if (state->font_settings.font_path) {
        free(state->font_settings.font_path);
        state->font_settings.font_path = NULL;
    }
}

static void debug_save_state(const SaveState* state) {
    if (!state) return;
    printf("Recent Files (%zu):\n", state->recent_files_count);
    for (uint64_t i = 0; i < state->recent_files_count; i++)
        printf("  - %s\n", state->recent_files[i] ? state->recent_files[i] : "NULL");

    printf("SaveState:\n");
    printf("  Remembered Files (%zu):\n", state->remembered_count);
    for (uint64_t i = 0; i < state->remembered_count; i++) {
        const FileConfig* cfg = &state->remembered_files[i];
        printf("    - Video Path: %s\n", cfg->video_path ? cfg->video_path : "NULL");
        printf("      File Hash: %02X%02X%02X%02X%02X%02X%02X%02X\n",
               cfg->file_hash[0], cfg->file_hash[1], cfg->file_hash[2],
               cfg->file_hash[3], cfg->file_hash[4], cfg->file_hash[5],
               cfg->file_hash[6], cfg->file_hash[7]);
        printf("      Last Position: %.2f\n",  cfg->last_position);
        printf("      Volume Percent: %u\n",   cfg->volume_percent);
        printf("      Playback Speed: %.2f\n", cfg->playback_speed);
        printf("      Audio Track: %d\n",      cfg->audio_track);
        printf("      Subtitle Track: %d\n",   cfg->subtitle_track);
        printf("      Audio Track Index: %d\n",   cfg->audio_track_index);
        printf("      Subtitle Track Index: %d\n",cfg->subtitle_track_index);
        printf("      Bookmarks (%u):\n", cfg->bookmark_count);
        for (int j = 0; j < cfg->bookmark_count; j++) {
            const Bookmark* bm = &cfg->bookmarks[j];
            printf("        - Time: %.2f, Name: %s, Color: 0x%08X, Default Color: %s\n",
                   bm->time, bm->name, bm->color_rgb, bm->is_default_color ? "true" : "false");
        }
    }
    printf("Font Settings:\n");
    printf("  Size: %d\n",          state->font_settings.size);
    printf("  Outline Size: %d\n",  state->font_settings.outline_size);
    printf("  Color: 0x%08X\n",     state->font_settings.color);
    printf("  Outline Color: 0x%08X\n", state->font_settings.outline_color);
    printf("  Font Path: %s\n",
           state->font_settings.font_path ? state->font_settings.font_path : "NULL");
    printf("HW Cache (%d):\n", state->hw_cache.count);
    for (int i = 0; i < state->hw_cache.count; i++)
        printf("  - %s\n", state->hw_cache.entries[i]);
    printf("Global:\n");
    printf("  Paused: %d\n", state->global.paused);
}

static void update_bookmarks_in_save_state(SaveState* state,
                                            const char* video_path,
                                            const Bookmark* bms,
                                            int bm_count) {
    if (!state || !video_path) return;
    int64_t idx = get_remembered_file_index(state, video_path, NULL);
    if (idx < 0) return;
    FileConfig* c = &state->remembered_files[idx];
    int cnt = (bm_count < MAX_BOOKMARKS_PER_FILE) ? bm_count : MAX_BOOKMARKS_PER_FILE;
    c->bookmark_count = cnt;
    if (bms && cnt > 0) {
        for (int i = 0; i < cnt; i++) c->bookmarks[i] = bms[i];
    }
}

static void get_bookmarks_from_save_state(const SaveState* state,
                                           const char* video_path,
                                           Bookmark* out_bm,
                                           int* out_count) {
    if (!out_count) return;
    *out_count = 0;
    if (!state || !video_path || !out_bm) return;
    int64_t idx = get_remembered_file_index((SaveState*)state, video_path, NULL);
    if (idx < 0) return;
    const FileConfig* c = &state->remembered_files[idx];
    int cnt = (c->bookmark_count < MAX_BOOKMARKS_PER_FILE)
            ? c->bookmark_count : MAX_BOOKMARKS_PER_FILE;
    for (int i = 0; i < cnt; i++) out_bm[i] = c->bookmarks[i];
    *out_count = cnt;
}