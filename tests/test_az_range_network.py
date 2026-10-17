"""Custom ranges through real local UDP HUD/3 and HTTP, with virtual motors."""
import unittest
import urllib.error
import test_hud_network as network


class AzRangeNetworkTests(unittest.TestCase):
    def setUp(self):
        network.HudNetworkTests.setUpClass()

    def tearDown(self):
        network.HudNetworkTests.tearDownClass()

    def test_ranges_selection_and_stale_revision(self):
        helper = network.HudNetworkTests()
        first, second, hud, _ = helper.nodes
        helper.request(first, '/api/azrange', {'profile': 'custom', 'start': -60, 'span': 450})
        helper.request(second, '/api/azrange', {'profile': 'custom', 'start': 50, 'span': 450})
        for rotor in (first, second):
            helper.request(hud, '/api/hud/discover', {'address': '127.0.0.1', 'port': rotor['ports']['hud']})
            helper.wait_for(lambda: any(p['id'] == rotor['id'] for p in helper.request(hud, '/api/hud/peers?discovered=1')[1]['items']))
            helper.command(hud, '/api/hud/peers', {'id': rotor['id']})
        helper.command(hud, '/api/hud/select', {'id': first['id']})
        helper.wait_for(lambda: helper.request(hud, '/api/status')[1]['azStart'] == -60)
        helper.command(hud, '/api/target', {'az': -50})
        self.assertEqual(helper.request(first, '/api/status')[1]['targetAz'], -50)
        old = helper.request(hud, '/api/status')[1]
        helper.command(hud, '/api/hud/select', {'id': second['id']})
        helper.wait_for(lambda: helper.request(hud, '/api/status')[1]['azStart'] == 50)
        with self.assertRaises(urllib.error.HTTPError) as response:
            helper.request(hud, '/api/target', {'az': 100, 'selection': old['hudSelectionEpoch']})
        self.assertEqual(response.exception.code, 409)
        helper.command(hud, '/api/target', {'az': 500})
        self.assertEqual(helper.request(second, '/api/status')[1]['targetAz'], 500)
        previous = helper.request(hud, '/api/status')[1]
        helper.request(second, '/api/azrange', {'profile': 'custom', 'start': 300, 'span': 90})
        helper.wait_for(lambda: helper.request(hud, '/api/status')[1]['azStart'] == 300)
        after = helper.request(hud, '/api/status')[1]
        self.assertEqual(after['azMax'], 390)
        self.assertFalse(after['targetValid'])
        with self.assertRaises(urllib.error.HTTPError) as response:
            helper.request(hud, '/api/target', {'az': 350, 'rangeRevision': previous['azRangeRevision']})
        self.assertEqual(response.exception.code, 409)
        helper.command(hud, '/api/move', {'d': 'S', 'rangeRevision': previous['azRangeRevision']})
        self.assertFalse(helper.request(second, '/api/status')[1]['moving'])

    def test_variable_elevation_and_legacy_model(self):
        helper = network.HudNetworkTests()
        first, second, hud, _ = helper.nodes
        for rotor in (first, second):
            helper.request(rotor, '/api/settings', {'model': 'G5500'})
            helper.request(rotor, '/api/azrange', {'profile': 'p45'})
        helper.request(first, '/api/elrange', {'profile': 'custom', 'start': -30, 'span': 240})
        helper.request(second, '/api/elrange', {'profile': 'custom', 'start': -180, 'span': 90})
        for rotor in (first, second):
            helper.request(hud, '/api/hud/discover', {'address': '127.0.0.1', 'port': rotor['ports']['hud']})
            helper.wait_for(lambda: any(p['id'] == rotor['id'] for p in helper.request(hud, '/api/hud/peers?discovered=1')[1]['items']))
            helper.command(hud, '/api/hud/peers', {'id': rotor['id']})
        helper.command(hud, '/api/hud/select', {'id': first['id']})
        helper.wait_for(lambda: helper.request(hud, '/api/status')[1].get('elStart') == -30)
        helper.command(hud, '/api/target', {'el': -20})
        self.assertEqual(helper.request(first, '/api/status')[1]['targetEl'], -20)
        previous = helper.request(hud, '/api/status')[1]
        helper.request(first, '/api/elrange', {'profile': 'custom', 'start': 20, 'span': 240})
        helper.wait_for(lambda: helper.request(hud, '/api/status')[1].get('elStart') == 20)
        with self.assertRaises(urllib.error.HTTPError) as response:
            helper.request(hud, '/api/target', {'el': 100, 'rangeRevision': previous['elRangeRevision']})
        self.assertEqual(response.exception.code, 409)
        helper.command(hud, '/api/move', {'d': 'S', 'rangeRevision': previous['elRangeRevision']})
        helper.command(hud, '/api/hud/select', {'id': second['id']})
        helper.wait_for(lambda: helper.request(hud, '/api/status')[1].get('elStart') == -180)
        helper.request(second, '/api/settings', {'model': 'G800DXA'})
        helper.request(second, '/api/target', {'az': 100, 'el': 0})
        helper.request(second, '/api/settings', {'model': 'G5500'})
        self.assertEqual(helper.request(second, '/api/status')[1]['elStart'], -180)


if __name__ == '__main__':
    unittest.main()
