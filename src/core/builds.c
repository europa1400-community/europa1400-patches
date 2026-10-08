/* Build tables (builds/<id>.ini): identify a game file by SHA-256 and name its functions.
 *
 *   [build]
 *   id=server-de-2.06
 *   target=server
 *   file=server.dll
 *   sha256=<64 hex digits>
 *   description=...
 *   [symbols]
 *   <name>=<rva>,<signature>      ; signature = FNV-1a of the first 16 file bytes at rva (detects moved code in other builds)
 */
#include "e1400core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const TARGET_NAMES[E1400_TARGET_COUNT] = {"server", "game"};

const char *e1400_target_name(E1400Target target)
{
    return (unsigned)target < E1400_TARGET_COUNT ? TARGET_NAMES[target] : "?";
}

int e1400_target_parse(const char *name, E1400Target *target)
{
    for (unsigned i = 0; i < E1400_TARGET_COUNT; i++)
        if (!_stricmp(name, TARGET_NAMES[i])) {
            *target = (E1400Target)i;
            return 0;
        }
    return -1;
}

int e1400_build_load(E1400Build *build, const char *file)
{
    char *section, *entry;
    DWORD size = 1 << 16, used;
    memset(build, 0, sizeof(*build));
    e1400_ini_get(file, "build", "id", "", build->id, sizeof(build->id));
    e1400_ini_get(file, "build", "target", "", build->target, sizeof(build->target));
    e1400_ini_get(file, "build", "file", "", build->file, sizeof(build->file));
    e1400_ini_get(file, "build", "sha256", "", build->sha256, sizeof(build->sha256));
    e1400_ini_get(file, "build", "description", "", build->description, sizeof(build->description));
    if (!build->id[0] || !build->target[0] || strlen(build->sha256) != 64) return -1;
    _strlwr(build->sha256);
    for (;;) {
        if (!(section = malloc(size))) return -1;
        used = GetPrivateProfileSectionA("symbols", section, size, file);
        if (used < size - 2) break;
        free(section);
        size *= 2;
    }
    for (entry = section; *entry; entry += strlen(entry) + 1) build->symbol_count++;
    build->symbols = calloc(build->symbol_count ? build->symbol_count : 1, sizeof(E1400Symbol));
    if (!build->symbols) {
        free(section);
        return -1;
    }
    build->symbol_count = 0;
    for (entry = section; *entry; entry += strlen(entry) + 1) {
        char *equals = strchr(entry, '='), *end;
        E1400Symbol *symbol = &build->symbols[build->symbol_count];
        if (!equals || equals == entry || (size_t)(equals - entry) >= sizeof(symbol->name)) continue;
        memcpy(symbol->name, entry, (size_t)(equals - entry));
        symbol->name[equals - entry] = 0;
        symbol->rva = (uint32_t)strtoul(equals + 1, &end, 0);
        symbol->signature = *end == ',' ? (uint32_t)strtoul(end + 1, NULL, 0) : 0;
        if (symbol->rva) build->symbol_count++;
    }
    free(section);
    return 0;
}

int e1400_builds_load(E1400BuildSet *set, const char *dir)
{
    char pattern[E1400_PATH], file[E1400_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;
    memset(set, 0, sizeof(*set));
    e1400_path_join(pattern, sizeof(pattern), dir, "*.ini");
    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        if (set->count == E1400_MAX_BUILDS) break;
        e1400_path_join(file, sizeof(file), dir, found.cFileName);
        if (!e1400_build_load(&set->builds[set->count], file)) set->count++;
    } while (FindNextFileA(search, &found));
    FindClose(search);
    return 0;
}

void e1400_builds_free(E1400BuildSet *set)
{
    for (unsigned i = 0; i < set->count; i++) free(set->builds[i].symbols);
    memset(set, 0, sizeof(*set));
}

const E1400Build *e1400_builds_find(const E1400BuildSet *set, const char *target, const char *sha256)
{
    for (unsigned i = 0; i < set->count; i++)
        if (!_stricmp(set->builds[i].target, target) && !_stricmp(set->builds[i].sha256, sha256)) return &set->builds[i];
    return NULL;
}

const E1400Build *e1400_builds_by_id(const E1400BuildSet *set, const char *id)
{
    for (unsigned i = 0; i < set->count; i++)
        if (!_stricmp(set->builds[i].id, id)) return &set->builds[i];
    return NULL;
}

const E1400Symbol *e1400_build_symbol(const E1400Build *build, const char *name)
{
    if (!build) return NULL;
    for (unsigned i = 0; i < build->symbol_count; i++)
        if (!strcmp(build->symbols[i].name, name)) return &build->symbols[i];
    return NULL;
}
