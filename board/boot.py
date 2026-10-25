# boot.py -- start AMY and amyboard.py (sketch.py, editor support) on every boot
# and soft reset.
# Install:  mpremote cp board/boot.py :boot.py      Remove:  mpremote rm :boot.py
try:
    import amyboard_boot
    amyboard_boot.boot()
except Exception as e:
    import sys
    print("boot.py: AMYboard start failed:")
    sys.print_exception(e)
