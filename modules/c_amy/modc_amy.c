// modc_amy.c -- the `c_amy` MicroPython module for rp2.
//
// Named and shaped like AMY's CPython binding (src/pyamy.c), so AMY's own Python
// package (`import amy`, frozen in) finds it and works unchanged:
//
//   import amy
//   amy.live()                       # I2S + UART MIDI on the AMY_MIDI_Synth pins
//   amy.send(synth=1, note=60, vel=1)
//
// live() kwargs (all optional): the feature flags chorus, reverb, echo, audio_in,
// default_synths, partials, custom, startup_bleep; max_oscs, max_voices,
// max_synths; pins bclk (lrc is always bclk + 1), dout, din, mclk, midi_in,
// midi_out, midi_uart; midi (False disables UART MIDI) and usb_midi (False
// disables USB MIDI).

#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"

#include "amy_rp2.h"
#include "hardware/structs/watchdog.h"

// Pin defaults match amy/examples/AMY_MIDI_Synth.
#define DEFAULT_I2S_BCLK 8   // LRCLK on 9
#define DEFAULT_I2S_DOUT 10
#define DEFAULT_MIDI_UART 1
#define DEFAULT_MIDI_OUT 4   // UART1 TX
#define DEFAULT_MIDI_IN 5    // UART1 RX

#define CMD_TIMEOUT_MS 1000

static int kw_int(mp_obj_t v, int lo, int hi, qstr name) {
    mp_int_t i = mp_obj_get_int(v);
    if (i < lo || i > hi) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%q out of range"), name);
    }
    return (int)i;
}

static mp_obj_t c_amy_live(size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
    if (n_args != 0) {
        mp_raise_TypeError(MP_ERROR_TEXT("live() takes keyword args only"));
    }
    amy_config_t c = amy_default_config();
    c.audio = AMY_AUDIO_IS_I2S;
    c.midi = AMY_MIDI_IS_UART | AMY_MIDI_IS_USB_GADGET;
    c.features.default_synths = 1;
    c.features.startup_bleep = 1;
    c.i2s_bclk = DEFAULT_I2S_BCLK;
    c.i2s_lrc = DEFAULT_I2S_BCLK + 1;
    c.i2s_dout = DEFAULT_I2S_DOUT;
    c.i2s_din = -1;
    c.i2s_mclk = -1;
    c.midi_uart = DEFAULT_MIDI_UART;
    c.midi_out = DEFAULT_MIDI_OUT;
    c.midi_in = DEFAULT_MIDI_IN;

    // Validate everything before touching a running AMY.
    for (size_t i = 0; i < kw->alloc; ++i) {
        if (!mp_map_slot_is_filled(kw, i)) continue;
        qstr k = mp_obj_str_get_qstr(kw->table[i].key);
        mp_obj_t v = kw->table[i].value;
        switch (k) {
            case MP_QSTR_chorus: c.features.chorus = mp_obj_is_true(v); break;
            case MP_QSTR_reverb: c.features.reverb = mp_obj_is_true(v); break;
            case MP_QSTR_echo: c.features.echo = mp_obj_is_true(v); break;
            case MP_QSTR_audio_in: c.features.audio_in = mp_obj_is_true(v); break;
            case MP_QSTR_default_synths: c.features.default_synths = mp_obj_is_true(v); break;
            case MP_QSTR_partials: c.features.partials = mp_obj_is_true(v); break;
            case MP_QSTR_custom: c.features.custom = mp_obj_is_true(v); break;
            case MP_QSTR_startup_bleep: c.features.startup_bleep = mp_obj_is_true(v); break;
            case MP_QSTR_max_oscs: c.max_oscs = kw_int(v, 1, 65535, k); break;
            case MP_QSTR_max_voices: c.max_voices = kw_int(v, 1, 65535, k); break;
            case MP_QSTR_max_synths: c.max_synths = kw_int(v, 1, 65535, k); break;
            case MP_QSTR_bclk:
                c.i2s_bclk = kw_int(v, 0, 46, k);
                c.i2s_lrc = c.i2s_bclk + 1;
                break;
            case MP_QSTR_dout: c.i2s_dout = kw_int(v, 0, 47, k); break;
            case MP_QSTR_din: c.i2s_din = kw_int(v, -1, 47, k); break;
            case MP_QSTR_mclk: c.i2s_mclk = kw_int(v, -1, 47, k); break;
            case MP_QSTR_midi_in: c.midi_in = kw_int(v, -1, 47, k); break;
            case MP_QSTR_midi_out: c.midi_out = kw_int(v, -1, 47, k); break;
            case MP_QSTR_midi_uart: c.midi_uart = kw_int(v, 0, 1, k); break;
            case MP_QSTR_midi:
                if (mp_obj_is_true(v)) c.midi |= AMY_MIDI_IS_UART; else c.midi &= ~AMY_MIDI_IS_UART;
                break;
            case MP_QSTR_usb_midi:
                if (mp_obj_is_true(v)) c.midi |= AMY_MIDI_IS_USB_GADGET; else c.midi &= ~AMY_MIDI_IS_USB_GADGET;
                break;
            default:
                mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("unexpected keyword argument '%q'"), k);
        }
    }
    if (c.features.audio_in) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("audio_in not supported yet"));
    }
    amy_rp2_start(c);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(c_amy_live_obj, 0, c_amy_live);

static mp_obj_t c_amy_stop(void) {
    amy_rp2_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_stop_obj, c_amy_stop);

static void push(uint8_t type, const uint8_t *data, size_t len) {
    if (!amy_rp2_running()) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("AMY not running; call amy.live()"));
    }
    if (len > MAX_MESSAGE_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("message too long"));
    }
    if (!amy_rp2_push(type, data, len, CMD_TIMEOUT_MS)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("AMY command queue full"));
    }
}

static mp_obj_t c_amy_send_wire(mp_obj_t msg) {
    size_t len;
    const char *s = mp_obj_str_get_data(msg, &len);
    push(AMY_RP2_CMD_WIRE, (const uint8_t *)s, len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(c_amy_send_wire_obj, c_amy_send_wire);

static mp_obj_t c_amy_send_wire_from_sysex(mp_obj_t msg) {
    size_t len;
    const char *s = mp_obj_str_get_data(msg, &len);
    push(AMY_RP2_CMD_WIRE_SYSEX, (const uint8_t *)s, len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(c_amy_send_wire_from_sysex_obj, c_amy_send_wire_from_sysex);

static mp_obj_t c_amy_inject_midi_bytes(size_t n_args, const mp_obj_t *args) {
    // (data, usb=0) as in the CPython binding: a raw MIDI byte stream, or with
    // usb=1, 3-byte-padded events (the parser stops after one message per call).
    mp_buffer_info_t buf;
    mp_get_buffer_raise(args[0], &buf, MP_BUFFER_READ);
    bool usb = n_args > 1 && mp_obj_is_true(args[1]);
    push(usb ? AMY_RP2_CMD_MIDI_USB : AMY_RP2_CMD_MIDI, buf.buf, buf.len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(c_amy_inject_midi_bytes_obj, 1, 2, c_amy_inject_midi_bytes);

static mp_obj_t c_amy_process_single_midi_byte(size_t n_args, const mp_obj_t *args) {
    // (byte, from_web_or_usb=1) as in the CPython binding.
    uint8_t b = mp_obj_get_int(args[0]);
    bool usb = n_args < 2 || mp_obj_is_true(args[1]);
    push(usb ? AMY_RP2_CMD_MIDI_USB : AMY_RP2_CMD_MIDI, &b, 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(c_amy_process_single_midi_byte_obj, 1, 2, c_amy_process_single_midi_byte);

static mp_obj_t c_amy_ticks_ms(void) {
    return mp_obj_new_int_from_uint(amy_sysclock());
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_ticks_ms_obj, c_amy_ticks_ms);

static mp_obj_t c_amy_sequencer_ticks(void) {
    return mp_obj_new_int_from_uint(amy_global.sequencer_tick_count);
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_sequencer_ticks_obj, c_amy_sequencer_ticks);

static mp_obj_t c_amy_render_load(void) {
    return mp_obj_new_float((float)amy_global.render_us / (float)AMY_BLOCK_US);
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_render_load_obj, c_amy_render_load);

static mp_obj_t c_amy_set_render_load_threshold(mp_obj_t t) {
    // Two plain stores read once per block by core 1; no lock needed.
    amy_set_render_load_threshold(mp_obj_get_float(t));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(c_amy_set_render_load_threshold_obj, c_amy_set_render_load_threshold);

static mp_obj_t c_amy_bleep(size_t n_args, const mp_obj_t *args) {
    char m[16];
    int n = snprintf(m, sizeof(m), "%lu", (unsigned long)(n_args ? mp_obj_get_int(args[0]) : 0));
    push(AMY_RP2_CMD_BLEEP, (const uint8_t *)m, n);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(c_amy_bleep_obj, 0, 1, c_amy_bleep);

static mp_obj_t c_amy_underruns(void) {
    return mp_obj_new_int_from_uint(amy_rp2_underruns());
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_underruns_obj, c_amy_underruns);

static mp_obj_t c_amy_mem(void) {
    // (sram_used, sram_free, psram_used, psram_pool_size, failed_allocs)
    uint32_t v[5];
    amy_rp2_mem_stats(&v[0], &v[1], &v[2], &v[3], &v[4]);
    mp_obj_t t[5];
    for (int i = 0; i < 5; ++i) t[i] = mp_obj_new_int_from_uint(v[i]);
    return mp_obj_new_tuple(5, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_mem_obj, c_amy_mem);

static mp_obj_t c_amy_crumbs(void) {
    // Watchdog scratch 0..2 -- see AMY_RP2_CRUMB in amy_rp2.h.
    mp_obj_t t[3];
    for (int i = 0; i < 3; ++i) t[i] = mp_obj_new_int_from_uint(watchdog_hw->scratch[i]);
    return mp_obj_new_tuple(3, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_crumbs_obj, c_amy_crumbs);

static mp_obj_t c_amy_midi_debug(void) {
    // (uart_bytes, messages, last_message, active_channel_mask, synth_mask)
    uint32_t v[5];
    amy_rp2_midi_debug(&v[0], &v[1], &v[2], &v[3], &v[4]);
    mp_obj_t t[5];
    for (int i = 0; i < 5; ++i) t[i] = mp_obj_new_int_from_uint(v[i]);
    return mp_obj_new_tuple(5, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_midi_debug_obj, c_amy_midi_debug);

static mp_obj_t c_amy_log(void) {
    // What AMY has printed to stderr since the last call.
    static char buf[4096];
    size_t n = amy_rp2_log_take(buf, sizeof(buf));
    return mp_obj_new_str(buf, n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_log_obj, c_amy_log);

static mp_obj_t c_amy_peak(void) {
    // Largest |output sample| (0..32767) since the last call.
    return mp_obj_new_int_from_uint(amy_rp2_take_peak());
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_peak_obj, c_amy_peak);

static mp_obj_t c_amy_version(void) {
    return mp_obj_new_str(AMY_RP2_VERSION, strlen(AMY_RP2_VERSION));
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_version_obj, c_amy_version);

extern void midi_out(uint8_t *bytes, uint16_t len);

static mp_obj_t c_amy_midi_out(mp_obj_t data) {
    // Send raw MIDI (e.g. sysex replies to the editor) out of USB and UART.
    mp_buffer_info_t buf;
    mp_get_buffer_raise(data, &buf, MP_BUFFER_READ);
    midi_out(buf.buf, buf.len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(c_amy_midi_out_obj, c_amy_midi_out);

static mp_obj_t c_amy_stack_unused(void) {
    return mp_obj_new_int_from_uint(amy_rp2_core1_stack_unused());
}
static MP_DEFINE_CONST_FUN_OBJ_0(c_amy_stack_unused_obj, c_amy_stack_unused);

// _tulip support, defined in amy_rp2_tulip.c.
extern const mp_obj_fun_builtin_fixed_t amy_rp2_seq_add_callback_obj, amy_rp2_seq_remove_callback_obj,
    amy_rp2_seq_remove_callbacks_obj, amy_rp2_seq_ticks_obj, amy_rp2_midi_callback_obj, amy_rp2_midi_in_obj,
    amy_rp2_sysex_in_obj, amy_rp2_overload_callback_obj, amy_rp2_bootloader_mode_obj;

static const mp_rom_map_elem_t c_amy_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_c_amy) },
    { MP_ROM_QSTR(MP_QSTR_live), MP_ROM_PTR(&c_amy_live_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&c_amy_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_send_wire), MP_ROM_PTR(&c_amy_send_wire_obj) },
    { MP_ROM_QSTR(MP_QSTR_send_wire_from_sysex), MP_ROM_PTR(&c_amy_send_wire_from_sysex_obj) },
    { MP_ROM_QSTR(MP_QSTR_inject_midi_bytes), MP_ROM_PTR(&c_amy_inject_midi_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_process_single_midi_byte), MP_ROM_PTR(&c_amy_process_single_midi_byte_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&c_amy_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_sequencer_ticks), MP_ROM_PTR(&c_amy_sequencer_ticks_obj) },
    { MP_ROM_QSTR(MP_QSTR_render_load), MP_ROM_PTR(&c_amy_render_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_render_load_threshold), MP_ROM_PTR(&c_amy_set_render_load_threshold_obj) },
    { MP_ROM_QSTR(MP_QSTR_bleep), MP_ROM_PTR(&c_amy_bleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_underruns), MP_ROM_PTR(&c_amy_underruns_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem), MP_ROM_PTR(&c_amy_mem_obj) },
    { MP_ROM_QSTR(MP_QSTR_crumbs), MP_ROM_PTR(&c_amy_crumbs_obj) },
    { MP_ROM_QSTR(MP_QSTR_midi_debug), MP_ROM_PTR(&c_amy_midi_debug_obj) },
    { MP_ROM_QSTR(MP_QSTR_log), MP_ROM_PTR(&c_amy_log_obj) },
    { MP_ROM_QSTR(MP_QSTR_stack_unused), MP_ROM_PTR(&c_amy_stack_unused_obj) },
    { MP_ROM_QSTR(MP_QSTR_peak), MP_ROM_PTR(&c_amy_peak_obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&c_amy_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_midi_out), MP_ROM_PTR(&c_amy_midi_out_obj) },
    { MP_ROM_QSTR(MP_QSTR_seq_add_callback), MP_ROM_PTR(&amy_rp2_seq_add_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_seq_remove_callback), MP_ROM_PTR(&amy_rp2_seq_remove_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_seq_remove_callbacks), MP_ROM_PTR(&amy_rp2_seq_remove_callbacks_obj) },
    { MP_ROM_QSTR(MP_QSTR_seq_ticks), MP_ROM_PTR(&amy_rp2_seq_ticks_obj) },
    { MP_ROM_QSTR(MP_QSTR_midi_callback), MP_ROM_PTR(&amy_rp2_midi_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_midi_in), MP_ROM_PTR(&amy_rp2_midi_in_obj) },
    { MP_ROM_QSTR(MP_QSTR_sysex_in), MP_ROM_PTR(&amy_rp2_sysex_in_obj) },
    { MP_ROM_QSTR(MP_QSTR_amy_overload_callback), MP_ROM_PTR(&amy_rp2_overload_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_bootloader_mode), MP_ROM_PTR(&amy_rp2_bootloader_mode_obj) },
};
static MP_DEFINE_CONST_DICT(c_amy_module_globals, c_amy_module_globals_table);

const mp_obj_module_t c_amy_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&c_amy_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_c_amy, c_amy_module);
