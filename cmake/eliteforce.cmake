message(STATUS "in eliteforce")

# Use old binary names on Windows for now to avoid breaking links
option(ELITEFORCE_OLD_WINDOWS_BINARY_NAMES "Use old binary names on Windows" ON)

if(EMSCRIPTEN)
    # Emscripten doesn't like dashes in names
    set(SERVER_NAME cMod_dedicated)
    set(CLIENT_NAME cMod)
elseif(ELITEFORCE_OLD_WINDOWS_BINARY_NAMES AND WIN32)
    include(utils/arch)
    if(ARCH STREQUAL "x86")
        set(SERVER_NAME cmod_dedicated.x86)
        set(CLIENT_NAME ioEF-cMod.x86)
    elseif(ARCH STREQUAL "x86_64")
        set(SERVER_NAME cmod_dedicated.x86_64)
        set(CLIENT_NAME ioEF-cMod.x86_64)
    endif()
else()
    set(SERVER_NAME cMod-dedicated)
    set(CLIENT_NAME cMod-stvoyHM)
endif()

set(ELITEFORCE_RENDERER_PREFIX cmod_renderer_)
add_compile_definitions(RENDERER_PREFIX="${ELITEFORCE_RENDERER_PREFIX}")

find_package(Threads REQUIRED)
list(APPEND CLIENT_LIBRARIES Threads::Threads)

set(ELITEFORCE_COMMON_SOURCES
    ${SOURCE_DIR}/cmod/cmod_cmd.c
    ${SOURCE_DIR}/cmod/cmod_cvar.c
    ${SOURCE_DIR}/cmod/cmod_logging.c
    ${SOURCE_DIR}/cmod/cmod_misc.c
    ${SOURCE_DIR}/cmod/vm_extensions.c
    ${SOURCE_DIR}/cmod/server/sv_cmd_tools.c
    ${SOURCE_DIR}/cmod/server/sv_maptable.c
    ${SOURCE_DIR}/cmod/server/sv_misc.c
    ${SOURCE_DIR}/cmod/server/sv_record_common.c
    ${SOURCE_DIR}/cmod/server/sv_record_convert.c
    ${SOURCE_DIR}/cmod/server/sv_record_main.c
    ${SOURCE_DIR}/cmod/server/sv_record_spectator.c
    ${SOURCE_DIR}/cmod/server/sv_record_writer.c
)

set(ELITEFORCE_MAD_SOURCES
    ${SOURCE_DIR}/cmod/mad/mad_bit.c
    ${SOURCE_DIR}/cmod/mad/mad_decoder.c
    ${SOURCE_DIR}/cmod/mad/mad_fixed.c
    ${SOURCE_DIR}/cmod/mad/mad_frame.c
    ${SOURCE_DIR}/cmod/mad/mad_layer12.c
    ${SOURCE_DIR}/cmod/mad/mad_layer3.c
    ${SOURCE_DIR}/cmod/mad/mad_madhuffman.c
    ${SOURCE_DIR}/cmod/mad/mad_stream.c
    ${SOURCE_DIR}/cmod/mad/mad_synth.c
    ${SOURCE_DIR}/cmod/mad/mad_timer.c
    ${SOURCE_DIR}/cmod/mad/mad_version.c
)

include(utils/disable_warnings)
disable_warnings(${ELITEFORCE_MAD_SOURCES})

set(ELITEFORCE_CLIENT_SOURCES
    ${SOURCE_DIR}/cmod/aspect_correct.c
    ${SOURCE_DIR}/cmod/cmod_crosshair.c
    ${SOURCE_DIR}/cmod/cmod_crosshair_builtins.c
    ${SOURCE_DIR}/cmod/cmod_map_adjust.c
    ${SOURCE_DIR}/cmod/snd_codec_mp3.c
    ${ELITEFORCE_MAD_SOURCES}
)

list(APPEND CLIENT_BINARY_SOURCES ${ELITEFORCE_COMMON_SOURCES} ${ELITEFORCE_CLIENT_SOURCES})
list(APPEND SERVER_BINARY_SOURCES ${ELITEFORCE_COMMON_SOURCES})
list(APPEND RENDERER_GL1_BINARY_SOURCES "${SOURCE_DIR}/cmod/cmod_fbo.c")

function(set_forced_include_global HEADER_PATH)
    # Convert relative path to absolute path if needed
    if(NOT IS_ABSOLUTE ${HEADER_PATH})
        set(HEADER_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${HEADER_PATH}")
    endif()

    message(STATUS "Adding global forced include: ${HEADER_PATH}")

    if(MSVC)
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/FI${HEADER_PATH}>)
    else()
        if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
            message(WARNING "Forced include may not be supported for compiler: ${CMAKE_C_COMPILER_ID}")
        endif()
        add_compile_options("SHELL:-include \"${HEADER_PATH}\"")
    endif()
endfunction()

set_forced_include_global(${SOURCE_DIR}/cmod/cmod_defs.h)
