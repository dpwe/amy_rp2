// amy_rp2_usb.c -- USB MIDI device for AMY, alongside MicroPython's REPL port.
//
// MicroPython owns TinyUSB here: tud_task() runs from its scheduler on core 0.
// We add TinyUSB's MIDI class (built with CFG_TUD_MIDI=1) by replacing the
// builtin configuration descriptor (linked with
// --wrap=tud_descriptor_configuration_cb, so the MicroPython tree is untouched)
// with MicroPython's CDC interfaces plus a MIDI interface.
//
// MIDI in:  tud_midi_rx_cb() runs inside tud_task() on core 0; the bytes go
//           into the command ring to core 1, like any other AMY input.
// MIDI out: midi_out() runs on core 1 (or core 0); bytes go into tx_ring, and
//           a 1 ms timer on core 0 schedules a MicroPython callback that
//           writes them to TinyUSB from tud_task()'s own context.  The same
//           tick schedules SPSS sysex handling (amy_rp2_sysex.c).

// MicroPython's QSTR scan (NO_QSTR) preprocesses this without TinyUSB's
// include paths; nothing here defines Python-visible names, so skip it.
#ifndef NO_QSTR

#include <string.h>

#include "py/mpconfig.h"
#include "py/runtime.h"
#include "tusb.h"
#include "mp_usbd.h"

#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico/time.h"

#include "amy_rp2.h"

// ------------- descriptor -------------

#define USBD_CDC_CMD_MAX_SIZE (8)
#define USBD_CDC_IN_OUT_MAX_SIZE (64)

enum {
    ITF_NUM_MIDI = USBD_ITF_BUILTIN_MAX,  // after MicroPython's CDC pair
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL,
};

#define EPNUM_MIDI_OUT (0x03)  // MicroPython's CDC uses 0x81, 0x82/0x02
#define EPNUM_MIDI_IN (0x83)

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

static const uint8_t amy_rp2_desc_cfg[CONFIG_TOTAL_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, USBD_STR_0, CONFIG_TOTAL_LEN, 0, USBD_MAX_POWER_MA),
    TUD_CDC_DESCRIPTOR(USBD_ITF_CDC, USBD_STR_CDC, USBD_CDC_EP_CMD,
        USBD_CDC_CMD_MAX_SIZE, USBD_CDC_EP_OUT, USBD_CDC_EP_IN, USBD_CDC_IN_OUT_MAX_SIZE),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

const uint8_t *__wrap_tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return amy_rp2_desc_cfg;
}

// ------------- MIDI in -------------

void tud_midi_rx_cb(uint8_t itf) {
    uint8_t buf[64];
    uint32_t n;
    while ((n = tud_midi_n_stream_read(itf, 0, buf, sizeof(buf))) > 0) {
        if (amy_rp2_running() && (amy_global.config.midi & AMY_MIDI_IS_USB_GADGET)) {
            amy_rp2_push(AMY_RP2_CMD_MIDI, buf, n, 50);
        }
    }
}

// ------------- MIDI out -------------

#define TX_SIZE 16384  // power of two; big enough for an AMY state dump from core 1
#define TX_MASK (TX_SIZE - 1)

static uint8_t tx_ring[TX_SIZE] __attribute__((section(".psram_uninitialised.amy_tx_ring")));
static volatile uint32_t tx_head = 0;  // producers, under tx_lock
static volatile uint32_t tx_tail = 0;  // core 0 pump only
static spin_lock_t *tx_lock = NULL;
static uint32_t tx_dropped = 0;

static mp_sched_node_t pump_node;
static repeating_timer_t pump_timer;
static bool pump_timer_running = false;

static void pump(mp_sched_node_t *node);

void amy_rp2_usb_midi_write(const uint8_t *bytes, size_t len) {
    if (tx_lock == NULL || !tud_midi_mounted() || len > TX_SIZE) return;
    if (get_core_num() == 0) {
        // Core 0 writers run in tud_task()'s context (the sysex handler, e.g.
        // a zD file dump), so instead of dropping, drain to TinyUSB until it fits.
        absolute_time_t deadline = make_timeout_time_ms(500);
        while (TX_SIZE - (tx_head - tx_tail) < len && !time_reached(deadline)) {
            pump(NULL);
            tud_task_ext(0, false);
        }
    }
    uint32_t save = spin_lock_blocking(tx_lock);
    if (TX_SIZE - (tx_head - tx_tail) >= len) {
        for (size_t i = 0; i < len; ++i) {
            tx_ring[(tx_head + i) & TX_MASK] = bytes[i];
        }
        __dmb();
        tx_head += len;
    } else {
        tx_dropped++;  // host isn't reading; drop whole messages, never split them
    }
    spin_unlock(tx_lock, save);
}

static void pump(mp_sched_node_t *node) {
    (void)node;
    while (tx_tail != tx_head) {
        __dmb();
        uint32_t t = tx_tail;
        uint32_t n = tx_head - t;
        uint32_t contiguous = TX_SIZE - (t & TX_MASK);
        if (n > contiguous) n = contiguous;
        uint32_t written = tud_midi_n_stream_write(0, 0, &tx_ring[t & TX_MASK], n);
        tx_tail = t + written;
        if (written < n) break;  // TinyUSB's FIFO is full; the next tick resumes
    }
}

static bool pump_tick(repeating_timer_t *rt) {
    (void)rt;
    if (tx_tail != tx_head) {
        mp_sched_schedule_node(&pump_node, pump);
    }
    amy_rp2_sysex_tick();
    amy_rp2_tulip_tick();
    return true;
}

void amy_rp2_usb_init(void) {
    // Called on core 0 from amy_rp2_start(); the timer fires on core 0.
    if (tx_lock == NULL) {
        tx_lock = spin_lock_init(spin_lock_claim_unused(true));
    }
    if (!pump_timer_running) {
        add_repeating_timer_us(-1000, pump_tick, NULL, &pump_timer);
        pump_timer_running = true;
    }
}

bool amy_rp2_usb_up(void) {
    return tud_inited();
}

uint32_t amy_rp2_usb_midi_dropped(void) {
    return tx_dropped;
}

#endif  // NO_QSTR
