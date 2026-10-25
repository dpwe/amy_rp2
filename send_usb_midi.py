# Host side: play notes into the board's USB MIDI port.
#   send_usb_midi.py          4 notes
#   send_usb_midi.py burst    sysex identity request, then a fast 16-note burst
import sys, time, mido
name = next(n for n in mido.get_output_names() if 'AMY' in n)
with mido.open_output(name) as out:
    if 'burst' in sys.argv:
        out.send(mido.Message('sysex', data=[0x7E, 0x7F, 0x06, 0x01]))
        for note in range(48, 64):
            out.send(mido.Message('note_on', note=note, velocity=90))
        time.sleep(0.5)
        for note in range(48, 64):
            out.send(mido.Message('note_off', note=note))
        print('sent sysex + 16-note burst (32 messages) to', name)
    else:
        for note in (60, 64, 67, 72):
            out.send(mido.Message('note_on', note=note, velocity=100))
            time.sleep(0.4)
            out.send(mido.Message('note_off', note=note))
            time.sleep(0.1)
        print('sent 4 notes (8 messages) to', name)
    time.sleep(0.3)  # let CoreMIDI deliver before the port closes
