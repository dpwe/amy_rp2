# amyboard_boot -- start this board the way an AMYboard starts: AMY first (on
# core 1), then the AMYBOARD_VCV steps of tulipcc's tulip/shared/py/_boot.py
# (which can't be frozen as is: it also imports Tulip's networked "world").
#
# Run from boot.py.  MicroPython brings USB up only after boot.py returns, so
# the sketch must not run in boot.py itself (one that hangs would leave the
# board with no USB at all): boot.py does the quick setup, and the sketch is
# queued to start once USB is up (c_amy.call_soon_unlocked), where USB,
# the editor's zB/zI and Ctrl-C keep working even if it hangs.  Each step
# reports failures and carries on, so the REPL always comes up.
import sys


def _step(name, fn):
    try:
        fn()
    except Exception as e:
        print("amyboard_boot: %s failed:" % name)
        sys.print_exception(e)


def _start_without_sketch(amy, tulip, amyboard):
    # amyboard.start_amy()'s AMYBOARD_VCV path, minus run_sketch().
    tulip.amy_overload_callback(amyboard._on_amy_overload)
    amy.send_raw("i1K257iv6Z")
    amy.send_raw("i10K384iv1Z")
    amyboard._ensure_current_env_layout()


def _run_sketch():
    import amyboard
    _step("run_sketch", amyboard.run_sketch)


def boot():
    import amy, c_amy
    _step("amy.live", amy.live)
    import tulip, midi, synth, sequencer, amyboard  # noqa: F401  (as _boot.py does)
    _step("ensure_user_environment", amyboard.ensure_user_environment)
    _step("init_display", amyboard.init_display)
    _step("start", lambda: _start_without_sketch(amy, tulip, amyboard))
    _step("midi callback", lambda: tulip.midi_callback(midi.c_fired_midi_event))
    if tulip.bootloader_mode():
        # zB0 (the editor's recovery for a wedged board): boot without the sketch.
        tulip.stderr_write("bootloader mode - sketch skipped, waiting for commands")
    elif c_amy.raw_repl():
        # A tool (mpremote etc.) soft-reset us through the raw REPL: the sketch's
        # output would land in the middle of its protocol.  AMY is running; the
        # sketch starts on the next real boot, Ctrl-D, or editor write.
        pass
    else:
        _step("queue sketch", lambda: c_amy.call_soon_unlocked(_run_sketch))
