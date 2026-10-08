# e1400_add_patch(<id> [MOD] SOURCES <file>...)
#
# Builds a patch (or mod) module into stage/patches/<id>/ (stage/mods/<id>/) and copies its manifest patch.ini from the
# calling directory. The module links nothing from the loader: it talks to it only through include/e1400patch/patch_api.h.
function(e1400_add_patch id)
    cmake_parse_arguments(ARG "MOD" "" "SOURCES" ${ARGN})
    if(ARG_MOD)
        set(kind_dir mods)
    else()
        set(kind_dir patches)
    endif()
    set(out "${E1400_STAGE}/${kind_dir}/${id}")
    add_library(patch_${id} SHARED ${ARG_SOURCES})
    target_include_directories(patch_${id} PRIVATE "${PROJECT_SOURCE_DIR}/include")
    set_target_properties(patch_${id} PROPERTIES OUTPUT_NAME ${id} PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${out}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/patch.ini" "${out}/patch.ini" COPYONLY)
    set_property(GLOBAL APPEND PROPERTY E1400_PATCHES ${id})
endfunction()
