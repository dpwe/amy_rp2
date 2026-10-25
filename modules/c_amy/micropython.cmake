# AMY as a MicroPython user C module (rp2 port).
# AMY_DIR defaults to the amy checkout next to this project.
if(NOT AMY_DIR)
    get_filename_component(AMY_DIR ${CMAKE_CURRENT_LIST_DIR}/../../amy ABSOLUTE)
endif()

set(AMY_RP2_AMY_SOURCES
    ${AMY_DIR}/src/algorithms.c
    ${AMY_DIR}/src/amy.c
    ${AMY_DIR}/src/amy_midi.c
    ${AMY_DIR}/src/api.c
    ${AMY_DIR}/src/custom.c
    ${AMY_DIR}/src/cv_trigger.c
    ${AMY_DIR}/src/delay.c
    ${AMY_DIR}/src/envelope.c
    ${AMY_DIR}/src/filters.c
    ${AMY_DIR}/src/instrument.c
    ${AMY_DIR}/src/interp_partials.c
    ${AMY_DIR}/src/log2_exp2.c
    ${AMY_DIR}/src/midi_mappings.c
    ${AMY_DIR}/src/note_output.c
    ${AMY_DIR}/src/oscillators.c
    ${AMY_DIR}/src/parse.c
    ${AMY_DIR}/src/patches.c
    ${AMY_DIR}/src/pcm.c
    ${AMY_DIR}/src/sequencer.c
    ${AMY_DIR}/src/transfer.c
)

add_library(usermod_c_amy INTERFACE)

target_sources(usermod_c_amy INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/modc_amy.c
    ${CMAKE_CURRENT_LIST_DIR}/amy_rp2.c
    ${CMAKE_CURRENT_LIST_DIR}/amy_rp2_mem.c
    ${CMAKE_CURRENT_LIST_DIR}/amy_rp2_usb.c
    ${CMAKE_CURRENT_LIST_DIR}/amy_rp2_sysex.c
    ${CMAKE_CURRENT_LIST_DIR}/amy_rp2_tulip.c
    ${CMAKE_CURRENT_LIST_DIR}/newlib_locks.c
    ${AMY_RP2_AMY_SOURCES}
)

target_include_directories(usermod_c_amy INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    ${AMY_DIR}/src
)

# We supply run_midi/stop_midi/midi_out (amy_rp2.c) and malloc_caps (amy_rp2_mem.c).
# PICO_MALLOC_PANIC=0: pico_malloc otherwise halts the chip when malloc fails,
# so malloc_caps' fallback to the PSRAM pool (e.g. for echo delay lines) never runs.
target_compile_definitions(usermod_c_amy INTERFACE AMY_HOST_MIDI=1 MALLOC_CAPS_DEFINED=1 PICO_MALLOC_PANIC=0)

# AMY is third-party to MicroPython's warning policy; build it optimised.
# Its free() calls go to amy_rp2_free(), which knows about the PSRAM pool
# (amy_rp2_mem.c).  pico_malloc already owns --wrap=free.  Its fprintf()s go to
# amy_rp2_fprintf(), readable as c_amy.log().
set_source_files_properties(${AMY_RP2_AMY_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;-O2;-Dfree=amy_rp2_free;-Dfprintf=amy_rp2_fprintf")

# USB MIDI (amy_rp2_usb.c): turn on TinyUSB's MIDI class, and swap in a
# configuration descriptor with MicroPython's CDC REPL plus a MIDI interface.
target_compile_definitions(usermod_c_amy INTERFACE
    CFG_TUD_MIDI=1
    CFG_TUD_MIDI_RX_BUFSIZE=256
    CFG_TUD_MIDI_TX_BUFSIZE=256
    CFG_TUD_MIDI_EP_BUFSIZE=64
)
target_link_options(usermod_c_amy INTERFACE -Wl,--wrap=tud_descriptor_configuration_cb)

# c_amy.version(): "YYYYMMDD-<git hash>" (configure date and this repo's
# commit), the form amyboard.com/editor expects from report_version().
string(TIMESTAMP AMY_RP2_BUILD_DATE "%Y%m%d")
execute_process(COMMAND git -C ${CMAKE_CURRENT_LIST_DIR} rev-parse --short HEAD
    OUTPUT_VARIABLE AMY_RP2_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT AMY_RP2_GIT_HASH)
    set(AMY_RP2_GIT_HASH "unknown")
endif()
target_compile_definitions(usermod_c_amy INTERFACE AMY_RP2_VERSION="${AMY_RP2_BUILD_DATE}-${AMY_RP2_GIT_HASH}")

target_link_libraries(usermod INTERFACE usermod_c_amy)
