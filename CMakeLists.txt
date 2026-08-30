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
    src/core/hotkey_utils.cpp
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

# Behaviour lock for the 6DOF lean boundary, which is a pure function of the
# processed offsets. A separate executable linking none of the hook or Windows
# machinery, so `pixi run test` runs anywhere the mod builds, with no game
# installed.
option(SKYRIMHT_BUILD_TESTS "Build the lean-direction behaviour tests" ON)
if(SKYRIMHT_BUILD_TESTS)
    add_executable(skyrimht_lean_tests tests/lean_direction_tests.cpp)
    target_include_directories(skyrimht_lean_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp/include
    )
endif()
