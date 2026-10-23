#!/usr/bin/env python3
"""Генерация и отправка целей поворотке: меню или параметры командной строки."""
import argparse
from decimal import Decimal, InvalidOperation, localcontext
import json
import math
import re
import sys
import time
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from controller_api import ApiError, Client, SerialTransport, SocketTransport


PROTOCOLS = {
    'gs232': 'GS-232', 'rotctld': 'rotctld / Hamlib', 'n1mm': 'N1MM UDP',
    'pst': 'PstRotator UDP', 'http': 'HTTP', 'api': 'Командный API',
}
TRANSPORTS = {
    'gs232': ('tcp', 'serial'), 'rotctld': ('tcp', 'serial'),
    'n1mm': ('udp',), 'pst': ('udp',), 'http': ('http',),
    'api': ('tcp', 'udp', 'serial'),
}
PORTS = {'gs232': 4533, 'rotctld': 4533, 'n1mm': 12040, 'pst': 1234, 'http': 80}


def number(value):
    try:
        result = Decimal(str(value))
    except InvalidOperation as error:
        raise ValueError('Ожидается число: ' + str(value)) from error
    if not result.is_finite():
        raise ValueError('Число должно быть конечным')
    # Bound formatting and arithmetic before accepting user-controlled exponents.
    if len(result.as_tuple().digits) > 60 or result.adjusted() > 60 or result.as_tuple().exponent < -60:
        raise ValueError('Слишком длинное числовое значение')
    return result


def text_number(value):
    text = format(value, 'f')
    return text.rstrip('0').rstrip('.') if '.' in text else text


def sweep(low, high, step):
    """Exact decimal steps, shortened at each endpoint; endpoints occur once."""
    current, direction = low, 1
    while True:
        yield current
        if low == high:
            continue
        with localcontext() as context:
            context.prec = 128
            current = min(high, current + step) if direction > 0 else max(low, current - step)
        if current == high:
            direction = -1
        elif current == low:
            direction = 1


def selected_axes(args):
    return ('az', 'el') if args.axes == 'both' else (args.axes,)


def needs_position(args):
    return (args.protocol == 'rotctld' and args.axes != 'both') or (args.protocol == 'gs232' and args.axes == 'el')


def targets(args):
    axes = selected_axes(args)
    if args.mode == 'single':
        yield {axis: getattr(args, axis) for axis in axes}
        return
    streams = {axis: sweep(getattr(args, axis + '_min'), getattr(args, axis + '_max'),
                           getattr(args, axis + '_step')) for axis in axes}
    index = 0
    while args.count is None or index < args.count:
        yield {axis: next(stream) for axis, stream in streams.items()}
        index += 1


def wire_target(args, coordinates):
    fields = {axis: text_number(value) for axis, value in coordinates.items()}
    if args.protocol == 'gs232':
        return 'W{az} {el}'.format(**fields) if 'el' in fields else 'M' + fields['az']
    if args.protocol == 'rotctld':
        return 'P {az} {el}'.format(**fields)
    if args.protocol == 'n1mm':
        return '<N1MMRotor><rotor>{}</rotor><goazi>{}</goazi></N1MMRotor>'.format(args.rotor_name, fields['az'])
    if args.protocol == 'pst':
        # Do not inherit an earlier client's cached long-path modifier.
        tags = ['<LP>0</LP>'] if 'az' in fields else []
        for axis, value in fields.items():
            tag = 'AZIMUTH' if axis == 'az' else 'ELEVATION'
            tags.append('<{0}>{1}</{0}>'.format(tag, value))
        return '<PST>' + ''.join(tags) + '</PST>'
    return ('/api/target', fields)


def wire_stop(args):
    return {'gs232': 'S', 'rotctld': 'stop',
            'n1mm': '<N1MMRotor><stop><rotor>{}</rotor></stop></N1MMRotor>'.format(args.rotor_name),
            'pst': '<PST><STOP>1</STOP></PST>'}.get(args.protocol, ('/api/move', {'d': 'S'}))


def describe(command):
    if isinstance(command, tuple):
        path, fields = command
        return 'POST ' + path + ' ' + urlencode(fields)
    return command


class Sender:
    def __init__(self, args):
        self.args = args
        self.link = None
        self.client = None

    def open(self):
        args = self.args
        if args.transport != 'http':
            self.link = (SerialTransport(args.serial, args.baud) if args.transport == 'serial' else
                         SocketTransport('{}:{}'.format(args.host, args.port), args.transport == 'udp', args.timeout))
        if args.protocol == 'api':
            self.client = Client(self.link, timeout=args.timeout)

    def close(self):
        if self.link:
            self.link.close()

    def request(self, path, fields):
        if self.client:
            response = self.client.request('POST', path, fields)
            if response['status'] >= 400:
                raise ApiError(str(response))
            body = response['body']
        else:
            host = self.args.host
            if ':' in host:
                host = '[' + host + ']'
            request = Request('http://{}:{}{}'.format(host, self.args.port, path),
                              urlencode(fields).encode('ascii'), method='POST')
            with urlopen(request, timeout=self.args.timeout) as response:
                body = json.load(response)
        if not isinstance(body, dict) or body.get('ok') is False:
            raise ApiError('Команда отклонена: ' + str(body))
        return body

    def line(self, deadline):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('Истекло время ожидания ответа')
        return self.link.receive(remaining).decode('ascii').strip()

    def gs_position(self, stopping=False):
        self.link.send(b'C2')
        deadline = time.monotonic() + self.args.timeout
        while True:
            line = self.line(deadline)
            match = re.fullmatch(r'AZ=([+-]?\d+) EL=([+-]?\d+)', line)
            if match:
                return dict(zip(('az', 'el'), map(number, match.groups())))
            if line == '?>' and not stopping:
                raise ApiError('GS-232: команда отклонена (?>)')
            if line.startswith('RPRT ') and line != 'RPRT 0' and not stopping:
                raise ApiError(line)
            # Ignore empty lines, boot text and the TCP STOP acknowledgement.

    def position(self):
        if self.args.protocol == 'gs232':
            return self.gs_position()
        self.link.send(b'p')
        deadline = time.monotonic() + self.args.timeout
        values = []
        while len(values) < 2:
            line = self.line(deadline)
            if not line:
                continue
            if line.startswith('RPRT '):
                raise ApiError('Не удалось прочитать положение: ' + line)
            values.append(number(line))
        return dict(zip(('az', 'el'), values))

    def send(self, command, stopping=False):
        if isinstance(command, tuple):
            return self.request(*command)
        self.link.send(command.encode('utf-8'))
        if self.args.protocol == 'gs232':
            # Successful GS-232 targets have no acknowledgement. C2 also drains
            # any queued rejection before accepting the next target.
            self.gs_position(stopping=stopping)
        elif self.args.protocol == 'rotctld':
            deadline = time.monotonic() + self.args.timeout
            while True:
                line = self.line(deadline)
                if not line:
                    continue
                if line != 'RPRT 0':
                    raise ApiError('rotctld: ' + line)
                break


def run(args, sender=None, emit=print, sleep=time.sleep):
    sender = sender or Sender(args)
    attempted, stop_needed = False, False
    try:
        if args.send:
            sender.open()
        for index, coordinates in enumerate(targets(args), 1):
            if needs_position(args):
                missing = 'el' if args.axes == 'az' else 'az'
                coordinates[missing] = sender.position()[missing] if args.send else getattr(args, 'hold_' + missing)
            command = wire_target(args, coordinates)
            emit('{}: {}'.format(index, describe(command)), flush=True)
            if args.send:
                # A lost reply can mean the target was accepted. Stop even if
                # send() fails before returning, without replaying that target.
                attempted = stop_needed = True
                sender.send(command)
                if args.mode == 'sweep':
                    sleep(args.interval)
        if args.mode == 'single':
            stop_needed = False
    finally:
        if args.send:
            interrupted = sys.exc_info()[0] is not None
            try:
                if attempted and stop_needed:
                    command = wire_stop(args)
                    try:
                        emit('STOP: ' + describe(command), flush=True)
                    except OSError:
                        pass  # A closed stdout pipe must not prevent STOP.
                    sender.send(command, stopping=True)
            except (OSError, ValueError, ApiError) as error:
                # Preserve the original error/interrupt, and fail on an otherwise
                # normal completion if the STOP could not be delivered.
                print('Не удалось подтвердить STOP: ' + str(error), file=sys.stderr, flush=True)
                if not interrupted:
                    raise
            finally:
                sender.close()


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument('--interactive', action='store_true', help='Открыть меню (также при запуске без параметров)')
    result.add_argument('--protocol', choices=PROTOCOLS)
    result.add_argument('--transport', choices=('tcp', 'udp', 'serial', 'http'))
    result.add_argument('--host', default='192.168.4.1', help='Имя или IP без схемы и порта')
    result.add_argument('--port', type=int)
    result.add_argument('--serial', metavar='PORT', help='COM-порт или путь /dev/tty…')
    result.add_argument('--baud', type=int, default=9600)
    result.add_argument('--timeout', type=float, default=2)
    result.add_argument('--rotor-name', default='G5500', help='Имя поворотки для N1MM')
    result.add_argument('--axes', choices=('az', 'el', 'both'), default='az')
    result.add_argument('--mode', choices=('single', 'sweep'), default='sweep')
    action = result.add_mutually_exclusive_group()
    action.add_argument('--send', action='store_true', help='Отправлять команды контроллеру')
    action.add_argument('--dry-run', action='store_true', help='Предпросмотр без подключения (по умолчанию)')
    result.add_argument('--interval', type=float, default=1, help='Пауза после каждой команды, секунд')
    result.add_argument('--count', type=int, help='Число команд; по умолчанию 20 в предпросмотре, без ограничения при отправке')
    for axis in ('az', 'el'):
        result.add_argument('--' + axis, help='Цель ' + axis.upper() + ' в одиночном режиме')
        for suffix, label in (('min', 'Минимум'), ('max', 'Максимум'), ('step', 'Шаг'), ('hold', 'Фиксированная координата')):
            flag = '--hold-' + axis if suffix == 'hold' else '--' + axis + '-' + suffix
            result.add_argument(flag, help=label + ' ' + axis.upper() + ', градусы')
    return result


def choose(label, choices, default):
    print('\n' + label)
    numbered = {str(index): key for index, key in enumerate(choices, 1)}
    default_number = str(list(choices).index(default) + 1)
    for index, key in numbered.items():
        print('  {} — {}'.format(index, choices[key]))
    while True:
        answer = input('[{}]: '.format(default_number)).strip() or default_number
        if answer in numbered:
            return numbered[answer]
        print('Введите номер от 1 до {}.'.format(len(choices)))


def ask(label, default, convert=str):
    while True:
        value = input('{} [{}]: '.format(label, default)).strip() or str(default)
        try:
            return convert(value)
        except (ValueError, InvalidOperation):
            print('Некорректное значение.')


def menu(args):
    args.protocol = choose('Протокол', PROTOCOLS, args.protocol or 'gs232')
    args.axes = choose('Оси', {'az': 'AZ'} if args.protocol == 'n1mm' else
                       {'az': 'AZ', 'el': 'EL', 'both': 'AZ+EL'}, 'az' if args.protocol == 'n1mm' else args.axes)
    args.mode = choose('Режим', {'single': 'Одиночная цель', 'sweep': 'Непрерывно туда и обратно'}, args.mode)
    args.send = choose('Действие', {'preview': 'Предпросмотр', 'send': 'Отправка'}, 'send' if args.send else 'preview') == 'send'
    args.transport = choose('Подключение', {key: key.upper() for key in TRANSPORTS[args.protocol]},
                            'serial' if args.serial and 'serial' in TRANSPORTS[args.protocol] else TRANSPORTS[args.protocol][0])
    if args.transport == 'serial':
        args.serial = ask('Последовательный порт', args.serial or ('COM3' if sys.platform == 'win32' else '/dev/ttyUSB0'))
        args.baud = ask('Скорость, бод', args.baud, int)
    else:
        args.host = ask('Адрес контроллера', args.host)
        args.port = ask('Порт', args.port or default_port(args), int)
    if args.protocol == 'n1mm':
        args.rotor_name = ask('Имя поворотки N1MM', args.rotor_name)
    if args.send:
        args.timeout = ask('Таймаут ответа, секунд', args.timeout, float)
    for axis in selected_axes(args):
        if args.mode == 'single':
            setattr(args, axis, ask('Цель ' + axis.upper(), getattr(args, axis) or 0, number))
        else:
            for suffix, label, default in (('min', 'Минимум', 0), ('max', 'Максимум', 360 if axis == 'az' else 180), ('step', 'Шаг', 10)):
                name = axis + '_' + suffix
                setattr(args, name, ask(label + ' ' + axis.upper(), getattr(args, name) or default, number))
    if args.mode == 'sweep':
        args.interval = ask('Пауза между командами, секунд', args.interval, float)
        args.count = ask('Число команд' + (' (0 — без ограничения)' if args.send else ''),
                         args.count if args.count is not None else (0 if args.send else 20), int)
        if args.send and args.count == 0:
            args.count = None
    if not args.send and needs_position(args):
        missing = 'el' if args.axes == 'az' else 'az'
        name = 'hold_' + missing
        setattr(args, name, ask('Фиксированная ' + missing.upper() + ' для предпросмотра', getattr(args, name) or 0, number))


def default_port(args):
    return (4535 if args.transport == 'udp' else 4533) if args.protocol == 'api' else PORTS[args.protocol]


def validate(args):
    if not args.protocol:
        raise ValueError('Укажите --protocol или запустите скрипт без параметров для меню')
    args.transport = args.transport or ('serial' if args.serial else TRANSPORTS[args.protocol][0])
    if args.transport not in TRANSPORTS[args.protocol]:
        raise ValueError('Протокол не поддерживает выбранный транспорт')
    if args.protocol == 'n1mm' and args.axes != 'az':
        raise ValueError('N1MM поддерживает только AZ')
    if args.serial and args.transport != 'serial':
        raise ValueError('--serial несовместим с выбранным транспортом')
    if args.transport == 'serial' and not args.serial:
        raise ValueError('Укажите --serial PORT')
    args.port = args.port if args.port is not None else default_port(args)
    if not 1 <= args.port <= 65535 or args.baud <= 0:
        raise ValueError('Нужен порт 1…65535 и положительная скорость UART')
    if args.transport == 'serial' and sys.platform != 'win32':
        import termios
        if not hasattr(termios, 'B' + str(args.baud)):
            raise ValueError('Неподдерживаемая скорость UART: ' + str(args.baud))
    if not args.host or any(char in args.host for char in '/?#@\r\n'):
        raise ValueError('Нужен адрес без схемы, пути и порта')
    if args.protocol == 'n1mm' and (not args.rotor_name.strip() or any(char in args.rotor_name for char in '<>&,\r\n') or len(args.rotor_name.encode('utf-8')) > 128):
        raise ValueError('Некорректное имя N1MM: исключите XML-символы, запятые и переводы строк; максимум 128 байт')
    if any(not math.isfinite(value) or value <= 0 for value in (args.timeout, args.interval)):
        raise ValueError('Таймаут и пауза должны быть конечными и положительными')
    if args.count is not None and args.count <= 0:
        raise ValueError('--count должен быть положительным; для неограниченной отправки опустите параметр')
    for axis in selected_axes(args):
        names = (axis,) if args.mode == 'single' else tuple(axis + '_' + suffix for suffix in ('min', 'max', 'step'))
        for name in names:
            if getattr(args, name) is None:
                raise ValueError('Не указан --' + name.replace('_', '-'))
            setattr(args, name, number(getattr(args, name)))
        if args.mode == 'sweep' and (getattr(args, axis + '_min') > getattr(args, axis + '_max') or getattr(args, axis + '_step') <= 0):
            raise ValueError(axis.upper() + ': минимум должен быть ≤ максимума, шаг — положительным')
    if not args.send and needs_position(args):
        name = 'hold_el' if args.axes == 'az' else 'hold_az'
        if getattr(args, name) is None:
            raise ValueError('Для предпросмотра укажите --' + name.replace('_', '-'))
        setattr(args, name, number(getattr(args, name)))
    if args.mode == 'single':
        if args.count not in (None, 1):
            raise ValueError('Одиночный режим отправляет ровно одну цель')
        args.count = 1
    elif args.count is None and not args.send:
        args.count = 20
    return args


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    arg_parser = parser()
    args = arg_parser.parse_args(argv)
    try:
        if not argv or args.interactive:
            menu(args)
        validate(args)
        print('Отправка команд. Ctrl+C — STOP.' if args.send else 'Предпросмотр: подключения и отправки нет.', flush=True)
        if args.send and args.protocol in ('gs232', 'rotctld', 'n1mm', 'pst'):
            print('Движение зависит от режима, подтверждения внешних целей и настройки угла AZ контроллера.', flush=True)
        run(args)
        return 0
    except KeyboardInterrupt:
        return 130
    except (OSError, ValueError, OverflowError, ApiError, EOFError) as error:
        print('Ошибка: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
