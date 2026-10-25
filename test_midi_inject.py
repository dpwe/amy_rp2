# Injects MIDI through the same parser UART MIDI uses (on core 1) and checks
# for audio on DOUT; independent of MIDI wiring and the DAC.
import amy, time, c_amy
from machine import mem32
def dout_edges(n=3000):
    e = 0; prev = 0
    for _ in range(n):
        b = (mem32[0xd0000004] >> 10) & 1
        e += b ^ prev; prev = b
    return e
amy.live(startup_bleep=False); time.sleep(0.5); c_amy.log()
c_amy.inject_midi_bytes(b'\x90\x3c\x64'); time.sleep(0.3)
on = dout_edges()
c_amy.inject_midi_bytes(b'\x80\x3c\x00'); time.sleep(1.5)
off = dout_edges()
print('MIDI note on: DOUT edges', on, '| after off:', off, '| log', repr(c_amy.log()))
print('PASS' if on > 100 and off == 0 else 'FAIL')
