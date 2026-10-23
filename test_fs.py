import amy, time, c_amy
amy.live(startup_bleep=False)
amy.send(synth=1, note=57, vel=1)
time.sleep(0.3)
with open('/amy_test.txt', 'w') as f:
    for i in range(200):
        f.write('flash write while AMY plays %d\n' % i)
import os; os.sync() if hasattr(os, 'sync') else None
time.sleep(0.5)
amy.send(synth=1, note=57, vel=0)
print('fs ok, size', os.stat('/amy_test.txt')[6], 'ticks', amy.ticks_ms(), 'underruns', c_amy.underruns())
os.remove('/amy_test.txt')
