import amy, time, c_amy
amy.live(startup_bleep=False); time.sleep(0.5); c_amy.log(); c_amy.peak()
def note(label, on, off):
    c_amy.peak(); on(); time.sleep(0.5); p = c_amy.peak(); off(); time.sleep(1.5)
    print('%-28s peak %5d  log %r' % (label, p, c_amy.log()))
note('amy.send vel=1', lambda: amy.send(synth=1, note=60, vel=1), lambda: amy.send(synth=1, note=60, vel=0))
note('amy.send vel=0.74', lambda: amy.send(synth=1, note=60, vel=0.74), lambda: amy.send(synth=1, note=60, vel=0))
note('inject MIDI 90 3c 5e', lambda: c_amy.inject_midi_bytes(b'\x90\x3c\x5e'), lambda: c_amy.inject_midi_bytes(b'\x80\x3c\x00'))
note('inject MIDI 90 3c 7f', lambda: c_amy.inject_midi_bytes(b'\x90\x3c\x7f'), lambda: c_amy.inject_midi_bytes(b'\x80\x3c\x00'))
print('silence peak', c_amy.peak())
