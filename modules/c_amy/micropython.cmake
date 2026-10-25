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
    ${CMAKE_CURRENT_LIST_DIR}/newlib_locks.c
    ${AMY_RP2_AMY_SOURCES}
)

target_include_directories(usermod_c_amy INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    ${AMY_DIR}/src
)

# We supply run_midi/stop_midi/midi_out (amy_rp2.c) and malloc_caps (amy_rp2_mem.c).
target_compile_definitions(usermod_c_amy INTERFACE AMY_HOST_MIDI=1 MALLOC_CAPS_DEFINED=1)

# AMY is third-party to MicroPython's warning policy; build it optimised.
# Its free() calls go to amy_rp2_free(), which knows about the PSRAM pool
# (amy_rp2_mem.c).  pico_malloc already owns --wrap=free.  Its fprintf()s go to
# amy_rp2_fprintf(), readable as c_amy.log().
set_source_files_properties(${AMY_RP2_AMY_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;-O2;-Dfree=amy_rp2_free;-Dfprintf=amy_rp2_fprintf")

target_link_libraries(usermod INTERFACE usermod_c_amy)
