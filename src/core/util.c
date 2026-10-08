/* Paths, INI access, lists and the log. */
#include "e1400core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void e1400_module_dir(HMODULE module, char *out, size_t size)
{
    char *slash;
    if (!GetModuleFileNameA(module, out, (DWORD)size)) {
        out[0] = 0;
        return;
    }
    slash = strrchr(out, '\\');
    if (slash) *slash = 0;
}

int e1400_path_is_absolute(const char *path)
{
    return path[0] == '\\' || path[0] == '/' || (path[0] && path[1] == ':');
}

void e1400_path_join(char *out, size_t size, const char *dir, const char *name)
{
    if (e1400_path_is_absolute(name)) snprintf(out, size, "%s", name);
    else snprintf(out, size, "%s\\%s", dir, name);
    for (char *p = out; *p; p++)
        if (*p == '/') *p = '\\';
}

int e1400_path_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void env_or(const char *name, const char *fallback, char *out, size_t size)
{
    DWORD n = GetEnvironmentVariableA(name, out, (DWORD)size);
    if (!n || n >= size) snprintf(out, size, "%s", fallback);
}

int e1400_paths_init(E1400Paths *paths, const char *loader_dir)
{
    char value[E1400_PATH], fallback[E1400_PATH];
    snprintf(paths->loader, sizeof(paths->loader), "%s", loader_dir);
    e1400_path_join(fallback, sizeof(fallback), paths->loader, "e1400patch.ini");
    env_or("E1400PATCH_CONFIG", fallback, paths->config, sizeof(paths->config));

    /* game directory: parent of the loader directory unless configured */
    snprintf(fallback, sizeof(fallback), "%s", paths->loader);
    {
        char *slash = strrchr(fallback, '\\');
        if (slash) *slash = 0;
    }
    e1400_ini_get(paths->config, "loader", "game_dir", "", value, sizeof(value));
    if (value[0]) e1400_path_join(fallback, sizeof(fallback), paths->loader, value);
    env_or("E1400PATCH_GAME_DIR", fallback, paths->game, sizeof(paths->game));

    e1400_ini_get(paths->config, "loader", "patches_dir", "patches", value, sizeof(value));
    e1400_path_join(fallback, sizeof(fallback), paths->game, value);
    env_or("E1400PATCH_PATCHES_DIR", fallback, paths->patches, sizeof(paths->patches));

    e1400_ini_get(paths->config, "loader", "mods_dir", "mods", value, sizeof(value));
    e1400_path_join(fallback, sizeof(fallback), paths->game, value);
    env_or("E1400PATCH_MODS_DIR", fallback, paths->mods, sizeof(paths->mods));

    e1400_path_join(fallback, sizeof(fallback), paths->loader, "builds");
    env_or("E1400PATCH_BUILDS_DIR", fallback, paths->builds, sizeof(paths->builds));

    e1400_path_join(fallback, sizeof(fallback), paths->loader, "logs\\e1400patch.log");
    env_or("E1400PATCH_LOG", fallback, paths->log, sizeof(paths->log));
    return 0;
}

int e1400_resolve_original_server(const E1400Paths *paths, char *out, size_t size)
{
    char value[E1400_PATH];
    if (GetEnvironmentVariableA("E1400PATCH_ORIGINAL_SERVER", value, sizeof(value)) && value[0]) {
        e1400_path_join(out, size, paths->game, value);
        return 0;
    }
    e1400_ini_get(paths->config, "loader", "original_server", "Server\\server.dll", value, sizeof(value));
    e1400_path_join(out, size, paths->game, value);
    return 0;
}

/* ---- INI -------------------------------------------------------------------------------------------------------------- */

void e1400_ini_get(const char *file, const char *section, const char *key, const char *fallback, char *out, size_t size)
{
    GetPrivateProfileStringA(section, key, fallback, out, (DWORD)size, file);
}

int e1400_ini_get_int(const char *file, const char *section, const char *key, int fallback)
{
    char value[64];
    char *end;
    long number;
    e1400_ini_get(file, section, key, "", value, sizeof(value));
    if (!value[0]) return fallback;
    number = strtol(value, &end, 0);
    return end == value ? fallback : (int)number;
}

int e1400_ini_set(const char *file, const char *section, const char *key, const char *value)
{
    return WritePrivateProfileStringA(section, key, value, file) ? 0 : -1;
}

int e1400_list_contains(const char *list, const char *item)
{
    const char *p = list;
    size_t length = strlen(item);
    while (*p) {
        const char *start, *end;
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        start = p;
        while (*p && *p != ',') p++;
        end = p;
        while (end > start && isspace((unsigned char)end[-1])) end--;
        if (end - start == 1 && *start == '*') return 1;
        if ((size_t)(end - start) == length && !_strnicmp(start, item, length)) return 1;
    }
    return 0;
}

/* ---- log -------------------------------------------------------------------------------------------------------------- */

static FILE *g_log;
static CRITICAL_SECTION g_log_lock;
static LONG g_log_ready;

void e1400_log_open(const char *path)
{
    char dir[E1400_PATH];
    char *slash;
    if (InterlockedCompareExchange(&g_log_ready, 1, 0) == 0) InitializeCriticalSection(&g_log_lock);
    if (g_log) return;
    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '\\');
    if (slash) {
        *slash = 0;
        CreateDirectoryA(dir, NULL);
    }
    g_log = fopen(path, "a");
}

void e1400_log_v(const char *prefix, const char *format, va_list args)
{
    SYSTEMTIME now;
    if (!g_log) return;
    GetLocalTime(&now);
    EnterCriticalSection(&g_log_lock);
    fprintf(g_log, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] %s%s", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
            now.wSecond, now.wMilliseconds, GetCurrentThreadId(), prefix ? prefix : "", prefix ? ": " : "");
    vfprintf(g_log, format, args);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_log_lock);
}

void e1400_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    e1400_log_v(NULL, format, args);
    va_end(args);
}

void e1400_log_close(void)
{
    if (!g_log) return;
    EnterCriticalSection(&g_log_lock);
    fclose(g_log);
    g_log = NULL;
    LeaveCriticalSection(&g_log_lock);
}
