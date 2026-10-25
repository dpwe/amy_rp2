// amy_rp2.c -- AMY platform layer for MicroPython on rp2 (pico-sdk, no Arduino).
//
// Supplies what AMY's i2s.c (audio) and amy_midi.c device layer (MIDI) supply on
// other MCUs; build with AMY_HOST_MIDI so amy_midi.c leaves run_midi / stop_midi
// / midi_out to us.
//
// Threading: amy_start()/amy_stop() run on core 0 while core 1 is parked, so
// every malloc/free AMY does happens on one core at a time.  While running, only
// core 1 touches AMY state; core 0 talks to it through the command ring.
//
// Core 1 is launched once and never reset: it parks between runs.  (pico-sdk
// #2201: after multicore_reset_core1() the lockout victim still reads as
// initialised, and MicroPython's flash writes would then wait forever for it.)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amy_rp2.h"
#include "amy_midi.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/time.h"

#ifndef MP_ARRAY_SIZE
#define MP_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#include "pico-audio/audio_i2s.pio.h"

#define CRUMB(n) (watchdog_hw->scratch[0] = AMY_RP2_CRUMB | (n))

// ------------- command ring (core 0 -> core 1) -------------

#define RING_SIZE 16384  // power of two
#define RING_MASK (RING_SIZE - 1)
#define RING_HDR 3       // type, len lo, len hi

static uint8_t ring[RING_SIZE];
static volatile uint32_t ring_head = 0;  // advanced by core 0 only
static volatile uint32_t ring_tail = 0;  // advanced by core 1 only

static char cmd_buf[MAX_MESSAGE_LEN + 1];

// ------------- run state -------------

enum { CORE1_PARKED, CORE1_RUNNING };
static volatile bool core1_launched = false;
static volatile bool run_requested = false;
static volatile int core1_state = CORE1_PARKED;
static volatile bool amy_rp2_started = false;

// The ABI wants SP 8-byte aligned (varargs doubles, e.g. printf("%f"), read
// garbage otherwise); a bare uint32_t array only guarantees 4.
static uint32_t core1_stack[16 * 1024 / sizeof(uint32_t)] __attribute__((aligned(16)));

// ------------- I2S out: PIO + double-buffered DMA -------------

static PIO i2s_pio = NULL;
static uint i2s_sm = 0;
static uint i2s_offset = 0;
static int i2s_dma = -1;
static uint32_t dma_buf[2][AMY_BLOCK_SIZE];  // (L << 16) | R per frame
static int dma_next = 0;
static bool dma_primed = false;
static volatile uint32_t underruns = 0;
static volatile uint32_t out_peak = 0;  // max |sample| since last amy_rp2_take_peak()

// MIDI input counters, for c_amy.midi_debug().
static volatile uint32_t midi_uart_bytes = 0;
static volatile uint32_t midi_msgs = 0;
static volatile uint32_t midi_last_msg = 0;  // up to 3 bytes, big-endian

uart_inst_t *rp_get_uart(int8_t index) {
    if (index == 0) return uart0;
    if (index == 1) return uart1;
    return NULL;
}

static void poll_midi_uart(void) {
    if (!(amy_global.config.midi & AMY_MIDI_IS_UART)) return;
    uart_inst_t *u = rp_get_uart(amy_global.config.midi_uart);
    uint8_t bytes[32];
    size_t n = 0;
    while (n < sizeof(bytes) && uart_is_readable(u)) {
        bytes[n++] = uart_getc(u);
    }
    if (n) {
        midi_uart_bytes += n;
        convert_midi_bytes_to_messages(bytes, n, 0);
    }
}

static void drain_ring(void) {
    while (ring_tail != ring_head) {
        __dmb();  // see the payload core 0 wrote before it advanced ring_head
        uint32_t t = ring_tail;
        uint8_t type = ring[t & RING_MASK];
        size_t len = ring[(t + 1) & RING_MASK] | (ring[(t + 2) & RING_MASK] << 8);
        for (size_t i = 0; i < len; ++i) {
            cmd_buf[i] = ring[(t + RING_HDR + i) & RING_MASK];
        }
        cmd_buf[len] = '\0';
        __dmb();
        ring_tail = t + RING_HDR + len;
        switch (type) {
            case AMY_RP2_CMD_WIRE:
                amy_add_message(cmd_buf);
                break;
            case AMY_RP2_CMD_WIRE_SYSEX:
                amy_send_wire_from_sysex(cmd_buf);
                break;
            case AMY_RP2_CMD_MIDI:
                convert_midi_bytes_to_messages((uint8_t *)cmd_buf, len, 1);
                break;
            case AMY_RP2_CMD_BLEEP:
                amy_bleep(strtoul(cmd_buf, NULL, 10));
                break;
        }
    }
}

static void i2s_setup(void) {
    int bclk = amy_global.config.i2s_bclk;
    int dout = amy_global.config.i2s_dout;
    hard_assert(amy_global.config.i2s_lrc == bclk + 1);  // PIO side-set: LRCLK = BCLK + 1

    // The range must fit one PIO GPIO window (0-31 or 16-47 on RP2350B), so
    // claim just the span of pins we use.
    int lo = MIN(bclk, dout), hi = MAX(bclk + 1, dout);
    bool ok = pio_claim_free_sm_and_add_program_for_gpio_range(
        &audio_i2s_program, &i2s_pio, &i2s_sm, &i2s_offset, lo, hi - lo + 1, true);
    hard_assert(ok);
    gpio_set_function(dout, pio_get_funcsel(i2s_pio));
    gpio_set_function(bclk, pio_get_funcsel(i2s_pio));
    gpio_set_function(bclk + 1, pio_get_funcsel(i2s_pio));
    audio_i2s_program_init(i2s_pio, i2s_sm, i2s_offset, dout, bclk);

    // Two PIO cycles per bit, 32 bits per stereo frame: PIO clock = 64 * fs.
    // Divider in 8.8 fixed point = sys_clk * 256 / (64 * fs).
    uint32_t div = (uint32_t)(((uint64_t)clock_get_hz(clk_sys) * 4) / AMY_SAMPLE_RATE);
    pio_sm_set_clkdiv_int_frac(i2s_pio, i2s_sm, div >> 8, div & 0xff);

    i2s_dma = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(i2s_dma);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(i2s_pio, i2s_sm, true));
    dma_channel_configure(i2s_dma, &cfg, &i2s_pio->txf[i2s_sm], NULL, AMY_BLOCK_SIZE, false);

    memset(dma_buf, 0, sizeof(dma_buf));
    dma_next = 0;
    dma_primed = false;
    pio_sm_set_enabled(i2s_pio, i2s_sm, true);
}

static void i2s_teardown(void) {
    if (i2s_dma >= 0) {
        dma_channel_abort(i2s_dma);
        dma_channel_unclaim(i2s_dma);
        i2s_dma = -1;
    }
    if (i2s_pio) {
        pio_sm_set_enabled(i2s_pio, i2s_sm, false);
        pio_remove_program_and_unclaim_sm(&audio_i2s_program, i2s_pio, i2s_sm, i2s_offset);
        i2s_pio = NULL;
    }
}

// ------------- AMY platform hooks (normally in i2s.c) -------------

void amy_platform_init(void) {
    CRUMB(2);
    if (AMY_HAS_I2S) i2s_setup();
    CRUMB(3);
}

void amy_platform_deinit(void) {
    if (AMY_HAS_I2S) i2s_teardown();
}

void amy_update_tasks(void) {
    poll_midi_uart();
    drain_ring();
    amy_execute_deltas();
}

int16_t *amy_render_audio(void) {
    int64_t t0 = amy_get_us();
    amy_render(0, AMY_OSCS, 0);
    int16_t *block = amy_fill_buffer();
    amy_overload_check((uint32_t)(amy_get_us() - t0));
    return block;
}

size_t amy_i2s_write(const uint8_t *buffer, size_t nbytes) {
    const int16_t *s = (const int16_t *)buffer;
    uint32_t *d = dma_buf[dma_next];
    uint32_t peak = out_peak;
    for (int i = 0; i < AMY_BLOCK_SIZE; ++i) {
        d[i] = ((uint32_t)(uint16_t)s[2 * i] << 16) | (uint16_t)s[2 * i + 1];
        uint32_t l = abs(s[2 * i]), r = abs(s[2 * i + 1]);
        if (l > peak) peak = l;
        if (r > peak) peak = r;
    }
    out_peak = peak;
    if (dma_primed && !dma_channel_is_busy(i2s_dma)) {
        underruns++;
    }
    // Wait for the other buffer to finish playing; keep MIDI and Python
    // commands flowing meanwhile (we're between renders, so this is safe).
    while (dma_channel_is_busy(i2s_dma)) {
        poll_midi_uart();
        drain_ring();
    }
    dma_channel_transfer_from_buffer_now(i2s_dma, d, AMY_BLOCK_SIZE);
    dma_primed = true;
    dma_next ^= 1;
    return nbytes;
}

// ------------- MIDI device layer (AMY_HOST_MIDI) -------------

void run_midi(void) {
    if (sysex_buffer != NULL) return;
    sysex_buffer = malloc_caps(MAX_SYSEX_BYTES, amy_global.config.ram_caps_sysex);
    if (amy_global.config.midi & AMY_MIDI_IS_UART) {
        uart_inst_t *u = rp_get_uart(amy_global.config.midi_uart);
        uart_init(u, 31250);
        if (amy_global.config.midi_out >= 0)
            gpio_set_function(amy_global.config.midi_out, UART_FUNCSEL_NUM(u, amy_global.config.midi_out));
        if (amy_global.config.midi_in >= 0)
            gpio_set_function(amy_global.config.midi_in, UART_FUNCSEL_NUM(u, amy_global.config.midi_in));
        uart_set_hw_flow(u, false, false);
        uart_set_format(u, 8, 1, UART_PARITY_NONE);
        uart_set_fifo_enabled(u, true);
    }
}

void stop_midi(void) {
    if (sysex_buffer == NULL) return;
    if (amy_global.config.midi & AMY_MIDI_IS_UART) {
        uart_deinit(rp_get_uart(amy_global.config.midi_uart));
    }
    free(sysex_buffer);
    sysex_buffer = NULL;
}

void midi_out(uint8_t *bytes, uint16_t len) {
    midi_out_external_hook(bytes, len);
    if (amy_global.config.midi & AMY_MIDI_IS_UART) {
        uart_write_blocking(rp_get_uart(amy_global.config.midi_uart), bytes, len);
    }
}

// amy_default_synths() installs its own input hook, so ours goes in after
// amy_start() and passes everything on to whatever was there.
static void (*next_midi_input_hook)(uint8_t *, uint16_t, uint8_t) = NULL;

static void count_midi_input(uint8_t *bytes, uint16_t len, uint8_t is_sysex) {
    if (next_midi_input_hook) next_midi_input_hook(bytes, len, is_sysex);
    if (is_sysex) return;
    midi_msgs++;
    uint32_t m = 0;
    for (int i = 0; i < len && i < 3; ++i) m = (m << 8) | bytes[i];
    midi_last_msg = m;
}

extern bool _midi_channel_active[];
extern bool instrument_number_exists(int instrument_number, const char *tag);

void amy_rp2_midi_debug(uint32_t *uart_bytes, uint32_t *msgs, uint32_t *last_msg,
                        uint32_t *active_channels, uint32_t *synths) {
    *uart_bytes = midi_uart_bytes;
    *msgs = midi_msgs;
    *last_msg = midi_last_msg;
    // Bit n set = MIDI channel / synth n (1..16).
    uint32_t a = 0, e = 0;
    for (int ch = 1; ch <= 16; ++ch) {
        if (_midi_channel_active[ch]) a |= 1u << ch;
        if (instrument_number_exists(ch, NULL)) e |= 1u << ch;
    }
    *active_channels = a;
    *synths = e;
}

// ------------- AMY's stderr -------------
// AMY's sources are built with -Dfprintf=amy_rp2_fprintf; its messages land
// here for c_amy.log() instead of vanishing into newlib's stdio.

#define LOG_SIZE 4096
static char log_buf[LOG_SIZE];
static volatile uint32_t log_len = 0;

int amy_rp2_fprintf(FILE *f, const char *fmt, ...) {
    (void)f;
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return n;
    if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
    uint32_t len = log_len;
    if (len + n > LOG_SIZE) {
        len = 0;  // full: start over rather than grow
    }
    memcpy(log_buf + len, line, n);
    log_len = len + n;
    return n;
}

size_t amy_rp2_log_take(char *out, size_t max) {
    uint32_t len = log_len;
    if (len > max) len = max;
    memcpy(out, log_buf, len);
    log_len = 0;
    return len;
}

// ------------- core 1 -------------

static void core1_main(void) {
    // Lets core 0 pause us (in RAM) while it writes flash.
    multicore_lockout_victim_init();
    while (true) {
        while (!run_requested) {
            core1_state = CORE1_PARKED;
            __wfe();
        }
        core1_state = CORE1_RUNNING;
        while (run_requested) {
            amy_update();
        }
        if (AMY_HAS_I2S && i2s_dma >= 0) {
            // Let the last block finish so the DAC isn't left mid-sample.
            dma_channel_wait_for_finish_blocking(i2s_dma);
        }
    }
}

bool amy_rp2_running(void) {
    return amy_rp2_started;
}

bool amy_rp2_start(amy_config_t c) {
    if (amy_rp2_started) amy_rp2_stop();
    // AMY renders everything on core 1 itself; no AMY-side threads.
    c.platform.multicore = 0;
    c.platform.multithread = 0;
    ring_head = ring_tail = 0;
    underruns = 0;
    midi_uart_bytes = midi_msgs = midi_last_msg = 0;
    // If startup wedges (e.g. AMY touching a failed allocation), reboot rather
    // than hang USB; the crumbs say how far it got.
    watchdog_enable(5000, true);
    CRUMB(1);
    amy_start(c);
    CRUMB(4);
    next_midi_input_hook = amy_global.config.amy_external_midi_input_hook;
    amy_global.config.amy_external_midi_input_hook = count_midi_input;
    amy_rp2_started = true;
    if (!core1_launched) {
        // Paint the stack so amy_rp2_core1_stack_unused() can find the high-water mark.
        for (size_t i = 0; i < MP_ARRAY_SIZE(core1_stack); ++i) core1_stack[i] = 0xDEADBEEF;
        multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof(core1_stack));
        core1_launched = true;
    }
    CRUMB(5);
    run_requested = true;
    __sev();
    watchdog_disable();
    return true;
}

void amy_rp2_stop(void) {
    if (!amy_rp2_started) return;
    run_requested = false;
    __sev();
    while (core1_state != CORE1_PARKED) {
        tight_loop_contents();
    }
    amy_rp2_started = false;
    amy_stop();
}

void amy_rp2_soft_reset(void) {
    amy_rp2_stop();
}

bool amy_rp2_push(uint8_t type, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (!amy_rp2_started || len > MAX_MESSAGE_LEN) return false;
    size_t need = RING_HDR + len;
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (RING_SIZE - (ring_head - ring_tail) < need) {
        if (time_reached(deadline)) return false;
        tight_loop_contents();
    }
    uint32_t h = ring_head;
    ring[h & RING_MASK] = type;
    ring[(h + 1) & RING_MASK] = len & 0xff;
    ring[(h + 2) & RING_MASK] = len >> 8;
    for (size_t i = 0; i < len; ++i) {
        ring[(h + RING_HDR + i) & RING_MASK] = data[i];
    }
    __dmb();  // payload visible before the new head
    ring_head = h + need;
    return true;
}

uint32_t amy_rp2_core1_stack_unused(void) {
    size_t i = 0;
    while (i < MP_ARRAY_SIZE(core1_stack) && core1_stack[i] == 0xDEADBEEF) ++i;
    return i * sizeof(uint32_t);
}

uint32_t amy_rp2_take_peak(void) {
    uint32_t p = out_peak;
    out_peak = 0;
    return p;
}

uint32_t amy_rp2_underruns(void) {
    return underruns;
}
