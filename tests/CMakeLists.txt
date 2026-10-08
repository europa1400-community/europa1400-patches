# Tests without game files (they also run in CI).
add_library(fixture_target SHARED fixture_target.c)
set_target_properties(fixture_target PROPERTIES PREFIX "")
add_library(fixture_patch SHARED fixture_patch.c)
target_include_directories(fixture_patch PRIVATE "${PROJECT_SOURCE_DIR}/include")
set_target_properties(fixture_patch PROPERTIES PREFIX "")
add_library(fixture_failing_patch SHARED fixture_failing_patch.c)
target_include_directories(fixture_failing_patch PRIVATE "${PROJECT_SOURCE_DIR}/include")
set_target_properties(fixture_failing_patch PROPERTIES PREFIX "")

add_executable(test_core test_core.c)
target_link_libraries(test_core PRIVATE e1400patch_core)
add_dependencies(test_core e1400patch_loader fixture_target fixture_patch fixture_failing_patch)

add_executable(test_cli test_cli.c)
target_link_libraries(test_cli PRIVATE e1400patch_core)
add_dependencies(test_cli e1400patch_cli fixture_target)
add_test(NAME cli COMMAND test_cli $<TARGET_FILE:e1400patch_cli> $<TARGET_FILE:fixture_target> "${CMAKE_BINARY_DIR}/test-work")

add_test(NAME core
    COMMAND test_core
        $<TARGET_FILE:e1400patch_loader>
        $<TARGET_FILE:fixture_target>
        $<TARGET_FILE:fixture_patch>
        $<TARGET_FILE:fixture_failing_patch>
        "${CMAKE_BINARY_DIR}/test-work")
