# Recovery tests with a sketch that hangs at top level.
#   hang_test.py hang      write the hanging sketch via the editor protocol, ping
#   hang_test.py reboot    zB1 (normal reboot, sketch still in place), ping
#   hang_test.py fix       write test_sketch_loop.py via the editor protocol
import sys, time, mido
sys.path.insert(0, '.')
from spss_host import Board

def wait_board():
    for i in range(20):
        if any('AMYboard' in n for n in mido.get_output_names()):
            time.sleep(2)
            return True
        time.sleep(1)
    return False

cmd = sys.argv[1]
b = Board()
if cmd == 'hang':
    b.write('/user/current/sketch.py', b"import amy\nwhile True:\n    pass\n")
    try:
        b.exec('import amyboard; amyboard.environment_transfer_done()')
        print('restart acknowledged')
    except TimeoutError:
        print('restart not acknowledged (sketch never finishes loading)')
elif cmd == 'reboot':
    b.send('zB1Z', ack=False)
    b.inp.close(); b.out.close()
    time.sleep(3)
    print('board back on USB after reboot:', wait_board())
    b = Board()
elif cmd == 'fix':
    b.write('/user/current/sketch.py', open('test_sketch_loop.py', 'rb').read())
    print('fixed sketch written; restart AK in %.0f ms' % b.exec('import amyboard; amyboard.environment_transfer_done()'))
ms = b.ping()
print('ping:', 'OK in %.0f ms' % ms if ms else 'NO REPLY')
