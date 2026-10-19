#!/usr/bin/env python3
"""Render direct and PC817C-isolated IBT-2 wiring in SVG, PNG and vector PDF.

Requires Pillow, rsvg-convert, pdfunite and DejaVu Sans. The isolated PDF has two pages:
connections and the two distinct optocoupler circuits. No network is used.
"""
from html import escape
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/hardware'
W, H = 2400, 1800
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
BOLD = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
INK, BLUE, GREEN, ORANGE, GRAY, RED = '#172b46', '#245fbe', '#087c60', '#a65600', '#586575', '#ac3434'
CHANNELS = [('AZ RPWM / CW', 16, 16, 'U1'), ('AZ LPWM / CCW', 34, 15, 'U2'),
            ('AZ ENABLE', 21, 21, 'U5'), ('EL RPWM / UP', 17, 17, 'U3'),
            ('EL LPWM / DOWN', 18, 18, 'U4'), ('EL ENABLE', 33, 47, 'U6')]

class Drawing:
    def __init__(self, stem, title):
        self.stem = stem
        self.pages = []
        self.page(stem, title)

    def page(self, stem, title):
        self.page_stem = stem
        self.im = Image.new('RGB', (W, H), 'white')
        self.d = ImageDraw.Draw(self.im)
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
                    f'<title>{escape(title)}</title>', '<rect width="100%" height="100%" fill="white"/>']
        self.bounds = []

    def text(self, x, y, s, size=22, c=INK, bold=False):
        s = str(s)
        f = ImageFont.truetype(BOLD if bold else FONT, size)
        self.d.text((x, y), s, font=f, fill=c, anchor='lt')
        baseline = y - f.getbbox(s, anchor='ls')[1]
        self.svg.append(f'<text x="{x}" y="{baseline}" font-family="DejaVu Sans,sans-serif" font-size="{size}" font-weight="{"bold" if bold else "normal"}" fill="{c}">{escape(s)}</text>')
        self.bounds.append((x, y, x+f.getlength(s), y+size, s))

    def line(self, points, c=INK, width=3):
        self.d.line(points, fill=c, width=width)
        self.svg.append('<polyline points="' + ' '.join(f'{x},{y}' for x,y in points) + f'" fill="none" stroke="{c}" stroke-width="{width}"/>')

    def box(self, x, y, w, h, fill='#f6f8fc', stroke='#aebed2'):
        self.d.rectangle((x,y,x+w,y+h),fill=fill,outline=stroke,width=2)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')

    def dot(self, x, y, c=BLUE):
        self.d.ellipse((x-4,y-4,x+4,y+4),fill=c)
        self.svg.append(f'<circle cx="{x}" cy="{y}" r="4" fill="{c}"/>')

    def ground(self, x, y, net='GND'):
        for w,dy in [(12,0),(8,6),(3,12)]:
            self.line([(x-w,y+dy),(x+w,y+dy)],GRAY,2)
        if net != 'GND': self.text(x-45,y+18,net,15,GRAY)

    def resistor(self, x1, x2, y, label, c=BLUE):
        m=(x1+x2)/2
        self.line([(x1,y),(m-18,y)],c)
        self.box(m-18,y-7,36,14,'white',c)
        self.line([(m+18,y),(x2,y)],c)
        self.text(m-28,y-31,label,18,c)

    def vertical_r(self, x, y1, y2, label, c=GREEN):
        m=(y1+y2)/2
        self.line([(x,y1),(x,m-18)],c)
        self.box(x-7,m-18,14,36,'white',c)
        self.line([(x,m+18),(x,y2)],c)
        self.text(x+18,m-12,label,18,c)

    def pulldown(self, x, y, label='10k', net='GND'):
        self.dot(x,y)
        self.vertical_r(x,y+8,y+50,label,GRAY)
        self.line([(x,y),(x,y+8)],GRAY)
        self.ground(x,y+50,net)

    def cap(self, x, y, net='GND'):
        self.dot(x,y,GREEN)
        self.line([(x,y),(x,y+20)],GREEN)
        self.line([(x-12,y+20),(x+12,y+20)],GREEN,2)
        self.line([(x-12,y+27),(x+12,y+27)],GREEN,2)
        self.line([(x,y+27),(x,y+50)],GREEN)
        self.ground(x,y+50,net)
        self.text(x+17,y+16,'100nF',18,GREEN)

    def finish_page(self):
        for x1,y1,x2,y2,s in self.bounds:
            if x1<0 or y1<0 or x2>W-15 or y2>H-10:
                raise ValueError(f'Text outside page: {s}')
        self.svg.append('</svg>')
        (OUT/(self.page_stem+'.svg')).write_text('\n'.join(self.svg)+'\n')
        self.im.save(OUT/(self.page_stem+'.png'))
        self.pages.append(self.page_stem)
        for suffix in ('.svg','.png'): print(OUT/(self.page_stem+suffix))

    def save(self):
        with tempfile.TemporaryDirectory(prefix='bts7960-pdf-') as directory:
            pages = []
            for stem in self.pages:
                page = str(Path(directory) / (stem + '.pdf'))
                subprocess.run(['rsvg-convert', '--format', 'pdf', '--dpi-x', '144', '--dpi-y', '144',
                                '--output', page, str(OUT / (stem + '.svg'))], check=True)
                pages.append(page)
            target = OUT / (self.stem + '.pdf')
            if len(pages) == 1:
                target.write_bytes(Path(pages[0]).read_bytes())
            else:
                subprocess.run(['pdfunite', *pages, str(target)], check=True)
        print(OUT/(self.stem+'.pdf'))

def notes(d,x,y,w,h,title,rows,c=INK,size=21):
    d.box(x,y,w,h,'#fff8ea' if c==ORANGE else '#f6f8fc')
    d.text(x+20,y+17,title,26,c,True)
    for i,s in enumerate(rows): d.text(x+20,y+65+i*37,s,size)

def module(d,top,axis,ys,vcc,gnd):
    x=1450
    d.box(x,top,530,325)
    d.text(x+180,top+15,f'IBT-2: {axis}',26,bold=True)
    d.text(x+180,top+51,'BTS7960 / BTN7960',20,GRAY)
    for yy,label in zip(ys+[ys[-1]+25],['1 RPWM','2 LPWM','3 R_EN','4 L_EN']):
        d.text(x+15,yy-14,label,21,BLUE)
    d.text(x+180,top+212,'Встроенный буфер сохранён',18,GRAY)
    d.text(x+180,top+245,'5 R_IS / 6 L_IS: NC',19,GRAY)
    d.text(x+15,top+285,f'7 VCC: {vcc}',18,GREEN)
    d.text(x+270,top+285,f'8 GND: {gnd}',18,GREEN)
    d.text(x+270,top+307,f'B−: {gnd}',15,GREEN)
    power_y=top-36 if axis=='AZ' else top
    d.line([(1880,top),(1880,power_y),(2300,power_y)],ORANGE)
    d.box(2050,power_y-9,60,18,'white',ORANGE)
    d.text(2055,power_y-40,'F1' if axis=='AZ' else 'F2',20,ORANGE)
    d.text(2250,power_y-38,'V_MOTOR',18,ORANGE)
    d.text(1890,top-22 if axis=='AZ' else top+10,'B+',18,ORANGE)
    d.box(2170,top+117,180,100,'#fff6e8',ORANGE)
    d.text(2190,top+134,f'M_{axis}',24,ORANGE,True)
    d.text(2190,top+172,'щёт. DC',21,ORANGE)
    for dy,label in [(147,'M+'),(191,'M−')]:
        d.text(1910,top+dy-22,label,20,ORANGE)
        d.line([(1980,top+dy),(2170,top+dy)],ORANGE)

def bleeder(d, x, y, isolated=False):
    """One load on the controller rail, shared by both ADC channels."""
    rail = '3V3_CTRL' if isolated else '3V3'
    net = 'GND_CTRL' if isolated else 'GND'
    d.text(x,y-31,rail,18,GREEN,True)
    d.line([(x,y),(x+125,y)],GREEN)
    d.resistor(x+125,x+265,y,'R35 · 500Ω',GREEN)
    d.line([(x+265,y),(x+345,y),(x+345,y+15)],GREEN)
    d.ground(x+345,y+15,net)

def feedback_frontend(d, isolated):
    """Repeat the divider and both clamps for each sensor; R35 is shared."""
    net = 'GND_CTRL' if isolated else 'GND'
    rail = '3V3_CTRL' if isolated else '3V3'
    d.box(45,1380,1250,360)
    d.text(65,1397,'Вход AZ / EL: повторить на каждую ось',25,INK,True)
    d.text(65,1450,'0...5 В',20,GREEN)
    d.line([(100,1490),(210,1490)],GREEN)
    d.resistor(210,360,1490,'6,8k · 1%',GREEN)
    d.line([(360,1490),(730,1490)],GREEN)
    d.pulldown(430,1490,'4,7k · 1%',net)
    d.cap(590,1490,net)
    d.resistor(730,850,1490,'1k',GREEN)
    d.line([(850,1490),(1160,1490)],GREEN);d.dot(910,1490,GREEN)
    d.text(1020,1460,'ADC4 / ADC5',21,GREEN,True)
    d.line([(910,1490),(910,1473)],GREEN)
    d.line([(900,1473),(920,1473),(910,1455),(900,1473)],GREEN,2)
    d.line([(898,1452),(922,1452)],GREEN,2)
    d.line([(910,1452),(910,1438),(940,1438)],GREEN)
    d.text(950,1427,rail,18,GREEN,True)
    d.line([(910,1490),(910,1507)],GREEN)
    d.line([(898,1510),(922,1510)],GREEN,2)
    d.line([(910,1513),(900,1531),(920,1531),(910,1513)],GREEN,2)
    d.line([(910,1531),(910,1550)],GREEN);d.ground(910,1550,net)
    d.text(950,1508,'BAT54S: два диода',18,GREEN)
    d.text(950,1537,'Нижний: A=GND, K=ADC',17,GREEN)
    for i, value in enumerate([
        'ADC_11db: 5 В → 2,04 В; вход 5,5 В → ≤2,275 В (1%).',
        'R35: один 500 Ом / 0,125 Вт на шину 3V3 / GND контроллера.',
        'Датчик на GND_M требует развязки; здесь только GND_CTRL.' if isolated else
        'Датчик и ограничители подключать к сигнальной земле ESP32.',
        'Для S3: 4,7 / 5,1 кОм (1%, вход ≤5,5 В); калибровать по мВ.',
    ]):
        d.text(65,1595+i*36,value,20)

def overview(isolated):
    stem='independent-pwm-bts7960'+('-isolated' if isolated else '')
    d=Drawing(stem,'IBT-2 PC817C isolated control' if isolated else 'IBT-2 direct 3.3 V control')
    d.text(45,30,'Два IBT-2: '+('развязка на шести PC817C' if isolated else 'прямое управление без внешнего буфера'),36,bold=True)
    d.text(47,82,'ESP32-S2 Mini / ESP32-S3  |  DRIVE_INTERFACE = DRIVE_BTS7960',25,BLUE)
    d.text(47,127,('PWM инвертируется программно; ENABLE аппаратно выключен без сигнала. Детали каналов - лист 2.' if isolated else
                   'VCC(7) модулей = 3,3 В. Требуется встроенный 74HC244 / 74AHC244; HCT/AHCT сюда не подходит.'),21,GRAY)
    d.box(45,245,500,685)
    d.text(65,262,'ESP32: выходы 3,3 В',26,bold=True)
    ys=[340,420,500,670,750,830]
    if isolated:
        d.box(825,245,350,685,'#fff8ea')
        d.text(845,262,'6 × PC817C, DIP4',25,bold=True)
        d.text(845,300,'1=A, 2=K, 3=E, 4=C',18,GRAY)
        for yy in range(330,925,22): d.line([(1000,yy),(1000,yy+11)],RED,2)
    else:
        d.text(690,205,'6 × 47 Ом у выходов ESP32',22,BLUE,True)
    for i,((name,s2,s3,u),yy) in enumerate(zip(CHANNELS,ys)):
        d.text(65,yy-28,name,22,BLUE,True)
        d.text(65,yy+5,f'S2: GPIO{s2} / S3: GPIO{s3}',20,GRAY)
        d.line([(545,yy),(825 if isolated else 1450,yy)],BLUE)
        if isolated:
            d.resistor(645,755,yy,'200Ω')
            d.text(842,yy-13,u,21,BLUE,True)
            d.text(1020,yy-13,'EN: HIGH' if i in (2,5) else 'PWM: INV',18,ORANGE)
            d.line([(1175,yy),(1450,yy)],BLUE)
        else:
            d.resistor(940,1040,yy,'47Ω')
            d.pulldown(725,yy)
        if i in (2,5):
            d.dot(1385,yy)
            d.line([(1385,yy),(1385,yy+25),(1450,yy+25)],BLUE)
            if not isolated: d.pulldown(1230,yy)
    bleeder(d,80,900,isolated)
    module(d,260,'AZ',ys[:3],'5V_M' if isolated else '3V3','GND_M' if isolated else 'GND★')
    module(d,590,'EL',ys[3:],'5V_M' if isolated else '3V3','GND_M' if isolated else 'GND★')
    d.text(47,962,('BTS7960_PWM_INVERTED=1: только RPWM/LPWM. ENABLE сохраняет активный HIGH.' if isolated else
                  'BTS7960_PWM_INVERTED=0: штатная полярность PWM и ENABLE. Общая земля GND★.'),23,BLUE,True)
    rows=[('Выход / назначение','S2 Mini','S3 N16R8'),('AZ: RPWM / LPWM / EN','16 / 34 / 21','16 / 15 / 21'),
          ('EL: RPWM / LPWM / EN','17 / 18 / 33','17 / 18 / 47'),('Обратная связь AZ / EL','ADC4 / ADC5','ADC4 / ADC5'),
          ('GS-232 UART RX / TX','37 / 39','44 / 43'),('HC-05 RX / TX / STATE','6 / 7 / 3','6 / 7 / 3')]
    d.box(45,1020,1080,315)
    d.text(65,1037,'Распиновка профилей',26,bold=True)
    for i,row in enumerate(rows):
        yy=1085+i*37
        for xx,s in zip([65,650,850],row): d.text(xx,yy,s,21,INK,i==0)
    if isolated:
        power=['V_MOTOR → стабилизатор 5 В → 5V_M; общий = GND_M.',
               'VCC(7) обоих IBT-2: 5V_M; GND(8) / B−: GND_M.',
               'ESP32 питается отдельно; 3V3_CTRL / GND_CTRL.',
               'Общий источник: изолированный DC/DC для ESP32.',
               'GND_CTRL и GND_M не соединять через БП, USB',
               'или датчики. F1/F2 - по мотору и проводам.']
    else:
        power=['VCC(7) обоих модулей: 3V3 контроллера.',
               'У VCC/GND каждого IBT-2: 100 нФ + 1 мкФ.',
               'B+ через F1/F2; V_MOTOR - по двигателям.',
               'B− и GND(8) идут к общей точке источника GND★.',
               'Земля ESP32 - отдельный сигнальный возврат.',
               'Силовой ток не проходит через ESP32 и его землю.']
    notes(d,1160,1020,1195,315,'Питание и земля',power,ORANGE)
    feedback_frontend(d,isolated)
    if isolated:
        extra=['PWM 1 кГц; фронты PC817 и малые duty проверить.',
               'Подтяжки входов IBT-2 должны быть ≥10 кОм.',
               'Если стоят 1 кОм - заменить входные на 10 кОм.',
               'Датчики, R_IS/L_IS, приборы не замыкают барьер.',
               'R_IS/L_IS: NC; разводка оптоканалов - лист 2.',
               'Кнопка: GPIO0 → GND_CTRL, INPUT_PULLUP.']
    else:
        extra=['R_IS / L_IS: NC; их выход не ограничен до 3,3 В.',
               'TFT: MOSI10/SCK11/MISO2, CS12/DC13/RST14;',
               'touch: CS8/IRQ9. LCD: RS8/E9, D4...D7=10...13.',
               'LCD RW=GND; логика LCD 5 В требует согласования.',
               'GPIO21 и GPIO33/47 - ENABLE, не выходы скорости.',
               'Кнопка: GPIO0 → GND, INPUT_PULLUP.']
    notes(d,1330,1380,1025,320,'Условия подключения',extra,size=20)
    d.text(47,1750,'Перед подключением двигателя проверьте уровни, питание, направления и STOP на своём оборудовании.',19,GRAY)
    d.text(47,1772,'Подключение и номиналы: инструкция рядом со схемой.',17,GRAY)
    d.finish_page()
    if isolated: details(d)
    d.save()

def opto(d,x,y,label):
    d.box(x,y,440,330,'white')
    d.text(x+110,y+15,label,25,bold=True)
    d.text(x+10,y+83,'1 A',20,BLUE)
    d.text(x+10,y+227,'2 K',20,BLUE)
    d.text(x+345,y+83,'4 C',20,ORANGE)
    d.text(x+345,y+227,'3 E',20,ORANGE)
    # LED and phototransistor symbols, with optical arrows across the barrier.
    d.line([(x,y+100),(x+105,y+100),(x+105,y+138)],BLUE)
    d.line([(x+85,y+138),(x+125,y+138),(x+105,y+168),(x+85,y+138)],BLUE,2)
    d.line([(x+85,y+172),(x+125,y+172)],BLUE,2)
    d.line([(x+105,y+172),(x+105,y+240),(x,y+240)],BLUE)
    for xx in (x+175,x+210):
        d.line([(xx,y+145),(xx+32,y+125)],GRAY,2)
        d.line([(xx+20,y+125),(xx+32,y+125),(xx+27,y+137)],GRAY,2)
    for yy in range(y+55,y+305,20): d.line([(x+235,yy),(x+235,yy+10)],RED,2)
    d.line([(x+300,y+132),(x+300,y+205)],ORANGE,3)
    d.line([(x+300,y+143),(x+345,y+115),(x+345,y+100),(x+440,y+100)],ORANGE)
    d.line([(x+300,y+192),(x+345,y+220),(x+345,y+240),(x+440,y+240)],ORANGE)
    d.line([(x+320,y+211),(x+345,y+220),(x+331,y+196)],ORANGE,2)

def details(d):
    d.page('independent-pwm-bts7960-isolated-channels','PC817C PWM and ENABLE channel circuits')
    d.text(45,30,'Развязка PC817C: два разных включения каналов',36,bold=True)
    d.text(47,82,'Лист 2. Корпус DIP4, вид сверху: 1 анод, 2 катод, 3 эмиттер, 4 коллектор.',24,BLUE)
    d.text(47,126,'Все резисторы LED: 200 Ом, 1%; GPIO: 10 кОм к GND_CTRL. Номиналы подтвердить по току и уровням.',21,GRAY)
    # Four PWM channels: common-emitter phototransistor, output pulled HIGH.
    d.box(45,195,2310,590)
    bleeder(d,1880,170,True)
    d.text(65,213,'PWM: повторить 4 раза (U1...U4)',28,BLUE,True)
    opto(d,940,305,'PC817C: PWM')
    d.text(65,380,'GPIO PWM',25,BLUE,True)
    d.line([(245,405),(940,405)],BLUE)
    d.pulldown(400,405,net='GND_CTRL')
    d.resistor(610,770,405,'200Ω')
    d.line([(940,545),(810,545),(810,590)],BLUE)
    d.ground(810,590,'GND_CTRL')
    d.line([(1380,405),(1900,405)],ORANGE)
    d.dot(1600,405,ORANGE)
    d.vertical_r(1600,285,405,'2,2k')
    d.text(1550,253,'5V_M',22,GREEN,True)
    d.text(1930,390,'RPWM / LPWM',25,ORANGE,True)
    d.line([(1380,545),(1490,545),(1490,590)],ORANGE)
    d.ground(1490,590,'GND_M')
    d.text(65,674,'GPIO HIGH → LED ON → выход LOW. GPIO LOW → LED OFF → выход HIGH.',24,BLUE,True)
    d.text(65,718,'На GPIO: duty = 1023 − требуемый duty. 0% моста → 100% GPIO; 100% моста → 0% GPIO.',23)
    # Two ENABLE channels: collector at motor-side supply, emitter drives EN.
    d.box(45,825,2310,600,'#fff8ea')
    d.text(65,843,'ENABLE: повторить 2 раза (U5 / AZ, U6 / EL)',28,ORANGE,True)
    opto(d,940,930,'PC817C: EN')
    d.text(65,1005,'GPIO ENABLE',25,BLUE,True)
    d.line([(300,1030),(940,1030)],BLUE)
    d.pulldown(400,1030,net='GND_CTRL')
    d.resistor(610,770,1030,'200Ω')
    d.line([(940,1170),(810,1170),(810,1220)],BLUE)
    d.ground(810,1220,'GND_CTRL')
    d.line([(1380,1030),(1530,1030),(1530,970)],GREEN)
    d.text(1480,937,'5V_M',22,GREEN,True)
    d.line([(1380,1170),(1950,1170)],ORANGE)
    d.pulldown(1600,1170,net='GND_M')
    d.dot(1840,1170,ORANGE)
    d.line([(1840,1170),(1840,1210),(1950,1210)],ORANGE)
    d.text(1970,1156,'R_EN (3)',24,ORANGE,True)
    d.text(1970,1197,'L_EN (4)',24,ORANGE,True)
    d.text(65,1318,'GPIO HIGH → LED ON → EN HIGH. LED OFF / ESP32 без питания → EN LOW.',24,ORANGE,True)
    d.text(65,1362,'ENABLE не инвертировать. Коллектор - к 5V_M, эмиттер - к двум EN и 10 кОм к GND_M.',23)
    notes(d,45,1465,1140,245,'Нагрузка и питание',[
        'На IBT-2: все 4 входные подтяжки ≥10 кОм.',
        'При 1 кОм заменить входные резисторы на 10 кОм.',
        'R_IS/SR не менять. C у VCC модулей: 100 нФ + 1 мкФ.',
        'LED каждого PC817C: измерить IF ≥5 мА при HIGH.',
    ],size=20)
    notes(d,1220,1465,1135,245,'Проверка 1 кГц',[
        'PC817: фронты до 18 мкс при условиях даташита.',
        '10-битная команда не гарантирует 10-битную точность.',
        'Проверить 1 / 5 / 18 / 50 / 99% и STOP на модуле.',
        'Отдельно измерить EN при потере питания ESP32.',
    ],size=20)
    d.text(47,1740,'GND_CTRL и GND_M не соединять. PC817C не имеет выводов VCC: питание 5V_M поступает в выходную цепь.',20,GRAY)
    d.text(47,1770,'Защита ADC показана на листе 1; R35 общий для AZ/EL, один на контроллер. Проверить отключение ENABLE.',19,GRAY)
    d.finish_page()

if __name__=='__main__':
    overview(False)
    overview(True)
