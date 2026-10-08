/* e1400patch.exe - install, configure and inspect the patch loader without the manager.
 *
 *   e1400patch install            hook the loader into game.ini (non-destructive, previous value saved)
 *   e1400patch uninstall          restore game.ini
 *   e1400patch status             loader state, original build, patches and mods with their compatibility
 *   e1400patch enable <id>        enable a patch or mod (e1400patch.ini [patches])
 *   e1400patch disable <id>
 *   e1400patch check <file> [<target>] [--write <build id>]
 *                                 identify a game file; for an unknown build look up every known symbol by signature and
 *                                 report which patches would work; --write stores a build table for it
 *   e1400patch version
 *
 * The tool lives in <game>/e1400patch/; the game directory is its parent (or [loader] game_dir / E1400PATCH_GAME_DIR). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "e1400core.h"

#define SHIM_ENTRY "e1400patch\\server.dll"

static E1400Paths g_paths;
static E1400BuildSet g_builds;
static char g_game_ini[E1400_PATH];

static void usage(void)
{
    puts("usage: e1400patch install | uninstall | status | enable <id> | disable <id> | check <file> [server|game] "
         "[--write <build id>] | version");
}

/* Build id of a server.dll candidate (NULL when unknown or missing). */
static const E1400Build *identify(const char *path, char sha256[65])
{
    if (e1400_sha256_file(path, sha256)) return NULL;
    return e1400_builds_find(&g_builds, "server", sha256);
}

static int is_shim_entry(const char *value)
{
    return !_stricmp(value, SHIM_ENTRY) || strstr(value, "e1400patch\\server.dll") || strstr(value, "e1400patch/server.dll");
}

static int command_install(void)
{
    char previous[E1400_PATH], candidate[E1400_PATH], fallback[E1400_PATH], sha256[65];
    const char *original;
    const E1400Build *build;
    if (!e1400_path_exists(g_game_ini)) {
        printf("game.ini not found: %s\n", g_game_ini);
        return 1;
    }
    e1400_ini_get(g_game_ini, "Network", "Server", "Server\\Server.DLL", previous, sizeof(previous));
    if (is_shim_entry(previous)) {
        puts("already installed");
        return 0;
    }
    if (!e1400_path_exists(g_paths.config)) {
        /* first installation: start from the shipped defaults (an existing configuration survives updates) */
        char defaults[E1400_PATH];
        e1400_path_join(defaults, sizeof(defaults), g_paths.loader, "e1400patch.ini.default");
        CopyFileA(defaults, g_paths.config, TRUE);
    }
    /* the original: the previous entry when it is a known original, else the default location */
    e1400_path_join(candidate, sizeof(candidate), g_paths.game, previous);
    e1400_path_join(fallback, sizeof(fallback), g_paths.game, "Server\\server.dll");
    if ((build = identify(candidate, sha256)) != NULL) original = previous;
    else if ((build = identify(fallback, sha256)) != NULL) original = "Server\\server.dll";
    else original = e1400_path_exists(candidate) ? previous : "Server\\server.dll";
    if (e1400_ini_set(g_paths.config, "install", "previous_server", previous) ||
        e1400_ini_set(g_paths.config, "loader", "original_server", original) ||
        e1400_ini_set(g_game_ini, "Network", "Server", SHIM_ENTRY)) {
        printf("cannot write %s or %s\n", g_paths.config, g_game_ini);
        return 1;
    }
    printf("installed: game.ini Server=%s (was %s)\n", SHIM_ENTRY, previous);
    printf("original server.dll: %s (%s)\n", original, build ? build->id : "unknown build: patches with build requirements stay off");
    return 0;
}

static int command_uninstall(void)
{
    char current[E1400_PATH], previous[E1400_PATH];
    e1400_ini_get(g_game_ini, "Network", "Server", "", current, sizeof(current));
    if (!is_shim_entry(current)) {
        puts("not installed (game.ini does not use the loader)");
        return 0;
    }
    e1400_ini_get(g_paths.config, "install", "previous_server", "Server\\Server.DLL", previous, sizeof(previous));
    if (e1400_ini_set(g_game_ini, "Network", "Server", previous)) {
        printf("cannot write %s\n", g_game_ini);
        return 1;
    }
    e1400_ini_set(g_paths.config, "install", "previous_server", NULL);
    printf("uninstalled: game.ini Server=%s\n", previous);
    return 0;
}

static void list_kind(const char *title, const char *dir, E1400Target target, const E1400Build *build)
{
    E1400Manifest manifests[E1400_MAX_PATCHES];
    unsigned count = e1400_manifests_scan(manifests, E1400_MAX_PATCHES, dir);
    printf("%s (%s): %u\n", title, dir, count);
    for (unsigned i = 0; i < count; i++) {
        const E1400Manifest *manifest = &manifests[i];
        char why[256] = "";
        int compatible = !(manifest->targets & (1u << target)) || !e1400_manifest_check(manifest, target, build, why, sizeof(why));
        printf("  %-16s %-10s %-8s %s%s%s\n", manifest->id, manifest->version,
               e1400_manifest_enabled(manifest, g_paths.config) ? "enabled" : "disabled", compatible ? "ok" : "incompatible",
               why[0] ? ": " : "", why);
    }
}

static int command_status(void)
{
    char current[E1400_PATH], original[E1400_PATH], sha256[65] = "";
    const E1400Build *build;
    e1400_ini_get(g_game_ini, "Network", "Server", "", current, sizeof(current));
    e1400_resolve_original_server(&g_paths, original, sizeof(original));
    build = identify(original, sha256);
    printf("e1400patch %s (patch API %u)\n", E1400PATCH_VERSION, E1400_PATCH_API_VERSION);
    printf("game:      %s\n", g_paths.game);
    printf("installed: %s (game.ini Server=%s)\n", is_shim_entry(current) ? "yes" : "no", current);
    printf("original:  %s\n           %s, build %s\n", original, sha256[0] ? sha256 : "missing", build ? build->id : "unknown");
    printf("builds:    %u table(s) in %s\n", g_builds.count, g_paths.builds);
    list_kind("patches", g_paths.patches, E1400_TARGET_SERVER, build);
    list_kind("mods", g_paths.mods, E1400_TARGET_SERVER, build);
    return 0;
}

static int command_enable(const char *id, int enable)
{
    if (e1400_ini_set(g_paths.config, "patches", id, enable ? "1" : "0")) {
        printf("cannot write %s\n", g_paths.config);
        return 1;
    }
    printf("%s %s\n", id, enable ? "enabled" : "disabled");
    return 0;
}

/* Identifies a file; for an unknown build every symbol of the known builds of the target is looked up by signature. */
static int command_check(const char *file, const char *target_name, const char *write_id)
{
    char sha256[65];
    E1400Target target = E1400_TARGET_SERVER;
    E1400Image image;
    E1400Build found = {0};
    E1400Symbol *symbols;
    unsigned capacity = 0, unique = 0, ambiguous = 0;
    const E1400Build *known;
    if (target_name && e1400_target_parse(target_name, &target)) {
        printf("unknown target %s\n", target_name);
        return 2;
    }
    if (e1400_sha256_file(file, sha256) || e1400_image_load(file, &image)) {
        printf("cannot read %s as a 32-bit PE file\n", file);
        return 1;
    }
    known = e1400_builds_find(&g_builds, e1400_target_name(target), sha256);
    printf("%s\nsha256 %s\n", file, sha256);
    if (known) {
        printf("known build: %s (%s), %u symbols\n", known->id, known->description, known->symbol_count);
        list_kind("patches", g_paths.patches, target, known);
        e1400_image_free(&image);
        return 0;
    }
    for (unsigned b = 0; b < g_builds.count; b++)
        if (!_stricmp(g_builds.builds[b].target, e1400_target_name(target))) capacity += g_builds.builds[b].symbol_count;
    symbols = calloc(capacity ? capacity : 1, sizeof(E1400Symbol));
    if (!symbols) return 1;
    printf("unknown build; searching %u known symbols by signature\n", capacity);
    for (unsigned b = 0; b < g_builds.count; b++) {
        const E1400Build *build = &g_builds.builds[b];
        if (_stricmp(build->target, e1400_target_name(target))) continue;
        for (unsigned s = 0; s < build->symbol_count; s++) {
            const E1400Symbol *symbol = &build->symbols[s];
            uint32_t at[2];
            unsigned hits;
            if (!symbol->signature || e1400_build_symbol(&found, symbol->name)) continue;
            found.symbols = symbols;
            hits = e1400_image_find_signature(&image, symbol->signature, at, 2);
            if (hits == 1) {
                symbols[found.symbol_count++] = (E1400Symbol){"", at[0], symbol->signature};
                snprintf(symbols[found.symbol_count - 1].name, E1400_NAME, "%s", symbol->name);
                unique++;
            } else if (hits > 1) {
                ambiguous++;
                printf("  ambiguous: %s (%u matches)\n", symbol->name, hits);
            } else {
                printf("  missing:   %s\n", symbol->name);
            }
        }
    }
    printf("found %u symbol(s) uniquely, %u ambiguous\n", unique, ambiguous);
    found.symbols = symbols;
    snprintf(found.id, sizeof(found.id), "%s", write_id ? write_id : "candidate");
    list_kind("patches with this candidate table", g_paths.patches, target, &found);
    if (write_id) {
        char path[E1400_PATH], value[64];
        const char *name = strrchr(file, '\\');
        snprintf(path, sizeof(path), "%s\\%s.ini", g_paths.builds, write_id);
        e1400_ini_set(path, "build", "id", write_id);
        e1400_ini_set(path, "build", "target", e1400_target_name(target));
        e1400_ini_set(path, "build", "file", name ? name + 1 : file);
        e1400_ini_set(path, "build", "sha256", sha256);
        e1400_ini_set(path, "build", "description", "generated by e1400patch check (signature matches only; review before release)");
        for (unsigned s = 0; s < found.symbol_count; s++) {
            snprintf(value, sizeof(value), "0x%08x,0x%08x", symbols[s].rva, symbols[s].signature);
            e1400_ini_set(path, "symbols", symbols[s].name, value);
        }
        printf("written: %s\n", path);
    }
    free(symbols);
    e1400_image_free(&image);
    return 0;
}

int main(int argc, char **argv)
{
    char loader_dir[E1400_PATH];
    int result;
    if (argc < 2) {
        usage();
        return 2;
    }
    e1400_module_dir(NULL, loader_dir, sizeof(loader_dir));
    e1400_paths_init(&g_paths, loader_dir);
    e1400_path_join(g_game_ini, sizeof(g_game_ini), g_paths.game, "game.ini");
    e1400_builds_load(&g_builds, g_paths.builds);
    if (!strcmp(argv[1], "install")) result = command_install();
    else if (!strcmp(argv[1], "uninstall")) result = command_uninstall();
    else if (!strcmp(argv[1], "status")) result = command_status();
    else if (!strcmp(argv[1], "enable") && argc == 3) result = command_enable(argv[2], 1);
    else if (!strcmp(argv[1], "disable") && argc == 3) result = command_enable(argv[2], 0);
    else if (!strcmp(argv[1], "check") && argc >= 3) {
        const char *target = NULL, *write_id = NULL;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--write") && i + 1 < argc) write_id = argv[++i];
            else target = argv[i];
        }
        result = command_check(argv[2], target, write_id);
    } else if (!strcmp(argv[1], "version")) {
        printf("e1400patch %s (patch API %u)\n", E1400PATCH_VERSION, E1400_PATCH_API_VERSION);
        result = 0;
    } else {
        usage();
        result = 2;
    }
    e1400_builds_free(&g_builds);
    return result;
}
