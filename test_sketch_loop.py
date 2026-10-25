# AMYboard sketch for testing loop(tick) on the board (written via the editor protocol).
import amy, amyboard
calls = 0
first_tick = None
last_tick = None
def loop(tick):
    global calls, first_tick, last_tick
    if first_tick is None:
        first_tick = tick
    last_tick = tick
    calls += 1
    step = tick // amyboard.TICKS_PER_STEP
    if step % 8 == 0:
        amy.send(synth=1, note=60 + (step // 8) % 12, vel=0.8)
    elif step % 8 == 4:
        amy.send(synth=1, note=60 + (step // 8) % 12, vel=0)
