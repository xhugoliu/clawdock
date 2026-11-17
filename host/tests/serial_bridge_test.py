"""Exercise CRLF serial reports through the real bridge without moving the mouse."""
import os
import pty
import subprocess
import time
from pathlib import Path

app = Path(__file__).resolve().parents[1] / 'JintaiMouse.app/Contents/MacOS/JintaiMouse'
master, slave = pty.openpty()
env = dict(os.environ, JINTAI_MOUSE_DEBUG='1', JINTAI_MOUSE_TEST_NO_POST='1')
proc = subprocess.Popen([str(app), os.ttyname(slave)], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, env=env)
try:
    time.sleep(0.5)
    os.write(master, b'JT_MOUSE 0 0 0 0 0\r\nJT_MOUSE 1 0 0 0 0\r\nJT_MOUSE 0 0 0 0 0\r\nJT_MOUSE 1 0 0 0 0\r\nJT_MOUSE 0 0 0 0 0\r\n' + b'JT_SCROLL 1000 1000\r\n' * 10 + b'JT_SCROLL -1000 -1000\r\nJT_TOUCH_SCROLL -5 7\r\n')
    time.sleep(0.5)
    os.set_blocking(proc.stdout.fileno(), False)
    output = proc.stdout.read().decode()
    assert 'report [0, 0, 0, 0, 0]' in output, output
    assert 'click 1 down 2' in output, output
    assert output.count('scroll [-12, 12]') == 10, output
    assert 'scroll [12, -12]' in output, output
    assert 'touch scroll [-5, 7]' in output, output
    print('serial bridge test passed')
finally:
    proc.terminate()
    proc.wait(timeout=3)
    os.close(master)
    os.close(slave)
