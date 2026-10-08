cmake_minimum_required(VERSION 3.25)
# Loader version: E1400PATCH_BUILD_VERSION (set by the release workflow), else the last release tag v<x.y.z> via
# git describe (development builds carry the distance, e.g. 0.1.0-3-gabc1234), else 0.0.0. Read at configure time.
set(E1400PATCH_VERSION "$ENV{E1400PATCH_BUILD_VERSION}")
if(NOT E1400PATCH_VERSION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE E1400PATCH_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REGEX REPLACE "^v" "" E1400PATCH_VERSION "${E1400PATCH_VERSION}")
    endif()
    if(NOT E1400PATCH_VERSION)
        set(E1400PATCH_VERSION "0.0.0")
    endif()
endif()
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" E1400PATCH_NUMERIC_VERSION "${E1400PATCH_VERSION}")
project(e1400patch VERSION ${E1400PATCH_NUMERIC_VERSION} LANGUAGES C)
message(STATUS "e1400patch version ${E1400PATCH_VERSION}")
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${E1400PATCH_VERSION}\n") # read by tools/package.py

# The game is a 32-bit process: everything that is loaded into it must be x86.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure for x86 (scripts/build.ps1, the msvc-x86 presets or an x86 developer prompt).")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
    # static CRT: nothing to redistribute next to the game
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

# Output mirrors the game directory: stage/e1400patch/ (loader), stage/patches/<id>/, stage/mods/<id>/.
set(E1400_STAGE "${CMAKE_BINARY_DIR}/stage" CACHE PATH "Staging tree that mirrors the game directory")
set(E1400_STAGE_LOADER "${E1400_STAGE}/e1400patch")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# MinHook (vendor/minhook, BSD-2-Clause), x86 only
add_library(minhook STATIC
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde32.c)
target_include_directories(minhook PUBLIC vendor/minhook/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /W0)
endif()

# Core shared by the loader, the shims, the command line tool and the tests
add_library(e1400patch_core STATIC
    src/core/util.c
    src/core/image.c
    src/core/iat.c
    src/core/builds.c
    src/core/manifest.c)
target_include_directories(e1400patch_core PUBLIC include src/core)
target_compile_definitions(e1400patch_core PUBLIC E1400PATCH_VERSION="${E1400PATCH_VERSION}")
target_link_libraries(e1400patch_core PUBLIC bcrypt)

# Loader runtime: e1400patch.dll
add_library(e1400patch_loader SHARED src/loader/loader.c)
target_link_libraries(e1400patch_loader PRIVATE e1400patch_core minhook)
set_target_properties(e1400patch_loader PROPERTIES OUTPUT_NAME e1400patch RUNTIME_OUTPUT_DIRECTORY "${E1400_STAGE_LOADER}")

# server.dll shim
add_library(e1400patch_server_shim SHARED src/shims/server/server_shim.c src/shims/server/server.def)
target_link_libraries(e1400patch_server_shim PRIVATE e1400patch_core)
set_target_properties(e1400patch_server_shim PROPERTIES OUTPUT_NAME server PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${E1400_STAGE_LOADER}")

# Command line tool: e1400patch.exe
add_executable(e1400patch_cli src/cli/e1400patch.c)
target_link_libraries(e1400patch_cli PRIVATE e1400patch_core)
set_target_properties(e1400patch_cli PROPERTIES OUTPUT_NAME e1400patch PDB_NAME e1400patch_cli RUNTIME_OUTPUT_DIRECTORY "${E1400_STAGE_LOADER}")

# Build tables and loader defaults into the stage
file(GLOB E1400_BUILD_TABLES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/builds/*.ini")
foreach(table IN LISTS E1400_BUILD_TABLES)
    get_filename_component(name "${table}" NAME)
    configure_file("${table}" "${E1400_STAGE_LOADER}/builds/${name}" COPYONLY)
endforeach()
configure_file(package/e1400patch.ini "${E1400_STAGE_LOADER}/e1400patch.ini.default" COPYONLY)

# Patches: one folder per patch under patches/ (see docs/patch-authoring.md)
include(cmake/E1400Patch.cmake)
file(GLOB E1400_PATCH_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/patches/*")
foreach(dir IN LISTS E1400_PATCH_DIRS)
    if(IS_DIRECTORY "${dir}" AND EXISTS "${dir}/CMakeLists.txt")
        add_subdirectory("${dir}")
    endif()
endforeach()

# Development tools (not shipped): the session replayer
add_subdirectory(tools/replay)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
