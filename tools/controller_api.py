#!/usr/bin/env python3
"""Manage a G5500 controller over UART/USB, UDP or TCP without a browser."""
import argparse
import base64
import json
import os
import secrets
import select
import socket
import sys
import time
from urllib.parse import urlencode

INPUT_MAX = 1024
REPLY_MAX = 1200


class ApiError(RuntimeError):
    pass


class SocketTransport:
    def __init__(self, address, datagram, timeout):
        host, separator, port = address.rpartition(':')
        if not separator:
            host, port = address, 4535 if datagram else 4533
        self.socket = socket.create_connection((host, int(port)), timeout) if not datagram else socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if datagram:
            self.socket.connect((host, int(port)))
        self.socket.settimeout(timeout)
        self.datagram = datagram
        self.buffer = bytearray()

    def send(self, data):
        if self.datagram:
            self.socket.send(data)
        else:
            self.socket.sendall(data + b'\r\n')

    def receive(self, timeout):
        deadline = time.monotonic() + timeout
        while True:
            if not self.datagram and b'\n' in self.buffer:
                line, _, rest = self.buffer.partition(b'\n')
                self.buffer[:] = rest
                return bytes(line).strip()
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('response timeout')
            self.socket.settimeout(remaining)
            data = self.socket.recv(REPLY_MAX + 2)
            if not data:
                raise ApiError('connection closed; read device state before repeating an action')
            if self.datagram:
                if len(data) > REPLY_MAX:
                    raise ApiError('oversized datagram')
                return data
            self.buffer.extend(data)
            if len(self.buffer) > 4 * REPLY_MAX:
                self.buffer.clear()
                raise ApiError('oversized stream response')

    def close(self):
        self.socket.close()


class SerialTransport:
    def __init__(self, port, baud):
        self.buffer = bytearray()
        self.serial = None
        self.fd = None
        if os.name == 'posix':
            import termios
            import tty
            self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            self.settings = termios.tcgetattr(self.fd)
            try:
                tty.setraw(self.fd)
                settings = termios.tcgetattr(self.fd)
                settings[4] = settings[5] = getattr(termios, 'B' + str(baud))
                termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            except BaseException:
                self.close()
                raise
        else:
            try:
                import serial
            except ImportError as error:
                raise ApiError('Install pyserial for serial access on this OS: python -m pip install pyserial') from error
            self.serial = serial.Serial(port=None, baudrate=baud, timeout=0, write_timeout=5)
            self.serial.dtr = False
            self.serial.rts = False
            self.serial.port = port
            self.serial.open()

    def send(self, data):
        data += b'\r\n'
        if self.serial:
            self.serial.write(data)
            return
        deadline = time.monotonic() + 5
        while data:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise TimeoutError('serial write timeout')
            try:
                count = os.write(self.fd, data)
            except BlockingIOError:
                continue
            data = data[count:]

    def receive(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b'\n' in self.buffer:
                line, _, rest = self.buffer.partition(b'\n')
                self.buffer[:] = rest
                return bytes(line).strip()
            if self.serial:
                data = self.serial.read(REPLY_MAX + 2)
                if not data:
                    time.sleep(0.01)
            else:
                if not select.select([self.fd], [], [], max(0, deadline - time.monotonic()))[0]:
                    break
                try:
                    data = os.read(self.fd, REPLY_MAX + 2)
                except BlockingIOError:
                    continue
            self.buffer.extend(data)
            if len(self.buffer) > 4 * REPLY_MAX:
                self.buffer.clear()
                raise ApiError('oversized serial response')
        raise TimeoutError('response timeout')

    def close(self):
        if self.serial:
            self.serial.close()
        if self.fd is not None:
            import termios
            try:
                termios.tcsetattr(self.fd, termios.TCSANOW, self.settings)
            finally:
                os.close(self.fd)
                self.fd = None


class Client:
    def __init__(self, transport, timeout=5, retries=3):
        self.transport = transport
        self.timeout = timeout
        self.retries = retries
        self.cid = 0
        self.boot = None
        self.sid = None
        self.selection = 0

    def exchange(self, fields):
        self.cid += 1
        fields = dict(v=1, cid=self.cid, **fields)
        request = ('@' + urlencode(fields)).encode('ascii')
        if len(request) > INPUT_MAX:
            raise ApiError('request exceeds 1024 bytes; split settings into smaller requests')
        for _ in range(self.retries):
            self.transport.send(request)
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                try:
                    data = self.transport.receive(deadline - time.monotonic())
                except (TimeoutError, socket.timeout):
                    break
                try:
                    reply = json.loads(data)
                except (ValueError, UnicodeError):
                    continue  # boot messages or an interrupted older stream reply
                if not isinstance(reply, dict) or reply.get('v') != 1 or reply.get('cid') != self.cid:
                    continue
                return reply
        raise ApiError('no response after identical retries; read device state before repeating an action')

    def hello(self):
        reply = self.exchange(dict(method='GET', path='/api/hello', client=secrets.randbelow(0xFFFFFFFF) + 1))
        if reply.get('status') != 200:
            raise ApiError(str(reply))
        self.boot, self.sid = reply['boot'], reply['sid']
        self.selection = reply.get('selection', 0)
        return reply

    def request(self, method, path, arguments=None):
        if self.sid is None:
            self.hello()
        arguments = arguments or {}
        reserved = {'v', 'cid', 'boot', 'sid', 'method', 'path', 'page', 'client', 'selection'}
        if reserved.intersection(arguments):
            raise ApiError('argument conflicts with protocol metadata')
        # A changing document is never silently assembled from different versions.
        # Writes are retried only with the identical request, never a new session.
        for attempt in range(3 if method == 'GET' else 1):
            data = bytearray()
            page, revision, total = 0, None, None
            while True:
                reply = self.exchange(dict(method=method, path=path, boot=self.boot, sid=self.sid, selection=self.selection, page=page, **arguments))
                if 'error' in reply:
                    raise ApiError(str(reply))
                if reply.get('boot') != self.boot or reply.get('sid') != self.sid:
                    raise ApiError('device/session changed; read device state before repeating an action')
                self.selection = reply.get('selection', self.selection)
                if revision is None:
                    revision, total = reply['revision'], reply['total']
                elif revision != reply['revision'] or total != reply['total']:
                    break
                if reply['offset'] != page:
                    raise ApiError('invalid response page')
                chunk = base64.b64decode(reply['base64'], validate=True)
                data.extend(chunk)
                if len(data) > total or total > 65536:
                    raise ApiError('invalid response size')
                following = reply['next']
                if following is None:
                    if len(data) != total:
                        raise ApiError('incomplete response')
                    text = data.decode('utf-8')
                    body = json.loads(text) if reply['type'] == 'application/json' else text
                    return dict(status=reply['status'], boot=self.boot, body=body)
                if following != page + len(chunk) or not chunk:
                    raise ApiError('invalid pagination')
                page = following
            if attempt == 2:
                raise ApiError('document changed during pagination; stop the changing operation or request a smaller view')
        raise ApiError('document changed during pagination')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument('--serial', metavar='PORT')
    transport.add_argument('--udp', metavar='HOST[:PORT]')
    transport.add_argument('--tcp', metavar='HOST[:PORT]')
    parser.add_argument('--baud', type=int, default=9600)
    parser.add_argument('--timeout', type=float, default=5)
    parser.add_argument('--retries', type=int, default=3)
    parser.add_argument('--watch', type=float, metavar='SECONDS', help='Repeat GET at this interval')
    parser.add_argument('--duration', type=float, default=0, help='Stop watching after this many seconds (0: until interrupted)')
    parser.add_argument('--log', help='Append timestamped JSONL results; flush every sample')
    parser.add_argument('method', choices=['GET', 'POST'])
    parser.add_argument('path')
    parser.add_argument('arguments', nargs='*', metavar='KEY=VALUE')
    args = parser.parse_args(argv)
    if args.timeout <= 0 or args.retries < 1 or args.duration < 0 or (args.watch is not None and (args.watch <= 0 or args.method != 'GET')):
        parser.error('positive timeout/retries/watch are required; watch supports GET only')
    arguments = {}
    for field in args.arguments:
        key, separator, value = field.partition('=')
        if not separator or not key or key in arguments:
            parser.error('each field must be a unique KEY=VALUE')
        arguments[key] = value
    link = log = None
    try:
        link = SerialTransport(args.serial, args.baud) if args.serial else SocketTransport(args.udp or args.tcp, bool(args.udp), args.timeout)
        client = Client(link, args.timeout, args.retries)
        if args.log:
            log = open(args.log, 'a', encoding='utf-8', buffering=1)
        started = time.monotonic()
        while True:
            result = client.request(args.method, args.path, arguments)
            if log:
                log.write(json.dumps(dict(time=time.time(), **result), ensure_ascii=False) + '\n')
                log.flush()
            print(json.dumps(result, ensure_ascii=False, indent=None if args.watch else 2), flush=True)
            if result['status'] >= 400:
                return 1
            elapsed = time.monotonic() - started
            if not args.watch or (args.duration and elapsed >= args.duration):
                return 0
            time.sleep(min(args.watch, args.duration - elapsed) if args.duration else args.watch)
    except (ApiError, OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    finally:
        if log:
            log.close()
        if link:
            link.close()


if __name__ == '__main__':
    raise SystemExit(main())
