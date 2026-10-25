# Host side of amyboard.com/editor's sysex protocol, for testing the board.
#   python spss_host.py test        ping, zP exec, zT write + zD read-back, wire msg
#   python spss_host.py write SRC DEST    write a local file to the board
#   python spss_host.py read PATH         read a file from the board (zD)
#   python spss_host.py exec "code"       run Python on the board (zP)
#   python spss_host.py editor            the editor's "Write to your AMYboard" sequence
import base64, os, sys, time, mido

ID = [0x00, 0x03, 0x45]
CHUNK = 188  # AMYBOARD_TRANSFER_CHUNK_BYTES in the editor


class Board:
    def __init__(self):
        out_name = next(n for n in mido.get_output_names() if 'AMYboard' in n)
        in_name = next(n for n in mido.get_input_names() if 'AMYboard' in n)
        self.frames = []
        self.inp = mido.open_input(in_name, callback=self._rx)
        self.out = mido.open_output(out_name)
        time.sleep(0.2)

    def _rx(self, m):
        if m.type == 'sysex' and list(m.data[:3]) == ID:
            self.frames.append(bytes(m.data[3:]))

    def _wait(self, pred, timeout):
        t0 = time.time()
        while time.time() - t0 < timeout:
            for i, f in enumerate(self.frames):
                if pred(f):
                    del self.frames[i]
                    return f, (time.time() - t0) * 1000
            time.sleep(0.001)
        return None, timeout * 1000

    def send(self, msg, ack=True, timeout=5):
        self.out.send(mido.Message('sysex', data=ID + list(msg.encode())))
        if ack:
            f, ms = self._wait(lambda f: f == b'AK', timeout)
            if f is None:
                raise TimeoutError('no AK for %r' % msg[:40])
            return ms

    def ping(self):
        self.send('zIZ', ack=False)
        f, ms = self._wait(lambda f: f == b'OK', 2)
        return ms if f else None

    def exec(self, code):
        return self.send('zP' + code + 'Z')

    def write(self, path, data):
        t0 = time.time()
        self.send('zT%s,%dZ' % (path, len(data)))
        for i in range(0, len(data), CHUNK):
            self.send(base64.b64encode(data[i:i + CHUNK]).decode())
        return time.time() - t0

    def read(self, path, timeout=5):
        # zD<path> replies with base64 sysex chunks: marker '0' (only), 'C' (more), 'E' (last).
        self.frames.clear()
        self.out.send(mido.Message('sysex', data=ID + list(('zD%sZ' % path).encode())))
        b64 = b''
        while True:
            f, _ = self._wait(lambda f: f[:1] in (b'0', b'C', b'E'), timeout)
            if f is None:
                raise TimeoutError('zD %s: no data' % path)
            b64 += f[1:]
            if f[:1] in (b'0', b'E'):
                break
        self._wait(lambda f: f == b'AK', 2)
        return base64.b64decode(b64)


def editor_write(b, sketch_text):
    """What the editor's "Write to your AMYboard" sends, in order."""
    b.exec('import amyboard; amyboard.report_version()')
    f, ms = b._wait(lambda f: f[:1] == b'V', 3)
    print('version probe:', ('V %s (%.0f ms)' % (f[1:].decode(), ms)) if f else 'NO V REPLY')
    b.write('/user/current/sketch.py', sketch_text.encode())
    b.frames.clear()
    b.exec('import amyboard; amyboard.environment_transfer_done()')
    f, _ = b._wait(lambda f: f[:1] == b'X' and len(f) > 1, 1.5)
    if f:
        print('sketch error reported:', base64.b64decode(f[1:]).decode().splitlines()[-1])
    else:
        print('sketch ran (no error reported)')


def test():
    b = Board()
    ms = b.ping()
    print('ping:', 'OK in %.0f ms' % ms if ms else 'NO REPLY')
    print('zP exec: AK in %.0f ms' % b.exec("f=open('/zp_test.txt','w'); f.write('written by zP'); f.close()"))
    print('zD read of zP file:', b.read('/zp_test.txt'))
    data = os.urandom(1000) + b'\nend of test file\n'
    secs = b.write('/user/current/transfer_test.bin', data)
    back = b.read('/user/current/transfer_test.bin')
    print('zT write %d bytes in %.2f s (%d sysex msgs); zD read back %d bytes: %s' % (
        len(data), secs, 1 + (len(data) + CHUNK - 1) // CHUNK, len(back), 'MATCH' if back == data else 'MISMATCH'))
    print('wire msg over sysex (note on): AK in %.0f ms' % b.send('i1n64l1Z'))
    time.sleep(0.4)
    b.send('i1n64l0Z')


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'test'
    if cmd == 'test':
        test()
    elif cmd == 'write':
        print('%.2f s' % Board().write(sys.argv[3], open(sys.argv[2], 'rb').read()))
    elif cmd == 'read':
        sys.stdout.buffer.write(Board().read(sys.argv[2]))
    elif cmd == 'editor':
        b = Board()
        editor_write(b, "import amy\namy.send(synth=1, note=67, vel=1)\nprint('test sketch ran')\n")
        time.sleep(0.8)
        editor_write(b, "import amy\nthis_is_not_defined()\n")
        b.send('i1n67l0Z')
    elif cmd == 'exec':
        print('AK in %.0f ms' % Board().exec(sys.argv[2]))
