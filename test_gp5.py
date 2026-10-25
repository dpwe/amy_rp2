# Samples GP5 (MIDI in / UART1 RX) directly while you play: edges = activity on the pin.
import amy, time, c_amy
from machine import mem32
amy.live(startup_bleep=False)
print('GP5 funcsel %d level %d' % (mem32[0x40028000 + 8*5 + 4] & 0x1f, (mem32[0xd0000004] >> 5) & 1))
for i in range(5):
    edges = 0; prev = 1; t0 = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), t0) < 3000:
        b = (mem32[0xd0000004] >> 5) & 1
        edges += b ^ prev; prev = b
    print('t=%2ds GP5 edges %5d  uart_bytes %d' % (3 * (i + 1), edges, c_amy.midi_debug()[0]))
