"""Command generation, interruption, real simulator sockets and serial framing."""
from contextlib import redirect_stderr, redirect_stdout
from decimal import Decimal
import io
from itertools import islice
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import threading
import time
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import control_generator as gen


def config(*arguments):
    return gen.validate(gen.parser().parse_args(list(arguments)))


class FakeSender:
    def __init__(self, fail=False, stop_fail=False):
        self.commands = []
        self.reads = 0
        self.opened = self.closed = False
        self.fail, self.stop_fail = fail, stop_fail

    def open(self):
        self.opened = True

    def close(self):
        self.closed = True

    def position(self):
        self.reads += 1
        return {'az': Decimal(self.reads), 'el': Decimal(10 + self.reads)}

    def send(self, command, stopping=False):
        self.commands.append((command, stopping))
        if stopping and self.stop_fail:
            raise OSError('stop failed')
        if not stopping and self.fail:
            raise TimeoutError('lost target reply')


def quiet(*args, **kwargs):
    pass


class GeneratorTests(unittest.TestCase):
    def sequence(self, low, high, step, count):
        return list(islice(gen.sweep(*map(Decimal, (low, high, step))), count))

    def test_fractional_signed_and_short_last_step(self):
        self.assertEqual(self.sequence('-.2', '.15', '.1', 9),
                         list(map(Decimal, ['-.2', '-.1', '0', '.1', '.15', '.05', '-.05', '-.15', '-.2'])))

    def test_large_step_and_fixed_axis(self):
        self.assertEqual(self.sequence('1', '2', '5', 5), [1, 2, 1, 2, 1])
        self.assertEqual(self.sequence('1', '1', '5', 3), [1, 1, 1])

    def test_two_independent_axes(self):
        args = config('--protocol', 'http', '--axes', 'both', '--az-min', '0', '--az-max', '2',
                      '--az-step', '1', '--el-min', '-1', '--el-max', '2', '--el-step', '3', '--count', '5')
        self.assertEqual(list(gen.targets(args)), [dict(az=0, el=-1), dict(az=1, el=2), dict(az=2, el=-1),
                                                  dict(az=1, el=2), dict(az=0, el=-1)])

    def test_formats(self):
        expected = {
            'gs232': 'W12.5 -2', 'rotctld': 'P 12.5 -2',
            'pst': '<PST><LP>0</LP><AZIMUTH>12.5</AZIMUTH><ELEVATION>-2</ELEVATION></PST>',
            'http': ('/api/target', {'az': '12.5', 'el': '-2'}),
            'api': ('/api/target', {'az': '12.5', 'el': '-2'}),
        }
        for protocol, command in expected.items():
            args = config('--protocol', protocol, '--axes', 'both', '--mode', 'single', '--az', '12.5', '--el', '-2')
            self.assertEqual(gen.wire_target(args, next(gen.targets(args))), command)
        args = config('--protocol', 'n1mm', '--mode', 'single', '--az', '12.5', '--rotor-name', 'TEST')
        self.assertEqual(gen.wire_target(args, {'az': Decimal('12.5')}),
                         '<N1MMRotor><rotor>TEST</rotor><goazi>12.5</goazi></N1MMRotor>')
        self.assertEqual(gen.wire_stop(args), '<N1MMRotor><stop><rotor>TEST</rotor></stop></N1MMRotor>')

    def test_reject_before_open(self):
        cases = [
            ['--protocol', 'n1mm', '--axes', 'el'],
            ['--protocol', 'pst', '--transport', 'tcp'],
            ['--protocol', 'http', '--interval', 'nan'],
            ['--protocol', 'http', '--timeout', 'inf'],
            ['--protocol', 'http', '--count', '0'],
            ['--protocol', 'http', '--port', '0'],
            ['--protocol', 'api', '--transport', 'serial'],
            ['--protocol', 'http', '--host', 'http://localhost'],
            ['--protocol', 'n1mm', '--rotor-name', 'A&B'],
            ['--protocol', 'gs232', '--axes', 'el', '--mode', 'single', '--el', '1'],
        ]
        for fields in cases:
            with self.subTest(fields=fields), patch.object(gen.Sender, 'open') as opened:
                with self.assertRaises(ValueError):
                    config(*fields)
                opened.assert_not_called()
        for low, high, step in [('nan', '2', '1'), ('2', '1', '1'), ('0', '2', '0'), ('0', '2', '-1'), ('0', '2', 'inf')]:
            with self.assertRaises(ValueError):
                config('--protocol', 'http', '--az-min', low, '--az-max', high, '--az-step', step)

    def test_preview_never_opens_and_defaults_to_twenty_without_sleep(self):
        args = config('--protocol', 'gs232', '--axes', 'el', '--el-min', '0', '--el-max', '10',
                      '--el-step', '2', '--hold-az', '23')
        sender = FakeSender()
        lines = []
        gen.run(args, sender, emit=lambda line, **_: lines.append(line), sleep=lambda _: self.fail('preview slept'))
        self.assertEqual(len(lines), 20)
        self.assertEqual(lines[0], '1: W23 0')
        self.assertFalse(sender.opened)
        self.assertEqual(sender.reads, 0)
        self.assertFalse(sender.commands)

    def test_all_protocol_previews_avoid_every_transport(self):
        for protocol in gen.PROTOCOLS:
            with self.subTest(protocol=protocol):
                args = config('--protocol', protocol, '--mode', 'single', '--az', '10', '--hold-el', '20')
                with patch.object(gen, 'SocketTransport', side_effect=AssertionError('opened socket')), \
                        patch.object(gen, 'SerialTransport', side_effect=AssertionError('opened serial')), \
                        patch.object(gen, 'urlopen', side_effect=AssertionError('opened HTTP')):
                    gen.run(args, emit=quiet)

    def test_unused_axis_read_for_every_target_and_stop_after_last_pause(self):
        args = config('--protocol', 'rotctld', '--send', '--az-min', '0', '--az-max', '10',
                      '--az-step', '5', '--count', '2', '--interval', '.2')
        sender = FakeSender()
        pauses = []
        gen.run(args, sender, quiet, sleep=lambda duration: pauses.append((duration, len(sender.commands))))
        self.assertEqual(sender.commands, [('P 0 11', False), ('P 5 12', False), ('stop', True)])
        self.assertEqual(sender.reads, 2)
        self.assertEqual(pauses, [(.2, 1), (.2, 2)])
        self.assertTrue(sender.closed)

    def test_single_success_has_no_stop(self):
        args = config('--protocol', 'gs232', '--send', '--mode', 'single', '--az', '20')
        sender = FakeSender()
        gen.run(args, sender, quiet)
        self.assertEqual(sender.commands, [('M20', False)])
        self.assertTrue(sender.closed)

    def test_lost_single_reply_stops_without_repeating_target(self):
        args = config('--protocol', 'gs232', '--send', '--mode', 'single', '--az', '20')
        sender = FakeSender(fail=True)
        with self.assertRaisesRegex(TimeoutError, 'lost target'):
            gen.run(args, sender, quiet)
        self.assertEqual(sender.commands, [('M20', False), ('S', True)])
        self.assertTrue(sender.closed)

    def test_keyboard_interrupt_stops(self):
        args = config('--protocol', 'pst', '--send', '--az-min', '0', '--az-max', '10', '--az-step', '1')
        sender = FakeSender()
        def interrupt(_):
            raise KeyboardInterrupt
        with self.assertRaises(KeyboardInterrupt):
            gen.run(args, sender, quiet, sleep=interrupt)
        self.assertEqual(sender.commands[-1], ('<PST><STOP>1</STOP></PST>', True))
        self.assertTrue(sender.closed)

    def test_stop_failure_reports_and_preserves_original_failure(self):
        args = config('--protocol', 'gs232', '--send', '--mode', 'single', '--az', '20')
        sender = FakeSender(fail=True, stop_fail=True)
        output = io.StringIO()
        with redirect_stderr(output), self.assertRaisesRegex(TimeoutError, 'lost target'):
            gen.run(args, sender, quiet)
        self.assertIn('STOP: stop failed', output.getvalue())
        self.assertTrue(sender.closed)

    def test_failed_stop_on_normal_completion_is_failure(self):
        args = config('--protocol', 'gs232', '--send', '--az-min', '0', '--az-max', '1', '--az-step', '1', '--count', '1')
        with redirect_stderr(io.StringIO()), self.assertRaisesRegex(OSError, 'stop failed'):
            gen.run(args, FakeSender(stop_fail=True), quiet, sleep=quiet)

    def test_broken_stdout_cannot_prevent_stop(self):
        args = config('--protocol', 'gs232', '--send', '--az-min', '0', '--az-max', '1', '--az-step', '1', '--count', '2')
        sender = FakeSender()
        def broken_output(line, **kwargs):
            if not line.startswith('1:'):
                raise BrokenPipeError('closed stdout')
        with self.assertRaises(BrokenPipeError):
            gen.run(args, sender, broken_output, sleep=quiet)
        self.assertEqual(sender.commands, [('M0', False), ('S', True)])
        self.assertTrue(sender.closed)

    def test_open_failure_closes_without_sending(self):
        args = config('--protocol', 'http', '--send', '--mode', 'single', '--az', '1')
        sender = FakeSender()
        sender.open = lambda: (_ for _ in ()).throw(OSError('connect failed'))
        with self.assertRaisesRegex(OSError, 'connect failed'):
            gen.run(args, sender, quiet)
        self.assertEqual(sender.commands, [])
        self.assertTrue(sender.closed)

    def test_menu_and_equivalent_cli(self):
        answers = ['0', '7', 'pst', '4', '3', '', '', '', '', '',
                   '-10', '10', '5', '0', '90', '30', '.5', '3']
        output = io.StringIO()
        with patch('builtins.input', side_effect=answers), redirect_stdout(output):
            self.assertEqual(gen.main([]), 0)
        self.assertIn('4 — PstRotator UDP', output.getvalue())
        self.assertIn('3 — AZ+EL', output.getvalue())
        self.assertEqual(output.getvalue().count('Введите номер от 1 до 6.'), 3)
        self.assertIn('1: <PST><LP>0</LP><AZIMUTH>-10</AZIMUTH><ELEVATION>0</ELEVATION></PST>', output.getvalue())
        self.assertIn('3: <PST><LP>0</LP><AZIMUTH>0</AZIMUTH><ELEVATION>60</ELEVATION></PST>', output.getvalue())


def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as connection:
        connection.bind(('127.0.0.1', 0))
        return connection.getsockname()[1]


class SimulatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.http, cls.live, cls.tcp = (free_port() for _ in range(3))
        cls.n1, cls.pst, cls.report = (free_port(socket.SOCK_DGRAM) for _ in range(3))
        cls.process = subprocess.Popen([sys.executable, str(ROOT / 'tools/simulator.py'), '--http-port', str(cls.http),
                                       '--live-port', str(cls.live), '--tcp-port', str(cls.tcp), '--n1mm-port', str(cls.n1),
                                       '--pst-port', str(cls.pst), '--report-port', str(cls.report)],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        cls.addClassCleanup(cls.cleanup)
        for _ in range(100):
            try:
                cls.state()
                break
            except OSError:
                if cls.process.poll() is not None:
                    raise RuntimeError(cls.process.stderr.read())
                time.sleep(.03)
        else:
            raise RuntimeError('simulator startup timeout')

    @classmethod
    def cleanup(cls):
        cls.process.terminate()
        cls.process.communicate(timeout=5)

    @classmethod
    def state(cls):
        with gen.urlopen('http://127.0.0.1:{}/api/status'.format(cls.http), timeout=2) as response:
            return json.load(response)

    def setUp(self):
        args = config('--protocol', 'http', '--host', '127.0.0.1', '--port', str(self.http),
                      '--mode', 'single', '--az', '0')
        self.http_sender = gen.Sender(args)
        self.http_sender.request('/api/move', {'d': 'S'})
        self.http_sender.request('/api/mode', {'mode': 'auto'})
        self.http_sender.request('/api/settings', {'externalAzMode': 'mechanical'})

    def live_args(self, protocol, *extra):
        port = {'http': self.http, 'gs232': self.tcp, 'rotctld': self.tcp, 'n1mm': self.n1, 'pst': self.pst}[protocol]
        return config('--protocol', protocol, '--host', '127.0.0.1', '--port', str(port), '--send', *extra)

    def test_network_targets_all_simulator_protocols(self):
        for protocol in ('http', 'gs232', 'rotctld', 'n1mm', 'pst'):
            with self.subTest(protocol=protocol):
                gen.run(self.live_args(protocol, '--axes', 'az', '--mode', 'single', '--az', '37.5'), emit=quiet)
                for _ in range(100):
                    if abs(self.state()['targetAz'] - 37.5) < .01:
                        break
                    time.sleep(.01)
                self.assertAlmostEqual(self.state()['targetAz'], 37.5)
                self.http_sender.request('/api/move', {'d': 'S'})

    def test_el_only_and_both_axes(self):
        for protocol in ('http', 'gs232', 'rotctld', 'pst'):
            for axes in ('el', 'both'):
                with self.subTest(protocol=protocol, axes=axes):
                    gen.run(self.live_args(protocol, '--mode', 'single', '--axes', axes, '--az', '25', '--el', '12.5'), emit=quiet)
                    time.sleep(.03)
                    self.assertAlmostEqual(self.state()['targetEl'], 12.5)
                    if axes == 'both':
                        self.assertAlmostEqual(self.state()['targetAz'], 25)
                    self.http_sender.request('/api/move', {'d': 'S'})

    def test_network_sweeps_end_with_stop(self):
        for protocol in ('http', 'gs232', 'rotctld', 'n1mm', 'pst'):
            with self.subTest(protocol=protocol):
                gen.run(self.live_args(protocol, '--az-min', '0', '--az-max', '30', '--az-step', '10',
                                       '--count', '3', '--interval', '.01'), emit=quiet)
                time.sleep(.04)
                state = self.state()
                self.assertEqual(state['directionAz'], 0)
                self.assertFalse(state['pendingValid'])

    def test_rejected_target_is_an_error(self):
        for protocol in ('http', 'gs232', 'rotctld'):
            with self.subTest(protocol=protocol), self.assertRaises((gen.ApiError, OSError)):
                gen.run(self.live_args(protocol, '--axes', 'both', '--mode', 'single', '--az', '900', '--el', '0'), emit=quiet)

    def test_pst_target_clears_cached_long_path(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
            connection.sendto(b'<PST><LP>1</LP><AZIMUTH>30</AZIMUTH></PST>', ('127.0.0.1', self.pst))
        time.sleep(.03)
        self.assertAlmostEqual(self.state()['targetAz'], 210)
        gen.run(self.live_args('pst', '--mode', 'single', '--az', '35'), emit=quiet)
        time.sleep(.03)
        self.assertAlmostEqual(self.state()['targetAz'], 35)


@unittest.skipUnless(os.name == 'posix', 'PTY requires POSIX')
class SerialTests(unittest.TestCase):
    def test_real_serial_transport_with_fragmented_gs232_stub(self):
        import pty
        master, slave = pty.openpty()
        stopped = threading.Event()
        received, errors = [], []
        def serve():
            try:
                buffer = bytearray()
                while not stopped.is_set():
                    if not select.select([master], [], [], .05)[0]:
                        continue
                    buffer.extend(os.read(master, 4096))
                    while b'\n' in buffer:
                        line, _, rest = buffer.partition(b'\n')
                        buffer[:] = rest
                        if not line.strip():
                            continue
                        received.append(bytes(line).strip())
                        if line.strip() == b'C2':
                            for fragment in (b'AZ=', b'024 EL=', b'012\r', b'\n'):
                                os.write(master, fragment)
            except BaseException as error:
                errors.append(error)
        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        try:
            args = config('--protocol', 'gs232', '--serial', os.ttyname(slave), '--send', '--axes', 'el',
                          '--el-min', '0', '--el-max', '2', '--el-step', '1', '--count', '2', '--interval', '.001')
            gen.run(args, emit=quiet)
            self.assertEqual(received, [b'C2', b'W24 0', b'C2', b'C2', b'W24 1', b'C2', b'S', b'C2'])
        finally:
            stopped.set()
            thread.join(timeout=2)
            os.close(master)
            os.close(slave)
        self.assertEqual(errors, [])


class CommandApiTests(unittest.TestCase):
    def test_api_tcp_udp_and_serial_use_existing_session_client(self):
        # Bridge runs the production DeviceApi/CommandServer against fake hardware.
        from test_command_api import Bridge
        for kind in ('tcp', 'udp', 'serial'):
            if kind == 'serial' and os.name != 'posix':
                continue
            with self.subTest(kind=kind):
                bridge = Bridge(kind)
                try:
                    link_args = ['--serial', bridge.address] if kind == 'serial' else [
                        '--transport', kind, '--host', '127.0.0.1', '--port', bridge.address.rsplit(':', 1)[1]]
                    args = config('--protocol', 'api', '--send', *link_args, '--axes', 'az',
                                  '--az-min', '0', '--az-max', '2', '--az-step', '1', '--count', '2', '--interval', '.001')
                    gen.run(args, emit=quiet)
                    fields = [parse_qs(data[1:].decode()) for data in bridge.requests]
                    paths = [item['path'][0] for item in fields]
                    self.assertEqual(paths, ['/api/hello', '/api/target', '/api/target', '/api/move'])
                    self.assertEqual(fields[-1]['d'], ['S'])
                    self.assertEqual(fields[2]['az'], ['1'])
                finally:
                    bridge.close()


if __name__ == '__main__':
    unittest.main()
