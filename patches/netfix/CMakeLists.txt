e1400_add_patch(netfix SOURCES netfix.c)
target_link_libraries(patch_netfix PRIVATE ws2_32)
