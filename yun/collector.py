#!/usr/bin/env python
"""UNO R4 USB collector for Dragino Yun. Python 2.7 / 3, standard library only.

Uses the Yun's existing web server and /dev/ttyACM* USB CDC port, not Bridge/UART.
The local checker imports these framing and validation routines too.
"""
from __future__ import print_function
import copy
import errno
import glob
import json
import math
import os
import select
import signal
import struct
import sys
import threading
import time

try:
    import fcntl
    import termios
except ImportError:
    fcntl = None
    termios = None

ROOT = '/tmp/workout'
PIDFILE = '/var/run/workout-usb.pid'
MAX_LINE = 4096
running = True
try:
    integer_types = (int, long)
    string_types = (basestring,)
except NameError:
    integer_types = (int,)
    string_types = (str,)


def clock():
    return time.monotonic() if hasattr(time, 'monotonic') else os.times()[4]


def reject_constant(value):
    raise ValueError('Non-finite JSON number: ' + value)


def loads(payload):
    if not isinstance(payload, string_types):
        payload = payload.decode('utf-8')
    return json.loads(payload, parse_constant=reject_constant)


def integer(value, low, high):
    return isinstance(value, integer_types) and not isinstance(value, bool) and low <= value <= high


def number(value, low, high, nullable=True):
    if value is None:
        return nullable
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and not math.isnan(float(value)) and not math.isinf(float(value))
            and low <= value <= high)


def validate(m):
    """Accept only the firmware's protocol 2; do not silently use the old protocol."""
    if not isinstance(m, dict) or m.get('kind') != 'workout' or m.get('protocol') != 2:
        return False
    bounds = {'seq': (0, 4294967295), 'uptime_ms': (0, 4294967295),
              'state': (0, 6), 'feedback': (0, 8), 'reps': (0, 100),
              'goal': (1, 100), 'seconds': (0, 4294967295), 'page': (0, 2),
              'ir': (0, 262143), 'overflows': (0, 4294967295)}
    if any(not integer(m.get(k), *b) for k, b in bounds.items()):
        return False
    if m['reps'] > m['goal'] or not number(m.get('bpm'), 0.1, 299.9):
        return False
    for k in ('heart_found', 'display_found', 'pulse_motion_warning'):
        if not isinstance(m.get(k), bool):
            return False
    for k in ('heart_status', 'chip'):
        if not isinstance(m.get(k), string_types) or len(m[k]) > 96:
            return False
    if m.get('display_address') not in (0, 0x3c, 0x3d):
        return False
    if m['display_found'] and m['display_address'] == 0:
        return False
    for k in ('heart_bus_adc', 'display_bus_adc'):
        a = m.get(k)
        if not isinstance(a, list) or len(a) != 2 or any(not integer(v, 0, 4095) for v in a):
            return False
    a = m.get('motion')
    if not isinstance(a, dict) or not isinstance(a.get('found'), bool) or not isinstance(a.get('fresh'), bool):
        return False
    if a.get('address') not in (0, 0x38, 0x53, 0x1d) or not integer(a.get('feedback'), 0, 11):
        return False
    for k in ('x_g', 'y_g', 'z_g'):
        if not number(a.get(k), -4, 4):
            return False
    for k in ('tilt_deg', 'span_deg'):
        if not number(a.get(k), 0, 180):
            return False
    if a['fresh'] and (not a['found'] or any(a.get(k) is None for k in ('x_g', 'y_g', 'z_g'))):
        return False
    if not a['fresh'] and any(a.get(k) is not None for k in ('x_g', 'y_g', 'z_g', 'tilt_deg')):
        return False
    for k in ('reference_ms', 'last_rep_ms', 'compared_reps', 'flagged_reps'):
        if not integer(a.get(k), 0, 4294967295):
            return False
    if a['flagged_reps'] > a['compared_reps']:
        return False
    return True


class Framer(object):
    """Bounded newline framing; overlong lines are discarded through the next LF."""
    def __init__(self):
        self.pending = bytearray()
        self.dropping = False
        self.rejected = 0

    def feed(self, block):
        lines = []
        for byte in bytearray(block):
            if byte == 10:
                if self.pending and not self.dropping:
                    lines.append(bytes(self.pending).rstrip(b'\r') if sys.version_info[0] >= 3
                                 else str(self.pending).rstrip('\r'))
                self.pending = bytearray()
                self.dropping = False
            elif not self.dropping:
                if len(self.pending) < MAX_LINE:
                    self.pending.append(byte)
                else:
                    self.pending = bytearray()
                    self.dropping = True
                    self.rejected += 1
        return lines


class State(object):
    def __init__(self):
        self.latest = {}
        self.last_seen = None
        self.received = 0
        self.rejected = 0
        self.heartbeat = 0
        self.port = ''
        self.message = 'Waiting for Arduino data'
        self.lock = threading.RLock()

    def accept(self, line, now):
        try:
            message = loads(line)
        except (ValueError, UnicodeError, TypeError):
            with self.lock:
                self.rejected += 1
            return False
        if not validate(message):
            with self.lock:
                self.rejected += 1
            return False
        with self.lock:
            self.latest = message
            self.last_seen = now
            self.received += 1
            self.message = 'Receiving Arduino data'
        return True

    def invalidate(self, message):
        with self.lock:
            self.last_seen = None
            self.message = message

    def snapshot(self, now):
        with self.lock:
            self.heartbeat += 1
            age = None if self.last_seen is None else max(0.0, now - self.last_seen)
            fresh = age is not None and age < 4.0
            out = copy.deepcopy(self.latest)
            out.update(link=fresh, packets=self.received, bad_packets=self.rejected,
                       heartbeat=self.heartbeat, usb_port=self.port,
                       collector_status=self.message, age_s=age)
        for k, value in [('state', 0), ('feedback', 0), ('reps', 0), ('goal', 10),
                         ('seconds', 0), ('ir', 0), ('overflows', 0), ('page', 0),
                         ('chip', 'Unknown'), ('heart_found', False), ('display_found', False)]:
            out.setdefault(k, value)
        if not fresh:
            out['bpm'] = None
            out['heart_status'] = 'Arduino data missing'
            out['pulse_motion_warning'] = False
            if isinstance(out.get('motion'), dict):
                out['motion'].update(fresh=False, x_g=None, y_g=None, z_g=None,
                                     tilt_deg=None, feedback=0)
        return out


def ensure_dir(path):
    try:
        os.makedirs(path)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise


def publish(state, now, directory=ROOT):
    ensure_dir(directory)
    temporary = os.path.join(directory, '.latest.%d' % os.getpid())
    payload = json.dumps(state.snapshot(now), allow_nan=False, separators=(',', ':'))
    with open(temporary, 'w') as stream:
        stream.write(payload)
    os.rename(temporary, os.path.join(directory, 'latest.json'))


def ports():
    if sys.platform == 'darwin':
        return sorted(glob.glob('/dev/cu.usbmodem*'))
    if os.name == 'nt':
        try:
            from serial.tools import list_ports
        except ImportError:
            return []
        return sorted(port.device for port in list_ports.comports())
    return sorted(glob.glob('/dev/ttyACM*'))


def open_port(path):
    if fcntl is None or termios is None:
        raise OSError(errno.ENOTSUP, 'POSIX serial backend is unavailable on this platform')
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
        attrs[4] = attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        termios.tcflush(fd, termios.TCIFLUSH)
        try:
            flags = termios.TIOCM_DTR | termios.TIOCM_RTS
            fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', flags))
        except (IOError, OSError) as e:
            if e.errno not in (errno.ENOTTY, errno.EINVAL):
                raise
        return fd
    except Exception:
        os.close(fd)
        raise


def run(device=None, directory=ROOT, state=None, should_run=None, publisher=publish):
    state = state or State()
    should_run = should_run or (lambda: running)
    fd = None
    framer = Framer()
    last_publish = last_open = -1e9
    while should_run():
        now = clock()
        if fd is None and now - last_open >= 2:
            last_open = now
            candidates = [device] if device else ports()
            if not device and len(candidates) > 1:
                state.message = 'Multiple USB devices; choose an explicit --device port'
                candidates = []
            for port in candidates:
                try:
                    fd = open_port(port)
                    state.port = port
                    state.message = 'USB open; waiting for protocol 2 JSON'
                    framer = Framer()
                    break
                except (IOError, OSError) as e:
                    state.message = 'Cannot open serial: %s' % e
            if not candidates and not state.message.startswith('Multiple'):
                state.message = 'No UNO USB port; check cable, Serial Monitor and USB CDC driver'
        if fd is not None:
            try:
                readable, _, _ = select.select([fd], [], [], 0.1)
                if readable:
                    block = os.read(fd, 4096)
                    if not block:
                        raise IOError(errno.EIO, 'USB disconnected')
                    before = framer.rejected
                    for line in framer.feed(block):
                        state.accept(line, clock())
                    state.rejected += framer.rejected - before
            except (IOError, OSError, select.error) as e:
                if getattr(e, 'errno', None) not in (errno.EAGAIN, errno.EINTR):
                    os.close(fd)
                    fd = None
                    state.port = ''
                    state.invalidate('USB disconnected; retrying')
        else:
            time.sleep(0.1)
        now = clock()
        if now - last_publish >= 0.5:
            publisher(state, now, directory)
            last_publish = now
    if fd is not None:
        os.close(fd)
    state.invalidate('Collector stopped')
    publisher(state, clock(), directory)


def stop(signum=None, frame=None):
    global running
    running = False


def daemonize():
    if fcntl is None:
        raise RuntimeError('Daemon mode requires the Yun/Linux platform')
    ensure_dir(ROOT)
    lock = open(PIDFILE, 'a+')
    try:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except IOError:
        print('Workout collector is already running')
        lock.close()
        return None
    if os.fork():
        lock.close()
        return None
    os.setsid()
    lock.seek(0); lock.truncate(); lock.write(str(os.getpid())); lock.flush()
    log = os.open(os.path.join(ROOT, 'collector.log'), os.O_CREAT | os.O_WRONLY | os.O_APPEND, 0o600)
    null = os.open('/dev/null', os.O_RDONLY)
    os.dup2(null, 0); os.dup2(log, 1); os.dup2(log, 2)
    if null > 2:
        os.close(null)
    if log > 2:
        os.close(log)
    return lock


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    device = None
    directory = ROOT
    for argument in sys.argv[1:]:
        if argument.startswith('--device='):
            device = argument.split('=', 1)[1]
        elif argument.startswith('--output='):
            directory = argument.split('=', 1)[1]
    lockfile = None
    if '--daemon' in sys.argv:
        lockfile = daemonize()
        if lockfile is None:
            sys.exit(0)
    try:
        run(device, directory)
    finally:
        if lockfile is not None:
            lockfile.close()
