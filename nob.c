#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "source/config.h"

#if USE_THEMES
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

static void add_theme_includes(Nob_Cmd *cmd) {
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*.h", THEMES_DIR);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", THEMES_DIR, fd.cFileName);
            nob_cmd_append(cmd, "-include", nob_temp_strdup(path));
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

#else
    DIR *d = opendir(THEMES_DIR);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d))) {
        const char *name = ent->d_name;

        size_t len = strlen(name);
        if (len > 2 && strcmp(name + len - 2, ".h") == 0) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", THEMES_DIR, name);
            nob_cmd_append(cmd, "-include", nob_temp_strdup(path));
        }
    }

    closedir(d);
#endif
}
#endif

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "source/config.h");

    Nob_Cmd cmd = {0};

    const char* sdl_lib = getenv("SDL_PATH");
    if (!sdl_lib) {
#ifdef _WIN32
        sdl_lib = "C:/SDL2/lib/x64";
#else
        sdl_lib = "/usr/lib";
#endif
    }
    const char* ffmpeg_lib = getenv("FFMPEG_PATH");
    if (!ffmpeg_lib) {
#ifdef _WIN32
        ffmpeg_lib = "C:/ffmpeg/lib";
#else
        ffmpeg_lib = "/usr/lib";
#endif
    }
    const char* ass_lib = getenv("ASS_PATH");
    if (!ass_lib) {
#ifdef _WIN32
        ass_lib = "C:/ass/lib";
#else
        ass_lib = "/usr/lib";
#endif
    }

    bool run = false;
    char** run_flags = NULL;
    unsigned int run_flags_count = 0;
    bool opt = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "run") == 0) {
            run = true;
            for (int i = 2; i < argc; i++) {
                run_flags = realloc(run_flags, sizeof(char*) * (run_flags_count + 1));
                run_flags[run_flags_count++] = argv[i];
            }
        } else if (strcmp(argv[i], "release") == 0) {
            opt = true;
        }
    }

#ifdef _WIN32
    nob_cmd_append(&cmd, CC);
    if (opt) nob_cmd_append(&cmd, RELEASE_CFLAGS);
    else nob_cmd_append(&cmd, CFLAGS);
    nob_cmd_append(&cmd,
                    "source/main.c",
                    "-DSDL_MAIN_HANDLED",
                    "-L", sdl_lib,
                    "-L", ffmpeg_lib,
                    "-L", ass_lib,
                    "-lmingw32",
                    "-lSDL2main",
                    "-lSDL2",
                    "-lSDL2_ttf",
                    "-lole32",
                    "-lcomdlg32",
                    "-lcomctl32",
                    "-lavformat",
                    "-lavcodec",
                    "-lavutil",
                    "-lswscale",
                    "-lswresample",
                    "-lass",
                    "-lfreetype",
                    "-lharfbuzz",
                    "-lfribidi",
                    "-mconsole",
                    "-o", OUT_EXE_NAME".exe");
#else
    nob_cmd_append(&cmd, CC);
    if (opt) nob_cmd_append(&cmd, RELEASE_CFLAGS);
    else nob_cmd_append(&cmd, CFLAGS);
    nob_cmd_append(&cmd,
                    "source/main.c",
                    "-L", sdl_lib,
                    "-L", ffmpeg_lib,
                    "-L", ass_lib,
                    "-lSDL2",
                    "-lSDL2_ttf",
                    "-lavformat",
                    "-lavcodec",
                    "-lavutil",
                    "-lswscale",
                    "-lswresample",
                    "-lass",
                    "-lfreetype",
                    "-lharfbuzz",
                    "-lfribidi",
                    "-lm",
                    "-o", OUT_EXE_NAME);
#endif

    #if USE_THEMES
    add_theme_includes(&cmd);
    #endif

    if (!nob_cmd_run(&cmd)) {
        fprintf(stderr, "Compilation failed!\n");
        return 1;
    }

    if (run) {
        cmd.count = 0;
        nob_cmd_append(&cmd, "./"OUT_EXE_NAME);
        for (int i = 0; i < run_flags_count; i++) {
            nob_cmd_append(&cmd, run_flags[i]);
        }
        if (!nob_cmd_run(&cmd)) {
            fprintf(stderr, "Execution failed!\n");
            return 1;
        }
    }

    return 0;
}