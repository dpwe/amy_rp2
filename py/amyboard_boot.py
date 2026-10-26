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


# The SH1107 / SSD1327 panels need time after power-up before they take
# commands (modules often hold the controller in reset with an RC on RST), and
# the RP2350 reaches boot.py within a fraction of a second.  Without a wait the
# first setup is lost (panel dark), and the next reset finds it half configured
# (only a strip of the screen shows).  An AMYboard's ESP32 boots too slowly to
# notice.
_PANEL_READY_MS = 1000   # don't set the panel up sooner than this after power-on
_PANEL_WAIT_MS = 500     # then wait up to this long for it to answer on I2C
_PANELS = {0x3C, 0x3D}   # SH1107, SSD1327


def _panel_on_bus(amyboard):
    try:
        return bool(set(amyboard.get_i2c().scan()) & _PANELS)
    except OSError:
        return False


_log = []  # (ms since power-on, event) for the panel bring-up; see below
_watch_timer = None
# False while a cold-boot panel watch is waiting for the SH1107 to get through
# its self-reset; the sketch waits for this (see _run_sketch).
_panel_settled = True
_SETTLE_MIN_MS = 3000    # not before this long after power-on...
_SETTLE_ON_MS = 500      # ...and not until the panel has read "on" this long


def _log_event(msg):
    import time
    _log.append((time.ticks_ms(), msg))


def _init_display(amyboard):
    import time
    cold = time.ticks_ms() < _PANEL_READY_MS + 2000  # just powered up (not a soft reset)
    if cold:
        while time.ticks_ms() < _PANEL_READY_MS:
            time.sleep_ms(10)
        deadline = time.ticks_add(time.ticks_ms(), _PANEL_WAIT_MS)
        while not _panel_on_bus(amyboard) and time.ticks_diff(deadline, time.ticks_ms()) > 0:
            time.sleep_ms(20)
        _log_event("panel on bus: %s" % _panel_on_bus(amyboard))
    amyboard.init_display()
    _log_event("init 1: %s" % amyboard.display.available)
    # Display() falls back to "no display" on any error; after a cold start a
    # first setup can also be only partly taken.  Either way, one more go.
    if cold or (not amyboard.display.available and _panel_on_bus(amyboard)):
        time.sleep_ms(50)
        amyboard.init_display()
        _log_event("init 2: %s" % amyboard.display.available)
    try:
        _resend_sh1107_addressing(amyboard)
    except OSError as e:
        _log_event("addressing resend failed: %s" % e)  # the watch below recovers
    if cold:
        _watch_sh1107(amyboard)


def _resend_sh1107_addressing(amyboard):
    # After a cold power-up the SH1107 has been seen in its power-on page
    # addressing mode despite the driver's setup having sent vertical
    # addressing (0x21) -- and the driver's show() writes the screen assuming
    # vertical addressing, so everything lands in one 8-pixel page.  Sending
    # the mode again once setup is over (and redrawing) fixed it on the board.
    hw = amyboard.display._hw
    if hw is None or type(hw).__name__ != "SH1107_I2C":
        return
    hw.write_command(bytes([0x20 | (0x00 if hw.rotate90 else 0x01)]))
    hw.write_command(b"\xb0")  # page address 0: show() never sets it, so any drift would stick
    amyboard.display.show(True)
    _log_event("addressing mode resent")


def _sh1107_status(amyboard):
    # A read returns the status byte only in command mode.  After a data write
    # (the driver's show()) it would read display RAM instead -- and advance the
    # panel's RAM address, shifting everything drawn afterwards.  So select
    # command mode first with a bare control byte.
    try:
        i2c = amyboard.get_i2c()
        i2c.writeto(0x3C, b"\x00")
        return i2c.readfrom(0x3C, 1)[0]
    except OSError:
        return None


def _reinit_sh1107(amyboard):
    # Set the panel up again, keeping what was drawn on it.
    hw = amyboard.display._hw
    keep = bytes(hw.displaybuf)
    hw.init_display()  # clears the buffer
    hw.displaybuf[:] = keep
    _resend_sh1107_addressing(amyboard)


def _watch_sh1107(amyboard, ms=10000, period=50, retry_ms=300):
    # On a cold power-up the SH1107 has been seen to reset itself ~2 s after
    # power-on, after we've set it up: it stops answering I2C for a moment and
    # comes back in its power-on state (display off, mirrored, page
    # addressing).  Its status byte (bit 6 = display off) shows that: for a
    # while after a cold boot, poll it and set the panel up again whenever it
    # reads "off".  Every change goes in _log.
    hw = amyboard.display._hw
    if hw is None or type(hw).__name__ != "SH1107_I2C":
        return
    import time
    from machine import Timer
    global _panel_settled
    _panel_settled = False
    end = time.ticks_add(time.ticks_ms(), ms)
    state = {"last": -1, "next_try": 0, "on_since": None}

    def tick(t):
        global _panel_settled
        now = time.ticks_ms()
        st = _sh1107_status(amyboard)
        if st != state["last"]:
            _log_event("status %s" % ("no answer" if st is None else "0x%02x" % st))
            state["last"] = st
        if st is None or st & 0x40:
            state["on_since"] = None
        elif state["on_since"] is None:
            state["on_since"] = now
        if (not _panel_settled and now >= _SETTLE_MIN_MS and state["on_since"] is not None
                and time.ticks_diff(now, state["on_since"]) >= _SETTLE_ON_MS):
            _panel_settled = True
            _log_event("panel settled")
        if st is not None and st & 0x40 and time.ticks_diff(now, state["next_try"]) >= 0:
            state["next_try"] = time.ticks_add(now, retry_ms)
            try:
                _reinit_sh1107(amyboard)
                _log_event("panel was off: set up again")
            except OSError as e:
                _log_event("set up again failed: %s" % e)
            state["last"] = -1
        if time.ticks_diff(now, end) > 0:
            t.deinit()
            _panel_settled = True
            try:
                _resend_sh1107_addressing(amyboard)  # leave the addressing known-good
            except OSError:
                pass
            _log_event("watch done")

    global _watch_timer
    _watch_timer = Timer(-1, period=period, callback=tick)  # keep it alive (GC)
    _log_event("watch started")


def _run_sketch():
    # On a cold boot, wait for the panel to get through its self-reset: a sketch
    # that draws at top level while the panel isn't answering raises an I2C
    # error, and amyboard.run_sketch() then takes the sketch for corrupt and
    # replaces it with the default sketch.  (We're unlocked here: USB and the
    # watch keep running while we wait.)
    import amyboard, time
    deadline = time.ticks_add(time.ticks_ms(), 10000)
    if not _panel_settled:
        _log_event("sketch waiting for panel")
    while not _panel_settled and time.ticks_diff(deadline, time.ticks_ms()) > 0:
        time.sleep_ms(20)
    _log_event("sketch start")
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
