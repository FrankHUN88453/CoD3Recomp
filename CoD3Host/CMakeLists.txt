cmake_minimum_required(VERSION 3.20)
project("CoD3Host" CXX)

# The host executable: first time setup, guest address space, and the kernel
# import stubs the recompiled code calls into. This is the part that will grow
# into an actual runtime.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT WIN32)
    message(FATAL_ERROR "The host is Windows only for now: it uses Win32 for the "
                        "address space reservation and the file pickers.")
endif()

# The icon and version information. RC_DEPENDS makes a change to the icon
# rebuild the executable, which it otherwise would not.
enable_language(RC)
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")

add_executable(CoD3 WIN32
    "log.cpp"
    "main.cpp"
    "installer.cpp"
    "xdvdfs.cpp"
    "kernel.cpp"
    "kernel_stubs.cpp"
    "kernel_memory.cpp"
    "kernel_system.cpp"
    "kernel_objects.cpp"
    "kernel_file.cpp"
    "kernel_xam.cpp"
    "kernel_thread.cpp"
    "kernel_pcr.cpp"
    "kernel_apc.cpp"
    "kernel_video.cpp"
    "kernel_audio.cpp"
    "kernel_misc.cpp"
    "title_trace.cpp"
    "heap_trace.cpp"
    "pool_trace.cpp"
    "table_trace.cpp"
    "anim_heap_trace.cpp"
    "native_crt.cpp"
    "probe.cpp"
    "timeline.cpp"
    "render_d3d11.cpp"
    "render_state.cpp"
    "render_commands.cpp"
    "render_pipeline.cpp"
    "render_shaders.cpp"
    "render_resources.cpp"
    "render_stats.cpp"
    "xenos_hlsl.cpp"
    "gpu.cpp"
    "sampler.cpp"
    "watchpoint.cpp"
    "scheduler.cpp"
    "window.cpp"
    "input.cpp"
    "mouse_look.cpp"
    "cpu_depth.cpp"
    "menu_mouse.cpp"
    "pc_prompts.cpp"
    "render_prompts.cpp"
    "sprint.cpp"
    "video_menu.cpp"
    "widescreen.cpp"
    "settings.cpp"
    "overlay.cpp"
    "../thirdparty/imgui/imgui.cpp"
    "../thirdparty/imgui/imgui_draw.cpp"
    "../thirdparty/imgui/imgui_tables.cpp"
    "../thirdparty/imgui/imgui_widgets.cpp"
    "../thirdparty/imgui/backends/imgui_impl_dx11.cpp"
    "../thirdparty/imgui/backends/imgui_impl_win32.cpp"
    "audio_out.cpp"
    "xma.cpp"
    "parallel.cpp"
    "title_fixes.cpp"
    "d3d_hooks.cpp"
    "native_state.cpp"
    "write_watch.cpp"
    "watchdog.cpp"
    "modules.cpp"
    "coroutines.cpp"
    "embedded_libraries.cpp"
    "updater.cpp"
    "resources/CoD3.rc")

# A console application: setup is interactive and the import trap has to be
# readable. WIN32 above is overridden here deliberately. The file it makes is
# CoD3SP_s.exe (the target keeps its name for the build commands).
set_target_properties(CoD3 PROPERTIES WIN32_EXECUTABLE OFF OUTPUT_NAME "CoD3SP_s")

# The version, which the updater holds against the newest release on GitHub
# (updater.cpp) and the executable's version information shows: one header
# for both, written here. Raised with each release (its tag is v<version>).
set(COD3_VERSION_MAJOR 0)
set(COD3_VERSION_MINOR 5)
set(COD3_VERSION_PATCH 0)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cod3_version.h"
"#pragma once
// Written by CMake (CoD3Host/CMakeLists.txt).
#define COD3_VERSION_MAJOR ${COD3_VERSION_MAJOR}
#define COD3_VERSION_MINOR ${COD3_VERSION_MINOR}
#define COD3_VERSION_PATCH ${COD3_VERSION_PATCH}
#define COD3_VERSION_TEXT \"${COD3_VERSION_MAJOR}.${COD3_VERSION_MINOR}.${COD3_VERSION_PATCH}\"
")
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I\"${CMAKE_CURRENT_BINARY_DIR}\"")

target_link_libraries(CoD3 PRIVATE
    CoD3RecompLib   # the recompiled game code
    ${COD3_LEVEL_LIBRARIES}   # the levels' own code, one library each
    XenonUtils      # XEX parsing, reused so the loader matches the recompiler
    ole32           # IFileOpenDialog
    winmm           # sound out
    dbghelp         # the watchdog's stack walks
    d3d11           # the host GPU
    dxgi
    d3dcompiler     # the menu's own shaders (Dear ImGui)
    windowscodecs   # screenshots as PNG
    shell32
    winhttp)        # the updater

# Decompiled functions, from a CoD3Decomp checkout beside this repository (or
# wherever COD3_DECOMP_DIR says): each takes the place of the recompiled
# function of its name, the recompiled one staying callable as __imp__sub_X.
# That code is derived from the game's and is kept in a private repository;
# nothing of it is here. Without the checkout the build is as before.
set(COD3_DECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../CoD3Decomp" CACHE PATH "A CoD3Decomp checkout, if there is one")
if (EXISTS "${COD3_DECOMP_DIR}/src/decomp.h")
    file(GLOB_RECURSE COD3_DECOMP_SOURCES CONFIGURE_DEPENDS "${COD3_DECOMP_DIR}/src/*.cpp")
    list(LENGTH COD3_DECOMP_SOURCES decompCount)
    message(STATUS "CoD3: ${decompCount} decompiled source files from ${COD3_DECOMP_DIR}")
    target_sources(CoD3 PRIVATE ${COD3_DECOMP_SOURCES})
    target_compile_definitions(CoD3 PRIVATE COD3_HAVE_DECOMP=1)
endif()

# FFmpeg's headers, for XMA decoding through its avcodec, loaded at run time
# (xma.cpp). Without them the build decodes nothing and says so.
find_path(COD3_FFMPEG_INCLUDE libavcodec/avcodec.h
    PATHS "$ENV{FFMPEG_DIR}/include" "C:/ffmpeg/include" "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/ffmpeg/include")
if (COD3_FFMPEG_INCLUDE)
    message(STATUS "CoD3: XMA decoding through FFmpeg, headers at ${COD3_FFMPEG_INCLUDE}")
    target_include_directories(CoD3 PRIVATE "${COD3_FFMPEG_INCLUDE}")
    target_compile_definitions(CoD3 PRIVATE COD3_HAVE_FFMPEG_HEADERS=1)

    # Its libraries go inside the executable (embedded_libraries.cpp): packed
    # by embed_pack at build time into resources from 3001 on, written out
    # once at run time. Without them beside the headers the executable looks
    # for them beside itself, as before.
    get_filename_component(COD3_FFMPEG_BIN "${COD3_FFMPEG_INCLUDE}/../bin" ABSOLUTE)
    file(GLOB COD3_FFMPEG_AVCODEC "${COD3_FFMPEG_BIN}/avcodec-*.dll")
    file(GLOB COD3_FFMPEG_AVUTIL "${COD3_FFMPEG_BIN}/avutil-*.dll")
    file(GLOB COD3_FFMPEG_SWRESAMPLE "${COD3_FFMPEG_BIN}/swresample-*.dll")
    if (COD3_FFMPEG_AVCODEC AND COD3_FFMPEG_AVUTIL AND COD3_FFMPEG_SWRESAMPLE)
        add_executable(embed_pack "tools/embed_pack.cpp")
        set(embeddedRc "// Written by CMake: the libraries carried inside the executable.\n")
        set(embeddedBins "")
        set(resourceId 3001)
        foreach(library ${COD3_FFMPEG_AVUTIL} ${COD3_FFMPEG_SWRESAMPLE} ${COD3_FFMPEG_AVCODEC})
            get_filename_component(libraryName "${library}" NAME)
            set(packed "${CMAKE_CURRENT_BINARY_DIR}/embedded/${libraryName}.bin")
            add_custom_command(OUTPUT "${packed}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/embedded"
                COMMAND embed_pack "${library}" "${packed}" "${libraryName}"
                DEPENDS embed_pack "${library}"
                COMMENT "Packing ${libraryName} into the executable")
            list(APPEND embeddedBins "${packed}")
            string(APPEND embeddedRc "${resourceId} RCDATA \"${packed}\"\n")
            math(EXPR resourceId "${resourceId} + 1")
            message(STATUS "CoD3: ${libraryName} goes inside the executable")
        endforeach()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/embedded_libraries.rc" "${embeddedRc}")
        target_sources(CoD3 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/embedded_libraries.rc")
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/embedded_libraries.rc" PROPERTIES OBJECT_DEPENDS "${embeddedBins}")
    else()
        message(STATUS "CoD3: no FFmpeg libraries beside its headers; the executable looks for them beside itself")
    endif()
else()
    message(STATUS "CoD3: no FFmpeg headers, XMA sound will be silent")
endif()

# Dear ImGui, for the settings menu, from thirdparty/imgui.
target_include_directories(CoD3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui")

# The levels whose code was recompiled and linked in, for modules.cpp: one
# COD3_LEVEL(name) line each, written at configure time from what the
# levels directory found.
set(COD3_LEVELS_INC "${CMAKE_CURRENT_BINARY_DIR}/cod3_levels.inc")
set(levelsText "// Generated by CMake: the level libraries linked into this build.
")
foreach(library ${COD3_LEVEL_LIBRARIES})
    string(REPLACE "CoD3Level_" "" levelName "${library}")
    string(APPEND levelsText "COD3_LEVEL(${levelName})
")
endforeach()
file(WRITE "${COD3_LEVELS_INC}" "${levelsText}")
target_include_directories(CoD3 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(CoD3 PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS)

# kernel_stubs.cpp is one line per import and will shrink as imports get
# implemented; the unused parameter warnings there are not interesting.
target_compile_options(CoD3 PRIVATE -Wno-unused-parameter)

# Debug information for the host alone, so the watchdog can name the
# functions on a stuck thread's stack. The recompiled library stays as it
# is: its symbols would be gigabytes and say nothing a guest address does not.
target_compile_options(CoD3 PRIVATE -g -gcodeview)

# The main thread runs guest code too, at whatever depth the title's own
# calls reach; see ExCreateThread for why that wants a large host stack.
target_link_options(CoD3 PRIVATE /STACK:16777216)

target_link_options(CoD3 PRIVATE /DEBUG)
