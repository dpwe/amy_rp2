# _tulip -- the parts of Tulip's C _tulip module that tulip.py, midi.py,
# sequencer.py and amyboard.py (frozen unchanged from tulipcc, py/tulipcc/)
# use on an AMYboard, for the RP2350.
#
# We present as AMYBOARD, so amyboard.py drives display, encoders, buttons and
# CV out itself over I2C (I2C1 on GP2/GP3, opened by amyboard_boot).  What it
# asks of the firmware is below; the C-backed functions are in c_amy
# (modules/c_amy/amy_rp2_tulip.c and modc_amy.c).
import c_amy
from c_amy import (  # noqa: F401  (re-exported for "from _tulip import *")
    midi_out,
    midi_callback,
    midi_in,
    sysex_in,
    seq_add_callback,
    seq_remove_callback,
    seq_remove_callbacks,
    seq_ticks,
    amy_overload_callback,
    bootloader_mode,
)

running_apps = {}


def board():
    return "AMYBOARD"


def build_strings():
    # tulip.version() turns this into "YYYYMMDD-<commit>".
    date, _, commit = c_amy.version().partition("-")
    return ("AMYboard RP2350", commit, "%s-%s-%s" % (date[:4], date[4:6], date[6:]))


def stderr_write(s):
    print(s)


def sequencer_start():
    c_amy.send_wire("zY1Z")


def amy_get_output_buffer():
    return None


# CV in: read the AMYboard's ADS1015 (I2C 0x48) on demand, mapped to volts the
# way AMYboard's cv_read_task does; 0V when there's no ADC (checked again every
# few seconds, so a missing one doesn't cost an I2C timeout per call).
_cv_retry_ms = 0


def cv_in(channel=0):
    global _cv_retry_ms
    import time
    if time.ticks_diff(time.ticks_ms(), _cv_retry_ms) < 0:
        return 0.0
    try:
        import amyboard
        raw = amyboard.ads1015_raw(channel)
    except Exception:
        _cv_retry_ms = time.ticks_add(time.ticks_ms(), 5000)
        return 0.0
    return ((raw - 10064) / (30096 - 10064)) * 10.0 - 5.0


# Not here yet: streaming a synth's output to the CV DAC (AMYboard does it from
# a C task), and the background I2C task (tulip.i2c_bg_write; amyboard.py falls
# back to plain I2C writes without it).
def set_cv_synth(*_args):
    pass


# Hooks for amyboard.py's other targets (the VCV plugin, AMYboard firmware's
# MIDI-out and start-up paths), unused on this board.
def vcv_encoder(*_args):
    return 0


def vcv_encoder_button(*_args):
    return False


def vcv_cv_out(*_args):
    pass


def framebuf_web_update(*_args):
    pass


def amyboard_start(*_args):
    pass


def amyboard_set_midi_out(*_args):
    pass
