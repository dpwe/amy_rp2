#!/bin/bash
# Flash-independent USB MIDI check: start AMY + USB MIDI on the board, play notes
# from this Mac, and report what the board parsed and how loud it was.
cd "$(dirname "$0")"
PORT=${PORT:-$(ls /dev/cu.usbmodem* | head -1)}
MP=".venv/bin/mpremote connect $PORT"
# start() re-enumerates USB, so this session is cut off; later calls use 'resume'
# (no soft reset, which would drop the USB MIDI config).
( $MP exec "import amy, amy_usb_midi; amy.live(startup_bleep=False); amy_usb_midi.start()" >/dev/null 2>&1 & P=$!; sleep 5; kill $P 2>/dev/null; wait $P ) 2>/dev/null
for i in $(seq 1 10); do .venv/bin/python -c "import mido,sys; sys.exit(0 if any('AMY' in n for n in mido.get_output_names()) else 1)" && break; sleep 1; done
PORT=$(ls /dev/cu.usbmodem* | head -1); MP=".venv/bin/mpremote connect $PORT"
for mode in "" burst; do
    $MP resume exec "import c_amy; c_amy.peak(); c_amy.log()" >/dev/null
    .venv/bin/python send_usb_midi.py $mode
    $MP resume exec "import c_amy; b,m,last,a,s = c_amy.midi_debug(); print('  board: total msgs %d last %06x peak %d log %r' % (m, last, c_amy.peak(), c_amy.log()))"
done
