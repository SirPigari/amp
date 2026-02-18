#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "source/config.h"

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
    nob_cmd_append(&cmd,
                    CC,
                    opt ? RELEASE_CFLAGS : CFLAGS,
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
    nob_cmd_append(&cmd,
                    CC,
                    opt ? RELEASE_CFLAGS : CFLAGS,
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