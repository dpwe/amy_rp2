# Force echo delay lines into the PSRAM pool (3 s max delay ~ 1 MB) and see what happens.
import amy, time, c_amy
def show(tag):
    print('%-20s mem %s load %.2f underruns %d log %r' % (tag, c_amy.mem(), c_amy.render_load(), c_amy.underruns(), c_amy.log()))
amy.live(startup_bleep=False); time.sleep(0.5); c_amy.log()
show('started')
amy.send(synth=1, note=60, vel=1); time.sleep(0.5)
amy.echo(level=0.5, delay_ms=400, max_delay_ms=3000, feedback=0.5); time.sleep(1.5)
show('echo 3s max')
amy.send(synth=1, note=60, vel=0); time.sleep(1)
show('note off')
