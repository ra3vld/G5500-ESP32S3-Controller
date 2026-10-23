#!/usr/bin/env python3
"""Render the native Waveshare ESP32-S3-ETH / dual BTS7960 wiring."""
from pathlib import Path
from html import escape
from schematic_svg import render_pages

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/boards/waveshare-esp32-s3-eth-bts7960'
parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1800 1480">',
         '<rect width="1800" height="1480" fill="white"/>',
         '<g font-family="DejaVu Sans,sans-serif" fill="#17324d">']

def text(x, y, label, size=24, bold=False, color='#17324d'):
    parts.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" font-weight="{700 if bold else 400}">{escape(label)}</text>')

def box(x, y, w, h, fill='#f3f7fb'):
    parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="{fill}" stroke="#859db4" stroke-width="2"/>')

def line(x1, y1, x2, y2, color='#186a88'):
    parts.append(f'<path d="M{x1},{y1} L{x2},{y2}" fill="none" stroke="{color}" stroke-width="3"/>')

def dot(x, y):
    parts.append(f'<circle cx="{x}" cy="{y}" r="5" fill="#186a88"/>')

def ground(x, y):
    line(x, y, x, y+12)
    for dy, width in [(12,18),(19,12),(26,6)]:
        line(x-width, y+dy, x+width, y+dy)

text(60, 62, 'Waveshare ESP32-S3-ETH · камера + два BTS7960 / IBT-2', 36, True)
text(60, 105, 'Четыре PWM 1 кГц · встроенные ADC1 · логика модулей 3,3 В (HC/AHC244)', 25)
box(60, 155, 540, 790)
text(90, 205, 'ESP32-S3-ETH', 30, True)
signals = [(260,17,'AZ RPWM','RPWM (1)'),(340,16,'AZ LPWM','LPWM (2)'),
           (420,6,'AZ ENABLE','R_EN (3) + L_EN (4)'),
           (610,44,'EL RPWM','RPWM (1)'),(690,43,'EL LPWM','LPWM (2)'),
           (770,7,'EL ENABLE','R_EN (3) + L_EN (4)')]
for y, gpio, name, target in signals:
    text(90,y+8,f'GPIO{gpio} · {name}',25,True)
    if gpio in (6,7):
        text(90,y+40,f'TF1 {"CMD · контакт 3" if gpio==6 else "CLK · контакт 5"}',21)
    elif gpio in (43,44):
        text(90,y+40,'Гребёнка TX / RX',21)
    line(600,y,740,y)
    parts.append(f'<rect x="740" y="{y-10}" width="70" height="20" fill="white" stroke="#186a88" stroke-width="3"/>')
    text(747,y-22,'47 Ω',21)
    line(810,y,1120,y)
    dot(960,y)
    line(960,y,960,y+14)
    parts.append(f'<rect x="951" y="{y+14}" width="18" height="25" fill="white" stroke="#186a88" stroke-width="2"/>')
    ground(960,y+39)
    text(986,y+36,'10 кΩ',20)
    text(1145,y+8,target,23,True)

# Module rectangles behind their labels/signals.
module_shapes=[]
for y, axis in [(165,'AZ'),(515,'EL')]:
    module_shapes.append(f'<rect x="1120" y="{y}" width="610" height="320" rx="12" fill="#edf8f2" stroke="#859db4" stroke-width="2"/>')
    text(1145,y+45,f'IBT-2 · {axis}',28,True)
    text(1440,y+105,'VCC (7) → 3V3',22)
    text(1440,y+145,'GND (8) → GND★',22)
    text(1440,y+185,'B+ → +U двигателя',21)
    text(1440,y+220,'B− → GND★',21)
    text(1440,y+255,f'M+ / M− → мотор {axis}',21)
    text(1440,y+290,'R_IS / L_IS: NC',21)
parts[3:3]=module_shapes

text(90,850,'GPIO4 · AZ_ADC ← TF1 CD/D3 (2)',23,True)
text(90,892,'GPIO5 · EL_ADC ← TF1 D0 (7)',23,True)
text(90,925,'3V3 и GND★ → логика обоих IBT-2',21)
box(60, 985, 540, 180)
text(90,1028,'Камера → штатный DVP-разъём',26,True)
text(90,1070,'W5500 / RJ45 → Ethernet',26,True)
text(90,1112,'USB-C → питание платы и Serial/JTAG',22)
text(90,1147,'Карта microSD отсутствует',24,True,color='#ad3f26')
box(60, 1200, 540, 205, '#fff9ed')
text(90,1242,'GPIO4–7: подключение к TF1',25,True)
text(90,1284,'Эти линии не выведены на гребёнки.',23)
text(90,1326,'Нумерация — контакты microSD;',23)
text(90,1368,'ориентацию сверяйте по схеме платы.',22)

text(700,960,'Обратная связь AZ и EL',29,True)
text(700,1002,'Два одинаковых делителя; вход 0…5 В → примерно 0…2,04 В',24)
for y, axis, gpio in [(1090,'AZ',4),(1290,'EL',5)]:
    text(700,y-35,f'Датчик {axis}',24,True)
    line(700,y,880,y)
    parts.append(f'<rect x="880" y="{y-10}" width="90" height="20" fill="white" stroke="#186a88" stroke-width="3"/>')
    text(878,y-26,'6,8 кΩ',22)
    line(970,y,1120,y);dot(1120,y)
    line(1120,y,1120,y+30)
    parts.append(f'<rect x="1111" y="{y+30}" width="18" height="45" fill="white" stroke="#186a88" stroke-width="3"/>')
    text(1005,y+65,'4,7 кΩ',22)
    ground(1120,y+92)
    line(1120,y,1640,y)
    text(1370,y-22,f'{axis}_ADC → GPIO{gpio}',24,True)
    line(1250,y,1250,y+38);dot(1250,y)
    line(1228,y+38,1272,y+38);line(1228,y+49,1272,y+49)
    line(1250,y+49,1250,y+92);ground(1250,y+92)
    text(1285,y+72,'100 нФ',22)
    text(1370,y+38,'+ защита ADC по руководству',21)
text(60,1450,'GND★ — общая земля контроллера, логики, датчиков и силовых B− (прямое подключение).',23)
parts.append('</g></svg>')
render_pages([(OUT.with_suffix('.svg'), '\n'.join(parts))], OUT.with_suffix('.pdf'))
print(OUT)
