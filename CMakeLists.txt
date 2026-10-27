cmake_minimum_required(VERSION 3.20)
project(eaglenest VERSION 1.0.0 LANGUAGES C)

# Windows resource compiler, used to embed the application icon into the .exe.
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter)
endif()

# Release/Debug flags
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g -DDEBUG")

# SDL3
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)

# libopenmpt is no longer used: music is decoded by the built-in ProTracker
# decoder in src/audio/mod.c, so SDL3 is the only external dependency.

# The set of MSYS2 runtime DLLs copied next to eaglenest.exe on Windows. SDL3 is
# the game's only external dependency; only this explicit list is bundled (a
# full import scan would also drag in every Windows system DLL). Add entries
# here if a new dependency is introduced.
set(EAGLE_WINDOWS_RUNTIME_DLLS
    "SDL3.dll"
    CACHE STRING "Runtime DLLs copied next to the Windows executable")
# The MinGW GCC/C++ runtime DLLs (libgcc_s_seh-1, libstdc++-6, libwinpthread-1)
# are not needed by MSYS2's SDL3. Turn this on if a clean machine (without
# MSYS2 installed) complains about a missing libgcc/libstdc++.
option(EAGLE_BUNDLE_GCC_RUNTIME "Also copy the MinGW GCC/C++ runtime DLLs" OFF)

include_directories(${SDL3_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/src)
link_directories(${SDL3_LIBRARY_DIRS})

# Source files shared by the game and the map viewer (everything but the entry
# point: each binary supplies its own main()).
set(COMMON_SOURCES
    src/core/platform.c
    src/core/memory.c
    src/gfx/renderer.c
    src/gfx/palette.c
    src/gfx/tiles.c
    src/input/input.c
    src/audio/audio.c
    src/audio/mod.c
    src/game/game.c
    src/game/cheats.c
    src/game/entities.c
    src/game/player.c
    src/game/prisoner.c
    src/game/enemies.c
    src/game/bullets.c
    src/game/effects.c
    src/game/map.c
    src/game/hud.c
    src/game/objects.c
    src/game/objdata.c
    src/game/highscore.c
)

# Embed the hero-pictogram application icon into the Windows executable. The
# .ico is generated from the tileset by tools/make_icon.py.
set(APP_ICON_SOURCE "")
if(WIN32)
    configure_file(${CMAKE_SOURCE_DIR}/src/resources/eaglenest.rc.in
                   ${CMAKE_BINARY_DIR}/eaglenest.rc @ONLY)
    set(APP_ICON_SOURCE ${CMAKE_BINARY_DIR}/eaglenest.rc)
endif()

add_executable(eaglenest src/main.c ${COMMON_SOURCES} ${APP_ICON_SOURCE})
target_link_libraries(eaglenest ${SDL3_LIBRARIES} m)

# Collect the runnable game in a clean "dist" folder (instead of mixing it with
# CMake's own build files: CMakeFiles, Makefile, objects, the asset converter,
# ...) on every platform. Set on the target only, so convert_assets stays in the
# build root. The assets copy (below) lands next to the binary, so dist/ is a
# self-contained runnable folder.
set_target_properties(eaglenest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dist
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/dist
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/dist
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/dist
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/dist)

# Platform-specific
if(WIN32)
    target_link_libraries(eaglenest user32 gdi32)
    target_compile_definitions(eaglenest PRIVATE PLATFORM_WINDOWS)
    # Build a GUI-subsystem binary (launching from Explorer must not flash a
    # console window first; SDL3 provides WinMain() which forwards to main()).
    set_target_properties(eaglenest PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    target_link_libraries(eaglenest "-framework Cocoa" "-framework Metal" "-framework QuartzCore")
    target_compile_definitions(eaglenest PRIVATE PLATFORM_MACOS)
elseif(UNIX)
    target_link_libraries(eaglenest pthread dl rt)
    target_compile_definitions(eaglenest PRIVATE PLATFORM_LINUX)
endif()

# Map inspector: a standalone binary that reuses the engine modules but only
# scrolls a level's map (no HUD; coordinates go to the console). It is a console
# app on every platform (so the coordinate readout is visible), hence no
# WIN32_EXECUTABLE and no icon resource.
add_executable(mapview src/mapview_main.c ${COMMON_SOURCES})
target_link_libraries(mapview ${SDL3_LIBRARIES} m)
set_target_properties(mapview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dist
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/dist
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/dist
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/dist
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/dist)
if(WIN32)
    target_link_libraries(mapview user32 gdi32)
    target_compile_definitions(mapview PRIVATE PLATFORM_WINDOWS)
elseif(APPLE)
    target_link_libraries(mapview "-framework Cocoa" "-framework Metal" "-framework QuartzCore")
    target_compile_definitions(mapview PRIVATE PLATFORM_MACOS)
elseif(UNIX)
    target_link_libraries(mapview pthread dl rt)
    target_compile_definitions(mapview PRIVATE PLATFORM_LINUX)
endif()

# Install
install(TARGETS eaglenest mapview DESTINATION bin)

add_executable(convert_assets tools/convert_assets.c)
target_compile_definitions(convert_assets PRIVATE _CRT_SECURE_NO_WARNINGS)

add_custom_target(assets
    COMMAND convert_assets ${CMAKE_SOURCE_DIR}/../eaglenest/data ${CMAKE_SOURCE_DIR}/assets
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Converting Amiga assets to SDL3 format"
    DEPENDS convert_assets
)

# Runtime assets: copy the whole assets tree next to the executable so it can be
# launched standalone (e.g. from Explorer, whose working directory is the exe's
# folder) without depending on the source tree.
add_custom_command(TARGET eaglenest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:eaglenest>/assets
    COMMENT "Copying assets next to the executable")

# Windows: copy the MSYS2 runtime DLLs the executable needs next to it, so it
# runs on a machine without MSYS2 on PATH.
if(WIN32)
    set(_dll_dirs "")
    execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=prefix sdl3
        OUTPUT_VARIABLE _sdl3_prefix OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_sdl3_prefix)
        list(APPEND _dll_dirs "${_sdl3_prefix}/bin")
    endif()

    set(_runtime_dlls ${EAGLE_WINDOWS_RUNTIME_DLLS})
    if(EAGLE_BUNDLE_GCC_RUNTIME)
        list(APPEND _runtime_dlls libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
    endif()

    foreach(_dll IN LISTS _runtime_dlls)
        set(_dll_src "")
        foreach(_dir IN LISTS _dll_dirs)
            if(EXISTS "${_dir}/${_dll}")
                set(_dll_src "${_dir}/${_dll}")
                break()
            endif()
        endforeach()
        if(_dll_src)
            add_custom_command(TARGET eaglenest POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll_src}" $<TARGET_FILE_DIR:eaglenest>
                COMMENT "Copying ${_dll}")
        else()
            message(WARNING "Windows runtime DLL not found in the MSYS2 bin directories: ${_dll}")
        endif()
    endforeach()
endif()