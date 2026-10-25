// amy_rp2.h -- AMY on MicroPython rp2 (RP2350 / RP2040).
//
// All AMY work (parsing, rendering, MIDI-UART polling, I2S DMA) runs on core 1.
// Core 0 (the MicroPython VM) only pushes commands into a single-producer /
// single-consumer ring that core 1 drains between blocks, so AMY's own locks
// (no-ops on bare pico-sdk) are never needed.

#ifndef AMY_RP2_H
#define AMY_RP2_H

#include <stdbool.h>
#include <stdint.h>
#include "amy.h"

// Ring entry types.
#define AMY_RP2_CMD_WIRE        0  // wire message, amy_add_message()
#define AMY_RP2_CMD_WIRE_SYSEX  1  // wire message, amy_send_wire_from_sysex()
#define AMY_RP2_CMD_MIDI        2  // raw MIDI byte stream, convert_midi_bytes_to_messages(usb=0)
#define AMY_RP2_CMD_BLEEP       3  // decimal start time, amy_bleep()
#define AMY_RP2_CMD_MIDI_USB    4  // 3-byte padded USB/Web MIDI events (usb=1)

// Largest SPSS sysex payload handled (and largest command-ring entry).
#define AMY_RP2_SYSEX_FRAME_MAX 4096

// Start AMY on core 1 with config c (audio must be AMY_AUDIO_IS_I2S or NONE).
// Returns false if core 1 could not be started.
bool amy_rp2_start(amy_config_t c);
void amy_rp2_stop(void);
bool amy_rp2_running(void);

// Queue a command for core 1.  Blocks (up to timeout_ms) if the ring is full.
// Returns false on timeout or if AMY isn't running.
bool amy_rp2_push(uint8_t type, const uint8_t *data, size_t len, uint32_t timeout_ms);

// Bytes of core 1's stack never touched so far (high-water mark).
uint32_t amy_rp2_core1_stack_unused(void);

// Largest |output sample| since the last call (0..32767).
uint32_t amy_rp2_take_peak(void);

// Wait (up to timeout_ms) until core 1 has processed everything queued so far.
bool amy_rp2_wait_idle(uint32_t timeout_ms);

// Number of blocks DMA had to restart after running dry (audio underruns).
uint32_t amy_rp2_underruns(void);

// MIDI input counters; channel/synth masks have bit n set for channel n (1..16).
void amy_rp2_midi_debug(uint32_t *uart_bytes, uint32_t *msgs, uint32_t *last_msg,
                        uint32_t *active_channels, uint32_t *synths);

// Take (and clear) what AMY has written to stderr.
size_t amy_rp2_log_take(char *out, size_t max);

// USB MIDI (amy_rp2_usb.c): start the output pump (core 0), queue bytes to the
// host from either core, and count output dropped because the host wasn't reading.
void amy_rp2_usb_init(void);
void amy_rp2_usb_midi_write(const uint8_t *bytes, size_t len);
uint32_t amy_rp2_usb_midi_dropped(void);

// SPSS sysex (amy_rp2_sysex.c): core 1 queues complete F0 00 03 45 ... F7
// payloads; a core 0 timer tick schedules their handling; AMY's file / exec /
// reboot hooks.  zB0 (reboot, skip sketch) leaves this in watchdog scratch[3].
#define AMY_RP2_SKIP_SKETCH_MAGIC 0x5B0057C0u
bool amy_rp2_sysex_push(const uint8_t *payload, size_t len);
void amy_rp2_sysex_tick(void);
uint32_t amy_rp2_sysex_dropped(void);
void amy_rp2_sysex_install_hooks(amy_config_t *c);

// Allocation stats (amy_rp2_mem.c).
void amy_rp2_mem_stats(uint32_t *sram_used, uint32_t *sram_free, uint32_t *psram_used,
                       uint32_t *psram_size, uint32_t *failures);

// Startup breadcrumbs, kept in watchdog scratch registers so they survive the
// watchdog reboot that amy_rp2_start() arms around amy_start().
//   scratch[0] = AMY_RP2_CRUMB | stage, [1] = PSRAM pool bytes, [2] = failed allocs
#define AMY_RP2_CRUMB 0xA3700000u

// Hook for MICROPY_BOARD_START_SOFT_RESET.
void amy_rp2_soft_reset(void);

#endif
