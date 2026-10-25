# Host side: send the editor's zI ping over USB MIDI and wait for AMY's OK reply.
import time, mido
out_name = next(n for n in mido.get_output_names() if 'AMY' in n)
in_name = next(n for n in mido.get_input_names() if 'AMY' in n)
got = []
with mido.open_input(in_name, callback=lambda m: got.append(m)) as inp, mido.open_output(out_name) as out:
    time.sleep(0.2)
    out.send(mido.Message('sysex', data=[0x00, 0x03, 0x45] + list(b'zIZ')))
    t0 = time.time()
    while time.time() - t0 < 2 and not got:
        time.sleep(0.01)
    ms = (time.time() - t0) * 1000
for m in got:
    print('reply:', m.hex(), '(%.0f ms)' % ms if m.type == 'sysex' and bytes(m.data) == b'\x00\x03\x45OK' else '')
print('PING OK' if any(m.type == 'sysex' and bytes(m.data) == b'\x00\x03\x45OK' for m in got) else 'NO REPLY')
