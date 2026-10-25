# _tulip -- the parts of Tulip's C _tulip module that tulip.py, midi.py,
# sequencer.py and amyboard.py (frozen unchanged from tulipcc, py/tulipcc/)
# use on an AMYboard, for the RP2350.
#
# We present as AMYBOARD_VCV: AMYboard firmware with AMY already running and no
# physical display, encoders or CV hardware -- amyboard.py's VCV paths ask the
# host for those, and here the answers are "none".  The C-backed functions are
# in c_amy (modules/c_amy/amy_rp2_tulip.c and modc_amy.c).
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
    return "AMYBOARD_VCV"


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


# Hardware amyboard.py's VCV paths get from the host; this board has none.
def vcv_encoder(*_args):
    return 0


def vcv_encoder_button(*_args):
    return False


def vcv_cv_out(*_args):
    pass


def cv_in(*_args):
    return 0.0


def set_cv_synth(*_args):
    pass


def framebuf_web_update(*_args):
    pass


def amyboard_start(*_args):
    pass


def amyboard_set_midi_out(*_args):
    pass
