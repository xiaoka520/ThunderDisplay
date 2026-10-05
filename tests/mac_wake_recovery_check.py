"""Probe a native --recovery-check app. No image files, input events or physical sleep."""
import socket
import struct
import sys
import time
from pathlib import Path

logfile = Path(sys.argv[1])
def wait_phase(phase):
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline:
        text = logfile.read_text() if logfile.exists() else ''
        if 'Recovery check: FAIL' in text:
            raise RuntimeError(text)
        if 'Recovery check: ' + phase in text:
            return
        time.sleep(.1)
    raise TimeoutError(phase)

def read_exact(tcp, count):
    data = b''
    while len(data) < count:
        chunk = tcp.recv(count - len(data))
        if not chunk:
            raise RuntimeError('Host closed before a new first frame')
        data += chunk
    return data

def read(tcp):
    size = struct.unpack('!I', read_exact(tcp, 4))[0]
    assert 0 < size <= 65536
    return read_exact(tcp, size)

def send(tcp, data):
    tcp.sendall(struct.pack('!I', len(data)) + data)

def first_frame(phase):
    with socket.create_connection(('127.0.0.1', 48079), timeout=4) as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.bind(('127.0.0.1', 0)); udp.settimeout(4)
        send(tcp, bytes([9, 6, 0])); assert read(tcp)[0] == 10
        hello = struct.pack('!BHHHHHIB', 1, 1, udp.getsockname()[1], 640, 360, 30, 10000000, 1) + bytes(32)
        send(tcp, hello); welcome = read(tcp)
        assert welcome[0] == 2, welcome
        session = struct.unpack_from('!Q', welcome, 3)[0]
        assert session and welcome[11] == 1
        send(tcp, bytes([8]))
        deadline = time.monotonic() + 4
        fragments, frame_id = {}, None
        while time.monotonic() < deadline:
            packet = udp.recv(1500)
            if len(packet) < 40:
                continue
            magic, version, codec, flags, sid, fid, pts, size, index, count, length, reserved = struct.unpack('!IBBHQIQIHHHH', packet[:40])
            assert magic == 0x54444231 and version == 1 and codec == 1 and sid == session and reserved == 0
            if not flags & 1:
                continue
            if frame_id != fid:
                fragments, frame_id = {}, fid
            assert len(packet) == 40 + length and index < count and size <= 4 * 1024 * 1024
            fragments[index] = packet[40:]
            if len(fragments) == count:
                frame = b''.join(fragments[i] for i in range(count))
                assert len(frame) == size and frame.startswith(b'\0\0\0\1')
                # H.264 SPS + IDR prove we received a complete new encoded keyframe.
                nals = frame.split(b'\0\0\0\1')[1:]
                assert {7, 5} <= {n[0] & 31 for n in nals if n}
                print(f'{phase}: new session; complete H.264 keyframe {size} bytes', flush=True)
                return session
        raise TimeoutError('No complete keyframe after ' + phase)

sessions = []
for phase in ('AUTOSTART', 'WAKE', 'DISPLAY'):
    wait_phase(phase)
    sessions.append(first_frame(phase))
assert len(set(sessions)) == 3, 'A stale session survived recovery'
wait_phase('PASS')
print('Native host auto-start, sleep/wake notification and display recovery passed; no physical sleep or image export.')
