import amy, time, c_amy
print('amy', amy.version)
amy.live()
print('crumbs', [hex(x) for x in c_amy.crumbs()], 'mem', c_amy.mem())
time.sleep(1)
print('ticks', amy.ticks_ms(), 'load %.3f' % amy.render_load(), 'underruns', c_amy.underruns())
for n in (60, 64, 67):
    amy.send(synth=1, note=n, vel=1)
    time.sleep(0.5)
time.sleep(1)
print('load w/ 3 juno voices %.3f' % amy.render_load())
for n in (60, 64, 67):
    amy.send(synth=1, note=n, vel=0)
time.sleep(1)
print('ticks', amy.ticks_ms(), 'underruns', c_amy.underruns())
print('mem', c_amy.mem())
