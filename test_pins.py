import amy, time, c_amy, machine
from machine import mem32
GPIO_IN = 0xd0000004  # SIO GPIO_IN (pins 0-31)
PINS = {'BCLK8': 8, 'LRCLK9': 9, 'DOUT10': 10}

def sample(n=4000):
    ones = {k: 0 for k in PINS}
    edges = {k: 0 for k in PINS}
    prev = mem32[GPIO_IN]
    for _ in range(n):
        v = mem32[GPIO_IN]
        for k, p in PINS.items():
            b = (v >> p) & 1
            ones[k] += b
            edges[k] += b ^ ((prev >> p) & 1)
        prev = v
    return {k: '%d%% high, %d edges' % (100 * ones[k] // n, edges[k]) for k in PINS}

print('sysclk', machine.freq())
print('before live:', sample())
amy.live(startup_bleep=False)
time.sleep(0.5)
print('silent     :', sample())
amy.send(synth=1, note=60, vel=1)
time.sleep(0.3)
print('note on    :', sample())
amy.send(synth=1, note=60, vel=0)
time.sleep(1.5)
print('note off   :', sample())
print('underruns', c_amy.underruns(), 'ticks', amy.ticks_ms())
# GPIO function select for each pin (6/7/8 = PIO0/1/2 on RP2350)
for k, p in PINS.items():
    print(k, 'funcsel', mem32[0x40028000 + 8 * p + 4] & 0x1f, 'pad', hex(mem32[0x40038000 + 4 + 4 * p]))
