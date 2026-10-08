/* e1400patch core: shared by the loader DLL (e1400patch.dll), the command line tool (e1400patch.exe) and the tests.
 * Not part of the patch API (include/e1400patch/patch_api.h). */
#pragma once

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "e1400patch/patch_api.h"

#ifndef E1400PATCH_VERSION
#define E1400PATCH_VERSION "0.0.0-dev"
#endif

enum {
    E1400_PATH = 520,
    E1400_NAME = 64,
    E1400_LIST = 512,
    E1400_MAX_BUILDS = 64,
    E1400_MAX_PATCHES = 64,
    E1400_SIGNATURE_BYTES = 16 /* bytes at a symbol whose FNV-1a hash identifies the code (no game bytes are stored) */
};

/* ---- paths and configuration ------------------------------------------------------------------------------------------ */

/* Layout: <game>/e1400patch/ (loader, e1400patch.ini, builds/, logs/), <game>/patches/<id>/, <game>/mods/<id>/.
 * Environment overrides (development, tests): E1400PATCH_CONFIG, E1400PATCH_GAME_DIR, E1400PATCH_PATCHES_DIR, E1400PATCH_MODS_DIR,
 * E1400PATCH_BUILDS_DIR, E1400PATCH_LOG, E1400PATCH_ORIGINAL_SERVER. */
typedef struct E1400Paths {
    char loader[E1400_PATH];  /* directory of the loader files */
    char config[E1400_PATH];  /* <loader>/e1400patch.ini */
    char game[E1400_PATH];    /* game directory (parent of <loader> unless configured) */
    char patches[E1400_PATH]; /* <game>/patches */
    char mods[E1400_PATH];    /* <game>/mods */
    char builds[E1400_PATH];  /* <loader>/builds */
    char log[E1400_PATH];     /* <loader>/logs/e1400patch.log */
} E1400Paths;

int e1400_paths_init(E1400Paths *paths, const char *loader_dir);
/* Directory of a module (no trailing separator). */
void e1400_module_dir(HMODULE module, char *out, size_t size);
/* dir + "\" + name, or name itself when it is absolute. */
void e1400_path_join(char *out, size_t size, const char *dir, const char *name);
int e1400_path_exists(const char *path);
int e1400_path_is_absolute(const char *path);

/* INI access (Windows profile API, ANSI files like the game's game.ini). */
void e1400_ini_get(const char *file, const char *section, const char *key, const char *fallback, char *out, size_t size);
int e1400_ini_get_int(const char *file, const char *section, const char *key, int fallback);
int e1400_ini_set(const char *file, const char *section, const char *key, const char *value); /* value NULL deletes */
/* Comma separated list membership (case insensitive, spaces ignored). "*" matches everything. */
int e1400_list_contains(const char *list, const char *item);

/* ---- log -------------------------------------------------------------------------------------------------------------- */

void e1400_log_open(const char *path);
void e1400_log(const char *format, ...);
void e1400_log_v(const char *prefix, const char *format, va_list args);
void e1400_log_close(void);

/* ---- hashing and PE images -------------------------------------------------------------------------------------------- */

int e1400_sha256_file(const char *path, char hex[65]);
uint32_t e1400_fnv1a(const void *data, size_t size);

/* A PE file read from disk (unrelocated bytes, so signatures do not depend on the load address). */
typedef struct E1400Image {
    uint8_t *data;
    size_t size;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t text_rva, text_size; /* first executable section */
    uint8_t *relocated;           /* bit per RVA: byte of a relocated absolute address */
} E1400Image;

int e1400_image_load(const char *path, E1400Image *image);
void e1400_image_free(E1400Image *image);
/* File bytes of [rva, rva + size) or NULL when the range is not backed by the file. */
const uint8_t *e1400_image_at(const E1400Image *image, uint32_t rva, uint32_t size);
/* Signature at an RVA: FNV-1a of E1400_SIGNATURE_BYTES file bytes, bytes of relocated absolute addresses counted as zero
 * (they differ between builds); 0 when out of range. */
uint32_t e1400_image_signature(const E1400Image *image, uint32_t rva);
/* Every RVA in the code section whose signature matches (count of matches, up to max written). */
unsigned e1400_image_find_signature(const E1400Image *image, uint32_t signature, uint32_t *out, unsigned max);

/* IAT slot of an import of a loaded module ("#n" for an ordinal; also matches an import by name of that ordinal). */
void **e1400_iat_slot(HMODULE module, const char *dll, const char *function);
int e1400_iat_write(void **slot, void *value);

/* ---- build tables ----------------------------------------------------------------------------------------------------- */

typedef struct E1400Symbol {
    char name[E1400_NAME];
    uint32_t rva;
    uint32_t signature;
} E1400Symbol;

typedef struct E1400Build {
    char id[E1400_NAME];
    char target[16];
    char file[E1400_NAME];
    char sha256[65];
    char description[160];
    E1400Symbol *symbols;
    unsigned symbol_count;
} E1400Build;

typedef struct E1400BuildSet {
    E1400Build builds[E1400_MAX_BUILDS];
    unsigned count;
} E1400BuildSet;

int e1400_build_load(E1400Build *build, const char *file);
int e1400_builds_load(E1400BuildSet *set, const char *dir);
void e1400_builds_free(E1400BuildSet *set);
const E1400Build *e1400_builds_find(const E1400BuildSet *set, const char *target, const char *sha256);
const E1400Build *e1400_builds_by_id(const E1400BuildSet *set, const char *id);
const E1400Symbol *e1400_build_symbol(const E1400Build *build, const char *name);
const char *e1400_target_name(E1400Target target);
int e1400_target_parse(const char *name, E1400Target *target);

/* ---- manifests -------------------------------------------------------------------------------------------------------- */

typedef struct E1400Manifest {
    char id[E1400_NAME];
    char name[128];
    char version[32];
    char kind[16]; /* "patch" or "mod" */
    char module[E1400_NAME];
    char description[256];
    char dir[E1400_PATH];
    char file[E1400_PATH];
    uint32_t api;
    unsigned targets;                       /* bit (1 << E1400Target) */
    char builds[E1400_TARGET_COUNT][E1400_LIST];  /* supported build ids per target ("*" = any) */
    char symbols[E1400_TARGET_COUNT][E1400_LIST]; /* symbols the patch uses per target */
    char requires[E1400_LIST];
    char conflicts[E1400_LIST];
    int enabled_default;
    int order;
} E1400Manifest;

int e1400_manifest_load(E1400Manifest *manifest, const char *dir);
/* All manifests in <dir>/<id>/patch.ini, sorted by order, then id. */
unsigned e1400_manifests_scan(E1400Manifest *out, unsigned max, const char *dir);
/* Enabled per e1400patch.ini [patches] <id>=0/1, else the manifest default. */
int e1400_manifest_enabled(const E1400Manifest *manifest, const char *config);
/* Checks a manifest against a build: 0 when the build is listed and all declared symbols are in its table; otherwise a
 * short reason in why. */
int e1400_manifest_check(const E1400Manifest *manifest, E1400Target target, const E1400Build *build, char *why, size_t size);

/* ---- loader runtime (e1400patch.dll) ---------------------------------------------------------------------------------- */

/* Exported by e1400patch.dll for the shims. */
typedef int(__cdecl *E1400AttachFn)(E1400Target target, HMODULE module, const char *path);
typedef void(__cdecl *E1400DetachFn)(E1400Target target);
typedef int(__cdecl *E1400OriginalServerFn)(char *out, size_t size);
#define E1400_ATTACH_EXPORT "e1400_attach"
#define E1400_DETACH_EXPORT "e1400_detach"
#define E1400_ORIGINAL_SERVER_EXPORT "e1400_original_server"

/* Resolves the original server.dll path from the configuration (E1400PATCH_ORIGINAL_SERVER, [loader] original_server,
 * default <game>/Server/server.dll). */
int e1400_resolve_original_server(const E1400Paths *paths, char *out, size_t size);
