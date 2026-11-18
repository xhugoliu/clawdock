"""Exercise the production session sources and frame writer through a pseudoterminal."""
import datetime as dt
import hashlib
import json
import os
import pty
import select
import struct
import subprocess
import tempfile
import threading
import time
import uuid
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit


app = Path(__file__).resolve().parents[1] / 'Jintai.app/Contents/MacOS/Jintai'
now = dt.datetime.now(dt.timezone.utc)
width_bytes = 544 // 8


def record(kind, payload, ago=0):
    return json.dumps({'type': kind, 'timestamp': (now - dt.timedelta(seconds=ago)).isoformat(),
                       'payload': payload}).encode() + b'\n'


def fixture(folder, ident, initial, ago=0, suffix='', subagent=False):
    file = folder / f'rollout-2026-01-01T00-00-00-{ident}{suffix}.jsonl'
    meta = {'id': str(ident), 'cwd': '/private/test-project',
            'thread_source': 'subagent' if subagent else 'user'}
    file.write_bytes(record('session_meta', meta, ago) + initial)
    return file


def event(kind, turn, ago=0, **fields):
    return record('event_msg', {'type': kind, 'turn_id': turn, **fields}, ago)


def request(master, line):
    os.write(master, (line + '\n').encode())
    received = bytearray()
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        if not select.select([master], [], [], max(0, deadline - time.monotonic()))[0]:
            break
        received.extend(os.read(master, 97))
        if len(received) >= 7:
            assert received[:4] == b'JCX4', received[:16]
            expected = 11 + struct.unpack_from('<H', received, 5)[0]
            if len(received) >= expected:
                assert len(received) == expected
                body = received[4:-4]
                assert struct.unpack('<I', received[-4:])[0] == zlib.crc32(body)
                return body[0], body[3:]
    raise AssertionError('no complete Codex snapshot received')


def bitmap(body, offset, height):
    size = struct.unpack_from('<H', body, offset)[0]
    offset += 2
    end = offset + size
    pixels = bytearray()
    while offset < end:
        token = body[offset]
        offset += 1
        length = (token & 0x7f) + 1
        if token & 0x80:
            pixels.extend(bytes(length))
        else:
            pixels.extend(body[offset:offset + length])
            offset += length
    assert offset == end and len(pixels) == (width_bytes * height if size else 0)
    return pixels, offset


def poll(master):
    kind, body = request(master, 'JT_CODEX 4')
    assert kind == 1 and body[0] == 1
    rows, offset = {}, 2
    for _ in range(body[1]):
        ident = uuid.UUID(bytes=bytes(body[offset:offset + 16]))
        state = body[offset + 16]
        title, offset = bitmap(body, offset + 17, 32)
        preview, offset = bitmap(body, offset, 32)
        rows[ident] = state, title, preview
    assert offset == len(body)
    return rows


def detail(master, ident, page):
    kind, body = request(master, f'JT_CODEX 4 {ident} {page}')
    assert kind == 2 and bytes(body[:16]) == ident.bytes
    actual, total = body[16:18]
    if total == 0:
        return actual, total, None
    title, offset = bitmap(body, 19, 32)
    text, offset = bitmap(body, offset, 320)
    assert offset == len(body) and any(title)
    return actual, total, text


with tempfile.TemporaryDirectory() as temp:
    home = Path(temp)
    old = home / 'sessions/2026/01/01'
    today = home / 'sessions' / dt.datetime.now().strftime('%Y/%m/%d')
    old.mkdir(parents=True)
    today.mkdir(parents=True, exist_ok=True)
    running, other, historical, child, long_running, long_completed, stale = [uuid.uuid4() for _ in range(7)]
    reply = '\n'.join(f'第 {number} 行近台测试。' for number in range(1, 29))
    visible = record('response_item', {'type': 'message', 'role': 'assistant', 'phase': 'commentary',
                                       'content': [{'type': 'output_text', 'text': reply}]}, 11)
    secret = record('response_item', {'type': 'message', 'role': 'developer',
                                      'content': [{'type': 'output_text', 'text': 'private fixture'}]}, 10)
    first = fixture(old, running, event('task_started', 'a', 12) + visible, ago=15)
    second = fixture(today, other, event('task_started', 'b', 10), ago=15)
    fixture(old, historical, event('task_started', 'old', 20) + event('task_complete', 'old', 19), ago=21)
    fixture(old, child, event('task_started', 'sub', 4), ago=5, subagent=True)
    # The latest turn started before the tail. A single tool result spans more than one read block.
    large_result = record('response_item', {'type': 'function_call_output', 'output': 'x' * (2 * 1024 * 1024 + 64)}, 5)
    long_log = fixture(old, long_running, event('task_started', 'long', 3600) + large_result +
                       event('token_count', 'long', 4), ago=3601)
    fixture(old, long_completed, event('task_started', 'complete', 3600) +
            event('task_complete', 'complete', 20) + large_result + event('token_count', 'complete', 4), ago=3601)
    fixture(old, stale, event('task_started', 'stale', 3600) + large_result + event('token_count', 'stale', 601), ago=3601)
    title = '近台会话状态和标题栏的长标题截断验证'
    (home / 'session_index.jsonl').write_text(json.dumps({'id': str(running), 'thread_name': title}) + '\n')

    master, slave = pty.openpty()
    proc = subprocess.Popen([str(app), '--test-codex-serial', os.ttyname(slave)], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=dict(os.environ, CODEX_HOME=temp))
    try:
        assert proc.stdout is not None
        assert '测试串口已连接' in proc.stdout.readline().decode()
        rows = poll(master)
        assert {key: value[0] for key, value in rows.items()} == {running: 1, other: 1, long_running: 1}
        assert sum(bin(byte).count('1') for byte in rows[running][1]) > 100, 'CJK title is empty or incomplete'
        assert rows[running][1] != rows[other][1], 'title index was ignored'
        title_bitmap = rows[running][1]
        for offset in range(0, len(title_bitmap), width_bytes):
            line = title_bitmap[offset:offset + width_bytes]
            assert not (line[53] & 0x0f) and not any(line[54:]), 'title overlaps the state column'
        assert any(rows[running][2]) and not rows[other][2], 'preview should come from a visible assistant reply'
        with first.open('ab') as file:
            file.write(secret)
        assert poll(master)[running][2] == rows[running][2], 'private message replaced the reply'
        page0, total, pixels0 = detail(master, running, 0)
        page1, total_again, pixels1 = detail(master, running, 1)
        assert page0 == 0 and page1 == 1 and total > 1 and total_again == total
        assert any(pixels0) and any(pixels1) and pixels0 != pixels1
        assert detail(master, running, 255)[:2] == (total - 1, total)
        assert detail(master, other, 0) == (0, 1, bytearray())
        assert detail(master, uuid.uuid4(), 0) == (0, 0, None)

        fixture(today, running, event('task_started', 'new', 8) +
                event('task_complete', 'new', 2, last_agent_message='收到，测试已经完成。'),
                ago=9, suffix=f'_{uuid.uuid4()}')
        interrupted = event('turn_aborted', 'b', 1)
        with second.open('ab') as file:
            file.write(interrupted[:30])
        completed = poll(master)
        assert {key: value[0] for key, value in completed.items()} == {running: 2, other: 1, long_running: 1}
        assert any(completed[running][2]) and completed[running][2] != rows[running][2]
        with second.open('ab') as file:
            file.write(interrupted[30:])
        assert {key: value[0] for key, value in poll(master).items()} == {running: 2, other: 4, long_running: 1}
        # A completion arriving in an oversized append still closes the already visible turn.
        with long_log.open('ab') as file:
            file.write(event('task_complete', 'long', 1) + large_result + event('token_count', 'long'))
        assert {key: value[0] for key, value in poll(master).items()} == {running: 2, other: 4, long_running: 2}
        os.close(master)
        master = -1
        assert proc.wait(timeout=3) == 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=3)
        if master >= 0:
            os.close(master)
        os.close(slave)

def session_id(source, native_id):
    digest = bytearray(hashlib.sha256(f'com.jintai.session:{source}:{native_id}'.encode()).digest()[:16])
    digest[6] = (digest[6] & 0x0f) | 0x80
    digest[8] = (digest[8] & 0x3f) | 0x80
    return uuid.UUID(bytes=bytes(digest))


def eventually(master, matches):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        rows = poll(master)
        if matches(rows):
            return rows
        time.sleep(0.15)
    raise AssertionError(f'unexpected session states: {[value[0] for value in rows.values()]}')


def mixed_sessions():
    started = int(time.time() * 1000) - 3000
    main_id, child_id = 'ses_main', 'ses_child'
    active = {main_id: {'type': 'running'}, child_id: {'type': 'running'}}
    info = {'id': main_id, 'title': 'Shared title', 'time': {'busy': started, 'updated': started}}
    child = {'id': child_id, 'parentID': main_id, 'time': {'busy': started, 'updated': started}}
    prior = {'id': 'ses_prior', 'outcome': 'succeeded',
             'time': {'busy': started - 100, 'updated': started, 'idle': started}}

    class OpenCodeServer(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def do_GET(self):
            assert self.headers.get('Authorization') == 'Basic b3BlbmNvZGU6Zml4dHVyZQ=='
            path = urlsplit(self.path).path
            if path == '/api/session/active':
                data = dict(active)
            elif path == '/api/session':
                data = [child, prior] + ([info] if main_id not in active else [])
            elif path == f'/api/session/{main_id}':
                data = info
            elif path == f'/api/session/{main_id}/message':
                data = [{'type': 'assistant', 'time': {'created': started + 1},
                         'content': [{'type': 'reasoning', 'text': 'hidden'}, {'type': 'text', 'text': 'Latest reply'}]}]
            else:
                self.send_error(404)
                return
            body = json.dumps({'data': data}).encode()
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with tempfile.TemporaryDirectory() as temp, ThreadingHTTPServer(('127.0.0.1', 0), OpenCodeServer) as http:
        home = Path(temp)
        registration = home / 'opencode/service.json'
        registration.parent.mkdir()
        registration.write_text(json.dumps({'url': f'http://127.0.0.1:{http.server_port}', 'password': 'fixture'}))
        events_dir = home / 'events'
        codex = uuid.uuid4()
        native = str(uuid.uuid4())
        codex_dir = home / 'sessions'
        codex_dir.mkdir()
        (home / 'session_index.jsonl').write_text(json.dumps({'id': str(codex), 'thread_name': 'Shared title'}) + '\n')
        fixture(codex_dir, codex, event('task_started', 'c', 1))
        transcript = home / f'{native}.jsonl'
        for folder in ('claude', 'pi'):
            path = events_dir / folder / 'event.json'
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps({'id': native, 'state': 'running', 'title': 'Shared title',
                                        'startedAt': started, 'updatedAt': started, 'transcript': str(transcript)}))

        server_thread = threading.Thread(target=http.serve_forever, daemon=True)
        server_thread.start()
        master, slave = pty.openpty()
        proc = subprocess.Popen([str(app), '--test-session-serial', os.ttyname(slave)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, env=dict(os.environ, CODEX_HOME=temp, XDG_STATE_HOME=temp,
                                                                  JINTAI_SESSION_EVENTS_DIR=str(events_dir)))
        try:
            assert '测试串口已连接' in proc.stdout.readline().decode()
            open_code, claude, pi = (session_id('OpenCode', main_id), session_id('Claude Code', native),
                                     session_id('Pi', native))
            ids = {codex, open_code, claude, pi}
            rows = eventually(master, lambda rows: set(rows) == ids)
            assert all(row[0] == 1 for row in rows.values())
            assert len({bytes(row[1]) for row in rows.values()}) == 1, 'Equal titles must render without Agent prefixes'
            assert any(rows[open_code][2]) and not any(rows[pi][2])
            kind, payload = request(master, f'JT_CODEX 4 {open_code} 0')
            assert kind == 2 and payload[:16] == open_code.bytes and payload[16:18] == b'\0\1'
            detail_title, offset = bitmap(payload, 19, 32)
            assert detail_title == rows[open_code][1], 'Detail and list must render the same title'
            assert any(bitmap(payload, offset, 320)[0])

            now_ms = int(time.time() * 1000)
            info['time'] = {'busy': started, 'updated': now_ms, 'idle': now_ms}
            info['outcome'] = 'succeeded'
            active.pop(main_id)
            interruption = {'type': 'user', 'sessionId': native,
                            'timestamp': dt.datetime.now(dt.timezone.utc).isoformat(),
                            'message': {'role': 'user', 'content': '[Request interrupted by user]'}}
            transcript.write_text(json.dumps(interruption) + '\n')
            mixed = eventually(master, lambda rows: rows.get(open_code, (0,))[0] == 2 and rows.get(claude, (0,))[0] == 4)
            assert set(mixed) == ids and list(mixed) == list(rows), 'Terminal updates must preserve the selected row'

            registration.unlink()
            eventually(master, lambda rows: set(rows) == ids - {open_code})
        finally:
            proc.terminate()
            proc.wait(timeout=3)
            os.close(master)
            os.close(slave)
            http.shutdown()
            server_thread.join(timeout=3)


mixed_sessions()
print('Session serial tests passed')
