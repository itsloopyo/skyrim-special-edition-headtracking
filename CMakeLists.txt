cmake_minimum_required(VERSION 3.20)
project(SkyrimSEHeadTracking VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows-specific settings
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include cameraunlock-core shared library
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# MinHook sources
set(MINHOOK_SOURCES
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)

# inih source
set(INIH_SOURCES
    extern/ini.c
)

# Mod source files
set(MOD_SOURCES
    src/pch.cpp
    src/dllmain.cpp
    src/core/mod.cpp
    src/core/logger.cpp
    src/core/config.cpp
    src/legacy_config/legacy_config.cpp
    src/core/path_utils.cpp
    src/core/rtti_utils.cpp
    src/core/sse_addresses.cpp
    src/hooks/hook_manager.cpp
    src/hooks/camera_hook.cpp
    src/hooks/input_hook.cpp
    src/hooks/player_hook.cpp
    src/hooks/hud_menu_hook.cpp
    src/hooks/crosshair_override.cpp
    src/hooks/projectile_hook.cpp
    src/game/game_state.cpp
    src/ui/notification.cpp
    src/ui/crosshair_overlay.cpp
)

# Create ASI (DLL)
add_library(SkyrimSEHeadTracking SHARED
    ${MINHOOK_SOURCES}
    ${INIH_SOURCES}
    ${MOD_SOURCES}
)

# Exclude C sources from precompiled headers (pch.h contains C++ headers)
set_source_files_properties(${MINHOOK_SOURCES} ${INIH_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# Precompiled header
target_precompile_headers(SkyrimSEHeadTracking PRIVATE src/pch.h)

# Output as .asi for Ultimate ASI Loader
set_target_properties(SkyrimSEHeadTracking PROPERTIES
    OUTPUT_NAME "SkyrimSEHeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

# Link cameraunlock core library
target_link_libraries(SkyrimSEHeadTracking PRIVATE cameraunlock)

option(SKYRIMHT_DEV_HOTKEYS "Build the diagnostic hotkeys (developer aid, not for release)" OFF)
if(SKYRIMHT_DEV_HOTKEYS)
    target_compile_definitions(SkyrimSEHeadTracking PRIVATE SKYRIMHT_DEV_HOTKEYS=1)
    message(WARNING "SKYRIMHT_DEV_HOTKEYS is ON. This build registers F8 to cycle "
                    "rotation-axis isolation and Insert to dump the camera matrices "
                    "to the log. Do not ship it.")
endif()

# Include directories
target_include_directories(SkyrimSEHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)

# Windows libraries
if(WIN32)
    target_link_libraries(SkyrimSEHeadTracking PRIVATE
        ws2_32
        psapi
        d3d11
        dxgi
        d3dcompiler
        shell32
        version
    )
endif()

# Native unit tests, in a build tree of their own: pixi run test configures build-tests/ with
# -DSKYRIMHT_BUILD_TESTS=ON (scripts/run-tests.ps1), so the tree the packager reads never
# builds a test.
option(SKYRIMHT_BUILD_TESTS "Build the SkyrimSEHeadTracking unit tests" OFF)
if(SKYRIMHT_BUILD_TESTS)
    enable_testing()

    # Every test hangs off this aggregate, so run-tests.ps1 builds them all with one --target
    # and selects them all with one ctest filter.
    add_custom_target(skyrimht_tests)

    function(skyrimht_add_test name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/extern
            ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include)
        target_link_libraries(${name} PRIVATE cameraunlock)
        set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        if(MSVC)
            target_compile_options(${name} PRIVATE /W4 /WX /permissive-)
        endif()
        add_test(NAME ${name} COMMAND ${name})
        add_dependencies(skyrimht_tests ${name})
    endfunction()

    # The frozen legacy reader parses with the inih in extern/, and every test that compiles the
    # config compiles it.
    add_library(skyrimht_inih STATIC extern/ini.c)

    set(SKYRIMHT_CONFIG_SOURCES
        src/core/config.cpp
        src/legacy_config/legacy_config.cpp
        src/core/logger.cpp
        src/core/path_utils.cpp
    )

    # Behaviour lock for the 6DOF lean boundary, which is a pure function of the processed
    # offsets, against the position settings this build hands the processor.
    skyrimht_add_test(skyrimht_lean_tests tests/lean_direction_tests.cpp ${SKYRIMHT_CONFIG_SOURCES})
    target_link_libraries(skyrimht_lean_tests PRIVATE skyrimht_inih)

    # The committed HeadTracking.ini against the table, a first start, the toggles' saves and
    # the legacy file's ANSI path. `--render-config <path>` makes it write the committed file
    # instead (pixi run render-config).
    skyrimht_add_test(skyrimht_config_tests tests/config_tests.cpp ${SKYRIMHT_CONFIG_SOURCES})
    target_link_libraries(skyrimht_config_tests PRIVATE skyrimht_inih)
    target_compile_definitions(skyrimht_config_tests PRIVATE
        SKYRIMHT_COMMITTED_CONFIG=L"${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")

    # The config differential test. The oracle is v0.3.0's reader (c3a1f7f), the newest
    # published build, copied byte for byte into tests/config_differential/oracle; renaming its
    # namespace lets it link beside this build's. It parses with the inih in extern/, which is
    # the same bytes at v0.3.0, so both readers share one copy of it. oracle_adapter.cpp copies
    # its Config out for the test.
    set(SKYRIMHT_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle)
    add_library(skyrimht_config_oracle STATIC
        ${SKYRIMHT_ORACLE_DIR}/src/core/config.cpp
        ${SKYRIMHT_ORACLE_DIR}/src/core/logger.cpp
        ${SKYRIMHT_ORACLE_DIR}/src/core/path_utils.cpp
        tests/config_differential/oracle_adapter.cpp
    )
    target_include_directories(skyrimht_config_oracle PRIVATE
        ${SKYRIMHT_ORACLE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(skyrimht_config_oracle PRIVATE SkyrimHT=skyrim_oracle)

    skyrimht_add_test(skyrimht_config_differential
        tests/config_differential/differential_tests.cpp
        ${SKYRIMHT_CONFIG_SOURCES}
    )
    target_include_directories(skyrimht_config_differential PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(skyrimht_config_differential PRIVATE
        SKYRIMHT_DIFFERENTIAL_INPUTS="${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/inputs"
        SKYRIMHT_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")
    target_link_libraries(skyrimht_config_differential PRIVATE skyrimht_config_oracle skyrimht_inih)

    # Core's canonical config lint over the committed file and every file the differential
    # test migrated, which it writes beside itself under migrated\.
    add_test(NAME skyrimht_config_lint COMMAND node
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/lint-migrated.mjs
        $<TARGET_FILE_DIR:skyrimht_config_differential>/migrated)
    set_tests_properties(skyrimht_config_lint PROPERTIES DEPENDS skyrimht_config_differential)

    # The frozen reader, the sources it compiles, the oracle and the inputs against the hashes
    # tests/config_differential/frozen.tsv records.
    add_test(NAME skyrimht_config_frozen COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/check-frozen.ps1)
endif()
