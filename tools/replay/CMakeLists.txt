# Session replayer (development tool, not shipped): replays a .e1rec recording against the loader shim or the original.
add_executable(e1400replay e1400replay.c)
target_link_libraries(e1400replay PRIVATE e1400patch_core ws2_32 winmm)
