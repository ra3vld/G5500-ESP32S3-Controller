"""Host trial orchestration, including lost replies and ownership changes."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('autotune_experiment', ROOT / 'tools/autotune_experiment.py')
trial = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trial)

VARIANT = dict(axis='az', mode='cascade', travel=60, outputLimit=50, tuneLambdaFactor=2)


class Client:
    boot = 'test-boot'

    def __init__(self, states, fail_axis=False):
        self.states = list(states)
        self.last = None
        self.calls = []
        self.fail_axis = fail_axis

    def request(self, method, path, arguments=None):
        self.calls.append((method, path, arguments))
        if method == 'POST':
            return dict(status=200, body={'ok': True})
        if path == '/api/status':
            if self.fail_axis:
                raise trial.ApiError('lost feedback reply')
            return dict(status=200, body=dict(current=90, measuredVelocity=3))
        if self.states:
            self.last = self.states.pop(0)
        return dict(status=200, body=self.last)


def state(identifier=11, active=True, phase='search'):
    return dict(id=identifier, active=active, phase=phase, options={'methodVersion': 1})


class TrialTests(unittest.TestCase):
    def test_minimum_speed_method_version(self):
        initial = state(10, False)
        initial['options']['methodVersion'] = 2
        records = []
        trial.experiment(Client([initial, state(active=False, phase='complete')]),
                         VARIANT, records.append, sleep=lambda _: None)
        self.assertEqual(records[0]['methodVersion'], 2)

    def test_result_and_effective_options_logged_without_apply(self):
        client = Client([state(10, False), state(), state(active=False, phase='complete')])
        records = []
        result = trial.experiment(client, VARIANT, records.append, sleep=lambda _: None)
        self.assertEqual(result['phase'], 'complete')
        self.assertEqual([r['event'] for r in records], ['configuration', 'sample', 'result'])
        self.assertEqual(records[0]['hypothesis'], VARIANT)
        posts = [c for c in client.calls if c[0] == 'POST']
        self.assertEqual(len(posts), 1)
        self.assertEqual(posts[0][2], dict(action='start', id=10, **VARIANT))

    def test_busy_does_not_send_any_post(self):
        client = Client([state()])
        with self.assertRaises(trial.ApiError):
            trial.experiment(client, VARIANT, lambda _: None)
        self.assertFalse(any(c[0] == 'POST' for c in client.calls))

    def test_old_firmware_never_runs_silently_with_default_method(self):
        old = state(10, False)
        del old['options']['methodVersion']
        client = Client([old])
        with self.assertRaisesRegex(trial.ApiError, 'firmware does not support'):
            trial.experiment(client, VARIANT, lambda _: None)
        self.assertFalse(any(c[0] == 'POST' for c in client.calls))

    def test_read_failure_cancels_own_trial(self):
        client = Client([state(10, False), state(), state()], fail_axis=True)
        records = []
        with self.assertRaises(trial.ApiError):
            trial.experiment(client, VARIANT, records.append)
        self.assertEqual(client.calls[-1][2], {'action': 'cancel', 'id': 11})
        self.assertEqual(records[-1]['event'], 'cancelled')

    def test_timeout_cancels(self):
        client = Client([state(10, False), state(), state()])
        clock = iter([0, 0, 2, 2, 2])
        with self.assertRaises(TimeoutError):
            trial.experiment(client, VARIANT, lambda _: None, duration=1, clock=lambda: next(clock))
        self.assertEqual(client.calls[-1][2], {'action': 'cancel', 'id': 11})

    def test_never_cancels_replacement_trial(self):
        client = Client([state(10, False), state(12), state(12)])
        with self.assertRaises(trial.ApiError):
            trial.experiment(client, VARIANT, lambda _: None)
        self.assertEqual(len([c for c in client.calls if c[0] == 'POST']), 1)

    def test_failed_tune_is_recorded_without_apply(self):
        client = Client([state(10, False), state(active=False, phase='fault')])
        records = []
        self.assertEqual(trial.experiment(client, VARIANT, records.append)['phase'], 'fault')
        self.assertEqual(records[-1]['event'], 'result')
        self.assertFalse(any(c[2] and c[2].get('action') == 'apply' for c in client.calls))

    def test_validate_options_before_opening_device(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'variants.json'
            def load(value):
                path.write_text(json.dumps({'test': value}))
                return trial.hypothesis(path, 'test')
            self.assertEqual(load(VARIANT), VARIANT)
            for change in ({'tuneLambdaFactor': .1}, {'tuneTypo': 2}, {'travel': float('nan')},
                           {'tunePlateauMinMs': 1000, 'tunePlateauTimeoutMs': 1000},
                           {'outputLimit': 10, 'tuneSearchStartPercent': 15},
                           {'tunePlateauMinMs': 200.5}, {'travel': True}):
                with self.subTest(change=change), self.assertRaises(ValueError):
                    load(dict(VARIANT, **change))


if __name__ == '__main__':
    unittest.main()
