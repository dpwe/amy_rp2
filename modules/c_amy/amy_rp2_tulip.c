// amy_rp2_tulip.c -- the C half of the _tulip functions amyboard.py relies on
// (py/_tulip.py holds the rest).
//
// AMY's sequencer, MIDI-input and overload hooks fire on core 1, but the Python
// callbacks they feed must run on core 0.  So the hooks only record what is due;
// amy_rp2_tulip_tick(), from the 1 ms core-0 timer (amy_rp2_usb.c), schedules
// the callbacks.  Semantics follow Tulip's modtulip.c / tsequencer.c.

#include <string.h>

#include "py/runtime.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"

#include "amy_rp2.h"

// ------------- sequencer callbacks: seq_add_callback(fn, tick, period) -------------

#define SEQ_SLOTS 8
MP_REGISTER_ROOT_POINTER(mp_obj_t amy_rp2_seq_callbacks[SEQ_SLOTS]);

static volatile uint32_t seq_period[SEQ_SLOTS];  // 0 = slot free
static volatile uint32_t seq_offset[SEQ_SLOTS];
static volatile uint32_t seq_due_tick[SEQ_SLOTS];
static volatile bool seq_due[SEQ_SLOTS];
static uint32_t seq_run_tick[SEQ_SLOTS];  // the tick handed to the callback

static void seq_hook(uint32_t tick) {
    // Core 1, once per AMY sequencer tick: fn(tick) is due when tick % period == offset.
    for (int i = 0; i < SEQ_SLOTS; ++i) {
        uint32_t p = seq_period[i];
        if (p && tick % p == seq_offset[i]) {
            seq_due_tick[i] = tick;
            seq_due[i] = true;
        }
    }
}

extern mp_obj_t amy_rp2_call_unlocked(mp_obj_t fn, mp_obj_t arg);

static mp_obj_t seq_trampoline(mp_obj_t slot) {
    // Scheduled for a due slot: run its callback (a sketch's loop(tick), via
    // amyboard's TulipSequence) with the scheduler unlocked, so a slow or stuck
    // loop() doesn't freeze USB.
    int i = MP_OBJ_SMALL_INT_VALUE(slot);
    mp_obj_t fn = MP_STATE_PORT(amy_rp2_seq_callbacks)[i];
    if (fn == MP_OBJ_NULL) return mp_const_none;
    return amy_rp2_call_unlocked(fn, mp_obj_new_int_from_uint(seq_run_tick[i]));
}
static MP_DEFINE_CONST_FUN_OBJ_1(seq_trampoline_obj, seq_trampoline);

static mp_obj_t seq_add_callback(mp_obj_t fn, mp_obj_t tick, mp_obj_t period) {
    for (int i = 0; i < SEQ_SLOTS; ++i) {
        if (seq_period[i] == 0 && MP_STATE_PORT(amy_rp2_seq_callbacks)[i] == MP_OBJ_NULL) {
            MP_STATE_PORT(amy_rp2_seq_callbacks)[i] = fn;
            seq_offset[i] = mp_obj_get_int(tick);
            seq_due[i] = false;
            __dmb();
            seq_period[i] = mp_obj_get_int(period);  // arms the slot for core 1
            return MP_OBJ_NEW_SMALL_INT(i);
        }
    }
    return MP_OBJ_NEW_SMALL_INT(-1);
}
MP_DEFINE_CONST_FUN_OBJ_3(amy_rp2_seq_add_callback_obj, seq_add_callback);

static void seq_free(int i) {
    seq_period[i] = 0;
    seq_due[i] = false;
    MP_STATE_PORT(amy_rp2_seq_callbacks)[i] = MP_OBJ_NULL;
}

static mp_obj_t seq_remove_callback(mp_obj_t index) {
    int i = mp_obj_get_int(index);
    if (i >= 0 && i < SEQ_SLOTS) seq_free(i);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(amy_rp2_seq_remove_callback_obj, seq_remove_callback);

static mp_obj_t seq_remove_callbacks(void) {
    for (int i = 0; i < SEQ_SLOTS; ++i) seq_free(i);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(amy_rp2_seq_remove_callbacks_obj, seq_remove_callbacks);

static mp_obj_t seq_ticks(void) {
    return mp_obj_new_int_from_uint(amy_global.sequencer_tick_count);
}
MP_DEFINE_CONST_FUN_OBJ_0(amy_rp2_seq_ticks_obj, seq_ticks);

// ------------- MIDI in for Python: midi_callback(fn), midi_in() -------------

#define MIDIQ_DEPTH 64
MP_REGISTER_ROOT_POINTER(mp_obj_t amy_rp2_midi_callback);

static uint8_t midiq[MIDIQ_DEPTH][3];
static uint8_t midiq_len[MIDIQ_DEPTH];
static volatile uint32_t midiq_head = 0;  // core 1
static volatile uint32_t midiq_tail = 0;  // core 0
static volatile bool midi_cb_pending = false;

void amy_rp2_tulip_midi_in(uint8_t *bytes, uint16_t len, uint8_t is_sysex) {
    // Core 1 (AMY's MIDI input hook).  Sysex isn't passed to Python yet.
    if (is_sysex || len == 0 || len > 3) return;
    if (midiq_head - midiq_tail >= MIDIQ_DEPTH) return;  // Python isn't keeping up: drop
    uint32_t h = midiq_head % MIDIQ_DEPTH;
    memcpy(midiq[h], bytes, len);
    midiq_len[h] = len;
    __dmb();
    midiq_head++;
}

static mp_obj_t midi_callback(mp_obj_t fn) {
    MP_STATE_PORT(amy_rp2_midi_callback) = fn;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(amy_rp2_midi_callback_obj, midi_callback);

static mp_obj_t midi_in(void) {
    if (midiq_tail == midiq_head) return mp_const_none;
    __dmb();
    uint32_t t = midiq_tail % MIDIQ_DEPTH;
    mp_obj_t m = mp_obj_new_bytes(midiq[t], midiq_len[t]);
    __dmb();
    midiq_tail++;
    return m;
}
MP_DEFINE_CONST_FUN_OBJ_0(amy_rp2_midi_in_obj, midi_in);

static mp_obj_t midi_trampoline(mp_obj_t is_sysex) {
    // Scheduled instead of the Python callback itself: clear the pending flag
    // first, so a callback that raises (a sketch's midi.add_callback handler,
    // say) can't leave MIDI to Python blocked for good; run it unlocked.
    midi_cb_pending = false;
    mp_obj_t cb = MP_STATE_PORT(amy_rp2_midi_callback);
    if (cb == MP_OBJ_NULL) return mp_const_none;
    return amy_rp2_call_unlocked(cb, is_sysex);  // errors are reported, not raised
}
static MP_DEFINE_CONST_FUN_OBJ_1(midi_trampoline_obj, midi_trampoline);

static mp_obj_t sysex_in(void) {
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(amy_rp2_sysex_in_obj, sysex_in);

// ------------- overload: amy_overload_callback(fn) -------------

MP_REGISTER_ROOT_POINTER(mp_obj_t amy_rp2_overload_callback);
static volatile int32_t overload_pct = -1;

static void overload_hook(float load) {
    overload_pct = (int32_t)(load * 100.0f);  // core 1; AMY has already reset itself
}

static mp_obj_t overload_callback(mp_obj_t fn) {
    MP_STATE_PORT(amy_rp2_overload_callback) = fn;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(amy_rp2_overload_callback_obj, overload_callback);

// ------------- bootloader_mode(): did zB0 ask to skip the sketch? -------------

static mp_obj_t bootloader_mode(void) {
    bool skip = watchdog_hw->scratch[3] == AMY_RP2_SKIP_SKETCH_MAGIC;
    watchdog_hw->scratch[3] = 0;  // one boot only
    return mp_obj_new_bool(skip);
}
MP_DEFINE_CONST_FUN_OBJ_0(amy_rp2_bootloader_mode_obj, bootloader_mode);

// ------------- glue -------------

void amy_rp2_tulip_soft_reset(void) {
    // MicroPython is wiping its heap and scheduler queue: drop every reference
    // into it, or the next sequencer tick calls a freed loop().
    for (int i = 0; i < SEQ_SLOTS; ++i) seq_free(i);
    MP_STATE_PORT(amy_rp2_midi_callback) = MP_OBJ_NULL;
    MP_STATE_PORT(amy_rp2_overload_callback) = MP_OBJ_NULL;
    midi_cb_pending = false;
    midiq_tail = midiq_head;
    overload_pct = -1;
}

void amy_rp2_tulip_install_hooks(amy_config_t *c) {
    c->amy_external_sequencer_hook = seq_hook;
    c->amy_external_overload_hook = overload_hook;
}

void amy_rp2_tulip_tick(void) {
    // Core 0 timer IRQ: schedule whatever core 1 marked due.
    for (int i = 0; i < SEQ_SLOTS; ++i) {
        if (seq_due[i]) {
            seq_run_tick[i] = seq_due_tick[i];
            if (MP_STATE_PORT(amy_rp2_seq_callbacks)[i] == MP_OBJ_NULL
                || mp_sched_schedule(MP_OBJ_FROM_PTR(&seq_trampoline_obj), MP_OBJ_NEW_SMALL_INT(i))) {
                seq_due[i] = false;  // else the schedule queue is full: retry next tick
            }
        }
    }
    mp_obj_t mcb = MP_STATE_PORT(amy_rp2_midi_callback);
    if (midiq_tail != midiq_head && !midi_cb_pending && mcb != MP_OBJ_NULL) {
        if (mp_sched_schedule(MP_OBJ_FROM_PTR(&midi_trampoline_obj), mp_const_false)) midi_cb_pending = true;
    }
    int32_t pct = overload_pct;
    mp_obj_t ocb = MP_STATE_PORT(amy_rp2_overload_callback);
    if (pct >= 0) {
        if (ocb == MP_OBJ_NULL || mp_sched_schedule(ocb, MP_OBJ_NEW_SMALL_INT(pct))) overload_pct = -1;
    }
}
