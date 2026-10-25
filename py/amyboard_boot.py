# amyboard_boot -- start this board the way an AMYboard starts: AMY first (on
# core 1), then the AMYBOARD_VCV steps of tulipcc's tulip/shared/py/_boot.py
# (which can't be frozen as is: it also imports Tulip's networked "world").
# Run from boot.py; each step reports failures and carries on, so the REPL
# always comes up.
import sys


def _step(name, fn):
    try:
        fn()
    except Exception as e:
        print("amyboard_boot: %s failed:" % name)
        sys.print_exception(e)


def boot():
    import amy
    _step("amy.live", amy.live)
    import tulip, midi, synth, sequencer, amyboard  # noqa: F401  (as _boot.py does)
    _step("ensure_user_environment", amyboard.ensure_user_environment)
    _step("init_display", amyboard.init_display)
    _step("start_amy", amyboard.start_amy)  # defaults, then the sketch
    _step("midi callback", lambda: tulip.midi_callback(midi.c_fired_midi_event))
