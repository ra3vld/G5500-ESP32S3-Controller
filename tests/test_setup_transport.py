"""Commissioning/network API through the production paginated command transport."""
import unittest
from test_command_api import Bridge, api


class SetupTransportTests(unittest.TestCase):
    def test_network_profiles_and_commissioning_boundary(self):
        for kind in ('serial', 'tcp', 'udp'):
            with self.subTest(transport=kind):
                bridge = Bridge(kind)
                try:
                    link = api.SerialTransport(bridge.address, 9600) if kind == 'serial' else api.SocketTransport(bridge.address, kind == 'udp', 1)
                    try:
                        client = api.Client(link, timeout=1)
                        setup = client.request('GET', '/api/setup')
                        self.assertEqual(setup['status'], 200)
                        self.assertFalse(setup['body']['required'])
                        self.assertEqual(client.request('POST', '/api/setup', {'action': 'start'})['status'], 405)
                        saved = client.request('POST', '/api/network', {
                            'wifiMode': 'static', 'wifiAddress': '192.168.9.20',
                            'wifiMask': '255.255.255.0', 'wifiGateway': '192.168.9.1',
                            'ethMode': 'dhcp',
                        })
                        self.assertEqual(saved['status'], 200)
                        profiles = client.request('GET', '/api/network')['body']
                        self.assertEqual(profiles['wifi']['address'], '192.168.9.20')
                        self.assertEqual(profiles['wifi']['mode'], 'static')
                        self.assertEqual(profiles['eth']['mode'], 'dhcp')
                        self.assertEqual(client.request('POST', '/api/network', {'wifiGateway': '10.0.0.1'})['status'], 400)
                        self.assertEqual(client.request('GET', '/api/network')['body']['wifi']['gateway'], '192.168.9.1')
                        self.assertTrue(all(len(reply) <= 1200 for reply in bridge.responses))
                    finally:
                        link.close()
                finally:
                    bridge.close()


if __name__ == '__main__':
    unittest.main()
