"""Exercise CRLF serial reports through the real bridge without moving the mouse."""
import os
import pty
import subprocess
import time
from pathlib import Path

app = Path(__file__).resolve().parents[1] / 'JintaiMouse.app/Contents/MacOS/JintaiMouse'
master, slave = pty.openpty()
env = dict(os.environ, JINTAI_MOUSE_DEBUG='1')
proc = subprocess.Popen([str(app), os.ttyname(slave)], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, env=env)
try:
    time.sleep(0.5)
    os.write(master, b'JT_MOUSE 0 0 0 0 0\r\n')
    time.sleep(0.5)
    os.set_blocking(proc.stdout.fileno(), False)
    output = proc.stdout.read().decode()
    assert 'report [0, 0, 0, 0, 0]' in output, output
    print('serial bridge test passed')
finally:
    proc.terminate()
    proc.wait(timeout=3)
    os.close(master)
    os.close(slave)
