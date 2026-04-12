#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"
#include "thirdparty/ascii.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "source/config.h"

#ifdef _WIN32
static int is_system_dll(const char* name) {
    const char* sys[] = {
        "avrt", "bcd", "bcp47langs", "bcp47mrm", "biwinrt",
        "browcli", "cabinet", "certca", "certenroll",
        "chartv", "cldapi", "combase", "coml2",
        "contactactivation", "coremessaging", "coreuicomponents",
        "cryptdll", "cryptngc", "crypttpmeksvc", "cscapi",
        "d3dscache", "davhlpr", "dbgeng", "dbghelp", "dbgmodel",
        "declaredconfiguration", "dfscli", "diagnosticdatasettings",
        "dmcmnutils", "dmenterprisediagnostics", "dmpushproxy",
        "dmxmlhelputils", "dsclient", "dsparse", "dsreg", "dsrole",
        "dui70", "duser", "edpauditapi", "edpcsp", "edputil",
        "efscore", "efsutil", "efswrt", "elscore",
        "enterpriseresourcemanager", "faultrep", "feclient",
        "firewallapi", "fms", "fveapi", "fvecerts", "fveskybackup",
        "fwbase", "fwpolicyiomgr", "fwpuclnt", "gmsaclient",
        "hwreqchk", "iertutil", "iri", "kerb3961", "ktmw32",
        "linkinfo", "logoncli", "mfc42u",
        "mpr", "mrmcorer", "msasn1", "msiltcfg", "msimg32",
        "msvcp110_win", "msvcp_win", "msvcrt",
        "netutils", "ngcrecovery", "ngcutils", "nsi",
        "ntasn1", "ntdsapi", "ntshrui",
        "oledlg", "omadmapi", "policymanager",
        "policymanagerprecheck", "printui", "propsys",
        "rmclient", "samsrv", "scecli", "sechost",
        "setupcl", "spfileq", "spinf", "srpapi", "shell32", 
        "sspicli", "sspisrv", "tbs",
        "textinputframework", "textshaping",
        "twinapi", "umpdc", "urlmon", "user32", "uxtheme",
        "vaultcli", "vertdisk", "webio", "webservices",
        "winsta", "wldp", "wtsapi32", "xmllite"
    };

    char buf[260];
    strncpy(buf, name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    for (char* s = buf; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }

    char* dot = strrchr(buf, '.');
    if (dot) *dot = 0;

    for (int i = 0; i < (int)(sizeof(sys) / sizeof(sys[0])); i++) {
        if (strcmp(buf, sys[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

static int already_copied(const char* name, char copied[][260], int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(copied[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void copy_deps_ldd(const char* exe, const char* out_dir) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "ntldd -R %s", exe);

    FILE* pipe = _popen(cmd, "r");
    if (!pipe) return;

    char line[2048];

    char copied[1024][260];
    int copied_count = 0;

    while (fgets(line, sizeof(line), pipe)) {
        char* p = strstr(line, "=>");
        if (!p) continue;

        p += 2;
        while (*p == ' ') p++;

        char* end = strstr(p, " (");
        if (!end) continue;
        *end = 0;

        if (!strstr(p, ".dll")) continue;

        char* name = strrchr(p, '\\');
        if (!name) continue;
        name++;

        if (already_copied(name, copied, copied_count)) continue;

        if (is_system_dll(name)) continue;

        strncpy(copied[copied_count++], name, 259);
        copied[copied_count - 1][259] = 0;

        char out_path[1024];
        snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, name);

        FILE* src = fopen(p, "rb");
        if (!src) continue;

        FILE* dst = fopen(out_path, "wb");
        if (!dst) {
            fclose(src);
            continue;
        }

        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
            fwrite(buf, 1, n, dst);
        }

        fclose(src);
        fclose(dst);
    }

    _pclose(pipe);
}

static void write_readme(const char* out_dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s\\README.txt", out_dir);

    FILE* f = fopen(path, "w");
    if (!f) return;

    fprintf(f,
"%s\n"
"====================================\n"
"\n"
"     1. Locate \"amp.exe\" in this folder\n"
"     2. Open (double click) the amp.exe, if it opens a security prompt, allow it\n"
"     3. The first time you run it, it creates a file association for .mp4 and .mkv files,\n"
"        and creates an 'amp_save.dat' file in this folder.\n"
"        If you want to open any file with amp, right click the file, choose 'Open with', and select 'amp'\n"
"     4. It should open a file dialog, select a mp4 or mkv file that you want to watch\n"
"     5. It should open and play!\n"
"\n"
"====================================\n"
"\n"
"%s\n",
        ascii_art,
        ascii_art_note
    );

    fclose(f);
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
    bool dist = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "run") == 0) {
            run = true;
            for (int i = 2; i < argc; i++) {
                run_flags = realloc(run_flags, sizeof(char*) * (run_flags_count + 1));
                run_flags[run_flags_count++] = argv[i];
            }
        } else if (strcmp(argv[i], "release") == 0) {
            opt = true;
        } else if (strcmp(argv[i], "distribute") == 0) {
            dist = true;
        } else {
            nob_log(NOB_WARNING, "Unknown option: %s", argv[i]);
        }
    }

#ifdef _WIN32
    char maj[32], min[32], pat[32];
    snprintf(maj, sizeof(maj), "-DAMP_VER_MAJOR=%d", (AMP_VERSION >> 16) & 0xFF);
    snprintf(min, sizeof(min), "-DAMP_VER_MINOR=%d", (AMP_VERSION >> 8) & 0xFF);
    snprintf(pat, sizeof(pat), "-DAMP_VER_PATCH=%d", AMP_VERSION & 0xFF);
    nob_cmd_append(
        &cmd,
        "windres", "assets/amp.rc", "-O", "coff", "-o", "assets/amp.res",
        maj, min, pat
    );
    if (!nob_cmd_run(&cmd)) {
        fprintf(stderr, "Resource compilation failed!\n");
        return 1;
    }

    nob_cmd_append(&cmd, CC);
    if (opt) nob_cmd_append(&cmd, RELEASE_CFLAGS);
    else nob_cmd_append(&cmd, CFLAGS);
    nob_cmd_append(&cmd,
                    "source/main.c",
                    "assets/amp.res",
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
                    "-lavfilter",
                    "-lswscale",
                    "-lswresample",
                    "-lass",
                    "-lfreetype",
                    "-lharfbuzz",
                    "-lfribidi",
                    "-luuid",
                    "-mwindows",
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
                    "-lavfilter",
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

    if (dist) {
#ifdef _WIN32
    const char* out_dir = "dist";
    const char* dll_dir = "dist\\dll";

    system("rmdir /S /Q dist");
    system("del /f dist.zip");
    system("mkdir dist");
    system("mkdir dist\\dll");

    char exe[256];
    snprintf(exe, sizeof(exe), "%s.exe", OUT_EXE_NAME);

    char out_exe[256];
    snprintf(out_exe, sizeof(out_exe), "%s/amp.exe", out_dir);

    system("rmdir /S /Q dist\\assets");
    system("xcopy assets dist\\assets /E /I /Y /Q");

    FILE* src = fopen(exe, "rb");
    FILE* dst = fopen(out_exe, "wb");

    if (src && dst) {
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
    }

    if (src) fclose(src);
    if (dst) fclose(dst);

    copy_deps_ldd(exe, dll_dir);

    system("copy LICENSE dist\\LICENSE.txt /Y");
    system("copy CHANGELOG.md dist\\CHANGELOG.md /Y");
    write_readme(out_dir);

    printf("dist created in %s\n", out_dir);

    system("cd dist && 7z a -tzip -mx=9 ..\\dist.zip *");
    printf("dist.zip created\n");
#else
    nob_log(NOB_ERROR, "Distribution is only supported on Windows!\n");
#endif
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