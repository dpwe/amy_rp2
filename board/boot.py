# boot.py -- start AMY on every boot and soft reset, so the board answers MIDI
# (and amyboard.com/editor) without anyone running amy.live() first.
# Install:  mpremote cp board/boot.py :boot.py      Remove:  mpremote rm :boot.py
try:
    import amy
    amy.live()
except Exception as e:
    import sys
    print("boot.py: AMY failed to start:")
    sys.print_exception(e)
