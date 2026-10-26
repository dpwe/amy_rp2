# Turn and press the encoders while this runs: values show on the OLED and here.
import amyboard, time
d = amyboard.display
e = amyboard.encoder()
n = len(e._devs) and 4
seen_vals = set()
pressed = set()
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < 20000:
    vals = [e.read(i) for i in range(n)]
    btns = [e.button(i) for i in range(n)]
    d.fill(0)
    d.text("AMYboard RP2350", 0, 0, 255)
    for i in range(n):
        d.text("enc%d %5d %s" % (i, vals[i], "*" if btns[i] else ""), 0, 20 + 12 * i, 255)
    d.show()
    for i in range(n):
        seen_vals.add((i, vals[i]))
        if btns[i]:
            pressed.add(i)
    time.sleep_ms(50)
moved = sorted(set(i for i, v in seen_vals if v != 0))
print("encoders that moved:", moved, "| final values:", vals, "| buttons pressed:", sorted(pressed))
