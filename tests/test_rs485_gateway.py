"""Gateway protocol checks, including a ring with the production C++ peer."""
from pathlib import Path
import os
import shlex
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import rs485_gateway as g


class GatewayTests(unittest.TestCase):
    def test_usb_chunking(self):
        self.assertEqual(g.crc(bytes.fromhex('01030000000a')), 0xcdc5)
        packets = [g.frame(bytes.fromhex('010300000020')), g.frame(b'\x01\x03\x04\x00\x01\x00\x02'),
                   g.frame(bytes.fromhex('01100100001020') + bytes(32)),
                   g.frame(bytes.fromhex('011001000010')),
                   g.Token(g.Config(), 1, 0).message(2, 202)]
        joined = b''.join(packets)
        for size in (1, 2, 7, 32, len(joined)):
            decoder, parsed = g.Frames(), []
            for pos in range(0, len(joined), size):
                parsed += decoder.feed(joined[pos:pos+size])
            self.assertEqual(parsed, packets)
        bad = bytearray(packets[0]);bad[-1] ^= 1
        self.assertEqual(g.Frames().feed(bad), [])

    def test_gs232_mapping_and_tcp(self):
        class FakeBus:
            c = g.Config()
            stop_event = threading.Event()
            def __init__(self):
                self.requests = []
            def request(self, pdu):
                self.requests.append(pdu)
                if pdu[0] == 3:
                    state = bytearray(64)
                    state[:2] = b'\x00\x01'
                    struct.pack_into('>ii', state, 8, 123500, 42000)
                    struct.pack_into('>II', state, 52, 1500, 456)
                    return g.frame(b'\x01\x03\x40' + state)
                return g.frame(b'\x01' + pdu[:5])
        bus = FakeBus();gs = g.Gs232(bus)
        self.assertEqual(gs.line('C2'), b'AZ=124 EL=042\r\n')
        self.assertEqual(gs.line('B'), b'EL=042\r\n')
        self.assertEqual(gs.line('W210 060'), b'')
        values = struct.unpack('>HIIIIHHiiH', bus.requests[-1][6:])
        self.assertEqual(values[3:], (456, 1500, 1, 3, 210000, 60000, 0))
        self.assertEqual(gs.line('M-20'), b'')
        self.assertEqual(struct.unpack('>HIIIIHHiiH', bus.requests[-1][6:])[6:9], (1, -20000, 0))
        gs.line('S');self.assertEqual(bus.requests[-1], bytes.fromhex('0601100001'))
        for line in ('W20', 'M20 30', 'R', 'MNaN', 'M1e3'):
            self.assertEqual(gs.line(line), b'?>\r\n')
        with g.Server(('127.0.0.1', 0), bus) as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True);worker.start()
            try:
                with socket.create_connection(server.server_address, timeout=2) as client:
                    client.sendall(b'C');client.sendall(b'2\r\nB\n')
                    stream = client.makefile('rb')
                    self.assertEqual(stream.readline(), b'AZ=124 EL=042\r\n')
                    self.assertEqual(stream.readline(), b'EL=042\r\n')
                    client.sendall(b'x' * 140 + b'\nC\n')
                    self.assertEqual(stream.readline(), b'?>\r\n')
                    self.assertEqual(stream.readline(), b'AZ=124\r\n')
                    stream.close()
            finally:
                server.shutdown();worker.join()

    def test_serial_worker_transactions_timeout_and_failure(self):
        class Port:
            def __init__(self):
                self.pending, self.writes = bytearray(), []
                self.fail = self.silent = False
                self.due = 0
            def read(self, _):
                if self.fail:
                    raise OSError('adapter disconnected')
                if time.monotonic() < self.due:
                    return b''
                data=bytes(self.pending[:8]);del self.pending[:8]
                return data
            def write(self, data):
                self.writes.append(data)
                if data[1] != 0x41 and not self.silent:
                    if data[1] == 3:
                        state=bytearray(64);state[:2]=b'\x00\x01'
                        struct.pack_into('>ii', state, 8, 123000, 45000)
                        self.pending.extend(g.frame(b'\x01\x03\x40'+state))
                    else:
                        self.pending.extend(data)
                    self.due=time.monotonic()+.005
                return len(data)
        port=Port();bus=g.Bus(port,g.Config(node=201,count=1),timeout=2);bus.start()
        try:
            gs=g.Gs232(bus)
            self.assertEqual(gs.line('C2'),b'AZ=123 EL=045\r\n')
            self.assertEqual(gs.line('S'),b'')
            ordinary=[p for p in port.writes if p[1]!=0x41]
            self.assertEqual([p[1] for p in ordinary],[3,6])
            port.silent=True
            with self.assertRaisesRegex(OSError,'No RTU response'):
                gs.line('S')
            port.fail=True
            bus.join(timeout=1)
            with self.assertRaisesRegex(OSError,'adapter disconnected'):
                gs.line('C')
        finally:
            bus.stop_event.set();bus.join(timeout=1)

    def test_response_validation_and_config(self):
        query = g.frame(bytes.fromhex('010300000002'))
        self.assertTrue(g.response_matches(query, g.frame(bytes.fromhex('01030400010002'))))
        self.assertFalse(g.response_matches(query, g.frame(bytes.fromhex('02030400010002'))))
        self.assertFalse(g.response_matches(query, g.frame(bytes.fromhex('0103020001'))))
        for fields in ({'node': 200}, {'count': 5}, {'controller': 201}, {'first': 247, 'count': 2}, {'baud': 12345}):
            with self.assertRaises(ValueError):
                g.Config(**fields)
        for baud in (1200, 9600, 115200):
            c = g.Config(baud=baud, parity='O', stops=2)
            self.assertGreaterEqual(c.lease_us, c.transaction * 3 * 1000000 - 1)

    def test_firmware_ring_lost_ack_reboot_and_recovery(self):
        with tempfile.TemporaryDirectory() as work:
            peer = Path(work) / 'peer'
            subprocess.run([*shlex.split(os.environ.get('CXX','g++')), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'G5500_Fork'),
                            str(ROOT/'tests/gateway_token_peer.cpp'), '-o', str(peer)], check=True)
            process = subprocess.Popen([str(peer)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            try:
                token, pending = g.Token(g.Config(), 222, 0), []
                firmware_owns = False
                busy, claims, grants, acquisitions = 0, 0, 0, 0
                def command(text):
                    process.stdin.write(text+'\n');process.stdin.flush()
                    parts = process.stdout.readline().split()
                    self.assertTrue(parts)
                    self.assertEqual(parts[2], '0')
                    return parts[1] == '1', [bytes.fromhex(p) for p in parts[3:]]
                for us in range(1000, 10000000, 1000):
                    now = us / 1000000
                    def emit(source, packet):
                        nonlocal busy, claims, grants
                        self.assertGreaterEqual(us, busy)
                        busy = us + round(token.c.wire(len(packet))*1000000) + 2000
                        token.last_bus = now if source == 1 else token.last_bus
                        if packet[3] == 2:
                            claims += 1
                        if packet[3] == 3:
                            grants += 1
                        # Lose grant ACKs and one grant; old owners must stay silent.
                        if packet[3] != 0x83 and not (packet[3] == 3 and grants == 4):
                            pending.append((busy, source, packet))
                        return True
                    if pending and us >= pending[0][0]:
                        _, source, packet = pending.pop(0)
                        if source == 0:
                            token.observe(packet, now, lambda p: emit(1, p))
                        elif not (5_000_000 <= us < 7_000_000):
                            firmware_owns, messages = command(f'r {us} {packet.hex()}')
                            for message in messages:
                                emit(0, message)
                    if us == 7_000_000:
                        firmware_owns, _ = command(f'b {us}')
                    if not (5_000_000 <= us < 7_000_000):
                        firmware_owns, messages = command(f's {us} {int(us >= busy)} 0')
                        for message in messages:
                            emit(0, message)
                    else:
                        firmware_owns = False
                    owned_before = token.owns(now)
                    token.service(now, us >= busy, False, lambda p: emit(1, p))
                    if not owned_before and token.owns(now):
                        acquisitions += 1
                    self.assertLessEqual(int(firmware_owns) + int(token.owns(now)), 1)
                    if token.owns(now):
                        token.passing = True
                self.assertGreater(grants, 10)
                self.assertGreaterEqual(claims, 3)
                self.assertGreater(acquisitions, 3)
                wrong = g.Token(g.Config(count=3), 9, 0).message(2, 202)
                self.assertTrue(token.observe(wrong, 11, lambda _: True))
                self.assertTrue(token.fault)
                self.assertFalse(token.owns(11))
            finally:
                process.stdin.close();process.wait(timeout=5);process.stdout.close()


if __name__ == '__main__':
    unittest.main()
