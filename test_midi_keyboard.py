# Play a MIDI keyboard into UART1 RX (GP5) while this runs; prints what arrives
# and how loud the output is (peak |sample|, 0..32767).
import amy, time, c_amy
amy.live(startup_bleep=False); c_amy.log(); c_amy.peak()
for i in range(15):
    time.sleep(2)
    b, m, last, act, syn = c_amy.midi_debug()
    print('t=%2ds uart_bytes %5d msgs %4d last %06x peak %5d %s' % (2 * (i + 1), b, m, last, c_amy.peak(), c_amy.log().strip()))
