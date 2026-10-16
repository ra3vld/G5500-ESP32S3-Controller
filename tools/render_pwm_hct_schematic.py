#!/usr/bin/env python3
"""Render the reviewed two-axis BTS7960 reference as SVG, PNG and vector PDF.
Requires Pillow, ReportLab and DejaVu Sans; no network or image generation.
"""
from html import escape
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/hardware/independent-pwm-bts7960-hct'
W,H=2400,1730
FONT='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
BOLD='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
pdfmetrics.registerFont(TTFont('DejaVu',FONT));pdfmetrics.registerFont(TTFont('DejaVuBold',BOLD))
pdf=canvas.Canvas(str(OUT.with_suffix('.pdf')),pagesize=(W/2,H/2));pdf.setTitle('Two-axis BTS7960 wiring - ESP32-S2 Mini / ESP32-S3')
pdf.scale(.5,.5)
im=Image.new('RGB',(W,H),'white');d=ImageDraw.Draw(im)
svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">','<rect width="100%" height="100%" fill="white"/>']
INK='#172b46';BLUE='#245fbe';GREEN='#087c60';ORANGE='#a65600';GRAY='#586575'
def color(value):
    pdf.setStrokeColor(value);pdf.setFillColor(value)
def text(x,y,value,size=22,c=INK,bold=False):
    value=str(value);f=ImageFont.truetype(BOLD if bold else FONT,size)
    d.text((x,y),value,font=f,fill=c,anchor='lt')
    svg.append(f'<text x="{x}" y="{y}" dominant-baseline="text-before-edge" font-family="DejaVu Sans,sans-serif" font-size="{size}" font-weight="{"bold" if bold else "normal"}" fill="{c}">{escape(value)}</text>')
    pdf.setFillColor(c);pdf.setFont('DejaVuBold' if bold else 'DejaVu',size);pdf.drawString(x,H-y-size*.8,value)
def line(points,c=INK,width=3):
    d.line(points,fill=c,width=width)
    svg.append('<polyline points="'+' '.join(f'{x},{y}' for x,y in points)+f'" fill="none" stroke="{c}" stroke-width="{width}"/>')
    color(c);pdf.setLineWidth(width)
    for (x1,y1),(x2,y2) in zip(points,points[1:]):pdf.line(x1,H-y1,x2,H-y2)
def box(x,y,w,h,fill='#f6f8fc',stroke='#aebed2'):
    d.rectangle((x,y,x+w,y+h),fill=fill,outline=stroke,width=2)
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
    pdf.setFillColor(fill);pdf.setStrokeColor(stroke);pdf.setLineWidth(2);pdf.rect(x,H-y-h,w,h,fill=1,stroke=1)
def dot(x,y,c=INK):
    d.ellipse((x-4,y-4,x+4,y+4),fill=c);svg.append(f'<circle cx="{x}" cy="{y}" r="4" fill="{c}"/>');color(c);pdf.circle(x,H-y,4,fill=1,stroke=0)
def ground(x,y):
    for w,dy in [(12,0),(8,6),(3,12)]:line([(x-w,y+dy),(x+w,y+dy)],GRAY,2)
def resistor(x1,x2,y,label,c=BLUE,size=18):
    mid=(x1+x2)/2;line([(x1,y),(mid-18,y)],c);box(mid-18,y-7,36,14,'white',c);line([(mid+18,y),(x2,y)],c);text(mid-25,y-30,label,size,c)
def pulldown(x,y,label='10k',height=35,c=GRAY):
    dot(x,y,BLUE);line([(x,y),(x,y+8)],c);box(x-6,y+8,12,18,'white',c);line([(x,y+26),(x,y+height)],c);ground(x,y+height);text(x+15,y+8,label,16,c)
def cap(x,y,label='100nF'):
    line([(x,y),(x,y+20)],GREEN);line([(x-12,y+20),(x+12,y+20)],GREEN,2);line([(x-12,y+27),(x+12,y+27)],GREEN,2);line([(x,y+27),(x,y+50)],GREEN);ground(x,y+50);text(x+17,y+16,label,18,GREEN)
text(45,30,'Два независимых привода BTS7960',39,bold=True)
text(47,85,'ESP32-S2 Mini / ESP32-S3 N16R8  |  DRIVE_INTERFACE = DRIVE_BTS7960',25,BLUE)
text(47,125,'Один H-модуль на щёточный DC-двигатель. PWM 1 кГц, 10 бит. ENABLE выключен при STOP и ошибке.',21,GRAY)
box(45,205,480,590)
text(65,220,'ESP32: выходы 3,3 В',27,bold=True)
box(820,205,310,590)
text(838,220,'U1: 74HCT245',27,bold=True)
text(837,260,'TTL-входы, питание 5 В',18,GRAY)
channels=[('AZ RPWM / CW',16,16,320),('AZ LPWM / CCW',34,15,390),('AZ ENABLE',21,21,460),('EL RPWM / UP',17,17,600),('EL LPWM / DOWN',18,18,670),('EL ENABLE',33,47,740)]
for i,(name,s2,s3,y) in enumerate(channels):
    text(65,y-28,name,22,BLUE,True);text(65,y+3,f'S2: GPIO{s2}  /  S3: GPIO{s3}',20,GRAY)
    line([(525,y),(820,y)],BLUE);pulldown(650,y)
    text(835,y-13,f'A{i+1} [{i+2}]',21,BLUE);text(1000,y-13,f'B{i+1} [{18-i}]',21,BLUE)
    line([(1130,y),(1430,y)],BLUE);resistor(1190,1290,y,'100Ω')
    if i in (2,5):
        dot(1355,y,BLUE);line([(1355,y),(1355,y+25),(1430,y+25)],BLUE)
        pulldown(1320,y,height=38)
# Fixed control pins, explicit net labels.
text(838,767,'A7[8], A8[9] → GND',18,GRAY)
text(47,810,'Каждый A1...A6: 10 кОм к GND. Неиспользуемые B7[12], B8[11]: NC.',20,GRAY)
text(47,842,'Корпус U1: PDIP/SOIC20, номера выводов - вид сверху; нумерацию модуля сверить по маркировке.',19,GRAY)
line([(920,205),(920,180),(1020,180),(1020,205)],GREEN);dot(970,180,GREEN)
text(795,157,'+5V_REG: VCC[20] и DIR[1]',20,GREEN)
line([(970,180),(1170,180)],GREEN);cap(1170,180)
text(1250,170,'U1: /OE[19] и GND[10] → GND',21,GREEN)
for axis,y0,motor in [('AZ',300,'M_AZ'),('EL',580,'M_EL')]:
    box(1430,y0-25,535,250)
    text(1590,y0-8,f'H-модуль BTS7960: {axis}',24,bold=True)
    for dy,label in [(20,'1 RPWM'),(90,'2 LPWM'),(160,'3 R_EN'),(185,'4 L_EN')]:text(1445,y0+dy-10,label,20,BLUE)
    text(1590,y0+154,'5 R_IS / 6 L_IS: NC',19,GRAY)
    # Logic VCC and logic/motor ground, same named ground star.
    text(1465,y0+203,'7 VCC: +5V_REG',18,GREEN)
    text(1690,y0+203,'8 GND / B−: GND★',18,GREEN)
    # Supply with individual fuse.
    line([(1830,y0-25),(1830,y0-55),(2240,y0-55)],ORANGE)
    box(1990,y0-64,60,18,'white',ORANGE);text(1995,y0-95,'F1' if axis=='AZ' else 'F2',20,ORANGE)
    text(1837,y0-52,'B+',20,ORANGE);text(2250,y0-70,'V_MOTOR',20,ORANGE,True)
    # Motor terminals.
    text(1900,y0+70,'M+',20,ORANGE);text(1900,y0+116,'M−',20,ORANGE)
    line([(1965,y0+90),(2140,y0+90)],ORANGE);line([(1965,y0+136),(2140,y0+136)],ORANGE)
    box(2140,y0+63,195,98,'#fff6e8',ORANGE)
    text(2170,y0+80,motor,24,ORANGE,True);text(2160,y0+118,'щёт. DC',21,ORANGE)
# Main pin table, readable alongside peripheral wiring.
box(45,895,1060,355)
text(65,912,'Назначения двух профилей',27,bold=True)
rows=[('Вход / назначение','S2 Mini','S3 N16R8'),('AZ: RPWM / LPWM / ENABLE','16 / 34 / 21','16 / 15 / 21'),('EL: RPWM / LPWM / ENABLE','17 / 18 / 33','17 / 18 / 47'),('Обратная связь AZ / EL','ADC4 / ADC5','ADC4 / ADC5'),('GS-232 UART RX / TX','37 / 39','44 / 43'),('HC-05 RX / TX / STATE','6 / 7 / 3','6 / 7 / 3'),('Индикация','LED15','RGB48')]
for i,row in enumerate(rows):
    y=958+i*39
    if i%2==0:box(60,y-3,1029,38,'#edf2fa','#edf2fa')
    for x,value in zip([72,650,858],row):text(x,y,value,21,INK,i==0)
# Power notes and physical star topology.
box(1140,895,1215,355,'#fff8ea','#d8b16f')
text(1160,912,'Питание и силовая земля',27,ORANGE,True)
for i,s in enumerate([
'V_MOTOR: по паспорту двигателя и диапазону конкретного модуля.',
'F1/F2: подобрать по току двигателя, проводам и условиям пуска.',
'+5V_REG: отдельные стабилизированные 5 В для U1 и VCC модулей.',
'GND★: общая точка у источника питания. B− обоих модулей идут',
'к ней силовыми проводами; GND ESP32 - отдельным проводом.',
'Ток двигателей не проходит через плату или землю ESP32.',
'100 нФ у U1 между VCC и GND; ёмкость питания - у модулей.'
]):text(1160,958+i*38,s,21,ORANGE if i in (0,5) else INK)
# ADC front end, repeated independently per sensor.
box(45,1290,1300,305)
text(65,1305,'Обратная связь S2/S3: AZ и EL, резисторы делителя 1%',26,bold=True)
text(65,1348,'Датчик 0...5 В',21,GREEN)
text(65,1562,'Подбирать под МК: S3 — 4,7 кОм от сигнала / 5,1 кОм на землю (1%, вход ≤5,5 В).',20,GREEN)
line([(220,1400),(385,1400)],GREEN);resistor(220,385,1400,'6,8k',GREEN)
line([(385,1400),(720,1400)],GREEN);dot(445,1400,GREEN)
pulldown(445,1400,'4,7k',height=75,c=GREEN)
cap(560,1400)
resistor(720,850,1400,'1k',GREEN)
line([(850,1400),(930,1400)],GREEN);dot(895,1400,GREEN)
text(950,1388,'ADC4 (AZ) / ADC5 (EL)',22,GREEN,True)
# Both external clamp diodes connect to the ADC node.
line([(895,1400),(895,1375)],GREEN)
line([(885,1375),(905,1375),(895,1358),(885,1375)],GREEN,2)
line([(883,1355),(907,1355)],GREEN,2)
line([(895,1355),(895,1340),(930,1340)],GREEN);text(945,1331,'3,3 В',18,GREEN)
line([(895,1400),(895,1430)],GREEN)
line([(883,1433),(907,1433)],GREEN,2)
line([(895,1436),(885,1455),(905,1455),(895,1436)],GREEN,2)
line([(895,1455),(895,1490)],GREEN);ground(895,1490)
# External clamps: lower diode anode GND, upper cathode 3V3.
text(655,1450,'D1: A=ADC, K=3,3 В',18,GREEN)
text(655,1480,'D2: A=GND, K=ADC',18,GREEN)
text(65,1530,'ADC_11db: 5 В → 2,04 В; при 5,5 В и допусках 1% ≤2,275 В. D1/D2: BAT54S. Калибровать по мВ.',20,GRAY)
# User button and optional devices.
box(1380,1290,975,305)
text(1400,1305,'Кнопка и периферия',26,bold=True)
text(1400,1348,'USER_BUTTON_GPIO → кнопка → GND',23,BLUE,True)
text(1400,1388,'По умолчанию GPIO0, active LOW, INPUT_PULLUP.',21)
text(1400,1423,'Другой GPIO - в build_config.hpp; -1 отключает кнопку.',20)
text(1400,1464,'TFT: MOSI10/SCK11/MISO2, CS12/DC13/RST14;',20)
text(1400,1498,'touch: CS8/IRQ9. LCD: RS8/E9, D4...D7=10...13.',20)
text(1400,1532,'LCD RW=GND; 5 В LCD - через отдельный TTL-буфер.',20,GRAY)
text(47,1615,'BTS7960: PWM идёт на RPWM/LPWM; GPIO21 и GPIO33/47 - ENABLE, не выходы скорости.',23,BLUE,True)
text(47,1652,'Схема для стандартного DC-модуля, проверка реального оборудования отдельно. BTN7960: только с тем же интерфейсом.',20,GRAY)
text(47,1687,'Источники: Handson Technology BTS7960 Motor Driver; TI SN74HCT245 (SCLS020H). Подробные ссылки и сборка - в .md рядом.',18,GRAY)
svg.append('</svg>');OUT.with_suffix('.svg').write_text('\n'.join(svg)+'\n');im.save(OUT.with_suffix('.png'));pdf.showPage();pdf.save()
for suffix in ('.svg','.png','.pdf'):print(OUT.with_suffix(suffix))
