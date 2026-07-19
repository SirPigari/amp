#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"
#include "thirdparty/ascii.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "source/config.h"

#ifdef _WIN32
static int already_copied(const char* name, char** copied, int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(copied[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void copy_deps_ldd(const char* exe, const char* out_dir, char** copied, int* copied_count) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "ntldd -R %s", exe);

    FILE* pipe = _popen(cmd, "r");
    if (!pipe) return;

    char line[2048];

    while (fgets(line, sizeof(line), pipe)) {
        char* p = strstr(line, "=>");
        if (!p) continue;
        p += 2;
        while (*p == ' ') p++;

        char* end = strstr(p, " (");
        if (!end) continue;
        *end = 0;

        if (!strstr(p, ".dll") && !strstr(p, ".DLL")) continue;

        char lower_path[2048];
        strncpy(lower_path, p, sizeof(lower_path) - 1);
        lower_path[sizeof(lower_path) - 1] = 0;
        for (char* s = lower_path; *s; s++) *s = (char)tolower((unsigned char)*s);

        if (strstr(lower_path, "\\system32\\") ||
            strstr(lower_path, "\\syswow64\\") ||
            strstr(lower_path, "\\sysnative\\") ||
            strstr(lower_path, "\\windows\\winsxs\\")) continue;

        char* name = strrchr(p, '\\');
        if (!name) continue;
        name++;

        if (already_copied(name, copied, *copied_count)) continue;

        assert(*copied_count < 1024);

        strncpy(copied[*copied_count], name, 259);
        copied[*copied_count][259] = 0;
        (*copied_count)++;

        char out_path[1024];
        snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, name);

        FILE* src = fopen(p, "rb");
        if (!src) continue;
        FILE* dst = fopen(out_path, "wb");
        if (!dst) { fclose(src); continue; }

        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);

        fclose(src);
        fclose(dst);
    }

    _pclose(pipe);
}

static int write_loader(const char* out_dir) {
    static const char loader_code[] =
"#include <windows.h>\n"
"\n"
"int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {\n"
"    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);\n"
"    AddDllDirectory(L\".\\\\dll\");\n"
"\n"
"    STARTUPINFOW si;\n"
"    PROCESS_INFORMATION pi;\n"
"    ZeroMemory(&si, sizeof(si));\n"
"    ZeroMemory(&pi, sizeof(pi));\n"
"    si.cb = sizeof(si);\n"
"\n"
"    wchar_t exePath[MAX_PATH];\n"
"    GetModuleFileNameW(NULL, exePath, MAX_PATH);\n"
"\n"
"    wchar_t* slash = wcsrchr(exePath, L'\\\\');\n"
"    if (slash) *(slash + 1) = 0;\n"
"    wcscat(exePath, L\"dll\\\\amp.exe\");\n"
"\n"
"    wchar_t args[4096];\n"
"    int len = MultiByteToWideChar(CP_ACP, 0, lpCmdLine, -1, NULL, 0);\n"
"    if (len > 1) MultiByteToWideChar(CP_ACP, 0, lpCmdLine, -1, args, 4096);\n"
"    else args[0] = 0;\n"
"\n"
"    wchar_t cmd[4096];\n"
"    if (args[0]) wsprintfW(cmd, L\"\\\"%s\\\" %s\", exePath, args);\n"
"    else wsprintfW(cmd, L\"\\\"%s\\\"\", exePath);\n"
"\n"
"    CreateProcessW(\n"
"        NULL,\n"
"        cmd,\n"
"        NULL,\n"
"        NULL,\n"
"        FALSE,\n"
"        CREATE_NO_WINDOW,\n"
"        NULL,\n"
"        NULL,\n"
"        &si,\n"
"        &pi\n"
"    );\n"
"\n"
"    CloseHandle(pi.hThread);\n"
"    CloseHandle(pi.hProcess);\n"
"    return 0;\n"
"}\n";

    char path[1024];
    snprintf(path, sizeof(path), "%s/__loader.c", out_dir);

    FILE* f = fopen(path, "wb");
    if (!f) return -1;

    fwrite(loader_code, 1, sizeof(loader_code) - 1, f);
    fclose(f);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "gcc -mwindows \"%s\" -o \"%s/amp.exe\"",
        path, out_dir);

    int r = system(cmd);

    remove(path);

    return r ? -1 : 0;
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
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "source/config.h", "thirdparty/nob.h", "thirdparty/ascii.h");

    Nob_Cmd cmd = {0};

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

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

#ifdef _WIN32
    char maj[32], min[32], pat[32], ffl[32], ogf[32];
    snprintf(maj, sizeof(maj), "-DAMP_VER_MAJOR=%d", (AMP_VERSION >> 16) & 0xFF);
    snprintf(min, sizeof(min), "-DAMP_VER_MINOR=%d", (AMP_VERSION >> 8) & 0xFF);
    snprintf(pat, sizeof(pat), "-DAMP_VER_PATCH=%d", AMP_VERSION & 0xFF);
    snprintf(ffl, sizeof(ffl), "-DAMP_FILEFLAGS=%d", opt ? 0 : 1);
    snprintf(ogf, sizeof(ogf), "-DAMP_ORIG_FILE=\\\"%s.exe\\\"", dist ? "amp" : OUT_EXE_NAME);
    nob_cmd_append(
        &cmd,
        "windres", "assets/amp.rc", "-O", "coff", "-o", "assets/amp.res",
        maj, min, pat, ffl, ogf
    );
    if (!nob_cmd_run(&cmd)) {
        fprintf(stderr, "Resource compilation failed!\n");
        return 1;
    }

    nob_cmd_append(&cmd, CC);
    if (opt) nob_cmd_append(&cmd, RELEASE_CFLAGS);
    else nob_cmd_append(&cmd, CFLAGS);
    if (dist) nob_cmd_append(&cmd, "-DUSE_SSE2_SIMD=0", "-DDIST=1");
    nob_cmd_append(&cmd,
                    "source/main.c",
                    "assets/amp.res",
                    "-DSDL_MAIN_HANDLED",
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
#elif defined(__linux__)
    nob_cmd_append(&cmd, CC);
    if (opt) nob_cmd_append(&cmd, RELEASE_CFLAGS);
    else nob_cmd_append(&cmd, CFLAGS);
    nob_cmd_append(&cmd,
                    "source/main.c",
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
#else
    nob_cmd_append(&cmd, CC);

    if (opt) nob_cmd_append(&cmd, RELEASE_CFLAGS);
    else nob_cmd_append(&cmd, CFLAGS);

    FILE* fp = popen("brew --prefix", "r");
    char brew_prefix[512] = {0};

    if (fp) {
        fgets(brew_prefix, sizeof(brew_prefix), fp);
        pclose(fp);
    }

    brew_prefix[strcspn(brew_prefix, "\n")] = 0;

    char lib_path[1024];
    snprintf(lib_path, sizeof(lib_path), "%s/lib", brew_prefix);

    nob_cmd_append(&cmd,
        "-L", lib_path,

        "source/main.c",

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
        "-liconv",
        "-lm",

        "-o", OUT_EXE_NAME
    );
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
    snprintf(out_exe, sizeof(out_exe), "%s\\dll\\amp.exe", out_dir);

    system("rmdir /S /Q dist\\assets");
    system("xcopy assets dist\\assets /E /I /Y /Q");

    CopyFileA(exe, out_exe, FALSE);

    char** dlls = calloc(1024, sizeof(char*));
    for (int i = 0; i < 1024; i++) {
        dlls[i] = malloc(260);
    }
    int dll_count = 0;
    copy_deps_ldd(exe, dll_dir, dlls, &dll_count);

    printf("Found %d dlls\n", dll_count);

    for (int i = 0; i < dll_count; i++) {
        free(dlls[i]);
    }
    free(dlls);
    
    write_loader(out_dir);

    system("copy LICENSE dist\\LICENSE.txt /Y");
    system("copy thirdparty\\LICENSE.Iosevka.md dist\\assets\\LICENSE.Iosevka.md /Y");
    system("copy CHANGELOG.md dist\\CHANGELOG.md /Y");
    write_readme(out_dir);

    printf("dist created in %s\n", out_dir);

    if (system("cd dist && 7z a -tzip -mx=9 ..\\dist.zip *")) { puts("Failed to create dist.zip"); return 1; }
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