#!/usr/bin/env python3
"""Render the mast installation and local wiring of the Waveshare controller."""
from pathlib import Path
from html import escape
from schematic_svg import render_pages

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/boards/waveshare-esp32-s3-eth-mast'
INK, BLUE, GREEN, PURPLE, ORANGE = '#18344c', '#1476a5', '#27815a', '#8057a6', '#bf6418'


class Page:
    def __init__(self, title, subtitle, number):
        self.items = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2000 1400">',
                      '<rect width="2000" height="1400" fill="white"/>',
                      '<defs>' + ''.join(f'<marker id="a{i}" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto-start-reverse"><path d="M0,0 L9,4.5 L0,9" fill="{c}"/></marker>' for i, c in enumerate([INK, BLUE, GREEN, PURPLE, ORANGE])) + '</defs>',
                      '<g font-family="DejaVu Sans,sans-serif">']
        self.text(50, 65, title, 38, True)
        self.text(50, 108, subtitle, 23)
        self.text(50, 1365, 'Waveshare ESP32-S3-ETH  |  AZ / EL  |  камера на мачте', 21, color='#61788a')
        self.text(1820, 1365, f'{number} / 2', 21, color='#61788a')

    def text(self, x, y, label, size=24, bold=False, color=INK):
        self.items.append(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{700 if bold else 400}" fill="{color}">{escape(label)}</text>')

    def box(self, x, y, w, h, fill='#f2f6fa', stroke='#a6b9c8', dash=False):
        self.items.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="{fill}" stroke="{stroke}" stroke-width="2"' + (' stroke-dasharray="12 8"' if dash else '') + '/>')

    def path(self, points, color=INK, arrow=False, width=4, dash=False):
        d = 'M' + ' L'.join(f'{x},{y}' for x, y in points)
        marker = [INK, BLUE, GREEN, PURPLE, ORANGE].index(color) if arrow else 0
        self.items.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round"' + (f' marker-end="url(#a{marker})"' if arrow else '') + (' stroke-dasharray="12 7"' if dash else '') + '/>')

    def dot(self, x, y, color=INK):
        self.items.append(f'<circle cx="{x}" cy="{y}" r="5" fill="{color}"/>')

    def ground(self, x, y):
        self.path([(x, y), (x, y+12)])
        for dy, w in [(12, 18), (20, 12), (28, 6)]:
            self.path([(x-w, y+dy), (x+w, y+dy)], width=2)

    def finish(self):
        return '\n'.join(self.items + ['</g></svg>'])


p = Page('Контроллер поворотки рядом с антеннами',
         'Короткие провода к двигателям и датчикам; вниз по мачте идут питание и связь.', 1)
p.box(40, 145, 1920, 770, '#f8fbfd')
p.text(70, 190, 'НА МАЧТЕ', 27, True)
# Antenna and mast, outside the weather enclosure.
p.path([(260, 250), (260, 850)], width=12)
p.path([(80, 260), (290, 260)], width=7)
for x, half in [(95, 45), (145, 65), (200, 85), (260, 105)]:
    p.path([(x, 260-half), (x, 260+half)], width=5)
p.text(70, 410, 'Антенны', 26, True)
p.text(70, 448, 'и поворотка', 24)
p.text(70, 790, 'Мачта', 24)
p.text(70, 827, 'Металл → PE', 21)

p.box(360, 190, 1160, 685, '#eef4f8', BLUE)
p.text(390, 232, 'УЛИЧНЫЙ КОРПУС ОКОЛО ПОВОРОТКИ', 26, True)
p.box(420, 265, 430, 280, '#e0f0fa', BLUE)
p.text(448, 310, 'Waveshare', 32, True)
p.text(448, 350, 'ESP32-S3-ETH', 32, True)
p.text(448, 402, 'W5500 / RJ45', 24, color=BLUE)
p.text(448, 444, 'Камера + 4 PWM + общая I²C', 21)
p.text(448, 487, 'Управление AZ / EL', 24)
p.text(448, 523, 'microSD не устанавливать', 21)

for y, axis in [(265, 'AZ'), (515, 'EL')]:
    p.box(1080, y, 360, 125, '#e8f5ed', GREEN)
    p.text(1105, y+43, f'BTS7960 / IBT-2 · {axis}', 26, True)
    p.text(1105, y+82, 'RPWM + LPWM + ENABLE', 21)
    p.text(1105, y+111, 'Своя силовая ветвь +U / 0V', 20)
    p.box(1640, y, 290, 105, '#e8f5ed', GREEN)
    p.text(1665, y+43, f'Двигатель {axis}', 27, True)
    p.text(1665, y+80, 'M+ / M−', 23)
    p.path([(1440, y+55), (1640, y+55)], GREEN, True, 6)
    p.text(1460, y+37, '2 провода', 19, color=GREEN)
    p.box(1640, y+130, 290, 100, '#f1eaf7', PURPLE)
    p.text(1665, y+168, f'Датчик {axis}', 26, True)
    p.text(1665, y+205, f'MT6701 · {"0x06" if axis=="AZ" else "0x46"}', 22)
p.path([(850, 330), (1080, 330)], GREEN, True)
p.text(879, 310, 'AZ · 3 сигнала', 21, color=GREEN)
p.path([(850, 470), (1040, 470), (1040, 575), (1080, 575)], GREEN, True)
p.text(870, 450, 'EL · 3 сигнала', 21, color=GREEN)

p.box(420, 595, 230, 105, '#e0f0fa', BLUE)
p.text(443, 635, 'Камера DVP', 27, True)
p.text(443, 675, 'За окном корпуса', 20)
p.path([(535, 595), (535, 545)], BLUE, True)
p.text(552, 577, 'Короткий FFC', 19, color=BLUE)

p.box(700, 625, 280, 95, '#fff2df', ORANGE)
p.text(722, 664, 'DC/DC +U → 5 В', 25, True)
p.text(722, 701, 'Плата и камера', 20)
p.path([(770, 625), (770, 545)], ORANGE, True)
p.text(786, 589, '5 В', 21, color=ORANGE)

p.box(1120, 730, 320, 95, '#f1eaf7', PURPLE)
p.text(1143, 767, 'Одна шина I²C', 24, True)
p.text(1143, 802, 'SDA: GPIO4 · SCL: GPIO5', 20)
p.path([(1640, 445), (1560, 445), (1560, 775), (1440, 775)], PURPLE, width=5)
p.path([(1640, 695), (1560, 695)], PURPLE, width=5)
p.dot(1560, 695, PURPLE)
p.text(1590, 824, '3V3, GND, SDA, SCL', 21, color=PURPLE)
p.path([(1120, 775), (1000, 775), (1000, 530), (850, 530)], PURPLE, True)
p.text(1018, 704, 'AZ / EL', 21, color=PURPLE)

p.box(420, 745, 300, 95, '#f1eaf7', PURPLE, True)
p.text(442, 781, 'RS-485 с развязкой', 24, True)
p.text(442, 816, 'Вариант вместо USB / RGB', 19)
p.path([(420, 495), (390, 495), (390, 790), (420, 790)], PURPLE, True, dash=True)

# Three independent drops, with separately labelled destinations.
p.box(40, 1060, 1920, 255, '#f6f7f8')
p.text(65, 1095, 'В ПОМЕЩЕНИИ', 23, True)
p.path([(420, 385), (320, 385), (320, 1110)], BLUE, width=6)
p.text(62, 956, 'Ethernet', 25, True, BLUE)
p.text(62, 992, 'Камера + команды', 22, color=BLUE)
p.path([(550, 840), (550, 1020), (900, 1020), (900, 1110)], PURPLE, width=5, dash=True)
p.text(620, 956, 'RS-485 · A / B + COM', 25, True, PURPLE)
p.text(620, 992, 'Команды и телеметрия', 22, color=PURPLE)
p.path([(840, 720), (840, 895), (1270, 895), (1270, 1020), (1620, 1020), (1620, 1110)], ORANGE, width=6)
p.text(1370, 956, 'Питание +U / 0V', 25, True, ORANGE)
p.text(1370, 992, 'Напряжение по двигателям', 22, color=ORANGE)

for x, w, title, lines, color in [
    (70, 500, 'Коммутатор / роутер', ['ПК: веб и изображение', 'HUD по сети / Wi-Fi', 'Ethernet не требует RS-485'], BLUE),
    (650, 600, 'Один из вариантов RS-485', ['HUD с RS-485', 'ПК через USB ↔ RS-485', 'Либо шлюз Ethernet ↔ RS-485'], PURPLE),
    (1370, 550, 'Источник питания +U', ['Отдельные предохранители AZ / EL', '5 В формируются наверху в DC/DC', 'Силовой ток не идёт через ESP32'], ORANGE),
]:
    p.box(x, 1110, w, 175, '#ffffff', color)
    p.text(x+23, 1148, title, 27, True, color)
    for i, label in enumerate(lines): p.text(x+23, 1187+i*34, label, 22)
page1 = p.finish()

p = Page('Соединения внутри корпуса на мачте',
         'Два MT6701 на одной шине I²C: AZ с адресом 0x06 и EL с заранее изменённым адресом 0x46.', 2)

p.box(50, 150, 620, 750)
p.text(78, 195, 'ДАТЧИКИ AZ / EL', 29, True, PURPLE)
p.text(78, 235, 'Общая SDA/SCL; адреса 7-битные.', 23)
p.text(78, 276, 'SDA и SCL: по 4,7 кΩ к 3V3', 23)
p.path([(245, 385), (620, 385)], PURPLE)
p.path([(245, 455), (620, 455)], PURPLE)
p.text(78, 393, 'GPIO4 / SDA', 22, True)
p.text(78, 463, 'GPIO5 / SCL', 22, True)
for x, wire in [(300, 385), (390, 455)]:
    if wire != 385:
        p.items.append(f'<circle cx="{x}" cy="385" r="7" fill="#f2f6fa"/>')
    p.text(x-26, 315, '3V3', 20)
    p.path([(x, 322), (x, 332)], PURPLE)
    p.items.append(f'<rect x="{x-8}" y="332" width="16" height="25" fill="white" stroke="{PURPLE}" stroke-width="2"/>')
    p.path([(x, 357), (x, wire)], PURPLE)
    p.dot(x, wire, PURPLE)
for x, axis, address in [(180, 'AZ', '0x06'), (440, 'EL', '0x46')]:
    p.box(x, 570, 190, 112, '#f1eaf7', PURPLE)
    p.text(x+16, 610, f'MT6701 · {axis}', 23, True)
    p.text(x+52, 652, address, 25, True, PURPLE)
    sda, scl = x+80, x+150
    p.dot(sda, 385, PURPLE)
    p.items.append(f'<circle cx="{sda}" cy="455" r="7" fill="#f2f6fa"/>')
    p.path([(sda, 385), (sda, 570)], PURPLE)
    p.dot(scl, 455, PURPLE)
    p.path([(scl, 455), (scl, 570)], PURPLE)
    p.text(sda-21, 552, 'SDA', 18)
    p.text(scl-21, 552, 'SCL', 18)
p.text(78, 728, 'EL: заранее изменить 0x06 → 0x46.', 23, True)
p.text(78, 770, '3V3 + GND → оба модуля датчиков.', 22)
p.text(78, 810, '100 нФ между VDD и GND у каждого.', 22)
p.text(78, 850, 'Два AS5600: совпадает адрес 0x36.', 22)
p.text(78, 884, 'OUT / ABZ не подключаются.', 21, True, PURPLE)

p.box(710, 150, 610, 750)
p.text(738, 195, 'ДВА PWM-ДРАЙВЕРА', 29, True, GREEN)
p.text(738, 235, 'Соединения ESP32 → IBT-2', 23)
for y, axis, pins in [(290, 'AZ', (17, 16, 6)), (555, 'EL', (44, 43, 7))]:
    p.text(738, y, f'Ось {axis}', 28, True, GREEN)
    for i, (gpio, dest) in enumerate(zip(pins, ['RPWM', 'LPWM', 'R_EN + L_EN'])):
        p.text(755, y+45+i*42, f'GPIO{gpio}', 24, True)
        p.path([(910, y+36+i*42), (1010, y+36+i*42)], GREEN, True)
        p.text(1030, y+45+i*42, dest, 23)
    p.text(755, y+181, 'M+ / M− → свой двигатель', 22)
p.text(738, 798, 'Каждый сигнал: 47 Ω; вход → 10 кΩ → GND', 21)
p.text(738, 840, 'VCC логики 3,3 В для HC/AHC244;', 22)
p.text(738, 875, 'для логики 5 В нужен буфер / оптопары.', 22)

p.box(1360, 150, 590, 750, '#fcf8ff', PURPLE, True)
p.text(1388, 195, 'ВАРИАНТ RS-485', 29, True, PURPLE)
p.text(1388, 234, 'Вместо USB Serial/JTAG и RGB', 24, True)
p.text(1388, 274, 'Требуется отдельный профиль прошивки.', 22)
for i, (gpio, dest) in enumerate([(19, 'TX → DI'), (20, 'RX ← RO'), (21, 'DE + /RE')]):
    y = 333+i*52
    p.text(1390, y, f'GPIO{gpio}', 24, True)
    p.text(1580, y, dest, 25)
p.text(1388, 495, 'GPIO19/20: точки USB D− / D+.', 22)
p.text(1388, 533, '3,3 В; изоляция сигнала и питания.', 22)
p.text(1388, 582, 'DE + /RE: подтяжка 10 кΩ к GND.', 22)
p.text(1388, 625, 'A / B: витая пара; COM: общий шины.', 22)
p.text(1388, 667, '120 Ω между A/B на обоих концах.', 22)
p.text(1388, 709, 'Одна цепь смещения на всю шину.', 22)
p.text(1388, 762, 'Сервисный разъём отключает 19/20/21', 22, True)
p.text(1388, 800, 'от RS-485 перед подключением USB.', 22)
p.text(1388, 855, 'Кадры камеры идут по Ethernet / Wi-Fi.', 22, True, BLUE)

p.box(50, 940, 1900, 350, '#fff8ee', ORANGE)
p.text(78, 986, 'ПИТАНИЕ, ЗЕМЛЯ И МОНТАЖ', 29, True, ORANGE)
for i, label in enumerate([
    '+U / 0V → предохранители ветвей → B+ / B− драйверов AZ и EL; отдельная ветвь → DC/DC 5 В → Waveshare.',
    'Локальная GND★: ESP32, датчики, входная логика IBT-2 и B−. Силовой возврат двигателей идёт к точке питания.',
    'Изолированная сторона RS-485 имеет свой COM; её не соединяют с GND★ в обход барьера развязки.',
    'TF1: общая SDA / GPIO4=контакт 2, SCL / GPIO5=7, AZ EN / GPIO6=3, EL EN / GPIO7=5. Слот microSD пустой.',
    'В настройках обеих осей: SDA=4, SCL=5; MT6701 AZ: адрес 6 (0x06), EL: 70 (0x46). Адрес датчика меняют отдельно.',
    'Корпус и мачту подключают к системе уравнивания потенциалов; кабельные вводы защищают УЗИП.',
    'MT6701 / AS5600: один оборот; рабочий ход не пересекает разрыв 0°/360°. Для AZ 450° нужен редуктор датчика.',
]): p.text(78, 1035+i*35, label, 23)
page2 = p.finish()

render_pages([(OUT.with_suffix('.svg'), page1),
              (OUT.with_name(OUT.name+'-connections').with_suffix('.svg'), page2)], OUT.with_suffix('.pdf'))
print(OUT.with_suffix('.pdf'))
