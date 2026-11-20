"""Exercise CRLF serial reports through the real bridge without moving the mouse."""
import os
import pty
import subprocess
import time
from pathlib import Path

app = Path(__file__).resolve().parents[1] / 'ClawDock.app/Contents/MacOS/ClawDock'
master, slave = pty.openpty()
proc = subprocess.Popen([str(app), '--test-serial', os.ttyname(slave)], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT)
try:
    time.sleep(0.5)
    os.write(master, b'JT_MOUSE 0 0')
    time.sleep(0.05)
    os.write(master, b' 0 0 0\r\nJT_MOUSE 1 0 0 0 0\r\nJT_MOUSE 0 0 0 0 0\r\nJT_MOUSE 1 0 0 0 0\r\nJT_MOUSE 0 0 0 0 0\r\n' + b'JT_SCROLL 1000 1000\r\n' * 10 + b'JT_SCROLL -1000 -1000\r\nJT_TOUCH_SCROLL -5 7\r\n')
    time.sleep(0.5)
    os.set_blocking(proc.stdout.fileno(), False)
    output = proc.stdout.read().decode()
    assert 'report [0, 0, 0, 0, 0]' in output, output
    assert 'click 1 down 2' in output, output
    assert output.count('scroll [-12, 12]') == 10, output
    assert 'scroll [12, -12]' in output, output
    assert 'touch scroll [-5, 7]' in output, output
    os.close(master)
    master = -1
    assert proc.wait(timeout=3) == 0, 'bridge did not handle disconnect'
    print('serial bridge test passed')
finally:
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=3)
    if master >= 0: os.close(master)
    os.close(slave)
