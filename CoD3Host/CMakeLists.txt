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
    "gpu.cpp"
    "edram.cpp"
    "shaders.cpp"
    "raster.cpp"
    "sampler.cpp"
    "watchpoint.cpp"
    "scheduler.cpp"
    "window.cpp"
    "input.cpp"
    "audio_out.cpp"
    "parallel.cpp"
    "watchdog.cpp"
    "resources/CoD3.rc")

# A console application: setup is interactive and the import trap has to be
# readable. WIN32 above is overridden here deliberately.
set_target_properties(CoD3 PROPERTIES WIN32_EXECUTABLE OFF)

target_link_libraries(CoD3 PRIVATE
    CoD3RecompLib   # the recompiled game code
    XenonUtils      # XEX parsing, reused so the loader matches the recompiler
    ole32           # IFileOpenDialog
    winmm           # sound out
    dbghelp         # the watchdog's stack walks
    shell32)

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

# The rasteriser is the one place the instruction set matters: its shader
# interpreter permutes lanes with AVX and blends with SSE4.1.
set_source_files_properties(raster.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
target_link_options(CoD3 PRIVATE /DEBUG)
