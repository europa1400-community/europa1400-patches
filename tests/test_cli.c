/* Install/uninstall of e1400patch.exe against a scratch game directory.
 *
 *   test_cli <e1400patch.exe> <fixture_target.dll> <work dir>
 *
 * The scratch game has a game.ini with a quoted original entry and Server\server.dll (the test target, identified through
 * a generated build table). install must point game.ini at the shim and remember the previous entry, a second install
 * must change nothing, enable/disable must write the configuration, uninstall must restore game.ini. */
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

static int run(const char *exe, const char *arguments)
{
    char command[2 * E1400_PATH];
    STARTUPINFOA startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    DWORD code = 99;
    snprintf(command, sizeof(command), "\"%s\" %s", exe, arguments);
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) return 98;
    WaitForSingleObject(process.hProcess, 30000);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return (int)code;
}

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file) {
        fputs(text, file);
        fclose(file);
    }
}

int main(int argc, char **argv)
{
    char game[E1400_PATH], server[E1400_PATH], ini[E1400_PATH], config[E1400_PATH], builds[E1400_PATH], table[E1400_PATH];
    char variable[E1400_PATH + 32], value[E1400_PATH], sha256[65], dir[E1400_PATH];
    if (argc != 4) {
        puts("usage: test_cli <e1400patch.exe> <fixture_target.dll> <work dir>");
        return 2;
    }
    CreateDirectoryA(argv[3], NULL);
    e1400_path_join(game, sizeof(game), argv[3], "cli-game");
    CreateDirectoryA(game, NULL);
    e1400_path_join(dir, sizeof(dir), game, "Server");
    CreateDirectoryA(dir, NULL);
    e1400_path_join(server, sizeof(server), dir, "server.dll");
    CopyFileA(argv[2], server, FALSE);
    e1400_path_join(ini, sizeof(ini), game, "game.ini");
    write_file(ini, "[General]\nGamePath=\"\"\n[Network]\nServer=\"Server\\Server.DLL\"\nPort=7531\n");
    e1400_path_join(config, sizeof(config), game, "cli-e1400patch.ini");
    DeleteFileA(config);
    e1400_path_join(builds, sizeof(builds), game, "builds");
    CreateDirectoryA(builds, NULL);
    e1400_path_join(table, sizeof(table), builds, "cli-target.ini");
    CHECK(!e1400_sha256_file(server, sha256));
    e1400_ini_set(table, "build", "id", "cli-target");
    e1400_ini_set(table, "build", "target", "server");
    e1400_ini_set(table, "build", "sha256", sha256);

    snprintf(variable, sizeof(variable), "E1400PATCH_GAME_DIR=%s", game);
    _putenv(variable);
    snprintf(variable, sizeof(variable), "E1400PATCH_CONFIG=%s", config);
    _putenv(variable);
    snprintf(variable, sizeof(variable), "E1400PATCH_BUILDS_DIR=%s", builds);
    _putenv(variable);

    CHECK(run(argv[1], "install") == 0);
    e1400_ini_get(ini, "Network", "Server", "", value, sizeof(value));
    CHECK(!strcmp(value, "e1400patch\\server.dll"));
    e1400_ini_get(config, "install", "previous_server", "", value, sizeof(value));
    CHECK(!strcmp(value, "Server\\Server.DLL"));
    e1400_ini_get(config, "loader", "original_server", "", value, sizeof(value));
    CHECK(!strcmp(value, "Server\\Server.DLL"));
    e1400_ini_get(ini, "Network", "Port", "", value, sizeof(value));
    CHECK(!strcmp(value, "7531"));

    CHECK(run(argv[1], "install") == 0); /* already installed: unchanged */
    e1400_ini_get(config, "install", "previous_server", "", value, sizeof(value));
    CHECK(!strcmp(value, "Server\\Server.DLL"));

    CHECK(run(argv[1], "disable netfix") == 0);
    CHECK(e1400_ini_get_int(config, "patches", "netfix", 1) == 0);
    CHECK(run(argv[1], "enable netfix") == 0);
    CHECK(e1400_ini_get_int(config, "patches", "netfix", 0) == 1);
    CHECK(run(argv[1], "status") == 0);

    CHECK(run(argv[1], "uninstall") == 0);
    e1400_ini_get(ini, "Network", "Server", "", value, sizeof(value));
    CHECK(!strcmp(value, "Server\\Server.DLL"));
    CHECK(run(argv[1], "uninstall") == 0); /* not installed: no change */
    e1400_ini_get(ini, "Network", "Server", "", value, sizeof(value));
    CHECK(!strcmp(value, "Server\\Server.DLL"));

    CHECK(run(argv[1], "nonsense") == 2);
    printf("%d checks, %d failures: %s\n", g_checks, g_failures, g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
