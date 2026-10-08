/* Core and loader tests without game files.
 *
 *   test_core <e1400patch.dll> <fixture_target.dll> <fixture_patch.dll> <fixture_failing_patch.dll> <work dir>
 *
 * Unit tests of the core (lists, hashes, manifests, build tables), then an end-to-end run of the loader against the test
 * target: build identification, a patch hooking a function by symbol and an import, user settings, a patch for another
 * build that must not be applied, a disabled patch, a failing patch whose hooks must be removed, and detach. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "e1400core.h"

static int g_failures, g_checks;

#define CHECK(condition)                                                                                                    \
    do {                                                                                                                    \
        g_checks++;                                                                                                         \
        if (!(condition)) {                                                                                                 \
            g_failures++;                                                                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                     \
        }                                                                                                                   \
    } while (0)

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file) {
        fputs(text, file);
        fclose(file);
    }
}

static void make_dir(const char *path)
{
    CreateDirectoryA(path, NULL);
}

static void unit_tests(const char *work)
{
    char path[E1400_PATH], sha256[65], text[64];
    E1400Manifest manifest;
    E1400Build build;

    CHECK(e1400_list_contains("a, netfix ,b", "NETFIX"));
    CHECK(!e1400_list_contains("a,netfixes", "netfix"));
    CHECK(e1400_list_contains("*", "anything"));
    CHECK(!e1400_list_contains("", "x"));
    CHECK(e1400_fnv1a("a", 1) == 0xe40c292cu);

    e1400_path_join(path, sizeof(path), work, "abc.txt");
    write_file(path, "abc");
    CHECK(!e1400_sha256_file(path, sha256));
    CHECK(!strcmp(sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    e1400_path_join(path, sizeof(path), work, "manifest");
    make_dir(path);
    e1400_path_join(text, sizeof(text), "manifest", "patch.ini");
    {
        char file[E1400_PATH];
        e1400_path_join(file, sizeof(file), work, text);
        write_file(file, "[patch]\nid=demo\nversion=1.2.3\napi=1\nmodule=demo.dll\ntargets=server, game\nrequires=base\n"
                         "[builds]\nserver=b1,b2\n[symbols]\nserver=alpha,beta\n");
        CHECK(!e1400_manifest_load(&manifest, path));
        CHECK(!strcmp(manifest.id, "demo") && !strcmp(manifest.version, "1.2.3") && manifest.api == 1);
        CHECK(manifest.targets == ((1u << E1400_TARGET_SERVER) | (1u << E1400_TARGET_GAME)));
        CHECK(manifest.enabled_default == 1 && manifest.order == 100 && !strcmp(manifest.requires, "base"));

        e1400_path_join(file, sizeof(file), work, "b1.ini");
        write_file(file, "[build]\nid=b1\ntarget=server\nfile=x.dll\n"
                         "sha256=BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\n"
                         "[symbols]\nalpha=0x1000,0x12345678\nbeta=0x2000\n");
        CHECK(!e1400_build_load(&build, file));
        CHECK(build.symbol_count == 2 && !strcmp(build.sha256, sha256));
        CHECK(e1400_build_symbol(&build, "alpha")->rva == 0x1000 && e1400_build_symbol(&build, "alpha")->signature == 0x12345678u);
        {
            char why[256];
            CHECK(!e1400_manifest_check(&manifest, E1400_TARGET_SERVER, &build, why, sizeof(why)));
            build.symbol_count = 1; /* beta missing */
            CHECK(e1400_manifest_check(&manifest, E1400_TARGET_SERVER, &build, why, sizeof(why)) && strstr(why, "beta"));
            CHECK(e1400_manifest_check(&manifest, E1400_TARGET_SERVER, NULL, why, sizeof(why)));
        }
        free(build.symbols);
    }
}

/* Writes a build table for the test target (sha256 of the file, symbol RVAs from its exports, signatures from the file). */
static int write_target_build(const char *builds, const char *target_path, HMODULE target)
{
    static const char *const names[] = {"target_add", "target_mul", "target_ticks"};
    char file[E1400_PATH], sha256[65], value[64];
    E1400Image image;
    if (e1400_sha256_file(target_path, sha256) || e1400_image_load(target_path, &image)) return -1;
    e1400_path_join(file, sizeof(file), builds, "test-target.ini");
    DeleteFileA(file);
    e1400_ini_set(file, "build", "id", "test-target");
    e1400_ini_set(file, "build", "target", "server");
    e1400_ini_set(file, "build", "file", "fixture_target.dll");
    e1400_ini_set(file, "build", "sha256", sha256);
    for (unsigned i = 0; i < 3; i++) {
        uint32_t rva = (uint32_t)((uint8_t *)GetProcAddress(target, names[i]) - (uint8_t *)target);
        snprintf(value, sizeof(value), "0x%08x,0x%08x", rva, e1400_image_signature(&image, rva));
        e1400_ini_set(file, "symbols", names[i], value);
    }
    /* a second build the "other" patch asks for (never matches) */
    e1400_path_join(file, sizeof(file), builds, "other.ini");
    write_file(file, "[build]\nid=other\ntarget=server\nfile=x.dll\n"
                     "sha256=0000000000000000000000000000000000000000000000000000000000000000\n");
    e1400_image_free(&image);
    return 0;
}

static void install_patch(const char *patches, const char *id, const char *dll, const char *manifest)
{
    char dir[E1400_PATH], file[E1400_PATH];
    const char *name = dll;
    for (const char *p = dll; *p; p++)
        if (*p == '\\' || *p == '/') name = p + 1;
    e1400_path_join(dir, sizeof(dir), patches, id);
    make_dir(dir);
    e1400_path_join(file, sizeof(file), dir, "patch.ini");
    write_file(file, manifest);
    e1400_path_join(file, sizeof(file), dir, name);
    CopyFileA(dll, file, FALSE);
}

static int log_contains(const char *path, const char *text)
{
    FILE *file = fopen(path, "r");
    char line[1024];
    int found = 0;
    if (!file) return 0;
    while (!found && fgets(line, sizeof(line), file)) found = strstr(line, text) != NULL;
    fclose(file);
    return found;
}

static void loader_tests(const char *loader_path, const char *target_path, const char *patch_dll, const char *failing_dll,
                         const char *work)
{
    char root[E1400_PATH], patches[E1400_PATH], mods[E1400_PATH], builds[E1400_PATH], config[E1400_PATH], log[E1400_PATH];
    char variable[E1400_PATH + 32];
    HMODULE loader, target;
    E1400AttachFn attach;
    E1400DetachFn detach;
    int(__cdecl *add)(int, int);
    int(__cdecl *mul)(int, int);
    DWORD(__cdecl *ticks)(void);

    e1400_path_join(root, sizeof(root), work, "game");
    make_dir(root);
    e1400_path_join(patches, sizeof(patches), root, "patches");
    e1400_path_join(mods, sizeof(mods), root, "mods");
    e1400_path_join(builds, sizeof(builds), root, "builds");
    e1400_path_join(config, sizeof(config), root, "e1400patch.ini");
    e1400_path_join(log, sizeof(log), root, "e1400patch.log");
    make_dir(patches);
    make_dir(mods);
    make_dir(builds);
    DeleteFileA(log);

    target = LoadLibraryA(target_path);
    CHECK(target != NULL);
    if (!target) return;
    add = (int(__cdecl *)(int, int))GetProcAddress(target, "target_add");
    mul = (int(__cdecl *)(int, int))GetProcAddress(target, "target_mul");
    ticks = (DWORD(__cdecl *)(void))GetProcAddress(target, "target_ticks");
    CHECK(!write_target_build(builds, target_path, target));

    install_patch(patches, "fixture", patch_dll,
                  "[patch]\nid=fixture\nversion=1.0.0\napi=1\nmodule=fixture_patch.dll\ntargets=server\n"
                  "[builds]\nserver=test-target\n[symbols]\nserver=target_add\n[settings]\nbonus=1\n");
    install_patch(patches, "failing", failing_dll,
                  "[patch]\nid=failing\napi=1\nmodule=fixture_failing_patch.dll\ntargets=server\norder=200\n"
                  "[builds]\nserver=test-target\n[symbols]\nserver=target_mul\n");
    install_patch(patches, "other", patch_dll,
                  "[patch]\nid=other\napi=1\nmodule=fixture_patch.dll\ntargets=server\n[builds]\nserver=other\n");
    install_patch(patches, "disabled", patch_dll,
                  "[patch]\nid=disabled\napi=1\nmodule=fixture_patch.dll\ntargets=server\n[builds]\nserver=test-target\n");
    install_patch(patches, "future", patch_dll,
                  "[patch]\nid=future\napi=99\nmodule=fixture_patch.dll\ntargets=server\n[builds]\nserver=*\n");
    write_file(config, "[patches]\ndisabled=0\n[fixture]\nbonus=1000\n");

#define SETENV(name, value)                                                                                                 \
    snprintf(variable, sizeof(variable), "%s=%s", name, value);                                                             \
    _putenv(variable)
    SETENV("E1400PATCH_CONFIG", config);
    SETENV("E1400PATCH_PATCHES_DIR", patches);
    SETENV("E1400PATCH_MODS_DIR", mods);
    SETENV("E1400PATCH_BUILDS_DIR", builds);
    SETENV("E1400PATCH_LOG", log);
    SETENV("E1400PATCH_GAME_DIR", root);

    loader = LoadLibraryA(loader_path);
    CHECK(loader != NULL);
    if (!loader) return;
    attach = (E1400AttachFn)GetProcAddress(loader, E1400_ATTACH_EXPORT);
    detach = (E1400DetachFn)GetProcAddress(loader, E1400_DETACH_EXPORT);
    CHECK(attach && detach);
    if (!attach || !detach) return;

    CHECK(add(2, 3) == 5 && mul(3, 4) == 12);
    CHECK(attach(E1400_TARGET_SERVER, target, target_path) == 0);
    CHECK(add(2, 3) == 1005);      /* hooked by symbol, setting from e1400patch.ini */
    CHECK(ticks() == 4242);        /* import hook */
    CHECK(mul(3, 4) == 12);        /* failing patch: hook removed again */
    CHECK(attach(E1400_TARGET_SERVER, target, target_path) == 0); /* second attach of the same module: no change */
    CHECK(add(2, 3) == 1005);
    CHECK(log_contains(log, "build test-target"));
    CHECK(log_contains(log, "fixture: applied to server"));
    CHECK(log_contains(log, "failing: apply to server failed (7), hooks removed"));
    CHECK(log_contains(log, "other: not applied to server: build test-target not supported"));
    CHECK(log_contains(log, "patch disabled 0.0.0: disabled") || log_contains(log, "disabled: disabled"));
    CHECK(log_contains(log, "skip future: needs patch API 99"));

    detach(E1400_TARGET_SERVER);
    CHECK(add(2, 3) == 5);
    CHECK(ticks() != 4242);
    CHECK(log_contains(log, "fixture: released"));
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        puts("usage: test_core <e1400patch.dll> <fixture_target.dll> <fixture_patch.dll> <fixture_failing_patch.dll> <work dir>");
        return 2;
    }
    CreateDirectoryA(argv[5], NULL);
    unit_tests(argv[5]);
    loader_tests(argv[1], argv[2], argv[3], argv[4], argv[5]);
    printf("%d checks, %d failures: %s\n", g_checks, g_failures, g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
