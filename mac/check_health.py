#!/usr/bin/env python3
"""Local health checker: direct UNO USB or read-only Yun Wi-Fi JSON."""
import argparse
import functools
import http.server
import json
import math
from pathlib import Path
import signal
import socket
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE.parent / 'yun'))
import collector


class RemoteReader:
    """Only a new firmware sequence/uptime pair refreshes the live-data timeout."""
    def __init__(self, state, url):
        self.state, self.url = state, url
        self.last_key = None

    def accept(self, message, now):
        if not isinstance(message, dict) or message.get('link') is not True:
            self.state.invalidate('Yun is reachable; its Arduino data is missing')
            return False
        if not collector.validate(message):
            self.state.rejected += 1
            self.state.message = 'Yun packet format mismatch; install the new Yun files'
            return False
        key = (message['seq'], message['uptime_ms'])
        if key == self.last_key:
            self.state.message = 'Wi-Fi reachable; waiting for a new Arduino packet'
            return False
        self.last_key = key
        ok = self.state.accept(json.dumps(message, allow_nan=False), now)
        if ok:
            self.state.message = 'Receiving through Yun Wi-Fi'
            self.state.port = message.get('usb_port', '')
        return ok

    def run(self, stopping):
        while not stopping.is_set():
            try:
                req = urllib.request.Request(self.url, headers={'Cache-Control': 'no-cache'})
                with urllib.request.urlopen(req, timeout=3) as response:
                    payload = response.read(16385)
                    if len(payload) > 16384:
                        raise ValueError('Yun response exceeds 16 KiB')
                self.accept(collector.loads(payload), collector.clock())
            except (OSError, ValueError, urllib.error.URLError) as error:
                self.state.invalidate('Cannot read Yun Wi-Fi: ' + str(error))
            stopping.wait(0.7)


def demo_packet(seq, elapsed):
    """Explicitly simulated data for checking the interface without any board."""
    angle = 45 + 40 * math.sin(elapsed / 2)
    return {'kind': 'workout', 'protocol': 2, 'seq': seq, 'uptime_ms': int(elapsed * 1000),
            'state': 4, 'feedback': 2, 'reps': min(int(elapsed / 6), 10), 'goal': 10,
            'seconds': int(elapsed), 'page': int(elapsed / 4) % 3,
            'bpm': 78 + 3 * math.sin(elapsed / 5), 'heart_status': 'SIMULATED pulse',
            'chip': 'SIMULATION', 'ir': 43000, 'overflows': 0,
            'heart_found': True, 'display_found': True, 'display_address': 60,
            'pulse_motion_warning': False, 'page_period_ms': 4000,
            'heart_bus_adc': [1470, 1470], 'display_bus_adc': [2700, 2700],
            'simulated': True,
            'motion': {'found': True, 'fresh': True, 'address': 83, 'feedback': 5,
                       'x_g': round(math.sin(math.radians(angle)), 3), 'y_g': 0,
                       'z_g': round(math.cos(math.radians(angle)), 3), 'tilt_deg': angle,
                       'span_deg': 80, 'reference_ms': 6000, 'last_rep_ms': 6100,
                       'compared_reps': 1, 'flagged_reps': 0,
                       'bus_sda_adc': 2700, 'bus_scl_adc': 2700}}


def run_demo(state, stopping):
    began = collector.clock()
    seq = 0
    while not stopping.is_set():
        now = collector.clock(); seq += 1
        state.accept(json.dumps(demo_packet(seq, now - began)), now)
        state.message = 'SIMULATED DATA - no hardware connected'
        stopping.wait(0.75)


def local_ipv4():
    """Return the LAN address without sending any network payload."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(('8.8.8.8', 80))
        return probe.getsockname()[0]
    except OSError:
        try:
            return socket.gethostbyname(socket.gethostname())
        except OSError:
            return '127.0.0.1'
    finally:
        probe.close()


def run_windows_usb(device, state, stopping):
    """Read the UNO USB CDC port with PySerial on Windows."""
    import serial

    framer = collector.Framer()
    stream = None
    last_open = -1e9
    while not stopping.is_set():
        now = collector.clock()
        if stream is None and now - last_open >= 2:
            last_open = now
            candidates = [device] if device else collector.ports()
            if not device and len(candidates) > 1:
                state.message = 'Multiple COM ports; choose one with --usb COM3'
                candidates = []
            for port in candidates:
                try:
                    stream = serial.Serial(
                        port=port, baudrate=115200, timeout=0.1, write_timeout=0.2)
                    state.port = stream.port or port
                    state.message = 'USB open; waiting for protocol 2 JSON'
                    framer = collector.Framer()
                    break
                except (OSError, serial.SerialException) as error:
                    state.message = 'Cannot open serial: %s' % error
            if not candidates and not state.message.startswith('Multiple'):
                state.message = (
                    'No COM port; close Arduino Serial Monitor, use a data cable, '
                    'and check the USB driver')
        if stream is not None:
            try:
                block = stream.read(4096)
                if block:
                    before = framer.rejected
                    for line in framer.feed(block):
                        state.accept(line, collector.clock())
                    state.rejected += framer.rejected - before
                if not stream.is_open:
                    raise OSError('USB disconnected')
            except (OSError, serial.SerialException):
                try:
                    stream.close()
                except (OSError, serial.SerialException):
                    pass
                stream = None
                state.port = ''
                state.invalidate('USB disconnected; retrying')
        else:
            time.sleep(0.1)
    if stream is not None:
        stream.close()
    state.invalidate('Collector stopped')


class Handler(http.server.BaseHTTPRequestHandler):
    def __init__(self, *args, state, mode, source, **kwargs):
        self.state, self.mode, self.source = state, mode, source
        super().__init__(*args, **kwargs)

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path == '/api/status':
            result = self.state.snapshot(collector.clock())
            result.update(mode=self.mode, source=self.source)
            payload = json.dumps(result, allow_nan=False).encode('utf-8')
            mime = 'application/json; charset=utf-8'
        elif path in ('/', '/index.html'):
            payload = (BASE.parent / 'yun' / 'index.html').read_bytes().replace(
                b'<!--LOCAL_CONFIG-->', b'<script>window.healthEndpoint="/api/status";</script>')
            mime = 'text/html; charset=utf-8'
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(payload)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        try:
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, format, *args):
        pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--usb', nargs='?', const='auto', metavar='PORT')
    group.add_argument('--wifi', metavar='URL', help='http://YOUR_YUN_IP/workout/latest.json')
    group.add_argument('--demo', action='store_true', help='Explicitly simulated interface check')
    parser.add_argument('--list-ports', action='store_true')
    parser.add_argument(
        '--host', default='127.0.0.1',
        help='Listening address; use 0.0.0.0 to view from a phone on the same network')
    parser.add_argument('--port', type=int, default=8770, help='Local browser server port')
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args(argv)
    if args.list_ports:
        if sys.platform == 'win32':
            try:
                import serial  # noqa: F401
            except ImportError:
                parser.error('Windows USB mode needs pyserial: py -3 -m pip install pyserial')
        print('\n'.join(collector.ports()) or 'No UNO USB port found')
        return 0
    if not 1 <= args.port <= 65535:
        parser.error('--port must be 1..65535')
    state = collector.State()
    stopping = threading.Event()
    if args.wifi:
        parsed = urllib.parse.urlsplit(args.wifi)
        if parsed.scheme not in ('http', 'https') or not parsed.hostname or parsed.username:
            parser.error('--wifi needs an http(s) URL without embedded credentials')
        mode, source = 'wifi', args.wifi
        remote = RemoteReader(state, args.wifi)
        worker = threading.Thread(target=remote.run, args=(stopping,), daemon=True)
    elif args.demo:
        mode, source = 'demo', 'SIMULATED - no sensors connected'
        worker = threading.Thread(target=run_demo, args=(state, stopping), daemon=True)
    else:
        if sys.platform == 'win32':
            try:
                import serial  # noqa: F401
            except ImportError:
                parser.error('Windows USB mode needs pyserial: py -3 -m pip install pyserial')
        port = None if args.usb in (None, 'auto') else args.usb
        if port is None and len(collector.ports()) > 1:
            parser.error('Multiple serial ports: run --list-ports, then --usb COM3 '
                         '(or /dev/cu.usbmodem...)')
        mode, source = 'usb', port or 'Automatic UNO USB detection'
        if sys.platform == 'win32':
            worker = threading.Thread(
                target=run_windows_usb, args=(port, state, stopping), daemon=True)
        else:
            worker = threading.Thread(target=collector.run, daemon=True,
                kwargs={'device': port, 'state': state, 'should_run': lambda: not stopping.is_set(),
                        'publisher': lambda *_: None})
    handler = functools.partial(Handler, state=state, mode=mode, source=source)
    try:
        server = http.server.ThreadingHTTPServer((args.host, args.port), handler)
    except OSError as error:
        parser.error('Cannot start local server: %s; try --port 8771' % error)
    worker.start()
    port = server.server_address[1]
    url = 'http://127.0.0.1:%d/' % port
    print('Health checker:', url)
    if args.host not in ('127.0.0.1', 'localhost', '::1'):
        print('Phone URL (same Wi-Fi): http://%s:%d/' % (local_ipv4(), port))
    print('Mode:', mode, '|', source)
    print('Keep this window open. Press Control-C to stop.')
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        stopping.set()
        server.server_close()
        worker.join(timeout=4)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
