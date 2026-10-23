"""Exercise the PC client against the production command dispatcher over real transports."""
import importlib.util
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import threading
import unittest
from urllib.parse import parse_qs

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('controller_api', ROOT / 'tools/controller_api.py')
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)


class Bridge:
    def __init__(self, kind, drop_post=False):
        self.kind = kind
        self.drop_post = drop_post
        self.requests = []
        self.responses = []
        self.stopped = threading.Event()
        self.error = None
        self.connection = None
        self.engine = subprocess.Popen([str(ROOT / 'build/device_api_tests'), '--serve'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if kind == 'serial':
            import pty
            self.master, self.slave = pty.openpty()
            self.address = os.ttyname(self.slave)
            self.socket = None
        else:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM if kind == 'udp' else socket.SOCK_STREAM)
            self.socket.bind(('127.0.0.1', 0))
            self.socket.settimeout(0.1)
            self.address = '127.0.0.1:' + str(self.socket.getsockname()[1])
            if kind == 'tcp':
                self.socket.listen(1)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def execute(self, data):
        self.requests.append(data)
        self.engine.stdin.write(data + b'\n')
        self.engine.stdin.flush()
        if not select.select([self.engine.stdout], [], [], 3)[0]:
            raise RuntimeError('native API engine did not reply')
        response = self.engine.stdout.readline().strip()
        if not response:
            raise RuntimeError(self.engine.stderr.read().decode())
        self.responses.append(response)
        fields = parse_qs(data[1:].decode())
        if self.drop_post and fields.get('method') == ['POST']:
            self.drop_post = False
            return None
        return response

    def run(self):
        try:
            buffer = bytearray()
            while not self.stopped.is_set():
                if self.kind == 'udp':
                    try:
                        data, peer = self.socket.recvfrom(4096)
                    except socket.timeout:
                        continue
                    response = self.execute(data)
                    if response:
                        self.socket.sendto(response, peer)
                    continue
                if self.kind == 'tcp' and self.connection is None:
                    try:
                        self.connection, _ = self.socket.accept()
                        self.connection.settimeout(0.1)
                    except socket.timeout:
                        continue
                if self.kind == 'serial':
                    if not select.select([self.master], [], [], 0.1)[0]:
                        continue
                    data = os.read(self.master, 4096)
                else:
                    try:
                        data = self.connection.recv(4096)
                    except socket.timeout:
                        continue
                if not data:
                    return
                buffer.extend(data)
                while b'\n' in buffer:
                    line, _, remaining = buffer.partition(b'\n')
                    buffer[:] = remaining
                    if not line.strip():
                        continue
                    response = self.execute(bytes(line).strip())
                    if response:
                        framed = response + b'\r\n'
                        # Fragment every JSON reply across arbitrary byte boundaries.
                        for offset in range(0, len(framed), 17):
                            if self.kind == 'serial':
                                os.write(self.master, framed[offset:offset + 17])
                            else:
                                self.connection.sendall(framed[offset:offset + 17])
        except (OSError, ValueError) as error:
            if not self.stopped.is_set():
                self.error = error
        except BaseException as error:
            self.error = error

    def close(self):
        self.stopped.set()
        if self.connection:
            self.connection.close()
        if self.socket:
            self.socket.close()
        self.engine.terminate()
        self.engine.communicate(timeout=3)
        self.thread.join(timeout=3)
        if self.kind == 'serial':
            os.close(self.master)
            os.close(self.slave)
        if self.error:
            raise self.error


class CommandApiTests(unittest.TestCase):
    def bridge(self, kind, **kwargs):
        bridge = Bridge(kind, **kwargs)
        self.addCleanup(bridge.close)
        return bridge

    def test_uart_usb_cli_fragmentation(self):
        bridge = self.bridge('serial')
        result = subprocess.run(['python3', str(ROOT / 'tools/controller_api.py'), '--serial', bridge.address, 'GET', '/api/status'], text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        body = json.loads(result.stdout)['body']
        self.assertEqual(body['role'], 'controller')
        self.assertEqual(len(body['axes']), 2)

    def test_udp_lost_response_replays_identical_write(self):
        bridge = self.bridge('udp', drop_post=True)
        link = api.SocketTransport(bridge.address, True, 0.15)
        self.addCleanup(link.close)
        client = api.Client(link, timeout=0.15)
        result = client.request('POST', '/api/mode', {'mode': 'auto'})
        self.assertEqual(result['status'], 200)
        self.assertEqual(bridge.requests[1], bridge.requests[2])
        self.assertEqual(bridge.responses[1], bridge.responses[2])
        self.assertEqual(client.request('GET', '/api/status')['body']['controlMode'], 'auto')

    def test_usb_runtime_speed_and_autotune_options(self):
        bridge = self.bridge('serial')
        link = api.SerialTransport(bridge.address, 9600)
        self.addCleanup(link.close)
        client = api.Client(link, timeout=1)
        before = client.request('GET', '/api/config')['body']['speedPercent']
        self.assertEqual(client.request('POST', '/api/speed', {'axis': 'az', 'value': '25'})['status'], 200)
        axis = client.request('GET', '/api/status', {'view': 'axis', 'axis': 'az'})['body']
        self.assertEqual(axis['speedSetpoint'], 25)
        self.assertTrue(axis['speedAdjustable'])
        self.assertEqual(client.request('GET', '/api/config')['body']['speedPercent'], before)
        self.assertEqual(client.request('POST', '/api/speed', {'axis': 'az', 'value': '101'})['status'], 400)
        axis = client.request('GET', '/api/status', {'view': 'axis', 'axis': 'az'})['body']
        self.assertEqual(axis['speedSetpoint'], 25)
        full = client.request('GET', '/api/motion/autotune', {'full': '1'})['body']
        self.assertEqual(full['options']['tuneLambdaFactor'], 1)
        self.assertEqual(full['options']['methodVersion'], 8)
        self.assertEqual(full['options']['tuneStages'], 31)
        rejected = client.request('POST', '/api/motion/autotune', dict(
            action='start', axis='az', mode='simple', id=full['id'], travel=60,
            outputLimit=50, tuneMisspelled=1))
        self.assertEqual(rejected['status'], 400)
        self.assertEqual(client.request('GET', '/api/motion/autotune')['body']['id'], full['id'])
        self.assertEqual(client.request('POST', '/api/settings', {
            'azRegulatorMode': 'cascade', 'azMotionVelocity': '.001'})['status'], 200)
        self.assertEqual(client.request('POST', '/api/speed', {'axis': 'az', 'value': '.001'})['status'], 200)
        slow = client.request('GET', '/api/status', {'view': 'axis', 'axis': 'az'})['body']
        self.assertEqual(slow['speedSetpoint'], .001)
        self.assertEqual(slow['speedLimit'], .001)
        self.assertTrue(all(len(reply) <= 1200 for reply in bridge.responses))

    def test_tcp_pages_and_settings(self):
        bridge = self.bridge('tcp')
        link = api.SocketTransport(bridge.address, False, 1)
        self.addCleanup(link.close)
        client = api.Client(link, timeout=1)
        motion = client.request('GET', '/api/motion')['body']
        self.assertEqual(len(motion['axes']), 2)
        self.assertIn('PositionKp', motion['axes'][0])
        self.assertGreater(len(bridge.requests), 2)
        self.assertTrue(all(len(reply) <= 1200 for reply in bridge.responses))
        self.assertEqual(client.request('POST', '/api/settings', {'speedPercent': '60'})['status'], 200)
        self.assertEqual(client.request('GET', '/api/config')['body']['speedPercent'], 60)

    def test_pin_configuration_and_capability_pages(self):
        for kind in ('serial', 'tcp', 'udp'):
            with self.subTest(transport=kind):
                bridge = self.bridge(kind)
                link = api.SerialTransport(bridge.address, 9600) if kind == 'serial' else api.SocketTransport(bridge.address, kind == 'udp', 1)
                self.addCleanup(link.close)
                client = api.Client(link, timeout=1)
                hardware = client.request('GET', '/api/hardware')['body']
                caps = client.request('GET', '/api/hardware/pins')['body']
                self.assertEqual(len(caps['roles']), 39)
                self.assertEqual(len(caps['gpios']), 49)
                fields = dict(token=hardware['token'], generation=hardware['generation'])
                fields['pin.azAdc'] = 36
                self.assertEqual(client.request('POST', '/api/hardware', fields)['status'], 200)
                configured = client.request('GET', '/api/hardware')['body']
                self.assertEqual(configured['configured']['pins']['azAdc'], 36)
                self.assertEqual(client.request('GET', '/api/hardware', {'view': 'confirmed'})['body']['configured']['pins']['azAdc'], 34)
                self.assertTrue(all(len(reply) <= 1200 for reply in bridge.responses))

    def test_wifi_unicode_response(self):
        bridge = self.bridge('udp')
        link = api.SocketTransport(bridge.address, True, 1)
        self.addCleanup(link.close)
        result = api.Client(link, timeout=1).request('POST', '/api/wifi', {'ssid': 'Станция', 'pass': '12345678'})
        self.assertEqual(result['status'], 200)
        self.assertIn('сохранены', result['body'])
        self.assertIn(b'%D0', bridge.requests[-1])

    def test_changed_document_never_mixed(self):
        class ChangingTransport:
            def send(self, data):
                self.fields = {key: values[0] for key, values in parse_qs(data[1:].decode()).items()}
            def receive(self, _timeout):
                fields = self.fields
                if fields['path'] == '/api/hello':
                    return json.dumps(dict(v=1, cid=int(fields['cid']), status=200, sid=1, boot='boot')).encode()
                page = int(fields['page'])
                return json.dumps(dict(v=1, cid=int(fields['cid']), sid=1, boot='boot', status=200, offset=page, next=1 if not page else None, total=2, revision=page, type='application/json', base64='YQ==')).encode()
        with self.assertRaisesRegex(api.ApiError, 'changed during pagination'):
            api.Client(ChangingTransport()).request('GET', '/api/motion')


if __name__ == '__main__':
    unittest.main()
