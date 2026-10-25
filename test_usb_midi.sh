#!/bin/bash
# USB MIDI check from the host: start AMY on the board, play notes and a
# sysex + 16-note burst into its USB MIDI port, ping it (zI -> OK), and report
# what the board parsed.  Needs: .venv with mpremote, mido, python-rtmidi.
cd "$(dirname "$0")"
PORT=${PORT:-$(ls /dev/cu.usbmodem* | head -1)}
MP=".venv/bin/mpremote connect $PORT"
$MP exec "import amy; amy.live(startup_bleep=False)" >/dev/null
for mode in "" burst; do
    $MP resume exec "import c_amy; c_amy.peak(); c_amy.log()" >/dev/null
    .venv/bin/python send_usb_midi.py $mode
    sleep 1
    $MP resume exec "import c_amy; b,m,last,a,s = c_amy.midi_debug(); print('  board: total msgs %d last %06x peak %d log %r' % (m, last, c_amy.peak(), c_amy.log()))"
done
.venv/bin/python ping_usb_midi.py
