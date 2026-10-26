// amy_rp2_sysex.c -- AMYboard-style SPSS sysex handling on core 0.
//
// amyboard.com/editor talks to an AMYboard with sysex frames F0 00 03 45 ... F7
// carrying AMY wire messages, AMY's transfer-layer 'z' commands (zT file
// transfer, zP Python exec, zD dumps, zB reboot, zI ping) and file-transfer
// payload.  On AMYboard these run in MicroPython's task, with AMY's locks
// guarding its state.  Here AMY runs alone on core 1 with no-op locks, so:
//
//  - core 1's scanner (amy_rp2.c) pulls complete SPSS frames out of the MIDI
//    byte stream and queues them here (amy_rp2_sysex_push);
//  - zB (reboot) and zI (ping) are answered right there on core 1, in plain C,
//    so they work even while Python is busy or stuck (as AMYboard does them in
//    parse_sysex);
//  - a MicroPython scheduler callback on core 0 handles the rest: file
//    transfers, zP, zD <file> here (they need the filesystem or the VM, and
//    only touch AMY's transfer state); everything else goes to core 1 through
//    the command ring, and we wait until core 1 has processed it;
//  - then, like AMYboard, ACK with F0 00 03 45 'A' 'K' F7.
//
// Python run from here (zP, the sketch restart after a transfer) can take long
// or never return, and on rp2 TinyUSB itself runs from the scheduler.  So that
// code runs from a Python-level scheduled callback with the scheduler unlocked
// (amy_rp2_call_unlocked): USB and other callbacks keep running, and Ctrl-C
// can stop it.
//
// AMY's file/exec/reboot hooks are implemented here too, for core 0 only.

#include <string.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/builtin.h"
#include "py/mphal.h"
#include "extmod/vfs.h"
#include "shared/runtime/interrupt_char.h"
#include "shared/readline/readline.h"  // CHAR_CTRL_C

#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/platform.h"

#include "amy_rp2.h"

extern void midi_out(uint8_t *bytes, uint16_t len);
extern uint16_t amy_parse_transfer_layer_message(char *message);
extern void parse_transfer_message(char *message, uint16_t len);

#define MAX_FILES 8  // open-file handles for AMY's file hooks (below)

// ------------- frame queue (core 1 -> core 0) -------------

#define FRAMEQ_SIZE 32768  // power of two
#define FRAMEQ_MASK (FRAMEQ_SIZE - 1)

static uint8_t frameq[FRAMEQ_SIZE] __attribute__((section(".psram_uninitialised.amy_frameq")));
static volatile uint32_t frameq_head = 0;  // core 1 only
static volatile uint32_t frameq_tail = 0;  // core 0 only
static uint32_t frames_dropped = 0;

static char frame[AMY_RP2_SYSEX_FRAME_MAX + 1];

bool amy_rp2_sysex_push(const uint8_t *payload, size_t len) {
    // Core 1. payload excludes F0, the 00 03 45 id and F7.
    if (len > AMY_RP2_SYSEX_FRAME_MAX) {
        frames_dropped++;
        return false;
    }
    uint32_t need = 2 + len;
    if (FRAMEQ_SIZE - (frameq_head - frameq_tail) < need) {
        frames_dropped++;
        return false;
    }
    uint32_t h = frameq_head;
    frameq[h & FRAMEQ_MASK] = len & 0xff;
    frameq[(h + 1) & FRAMEQ_MASK] = len >> 8;
    for (size_t i = 0; i < len; ++i) {
        frameq[(h + 2 + i) & FRAMEQ_MASK] = payload[i];
    }
    __dmb();
    frameq_head = h + need;
    return true;
}

// ------------- frame handling (core 0) -------------

static void send_spss(const char *two) {
    uint8_t m[] = { 0xF0, 0x00, 0x03, 0x45, (uint8_t)two[0], (uint8_t)two[1], 0xF7 };
    midi_out(m, sizeof(m));
}

static void to_core1(uint8_t type, const char *msg, size_t len) {
    if (!amy_rp2_push(type, (const uint8_t *)msg, len, 200)) return;
    amy_rp2_wait_idle(200);  // ACK only once core 1 has acted on it, as AMYboard does
}

void amy_rp2_reboot(uint8_t mode) {
    // Any core.
    if (mode == 2) {
        reset_usb_boot(0, 0);  // ROM download (BOOTSEL) mode
    }
    // Mode 0 asks the next boot to skip the sketch; leave that for boot code to read.
    watchdog_hw->scratch[3] = (mode == 0) ? AMY_RP2_SKIP_SKETCH_MAGIC : 0;
    watchdog_reboot(0, 0, 10);
    for (;;) {
        tight_loop_contents();
    }
}

static void handle_frame(char *msg, size_t len) {
    if (amy_global.transfer_flag == AMY_TRANSFER_TYPE_FILE) {
        parse_transfer_message(msg, len);  // payload chunk -> fwrite hook
    } else if (amy_global.transfer_flag != AMY_TRANSFER_TYPE_NONE) {
        to_core1(AMY_RP2_CMD_WIRE_SYSEX, msg, len);  // sample/audio payload: AMY's RAM
    } else if (len >= 2 && msg[0] == 'z') {
        switch (msg[1]) {
            case 'T':  // file transfer start
            case 'P':  // Python exec
                amy_parse_transfer_layer_message(msg + 1);
                break;
            case 'D':
                if (len > 2 && msg[2] != 'Z') {
                    amy_parse_transfer_layer_message(msg + 1);  // file dump: needs the filesystem
                } else {
                    to_core1(AMY_RP2_CMD_WIRE_SYSEX, msg, len);  // AMY state dump: core 1's data
                }
                break;
            default:
                to_core1(AMY_RP2_CMD_WIRE_SYSEX, msg, len);
                break;
        }
    } else {
        to_core1(AMY_RP2_CMD_WIRE_SYSEX, msg, len);
    }
    send_spss("AK");
}

mp_obj_t amy_rp2_call_unlocked(mp_obj_t fn, mp_obj_t arg) {
    // For Python-level scheduled callbacks only (the scheduler is locked while
    // they run, and nothing walks its queue after they return).  Unlock it
    // around fn so USB, MIDI and the other callbacks carry on if fn is slow or
    // hangs, and Ctrl-C can interrupt it; report rather than propagate errors.
    // Ctrl-C only interrupts while MicroPython is executing code; when this was
    // scheduled from the REPL's idle input wait it's off, so turn it on.
    int saved_interrupt_char = mp_interrupt_char;
    mp_hal_set_interrupt_char(CHAR_CTRL_C);
    mp_sched_unlock();
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_call_function_1(fn, arg);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    }
    mp_sched_lock();
    mp_hal_set_interrupt_char(saved_interrupt_char);
    return mp_const_none;
}

static mp_obj_t call0(mp_obj_t fn) {
    return mp_call_function_0(fn);
}
static MP_DEFINE_CONST_FUN_OBJ_1(call0_obj, call0);

static mp_obj_t call_soon_unlocked_tramp(mp_obj_t fn) {
    return amy_rp2_call_unlocked(MP_OBJ_FROM_PTR(&call0_obj), fn);
}
static MP_DEFINE_CONST_FUN_OBJ_1(call_soon_unlocked_tramp_obj, call_soon_unlocked_tramp);

MP_REGISTER_ROOT_POINTER(mp_obj_t amy_rp2_after_usb_fn);

static mp_obj_t call_soon_unlocked(mp_obj_t fn) {
    // c_amy.call_soon_unlocked(fn): run fn() from the scheduler, unlocked, once
    // MicroPython has brought USB up.  It does that only after boot.py returns,
    // and a scheduled callback can run at any bytecode boundary -- even in
    // boot.py -- so the timer tick waits for TinyUSB before queuing it.
    // boot.py uses this to start the sketch: one that hangs then leaves USB,
    // zB/zI and Ctrl-C working.
    MP_STATE_PORT(amy_rp2_after_usb_fn) = fn;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(amy_rp2_call_soon_unlocked_obj, call_soon_unlocked);

static volatile bool pump_queued = false;
static bool pump_running = false;

static mp_obj_t sysex_pump_body(mp_obj_t unused) {
    (void)unused;
    while (frameq_tail != frameq_head) {
        __dmb();
        uint32_t t = frameq_tail;
        size_t len = frameq[t & FRAMEQ_MASK] | (frameq[(t + 1) & FRAMEQ_MASK] << 8);
        for (size_t i = 0; i < len; ++i) {
            frame[i] = frameq[(t + 2 + i) & FRAMEQ_MASK];
        }
        frame[len] = '\0';
        __dmb();
        frameq_tail = t + 2 + len;
        if (amy_rp2_running()) {
            handle_frame(frame, len);
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sysex_pump_body_obj, sysex_pump_body);

static mp_obj_t sysex_pump(mp_obj_t unused) {
    pump_queued = false;
    if (pump_running) return mp_const_none;  // nested (scheduler unlocked below): the outer run drains
    pump_running = true;
    amy_rp2_call_unlocked(MP_OBJ_FROM_PTR(&sysex_pump_body_obj), mp_const_none);
    pump_running = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sysex_pump_obj, sysex_pump);

void amy_rp2_sysex_soft_reset(void) {
    // MicroPython is wiping its heap and scheduler queue (a queued pump goes
    // with it): forget what referred to them.
    pump_queued = false;
    pump_running = false;
    MP_STATE_PORT(amy_rp2_after_usb_fn) = MP_OBJ_NULL;
    for (int h = 0; h < MAX_FILES; ++h) MP_STATE_PORT(amy_rp2_files)[h] = MP_OBJ_NULL;
}

void amy_rp2_sysex_tick(void) {
    // Core 0 timer IRQ (amy_rp2_usb.c).
    mp_obj_t later = MP_STATE_PORT(amy_rp2_after_usb_fn);
    if (later != MP_OBJ_NULL && amy_rp2_usb_up()) {
        if (mp_sched_schedule(MP_OBJ_FROM_PTR(&call_soon_unlocked_tramp_obj), later)) {
            MP_STATE_PORT(amy_rp2_after_usb_fn) = MP_OBJ_NULL;
        }
    }
    if (frameq_tail != frameq_head && !pump_queued && !pump_running) {
        if (mp_sched_schedule(MP_OBJ_FROM_PTR(&sysex_pump_obj), mp_const_none)) pump_queued = true;
    }
}

uint32_t amy_rp2_sysex_dropped(void) {
    return frames_dropped;
}

// ------------- AMY hooks (core 0) -------------

MP_REGISTER_ROOT_POINTER(mp_obj_t amy_rp2_files[MAX_FILES]);

static bool on_core0(const char *what) {
    if (get_core_num() == 0) return true;
    fprintf(stderr, "%s: not available on core 1\n", what);
    return false;
}

static mp_obj_t file_obj(uint32_t h) {
    return (h >= 1 && h < MAX_FILES) ? MP_STATE_PORT(amy_rp2_files)[h] : MP_OBJ_NULL;
}

static void mkdirs_for(const char *path) {
    // mkdir -p the parent directories of path (best effort).
    char dir[MAX_FILENAME_LEN + 1];
    strncpy(dir, path, MAX_FILENAME_LEN);
    dir[MAX_FILENAME_LEN] = '\0';
    for (char *p = dir + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_vfs_mkdir(mp_obj_new_str(dir, strlen(dir)));
            nlr_pop();
        }  // else: it already exists (or can't be made; open will say)
        *p = '/';
    }
}

static uint32_t fopen_hook(char *filename, const char *mode) {
    if (!on_core0("fopen")) return 0;
    uint32_t h = 1;
    while (h < MAX_FILES && MP_STATE_PORT(amy_rp2_files)[h] != MP_OBJ_NULL) ++h;
    if (h == MAX_FILES) return 0;
    if (mode[0] == 'w' || mode[0] == 'a') mkdirs_for(filename);
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t args[2] = { mp_obj_new_str(filename, strlen(filename)), mp_obj_new_str(mode, strlen(mode)) };
        MP_STATE_PORT(amy_rp2_files)[h] = mp_vfs_open(2, args, (mp_map_t *)&mp_const_empty_map);
        nlr_pop();
        return h;
    }
    return 0;
}

static uint32_t fwrite_hook(uint32_t h, uint8_t *bytes, uint32_t len) {
    mp_obj_t f = file_obj(h);
    if (f == MP_OBJ_NULL || !on_core0("fwrite")) return 0;
    int err;
    return mp_stream_rw(f, bytes, len, &err, MP_STREAM_RW_WRITE);
}

static uint32_t fread_hook(uint32_t h, uint8_t *bytes, uint32_t len) {
    mp_obj_t f = file_obj(h);
    if (f == MP_OBJ_NULL || !on_core0("fread")) return 0;
    int err;
    return mp_stream_rw(f, bytes, len, &err, MP_STREAM_RW_READ);
}

static void fseek_hook(uint32_t h, uint32_t pos) {
    mp_obj_t f = file_obj(h);
    if (f == MP_OBJ_NULL || !on_core0("fseek")) return;
    int err;
    mp_stream_seek(f, pos, MP_SEEK_SET, &err);
}

static void fclose_hook(uint32_t h) {
    mp_obj_t f = file_obj(h);
    if (f == MP_OBJ_NULL || !on_core0("fclose")) return;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_stream_close(f);
        nlr_pop();
    }
    MP_STATE_PORT(amy_rp2_files)[h] = MP_OBJ_NULL;
}

static void exec_hook(const char *code) {
    if (!on_core0("exec")) return;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_call_function_1(MP_OBJ_FROM_PTR(&mp_builtin_exec_obj), mp_obj_new_str(code, strlen(code)));
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    }
}

static mp_obj_t environment_transfer_done(mp_obj_t unused) {
    // Same as AMYboard: a new sketch.py arrived, let amyboard.py pick it up.
    // The sketch's top-level code runs here, so keep the scheduler unlocked.
    mp_obj_t mod = mp_import_name(MP_QSTR_amyboard, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    return amy_rp2_call_unlocked(mp_load_attr(mod, MP_QSTR_environment_transfer_done), mp_const_none);
}
static MP_DEFINE_CONST_FUN_OBJ_1(environment_transfer_done_obj, environment_transfer_done);

static void file_transfer_done_hook(const char *filename) {
    const char *slash = strrchr(filename, '/');
    const char *leaf = slash ? slash + 1 : filename;
    if (strcmp(leaf, "sketch.py") == 0) {
        mp_sched_schedule(MP_OBJ_FROM_PTR(&environment_transfer_done_obj), mp_const_none);
    }
}

static void reboot_hook(uint8_t mode) {
    amy_rp2_reboot(mode);
}

void amy_rp2_sysex_install_hooks(amy_config_t *c) {
    c->amy_external_fopen_hook = fopen_hook;
    c->amy_external_fwrite_hook = fwrite_hook;
    c->amy_external_fread_hook = fread_hook;
    c->amy_external_fseek_hook = fseek_hook;
    c->amy_external_fclose_hook = fclose_hook;
    c->amy_external_exec_hook = exec_hook;
    c->amy_external_file_transfer_done_hook = file_transfer_done_hook;
    c->amy_external_reboot_hook = reboot_hook;
}
