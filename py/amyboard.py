# amyboard -- interim stand-in for tulipcc's amyboard.py.
#
# Just the calls amyboard.com/editor makes over sysex (zP import amyboard;
# amyboard.<fn>()), so "Write to your AMYboard" works: the version probe, sketch
# (re)start after a transfer, factory reset.  Sketches run their top-level code
# only; loop(tick) needs AMY's sequencer ticks on core 0, which comes with the
# real amyboard.py.
import sys
import os
import amy
import c_amy

ENV_DIR = "/user/current"


def _sysex(code, payload=b""):
    c_amy.midi_out(bytes([0xF0, 0x00, 0x03, 0x45, code]) + payload + b"\xf7")


def report_version(*_args):
    """Tell the editor this firmware's identity: F0 00 03 45 'V' <YYYYMMDD-hash> F7."""
    _sysex(0x56, c_amy.version().encode())


def _report_sketch_error(e):
    """F0 00 03 45 'X' <base64 traceback> F7, which the editor shows as an error."""
    try:
        import binascii, io
        buf = io.StringIO()
        sys.print_exception(e, buf)
        text = buf.getvalue().strip()[-900:]
        _sysex(0x58, binascii.b2a_base64(text.encode()).rstrip())
    except Exception:
        pass


def ensure_user_environment():
    path = ""
    for part in ENV_DIR.strip("/").split("/"):
        path += "/" + part
        try:
            os.mkdir(path)
        except OSError:
            pass  # exists


def stop_sketch():
    pass  # no loop(tick) to stop yet


def run_sketch():
    ensure_user_environment()
    try:
        os.stat(ENV_DIR + "/sketch.py")
    except OSError:
        return  # nothing written yet
    if ENV_DIR not in sys.path:
        sys.path.insert(0, ENV_DIR)
    sys.modules.pop("sketch", None)
    _sysex(0x58)  # clear the editor's error indicator
    try:
        import sketch  # noqa: F401  (top-level code runs on import)
    except Exception as e:
        print("sketch.py load failed:")
        sys.print_exception(e)
        _report_sketch_error(e)


def restart_sketch():
    stop_sketch()
    amy.send(reset=amy.RESET_SEQUENCER)
    run_sketch()


def environment_transfer_done(*_args):
    """A new sketch.py arrived over zT: restart the sequencer zT stopped, rerun the sketch."""
    c_amy.send_wire("zY1Z")
    restart_sketch()


def factory_reset(*_args):
    amy.send_raw("S%dZ" % amy.RESET_SYNTHS)
    try:
        for f in os.listdir(ENV_DIR):
            try:
                os.remove(ENV_DIR + "/" + f)
            except OSError:
                pass
    except OSError:
        pass
    restart_sketch()
