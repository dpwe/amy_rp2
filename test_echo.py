# Turn on echo while a note plays; report memory, load and AMY's log at each step.
import amy, time, c_amy
def show(tag):
    print('%-22s mem %s load %.2f underruns %d stack_unused %d log %r' % (
        tag, c_amy.mem(), c_amy.render_load(), c_amy.underruns(), c_amy.stack_unused(), c_amy.log()))
amy.live(startup_bleep=False); time.sleep(0.5); c_amy.log()
show('started')
amy.send(synth=1, note=60, vel=1); time.sleep(0.5)
show('note on')
amy.echo(level=0.5); time.sleep(1)
show('echo level 0.5')
amy.echo(level=0.5, delay_ms=400, feedback=0.6); time.sleep(1)
show('echo 400ms fb .6')
amy.send(synth=1, note=60, vel=0); time.sleep(1)
show('note off')
