"""Real UDP HUD/1 plus HTTP/TCP/logger ingress, using shared C++ firmware cores."""
import json
import pathlib
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]


USED_PORTS = set()

def unused_port():
    with socket.socket(type=socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
        if port in USED_PORTS or port-1 in USED_PORTS or port+1 in USED_PORTS:
            return unused_port()
        USED_PORTS.add(port)
        return port


class HudNetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='g5500-hud-network-')
        cls.nodes = []
        try:
            for number, role in [(101, 'controller'), (102, 'controller'), (201, 'hud'), (202, 'hud')]:
                ports = {name: unused_port() for name in ('http', 'tcp', 'n1mm', 'pst', 'report', 'hud')}
                log = open(pathlib.Path(cls.temp.name) / f'{number}.log', 'w')
                cmd = [sys.executable, str(ROOT / 'tools/simulator.py'), '--role', role, '--device-id', f'{number:x}', '--name', f'R{number}']
                for name, port in ports.items():
                    cmd += [f'--{name}-port', str(port)]
                process = subprocess.Popen(cmd, cwd=ROOT, stdout=log, stderr=log)
                node = dict(id=f'{number:012x}', ports=ports, process=process, log=log)
                cls.nodes.append(node)
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    try:
                        cls.request(node, '/api/status')
                        break
                    except (OSError, urllib.error.URLError):
                        if process.poll() is not None:
                            raise RuntimeError(pathlib.Path(log.name).read_text())
                        time.sleep(.05)
                else:
                    raise RuntimeError('simulator startup timeout')
        except BaseException:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        for node in cls.nodes:
            node['process'].terminate()
        for node in cls.nodes:
            try:
                node['process'].wait(timeout=5)
            except subprocess.TimeoutExpired:
                node['process'].kill()
                node['process'].wait()
            node['log'].close()
        cls.temp.cleanup()

    @staticmethod
    def request(node, path, form=None):
        data = urllib.parse.urlencode(form).encode() if form is not None else None
        request = urllib.request.Request(f"http://127.0.0.1:{node['ports']['http']}{path}", data=data)
        with urllib.request.urlopen(request, timeout=3) as response:
            return response.status, json.load(response)

    def wait_for(self, predicate, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(.03)
        self.fail('condition not reached')

    def command(self, node, path, form):
        status, result = self.request(node, path, form)
        if status == 202:
            self.assertFalse(result['ok'])
            request_id = result['requestId']
            def complete():
                _, value = self.request(node, f'/api/hud/command?id={request_id}')
                return value if value['result'] != 'deferred' else None
            result = self.wait_for(complete)
        self.assertTrue(result['ok'], result)
        return result

    def test_multi_controller_multi_hud(self):
        r1, r2, h1, h2 = self.nodes
        for hud, rotor in ((h1, r1), (h1, r2), (h2, r1)):
            self.request(hud, '/api/hud/discover', {'address': '127.0.0.1', 'port': rotor['ports']['hud']})
            self.wait_for(lambda: any(p['id'] == rotor['id'] for p in self.request(hud, '/api/hud/peers?discovered=1')[1]['items']))
            self.command(hud, '/api/hud/peers', {'id': rotor['id']})
        self.wait_for(lambda: self.request(h1, '/api/status')[1]['hudOnline'])
        self.command(h1, '/api/mode', {'mode': 'auto'})
        self.command(h1, '/api/target', {'az': '140'})
        self.wait_for(lambda: self.request(h2, '/api/status')[1]['targetAz'] == 140)
        self.assertEqual(self.request(r1, '/api/status')[1]['targetAz'], 140)
        # Programs can also enter directly at the motor controller.
        with socket.create_connection(('127.0.0.1', r1['ports']['tcp']), timeout=3) as client:
            client.sendall(b'P 170 0\n')
            self.assertEqual(client.recv(100), b'RPRT 0\n')
        self.wait_for(lambda: self.request(h1, '/api/status')[1]['targetAz'] == 170)
        # A TCP program connected to HUD waits for the remote acknowledgement.
        client = socket.create_connection(('127.0.0.1', h1['ports']['tcp']), timeout=3)
        client.sendall(b'P 190 0\n')
        self.assertEqual(client.recv(100), b'RPRT 0\n')
        self.assertEqual(self.request(r1, '/api/status')[1]['targetAz'], 190)
        # Delay the original controller's ACK across selection. Its accepted
        # command and reply keep their original destination and TCP session.
        os.kill(r1['process'].pid, signal.SIGSTOP)
        try:
            client.sendall(b'P 210 0\n')
            time.sleep(.08)
            self.command(h1, '/api/hud/select', {'id': r2['id']})
        finally:
            os.kill(r1['process'].pid, signal.SIGCONT)
        self.assertEqual(client.recv(100), b'RPRT 0\n')
        self.wait_for(lambda: self.request(r1, '/api/status')[1]['targetAz'] == 210)
        self.assertFalse(self.request(r2, '/api/status')[1]['moving'])
        self.command(h1, '/api/hud/select', {'id': r1['id']})
        client.sendall(b'P 190 0\n')
        self.assertEqual(client.recv(100), b'RPRT 0\n')
        # Keep this exact connection. A fragment whose suffix is itself a valid
        # jog must not move the new rotor after selection.
        client.sendall(b'M')
        time.sleep(.05)
        self.command(h1, '/api/hud/select', {'id': r2['id']})
        client.sendall(b'R\n')
        self.wait_for(lambda: self.request(h1, '/api/status')[1]['hudOnline'])
        self.assertFalse(self.request(r2, '/api/status')[1]['moving'])
        self.command(h1, '/api/mode', {'mode': 'auto'})
        client.sendall(b'P 230 0\n')
        self.assertEqual(client.recv(100), b'RPRT 0\n')
        self.assertEqual(self.request(r2, '/api/status')[1]['targetAz'], 230)
        self.assertEqual(self.request(r1, '/api/status')[1]['targetAz'], 190)
        # A completed line leaves no cancelled fragment. The next command sent
        # immediately after HTTP selection must survive on the same socket.
        for rotor, target in ((r1, 210), (r2, 250), (r1, 190), (r2, 230)):
            self.command(h1, '/api/hud/select', {'id': rotor['id']})
            client.sendall(f'P {target} 0\n'.encode())
            self.assertEqual(client.recv(100), b'RPRT 0\n')
            self.assertEqual(self.request(rotor, '/api/status')[1]['targetAz'], target)
        client.close()
        # The logger keeps the HUD's name while the selected drive changes.
        self.assertEqual(self.request(h1, '/api/status')[1]['rotorName'], 'R201')
        with socket.socket(type=socket.SOCK_DGRAM) as logger:
            logger.sendto(b'<N1MMRotor><rotor>R201</rotor><goazi>240</goazi></N1MMRotor>', ('127.0.0.1',h1['ports']['n1mm']))
        self.wait_for(lambda: self.request(r2, '/api/status')[1]['targetAz'] == 240)
        self.assertEqual(self.request(r1, '/api/status')[1]['targetAz'], 190)
        # Both saved rotators continue to receive background state updates.
        items = self.request(h1, '/api/hud/peers')[1]['items']
        self.assertEqual(len(items), 2)
        self.wait_for(lambda: all(p['ageMs'] is not None and p['ageMs'] < 1500 for p in self.request(h1, '/api/hud/peers')[1]['items']))
        self.command(h2, '/api/move', {'d': 'S'})
        self.assertFalse(self.request(r1, '/api/status')[1]['moving'])
        self.assertTrue(self.request(r2, '/api/status')[1]['moving'])
        # Lost HUD jog lease stops the original motor, while a numeric job elsewhere survives.
        self.command(h2, '/api/move', {'d': 'R'})
        h2['process'].terminate()
        h2['process'].wait(timeout=5)
        self.wait_for(lambda: not self.request(r1, '/api/status')[1]['moving'], timeout=3)
        self.assertTrue(self.request(r2, '/api/status')[1]['moving'])


if __name__ == '__main__':
    unittest.main()
