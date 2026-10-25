# amy_usb_midi -- make the board a USB MIDI device that plays AMY.
#
#   import amy, amy_usb_midi
#   amy.live()
#   amy_usb_midi.start()
#
# start() re-enumerates USB as a composite device: the usual REPL serial port
# plus a class-compliant MIDI port.  Incoming MIDI goes to AMY's parser on
# core 1 (c_amy.inject_midi_bytes), the same path as UART MIDI.  Runtime USB
# config is dropped on soft reset, so call start() from boot.py/main.py to
# have it every boot.

import usb.device
from usb.device.midi import MIDIInterface
import c_amy

# MIDI bytes carried by a USB-MIDI event packet, by its code index number (CIN).
_CIN_LEN = b"\x00\x00\x02\x03\x03\x01\x02\x03\x03\x03\x03\x03\x02\x02\x03\x01"

_RXLEN = 256  # bytes = 64 USB-MIDI events


class AmyMIDIInterface(MIDIInterface):
    def __init__(self):
        # The base class's 16-byte default fills with one burst (e.g. a sysex
        # identity request a host sends a new device), and it never resubmits
        # a stalled OUT transfer; a lost sysex tail then leaves AMY's parser
        # swallowing everything as sysex.
        super().__init__(rxlen=_RXLEN)
        self._out = bytearray(_RXLEN)  # MIDI bytes unpacked from one batch
        self._out_mv = memoryview(self._out)

    def _on_rx(self, _):
        # Replaces the base class's per-event dispatch: unpack every event in
        # the batch into one MIDI byte stream (trimmed to each event's real
        # length, so no usb=1 padding mode) and hand AMY the lot at once.
        m = self._rx.pend_read()
        out = self._out
        n = 0
        i = 0
        while i <= len(m) - 4:
            k = _CIN_LEN[m[i] & 0xF]
            for j in range(k):
                out[n + j] = m[i + 1 + j]
            n += k
            i += 4
        self._rx.finish_read(i)
        self._rx_xfer()  # restart reception if the buffer had filled
        if n:
            try:
                c_amy.inject_midi_bytes(self._out_mv[:n])
            except (RuntimeError, OSError):
                pass  # AMY not running (or queue full): drop it


_midi = None


def start(product="AMY RP2350"):
    """Add a USB MIDI port (keeping the REPL serial port) that feeds AMY."""
    global _midi
    if _midi is None:
        _midi = AmyMIDIInterface()
        usb.device.get().init(_midi, builtin_driver=True, product_str=product)
    return _midi


def is_open():
    """True once the host has configured the MIDI interface."""
    return _midi is not None and _midi.is_open()
