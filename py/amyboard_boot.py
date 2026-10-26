# amyboard_boot -- start this board the way an AMYboard starts: AMY first (on
# core 1), the accessory I2C bus, then the AMYboard steps of tulipcc's
# tulip/shared/py/_boot.py (which can't be frozen as is: it also imports
# Tulip's networked "world").
#
# Run from boot.py.  MicroPython brings USB up only after boot.py returns, so
# the sketch must not run in boot.py itself (one that hangs would leave the
# board with no USB at all): boot.py does the quick setup, and the sketch is
# queued to start once USB is up (c_amy.call_soon_unlocked), where USB,
# the editor's zB/zI and Ctrl-C keep working even if it hangs.  Each step
# reports failures and carries on, so the REPL always comes up.
import sys

# The AMYboard accessory bus (display, encoders, CV): I2C1 on these pins, since
# GP4/GP5 (I2C0's usual pair on this board) carry UART1 MIDI.
I2C_ID = 1
I2C_SDA = 2
I2C_SCL = 3
I2C_FREQ = 400000


def _step(name, fn):
    try:
        fn()
    except Exception as e:
        print("amyboard_boot: %s failed:" % name)
        sys.print_exception(e)


def _start_without_sketch(amy, tulip, amyboard):
    # amyboard.start_amy() without its ESP32 hardware setup (codec, MIDI-out
    # pins, boot button) or run_sketch().
    tulip.amy_overload_callback(amyboard._on_amy_overload)
    amy.send_raw("i1K257iv6Z")
    amy.send_raw("i10K384iv1Z")
    amyboard._ensure_current_env_layout()


def _recover_i2c_bus():
    # A reset can land mid-transfer (e.g. during a display update) and leave a
    # device holding SDA low.  Clock SCL until it lets go, then send a STOP.
    from machine import Pin
    import time
    sda = Pin(I2C_SDA, Pin.IN, Pin.PULL_UP)
    scl = Pin(I2C_SCL, Pin.OPEN_DRAIN, value=1)
    for _ in range(9):
        if sda.value():
            break
        scl.value(0)
        time.sleep_us(5)
        scl.value(1)
        time.sleep_us(5)
    sda = Pin(I2C_SDA, Pin.OPEN_DRAIN, value=0)  # STOP: SDA low -> high while SCL high
    time.sleep_us(5)
    sda.value(1)
    time.sleep_us(5)


def _open_i2c(amyboard):
    # amyboard.get_i2c() would open I2C(0); give it our bus first.
    from machine import I2C, Pin
    _recover_i2c_bus()
    amyboard.i2c = I2C(I2C_ID, sda=Pin(I2C_SDA), scl=Pin(I2C_SCL), freq=I2C_FREQ)


def _init_display(amyboard):
    # Display() falls back to "no display" on any error, so if a panel is on the
    # bus but didn't come up (a glitch after reset), give it one more go.
    amyboard.init_display()
    if not amyboard.display.available:
        try:
            on_bus = set(amyboard.get_i2c().scan()) & {0x3C, 0x3D}
        except OSError:
            on_bus = ()
        if on_bus:
            import time
            time.sleep_ms(50)
            amyboard.init_display()


def _run_sketch():
    import amyboard
    _step("run_sketch", amyboard.run_sketch)


def boot():
    import amy, c_amy
    _step("amy.live", amy.live)
    import tulip, midi, synth, sequencer, amyboard  # noqa: F401  (as _boot.py does)
    _step("I2C", lambda: _open_i2c(amyboard))
    _step("ensure_user_environment", amyboard.ensure_user_environment)
    _step("init_display", lambda: _init_display(amyboard))
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
