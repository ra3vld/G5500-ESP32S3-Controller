#!/usr/bin/env python3
"""GS-232 TCP gateway for the G5500 MTR/1 RS-485 bus. GPL-3.0.

Linux/Windows: python -m pip install pyserial
python tools/rs485_gateway.py --serial COM5 --node 202 --first 201 --count 2 --controller 1
"""
import argparse
from dataclasses import dataclass
import logging
import math
import queue
import re
import secrets
import socketserver
import struct
import threading
import time


def crc(data):
    value = 0xffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xa001 if value & 1 else 0)
    return value


def frame(data):
    return data + struct.pack('<H', crc(data))


def newer(value, old):
    return 0 < ((value - old) & 0xffffffff) < 0x80000000


@dataclass
class Config:
    node: int = 202
    first: int = 201
    count: int = 2
    controller: int = 1
    baud: int = 115200
    parity: str = 'E'
    stops: int = 1

    def __post_init__(self):
        if not (1 <= self.first <= self.node < self.first + self.count <= 248 and
                1 <= self.count <= 4 and 1 <= self.controller <= 247 and
                self.controller not in range(self.first, self.first + self.count)):
            raise ValueError('Invalid controller address or master ring')
        if self.baud not in (1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200) or self.parity not in 'NEO' or self.stops not in (1, 2):
            raise ValueError('Invalid UART format')

    def wire(self, size):
        return math.ceil(size * (9 + (self.parity != 'N') + self.stops) * 1000000 / self.baud) / 1000000

    @property
    def transaction(self):
        return max(.080, self.wire(512) + .020)

    @property
    def lease_us(self):
        return max(300000, round(self.transaction * 1000000) * 3)

    @property
    def recovery(self):
        return max(1., self.lease_us * 3 / 1000000)

    @property
    def gap(self):
        return .001750 if self.baud > 19200 else self.wire(1) * 3.5


class Token:
    """MTR/1 state machine, matching firmware modbus_token.hpp.

    Only the serial worker calls this object. Times are monotonic seconds.
    """
    def __init__(self, config, seed, now):
        self.c = config
        self.term, self.ticket = seed or 1, 0
        self.last_bus = self.last_service = now
        self.owner = self.candidate = self.probes = 0
        self.owned = self.pending = self.passing = self.probing = self.fault = False
        self.deadline = self.grant_at = self.probe_at = now

    def lose(self, now):
        self.owned = self.pending = self.passing = self.probing = False
        self.owner = 0
        self.last_bus = now

    def message(self, op, to):
        return frame(bytes((to, 0x41, 17, op, self.c.node, to, self.c.first, self.c.count)) +
                     struct.pack('>IIHH', self.term, self.ticket, self.c.lease_us // 1000, 0x4d54))

    def owns(self, now):
        return not self.fault and self.owned and self.deadline > now

    def can_request(self, now):
        return self.owns(now) and not self.passing and self.deadline - now >= self.c.transaction

    def observe(self, data, now, emit):
        if len(data) != 22 or data[1:3] != b'\x41\x11' or data[18:20] != b'MT':
            return False
        quiet = now - self.last_bus
        self.last_bus = now
        term, ticket, lease, _ = struct.unpack('>IIHH', data[8:20])
        if data[6:8] != bytes((self.c.first, self.c.count)) or lease != self.c.lease_us // 1000:
            self.fault = True
            self.lose(now)
            return True
        op, source, to = data[3:6]
        if source not in range(self.c.first, self.c.first + self.c.count) or to not in range(self.c.first, self.c.first + self.c.count) or data[0] != to:
            return True
        if op == 2:
            if self.owned or self.pending:
                self.lose(now)
            elif source == to and term and ticket and quiet >= self.c.recovery - .100:
                self.term, self.ticket, self.owner = term, ticket, source
        elif op == 3:
            if (source == to or not term or not ticket or
                    (self.owner and (self.owner != source or term != self.term)) or
                    (term == self.term and not newer(ticket, self.ticket))):
                return True
            if self.owned:
                self.lose(now)
                return True
            self.term, self.ticket, self.owner = term, ticket, to
            self.probing = False
            if to == self.c.node:
                if emit(frame(data[:3] + b'\x83' + data[4:-2])):
                    self.pending, self.grant_at = True, now
        elif op == 1 and to == self.c.node:
            if not self.owner or self.owner == source:
                emit(frame(data[:3] + b'\x81' + data[4:-2]))
        elif op == 0x81 and self.owns(now) and self.probing and source == self.c.node and to == self.candidate and term == self.term and ticket == self.ticket:
            self.ticket = (self.ticket + 1) & 0xffffffff or 1
            grant = self.message(3, self.candidate)
            self.owned = self.pending = self.passing = self.probing = False
            self.probes = 0
            self.owner = self.candidate if emit(grant) else 0
        return True

    def service(self, now, idle, outstanding, emit):
        gap = now - self.last_service
        self.last_service = now
        if self.fault:
            return
        if gap > .040:
            self.lose(now)
            return
        if self.pending and idle and now - self.grant_at >= .006:
            self.pending, self.owned, self.owner = False, True, self.c.node
            self.deadline = now + self.c.lease_us / 1000000
            self.passing = self.probing = False
            self.probes = 0
        if self.owned:
            if now >= self.deadline:
                self.lose(now)
                return
            if self.c.count == 1 and not outstanding:
                self.deadline = now + self.c.lease_us / 1000000
                self.passing = False
                return
            if self.deadline - now < self.c.transaction:
                self.passing = True
            if not self.passing or outstanding or not idle:
                return
            if self.probing:
                if now - self.probe_at < max(.030, self.c.wire(44) + .010):
                    return
                self.probing = False
                self.candidate = self.next(self.candidate)
                self.probes += 1
            elif not self.probes:
                self.candidate = self.next(self.c.node)
            if self.candidate == self.c.node or self.probes >= self.c.count - 1:
                self.passing, self.probes = False, 0
                self.deadline = now + self.c.lease_us / 1000000
                return
            if emit(self.message(1, self.candidate)):
                self.probing, self.probe_at = True, now
            return
        if self.pending or not idle or outstanding:
            return
        quiet, slot = now - self.last_bus, (self.c.node - self.c.first) * .050
        if quiet < self.c.recovery + slot:
            return
        if quiet >= self.c.recovery + slot + .020:
            self.last_bus = now
            return
        self.term = (self.term + 1) & 0xffffffff or 1
        self.ticket = (self.ticket + 1) & 0xffffffff or 1
        if emit(self.message(2, self.c.node)):
            self.owned, self.owner = True, self.c.node
            self.deadline = now + self.c.lease_us / 1000000
            self.probes, self.passing = 0, False

    def next(self, address):
        return self.c.first if address + 1 >= self.c.first + self.c.count else address + 1


class Frames:
    """Decode complete CRC-checked RTU messages despite USB read chunking.

    The bus can contain requests and replies. Known RTU lengths let us split
    coalesced USB reads without inventing per-byte arrival timestamps.
    """
    def __init__(self):
        self.data = bytearray()

    def feed(self, data):
        self.data.extend(data)
        result = []
        while len(self.data) >= 4:
            address, fn, count = self.data[:3]
            if not 1 <= address <= 247 or fn not in (3, 4, 6, 16, 0x41, 0x83, 0x84, 0x86, 0x90, 0xc1):
                del self.data[0]
                continue
            sizes = [5] if fn & 0x80 else [5 + count] if fn == 0x41 else [8] if fn == 6 else [8, 5 + count] if fn in (3, 4) else [8]
            if fn == 16 and len(self.data) >= 7:
                sizes.append(9 + self.data[6])
            size = next((n for n in sizes if 4 <= n <= 256 and len(self.data) >= n and crc(self.data[:n]) == 0), None)
            if size:
                result.append(bytes(self.data[:size]))
                del self.data[:size]
            elif len(self.data) >= 256:
                del self.data[0]
            else:
                break
        return result


class Job:
    def __init__(self, data, deadline):
        self.data, self.deadline = data, deadline
        self.done = threading.Event()
        self.result, self.error = None, None

    def finish(self, result=None, error=None):
        self.result, self.error = result, error
        self.done.set()


def response_matches(request, response):
    if response[:1] != request[:1] or (response[1] & 0x7f) != request[1]:
        return False
    if response[1] & 0x80:
        return len(response) == 5
    if response[1] in (3, 4):
        return response[2] == 2 * int.from_bytes(request[4:6], 'big') and len(response) == 5 + response[2]
    return len(response) == 8 and response[2:6] == request[2:6]


class Bus(threading.Thread):
    def __init__(self, port, config, timeout=5):
        super().__init__(daemon=True)
        self.port, self.c, self.timeout = port, config, timeout
        self.jobs = queue.Queue(maxsize=8)
        self.stop_event = threading.Event()
        self.failure = None

    def request(self, pdu):
        if self.failure:
            raise OSError(self.failure)
        job = Job(frame(bytes((self.c.controller,)) + pdu), time.monotonic() + self.timeout)
        try:
            self.jobs.put_nowait(job)
        except queue.Full as error:
            raise OSError('Gateway busy') from error
        if not job.done.wait(self.timeout):
            raise TimeoutError('Bus response timed out; check controller state before retrying')
        if job.error:
            raise OSError(job.error)
        if job.result[1] & 0x80:
            raise OSError(f'Modbus exception {job.result[2]:02X}')
        return job.result

    def run(self):
        token = Token(self.c, secrets.randbits(32), time.monotonic())
        decoder, active, received = Frames(), None, []
        last_rx = tx_until = time.monotonic()
        sent_at = last_rx
        def emit(data):
            nonlocal tx_until
            start = time.monotonic()
            if start < tx_until:
                return False
            if self.port.write(data) != len(data):
                raise OSError('Incomplete serial write')
            tx_until = max(time.monotonic(), start + self.c.wire(len(data))) + self.c.gap
            token.last_bus = start
            return True
        try:
            while not self.stop_event.is_set():
                now = time.monotonic()
                data = self.port.read(4096)
                if now-token.last_service > .040:
                    # Buffered USB bytes after a process stall have unknown age.
                    # Drop them and wait through recovery before transmitting.
                    decoder.data.clear()
                    received.clear()
                    token.lose(now)
                    data = b''
                    last_rx = now
                if data:
                    if now - last_rx > self.c.gap:
                        decoder.data.clear()
                    last_rx = now
                    received.extend(decoder.feed(data))
                if now-last_rx >= self.c.gap:
                    for message in received:
                        if token.observe(message, now, emit):
                            continue
                        token.last_bus = now
                        if active and now - sent_at < self.c.transaction and response_matches(active.data, message):
                            active.finish(message)
                            active = None
                            token.passing = True
                    received.clear()
                    if decoder.data:
                        decoder.data.clear()
                        token.last_bus = now
                if active and (now - sent_at >= self.c.transaction or now >= active.deadline or not token.owns(now)):
                    active.finish(error='No RTU response; command may have executed')
                    active = None
                    token.passing = True
                idle = now >= tx_until and now - last_rx >= self.c.gap and not decoder.data
                token.service(now, idle, active is not None, emit)
                if token.fault:
                    raise OSError('MTR/1 ring configuration mismatch; gateway stopped')
                if idle and time.monotonic() >= tx_until and not active and token.can_request(now):
                    try:
                        job = self.jobs.get_nowait()
                    except queue.Empty:
                        token.passing = True
                    else:
                        if now >= job.deadline:
                            job.finish(error='Expired before transmission')
                        elif emit(job.data):
                            active, sent_at = job, now
                        else:
                            job.finish(error='Serial transmitter busy')
                self.stop_event.wait(.001)
        except Exception as error:
            self.failure = str(error)
            logging.error('RS-485: %s', error)
        finally:
            self.failure = self.failure or 'Gateway stopped'
            if active:
                active.finish(error=self.failure)
            while not self.jobs.empty():
                self.jobs.get_nowait().finish(error=self.failure)


class Gs232:
    def __init__(self, bus):
        self.bus = bus
        self.session, self.command = secrets.randbits(32) or 1, 0

    def snapshot(self):
        state=self.bus.request(struct.pack('>BHH', 3, 0, 32))[3:-2]
        if len(state)!=64 or state[:2]!=b'\x00\x01':
            raise OSError('Unsupported controller register map')
        return state

    def line(self, text):
        text = text.strip().upper()
        if not text:
            return b''
        if text == 'S':
            self.bus.request(struct.pack('>BHH', 6, 0x110, 1))
            return b''
        if text in ('C', 'C2', 'B'):
            state = self.snapshot()
            az, el = struct.unpack('>ii', state[8:16])
            angle = lambda v: math.floor(v / 1000 + .5) if v >= 0 else math.ceil(v / 1000 - .5)
            a, e = angle(az), angle(el)
            return ((f'AZ={a:03d}' if text == 'C' else f'EL={e:03d}' if text == 'B' else f'AZ={a:03d} EL={e:03d}') + '\r\n').encode()
        match = re.fullmatch(r'(M|W)\s*([+-]?\d{1,3}(?:\.\d{1,3})?)(?:\s+([+-]?\d{1,3}(?:\.\d{1,3})?))?', text)
        if not match or (match[1] == 'W') != (match[3] is not None):
            return b'?>\r\n'
        state = self.snapshot()
        tick, boot = struct.unpack('>II', state[52:60])
        self.command = (self.command + 1) & 0xffffffff or 1
        az, el = round(float(match[2]) * 1000), round(float(match[3] or 0) * 1000)
        command = struct.pack('>HIIIIHHiiH', self.bus.c.node, self.session, self.command, boot, tick, 1, 3 if match[3] is not None else 1, az, el, 0)
        self.bus.request(struct.pack('>BHHB', 16, 0x100, 16, 32) + command)
        return b''


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        logging.info('GS-232 client %s', self.client_address)
        gs, data, overflow = Gs232(self.server.bus), bytearray(), False
        self.request.settimeout(1)
        while not self.server.bus.stop_event.is_set():
            try:
                chunk = self.request.recv(1024)
            except TimeoutError:
                continue
            if not chunk:
                return
            for byte in chunk:
                if byte in (10, 13):
                    if overflow:
                        reply = b'?>\r\n'
                    elif not data:
                        continue
                    else:
                        try:
                            reply = gs.line(data.decode('ascii'))
                        except (OSError, ValueError, UnicodeError) as error:
                            logging.warning('GS-232: %s', error)
                            reply = b'?>\r\n'
                    data.clear()
                    overflow = False
                    if reply:
                        self.request.sendall(reply)
                elif len(data) < 128 and not overflow:
                    data.append(byte)
                else:
                    data.clear()
                    overflow = True


class Server(socketserver.TCPServer):
    allow_reuse_address = True

    def __init__(self, address, bus):
        self.bus = bus
        super().__init__(address, Handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--serial', required=True, help='/dev/ttyUSB0 or COM5; automatic direction, no local echo')
    parser.add_argument('--node', type=int, default=202)
    parser.add_argument('--first', type=int, default=201)
    parser.add_argument('--count', type=int, default=2, help='All masters, including this PC (1..4)')
    parser.add_argument('--controller', type=int, default=1)
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--parity', choices=('N', 'E', 'O'), default='E')
    parser.add_argument('--stops', type=int, choices=(1, 2), default=1)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=4533)
    parser.add_argument('--timeout', type=float, default=5)
    args = parser.parse_args()
    try:
        config = Config(args.node, args.first, args.count, args.controller, args.baud, args.parity, args.stops)
        if not 1 <= args.port <= 65535 or not math.isfinite(args.timeout) or args.timeout <= 0:
            raise ValueError('Invalid TCP port or timeout')
        import serial
    except (ValueError, ImportError) as error:
        parser.error(str(error) + '; install dependency: python -m pip install pyserial')
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s')
    with serial.Serial(args.serial, args.baud, parity=args.parity, stopbits=args.stops, timeout=0, write_timeout=.020) as port:
        bus = Bus(port, config, args.timeout)
        with Server((args.host, args.port), bus) as server:
            logging.info('GS-232 %s:%d -> RS-485 controller %d, master %d', args.host, args.port, args.controller, args.node)
            bus.start()
            try:
                server.serve_forever(poll_interval=.1)
            except KeyboardInterrupt:
                pass
            finally:
                bus.stop_event.set()
                bus.join(timeout=1)


if __name__ == '__main__':
    main()
