/* Patch manifests (<dir>/patch.ini):
 *
 *   [patch]
 *   id=netfix
 *   name=Network fixes
 *   version=0.1.0
 *   kind=patch                  ; patch | mod
 *   api=1                       ; E1400_PATCH_API_VERSION the module was built against
 *   module=netfix.dll
 *   targets=server              ; server, game
 *   enabled=1                   ; default when e1400patch.ini [patches] does not decide
 *   order=100                   ; application order (lower first), then id
 *   requires=                   ; other patch ids that must be enabled
 *   conflicts=                  ; patch ids that must not be enabled
 *   description=...
 *   [builds]
 *   server=server-de-2.06       ; supported builds per target ("*" = any build; only for patches without symbols)
 *   [symbols]
 *   server=srv_RecvFromClient,srv_SendPending
 *   [settings]
 *   <key>=<default>
 */
#include "e1400core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int e1400_manifest_load(E1400Manifest *manifest, const char *dir)
{
    char targets[E1400_LIST];
    memset(manifest, 0, sizeof(*manifest));
    snprintf(manifest->dir, sizeof(manifest->dir), "%s", dir);
    e1400_path_join(manifest->file, sizeof(manifest->file), dir, "patch.ini");
    if (!e1400_path_exists(manifest->file)) return -1;
    e1400_ini_get(manifest->file, "patch", "id", "", manifest->id, sizeof(manifest->id));
    e1400_ini_get(manifest->file, "patch", "name", manifest->id, manifest->name, sizeof(manifest->name));
    e1400_ini_get(manifest->file, "patch", "version", "0.0.0", manifest->version, sizeof(manifest->version));
    e1400_ini_get(manifest->file, "patch", "kind", "patch", manifest->kind, sizeof(manifest->kind));
    e1400_ini_get(manifest->file, "patch", "module", "", manifest->module, sizeof(manifest->module));
    e1400_ini_get(manifest->file, "patch", "description", "", manifest->description, sizeof(manifest->description));
    e1400_ini_get(manifest->file, "patch", "requires", "", manifest->requires, sizeof(manifest->requires));
    e1400_ini_get(manifest->file, "patch", "conflicts", "", manifest->conflicts, sizeof(manifest->conflicts));
    e1400_ini_get(manifest->file, "patch", "targets", "", targets, sizeof(targets));
    manifest->api = (uint32_t)e1400_ini_get_int(manifest->file, "patch", "api", 0);
    manifest->enabled_default = e1400_ini_get_int(manifest->file, "patch", "enabled", 1);
    manifest->order = e1400_ini_get_int(manifest->file, "patch", "order", 100);
    for (unsigned t = 0; t < E1400_TARGET_COUNT; t++) {
        const char *name = e1400_target_name((E1400Target)t);
        if (e1400_list_contains(targets, name) && strcmp(targets, "*")) manifest->targets |= 1u << t;
        e1400_ini_get(manifest->file, "builds", name, "", manifest->builds[t], sizeof(manifest->builds[t]));
        e1400_ini_get(manifest->file, "symbols", name, "", manifest->symbols[t], sizeof(manifest->symbols[t]));
    }
    if (!manifest->id[0] || !manifest->module[0] || !manifest->targets) return -1;
    return 0;
}

static int compare_manifests(const void *a, const void *b)
{
    const E1400Manifest *x = a, *y = b;
    if (x->order != y->order) return x->order < y->order ? -1 : 1;
    return _stricmp(x->id, y->id);
}

unsigned e1400_manifests_scan(E1400Manifest *out, unsigned max, const char *dir)
{
    char pattern[E1400_PATH], sub[E1400_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;
    unsigned count = 0;
    e1400_path_join(pattern, sizeof(pattern), dir, "*");
    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || found.cFileName[0] == '.') continue;
        if (count == max) break;
        e1400_path_join(sub, sizeof(sub), dir, found.cFileName);
        if (!e1400_manifest_load(&out[count], sub)) count++;
    } while (FindNextFileA(search, &found));
    FindClose(search);
    qsort(out, count, sizeof(*out), compare_manifests);
    return count;
}

int e1400_manifest_enabled(const E1400Manifest *manifest, const char *config)
{
    return e1400_ini_get_int(config, "patches", manifest->id, manifest->enabled_default) != 0;
}

int e1400_manifest_check(const E1400Manifest *manifest, E1400Target target, const E1400Build *build, char *why, size_t size)
{
    const char *symbols = manifest->symbols[target];
    if (!(manifest->targets & (1u << target))) {
        snprintf(why, size, "does not target %s", e1400_target_name(target));
        return -1;
    }
    if (strcmp(manifest->builds[target], "*")) {
        if (!build) {
            snprintf(why, size, "unknown %s build", e1400_target_name(target));
            return -1;
        }
        if (!e1400_list_contains(manifest->builds[target], build->id)) {
            snprintf(why, size, "build %s not supported (supports: %s)", build->id, manifest->builds[target]);
            return -1;
        }
    } else if (symbols[0] && !build) {
        snprintf(why, size, "uses symbols but the %s build is unknown", e1400_target_name(target));
        return -1;
    }
    /* every declared symbol must be in the build table */
    while (*symbols) {
        char name[E1400_NAME];
        size_t length;
        while (*symbols == ',' || *symbols == ' ') symbols++;
        length = strcspn(symbols, ", ");
        if (!length) break;
        snprintf(name, sizeof(name), "%.*s", (int)length, symbols);
        if (!e1400_build_symbol(build, name)) {
            snprintf(why, size, "symbol %s missing in build %s", name, build ? build->id : "?");
            return -1;
        }
        symbols += length;
    }
    why[0] = 0;
    return 0;
}
