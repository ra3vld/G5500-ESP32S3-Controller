#!/usr/bin/env python3
"""Run one named autotune hypothesis over the command API and record its result."""
import argparse
import json
import math
from pathlib import Path
import sys
import time

from controller_api import ApiError, Client, SerialTransport, SocketTransport


OPTION_LIMITS = {
    'tuneSearchStartPercent': (1, 100, False),
    'tuneSearchStepPercent': (1, 20, False),
    'tuneWorkingFraction': (.1, 1, False),
    'tuneLambdaFactor': (.5, 4, False),
    'tunePositionGainFactor': (.25, 2, False),
    'tuneStartupObservationMs': (100, 2000, True),
    'tunePlateauMinMs': (160, 1000, True),
    'tunePlateauTimeoutMs': (1000, 10000, True),
}


def hypothesis(path, name):
    """Reject misspelled/invalid experimental fields before opening a device."""
    document = json.loads(Path(path).read_text(encoding='utf-8'))
    value = document[name]
    if not isinstance(value, dict):
        raise ValueError('hypothesis must be an object')
    required = {'axis', 'mode', 'travel', 'outputLimit'}
    if not required <= value.keys() or value.keys() - required - OPTION_LIMITS.keys():
        raise ValueError('expected axis, mode, travel, outputLimit and documented tune* options')
    if value['axis'] not in ('az', 'el') or value['mode'] not in ('simple', 'cascade'):
        raise ValueError('invalid axis/mode')
    for key in value.keys() - {'axis', 'mode'}:
        number = value[key]
        if isinstance(number, bool) or not isinstance(number, (int, float)) or not math.isfinite(number):
            raise ValueError('invalid numeric value: ' + key)
        low, high, integer = OPTION_LIMITS.get(key, (10, 90, False) if key == 'travel' else (10, 100, False))
        if not low <= number <= high or (integer and number != int(number)):
            raise ValueError('value out of range: ' + key)
        if integer:
            value[key] = int(number)
    if value.get('tuneSearchStartPercent', 5) > value['outputLimit']:
        raise ValueError('search start exceeds output limit')
    if value.get('tunePlateauTimeoutMs', 2500) < 2 * value.get('tunePlateauMinMs', 200) + 200:
        raise ValueError('plateau timeout is too short')
    return value


def body(client, method, path, arguments=None):
    response = client.request(method, path, arguments)
    if response['status'] >= 400:
        raise ApiError(str(response))
    return response['body']


def experiment(client, variant, emit, interval=1, duration=180, clock=time.monotonic, sleep=time.sleep):
    """Never apply a proposal; cancel our active trial on interruption or timeout."""
    started, identifier = clock(), None
    def record(event, **fields):
        emit(dict(event=event, elapsed=round(clock() - started, 3), **fields))
    initial = body(client, 'GET', '/api/motion/autotune', {'full': 1})
    if initial.get('active'):
        raise ApiError('autotune already active; cancel it explicitly before starting another trial')
    method_version = initial.get('options', {}).get('methodVersion')
    if method_version != 1:
        raise ApiError('firmware does not support this experiment methodology; update it before running trials')
    record('configuration', hypothesis=variant, previousId=initial['id'], boot=client.boot,
           methodVersion=method_version)
    try:
        # Keep the expected ID before the write: a lost start reply may still
        # mean the controller accepted it. Identical retries use the same cid.
        identifier = (initial['id'] + 1) & 0xFFFFFFFF or 1
        response = client.request('POST', '/api/motion/autotune', dict(action='start', id=initial['id'], **variant))
        if response['status'] >= 400:
            identifier = None  # A rejected write never grants ownership of a trial.
            raise ApiError(str(response))
        while True:
            state = body(client, 'GET', '/api/motion/autotune')
            if state['id'] != identifier:
                raise ApiError('autotune ID changed; no further commands sent to the replacement trial')
            if not state['active']:
                result = body(client, 'GET', '/api/motion/autotune', {'full': 1})
                if result['id'] != identifier:
                    raise ApiError('autotune ID changed while reading result')
                record('result', autotune=result)
                return result
            axis = body(client, 'GET', '/api/status', {'view': 'axis', 'axis': variant['axis']})
            record('sample', autotune=state, axis=axis)
            if clock() - started >= duration:
                raise TimeoutError('experiment time limit exceeded')
            sleep(interval)
    except BaseException:
        if identifier is not None:
            try:
                state = body(client, 'GET', '/api/motion/autotune')
                if state['id'] == identifier and state['active']:
                    body(client, 'POST', '/api/motion/autotune', {'action': 'cancel', 'id': identifier})
                    record('cancelled', id=identifier)
            except (ApiError, OSError, ValueError) as error:
                record('cancel_failed', error=str(error))
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    links = parser.add_mutually_exclusive_group()
    links.add_argument('--serial', metavar='PORT')
    links.add_argument('--udp', metavar='HOST[:PORT]')
    links.add_argument('--tcp', metavar='HOST[:PORT]')
    parser.add_argument('--baud', type=int, default=9600)
    parser.add_argument('--interval', type=float, default=1)
    parser.add_argument('--duration', type=float, default=180)
    parser.add_argument('--timeout', type=float, default=5)
    parser.add_argument('--log', help='New JSONL file for configuration, samples and result')
    parser.add_argument('--dry-run', action='store_true', help='Validate and print; do not open a device')
    parser.add_argument('config', help='JSON object of named hypotheses')
    parser.add_argument('name')
    args = parser.parse_args(argv)
    if any(not math.isfinite(n) or n <= 0 for n in (args.interval, args.duration, args.timeout)):
        parser.error('interval, duration and timeout must be finite and positive')
    link = None
    try:
        variant = hypothesis(args.config, args.name)
        if args.dry_run:
            print(json.dumps(variant, ensure_ascii=False, indent=2))
            return 0
        if not (args.serial or args.udp or args.tcp) or not args.log:
            parser.error('select a transport and --log, or use --dry-run')
        # Exclusive creation prevents accidentally mixing runs or erasing evidence.
        with open(args.log, 'x', encoding='utf-8', buffering=1) as log:
            link = SerialTransport(args.serial, args.baud) if args.serial else SocketTransport(args.udp or args.tcp, bool(args.udp), args.timeout)
            client = Client(link, timeout=args.timeout)
            def emit(value):
                line = json.dumps(dict(time=time.time(), name=args.name, **value), ensure_ascii=False)
                log.write(line + '\n')
                print(line, flush=True)
            result = experiment(client, variant, emit, args.interval, args.duration)
            return 0 if result['phase'] == 'complete' else 2
    except (ApiError, OSError, ValueError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    finally:
        if link:
            link.close()


if __name__ == '__main__':
    raise SystemExit(main())
