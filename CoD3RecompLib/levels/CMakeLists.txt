# The levels' own code.
#
# Every single player level ships its script code compiled to PowerPC in a
# XEX DLL (sp\<level>\<level>.dll, image base 0x89000000), loaded by the
# title with XexLoadImage when the level starts. Each is recompiled the same
# way as default.xex, into levels/<name>/, and built here as a library of its
# own. The helpers and the function table are renamed per level by
# scripts/rename_level.py so that they link side by side; the guest
# functions carry their address in their name and never clash.

function(cod3_level name)
    set(folder "${CMAKE_CURRENT_SOURCE_DIR}/${name}")
    file(GLOB sources CONFIGURE_DEPENDS "${folder}/*.cpp")
    list(LENGTH sources count)
    if (count EQUAL 0)
        message(STATUS "CoD3 level ${name}: not recompiled, skipped")
        return()
    endif()
    message(STATUS "CoD3 level ${name}: ${count} recompiled translation units")

    add_library(CoD3Level_${name} STATIC ${sources})

    # The level's own ppc_config.h and ppc_recomp_shared.h come first; the
    # rest is shared with the title's library.
    target_include_directories(CoD3Level_${name}
        PRIVATE "${folder}"
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include"
               "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/XenonRecomp/thirdparty/simde")
    target_compile_options(CoD3Level_${name} PRIVATE
        -fno-strict-aliasing
        -Wno-unused-variable
        -Wno-unused-but-set-variable)
    if (MSVC)
        target_compile_options(CoD3Level_${name} PRIVATE -FIcod3_mmio.h -g -gcodeview)
    else()
        target_compile_options(CoD3Level_${name} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/../include/cod3_mmio.h" -g)
    endif()
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        target_compile_options(CoD3Level_${name} PUBLIC -mcpu=apple-m1)
    else()
        target_compile_options(CoD3Level_${name} PUBLIC -march=x86-64-v2)
    endif()
    set(COD3_LEVEL_LIBRARIES ${COD3_LEVEL_LIBRARIES} CoD3Level_${name} PARENT_SCOPE)
endfunction()

cod3_level(saint_lo)
cod3_level(blkbrn)
cod3_level(chambois)
cod3_level(credits)
cod3_level(crssrds)
cod3_level(falaise)
cod3_level(forest)
cod3_level(fuelplnt)
cod3_level(hostage)
cod3_level(island)
cod3_level(laison)
cod3_level(mace2)
cod3_level(mayenne)
cod3_level(nightd)
cod3_level(stbert)

set(COD3_LEVEL_LIBRARIES ${COD3_LEVEL_LIBRARIES} PARENT_SCOPE)
