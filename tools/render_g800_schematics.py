#!/usr/bin/env python3
"""Render the two G-800DXA SVG/PNG drawings (requires Pillow and DejaVu Sans)."""
from pathlib import Path
from html import escape
from PIL import Image, ImageDraw, ImageFont
ROOT = Path(__file__).resolve().parents[1]
S=2
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
bold='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
ink='#172b46'; muted='#51627a'; blue='#2463ce'; green='#087f71'; orange='#ba650c'
def canvas(width, height):
    global W,H,im,d,svg
    W,H=width,height
    im=Image.new('RGB',(W*S,H*S),'#f5f7fb')
    d=ImageDraw.Draw(im)
    svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">', f'<rect width="{W}" height="{H}" fill="#f5f7fb"/>']
def text(x,y,t,size=20,color=ink,b=False):
    f=ImageFont.truetype(bold if b else font,size*S)
    d.text((x*S,y*S),t,font=f,fill=color,anchor='lt')
    svg.append(f'<text x="{x}" y="{y}" dominant-baseline="text-before-edge" font-family="DejaVu Sans,sans-serif" font-size="{size}" font-weight="{"bold" if b else "normal"}" fill="{color}">{escape(t)}</text>')
def line(points,color=ink,width=3,dash=False):
    # Dashes affect only explanatory outlines; all electrical wires are solid.
    d.line([(x*S,y*S) for x,y in points],fill=color,width=width*S)
    pts=' '.join(f'{x},{y}' for x,y in points)
    svg.append(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"'+(' stroke-dasharray="8 6"' if dash else '')+'/>')
def box(x,y,w,h,fill='white',stroke='#c8d3e2',r=16):
    d.rounded_rectangle((x*S,y*S,(x+w)*S,(y+h)*S),radius=r*S,fill=fill,outline=stroke,width=2*S)
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
def dot(x,y,color=ink,r=5):
    d.ellipse(((x-r)*S,(y-r)*S,(x+r)*S,(y+r)*S),fill=color)
    svg.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')
def terminal(x,y,color=ink):
    d.ellipse(((x-6)*S,(y-6)*S,(x+6)*S,(y+6)*S),fill='white',outline=color,width=2*S)
    svg.append(f'<circle cx="{x}" cy="{y}" r="6" fill="white" stroke="{color}" stroke-width="2"/>')
def ground(x,y):
    for dx,dy in [(15,0),(10,7),(4,14)]:line([(x-dx,y+dy),(x+dx,y+dy)],muted,2)
def resistor(a,b,y,label,color=green,size=18):
    mid=(a+b)/2
    line([(a,y),(mid-18,y)],color);box(mid-18,y-9,36,18,'white',color,0)
    line([(mid+18,y),(b,y)],color);text(a-5,y-36,label,size,color)
def vr(x,y1,y2,label,color=muted,size=17,tx=None):
    mid=(y1+y2)/2
    line([(x,y1),(x,mid-18)],color);box(x-9,mid-18,18,36,'white',color,0)
    line([(x,mid+18),(x,y2)],color);text(x+20 if tx is None else tx,y1+12,label,size,color)
def cap(x,y1,y2,label,color=green,tx=None):
    mid=(y1+y2)//2
    line([(x,y1),(x,mid-6)],color);line([(x-16,mid-6),(x+16,mid-6)],color)
    line([(x-16,mid+6),(x+16,mid+6)],color);line([(x,mid+6),(x,y2)],color)
    text(x+25 if tx is None else tx,mid-8,label,17,color)
def poly(points,color=ink,fill='white',width=3):
    d.polygon([(x*S,y*S) for x,y in points],fill=fill)
    line(points+[points[0]],color,width)
    svg.append(f'<polygon points="'+ ' '.join(f'{x},{y}' for x,y in points)+f'" fill="{fill}" stroke="{color}" stroke-width="{width}"/>')
def speed_selector(x,y,w,color=orange,ground_net='GND'):
    """SPDT selects the buffered PWM or a Yaesu-side potentiometer wiper."""
    box(x,y,w,380,'white','#c8d3e2',12)
    text(x+20,y+16,'S1: ШИМ / РУЧНАЯ СКОРОСТЬ · показано положение ШИМ',24,color,b=True)
    # VR1 is a voltage divider across EXT CONTROL pins 6 and 5.
    text(x+30,y+65,'EXT CONTROL pin 6 · +5 В*',20,color,b=True)
    line([(x+80,y+100),(x+480,y+100),(x+480,y+170)],color)
    terminal(x+80,y+100,color)
    box(x+471,y+170,18,40,'white',color,0)
    line([(x+480,y+210),(x+480,y+280),(x+80,y+280)],color)
    terminal(x+80,y+280,color);ground(x+480,y+280)
    text(x+30,y+243,'pin 5 · '+ground_net,20,color)
    text(x+220,y+175,'VR1 · 10 кОм',20,color,b=True)
    text(x+220,y+207,'линейный',18,color)
    line([(x+489,y+190),(x+900,y+190)],color)
    poly([(x+490,y+190),(x+506,y+183),(x+506,y+197)],color,color,2)
    text(x+620,y+207,'B · движок VR1',18,color)
    text(x+620,y+73,'A · от R22, выход ШИМ',18,color)
    line([(x+650,y+110),(x+900,y+110)],color)
    # Break-before-make, two stable positions, no centre OFF.
    line([(x+1050,y+150),(x+900,y+110)],color)
    for xx,yy in [(900,110),(900,190),(1050,150)]:terminal(x+xx,y+yy,color)
    line([(x+1050,y+150),(x+1200,y+150)],color);terminal(x+1200,y+150,color)
    text(x+1220,y+137,'EXT CONTROL pin 3 · скорость',21,color,b=True)
    text(x+1030,y+78,'S1 · SPDT ON-ON',20,color,b=True)
    text(x+1220,y+185,'Без среднего положения OFF.',19,color)
    text(x+1220,y+217,'Разрыв перед замыканием; менять при STOP.',19,color)
    text(x+20,y+310,'Вариант без VR1: вход B соединить с pin 6 для фиксированной скорости; выход ШИМ отделяется S1.',19,color)
    text(x+20,y+345,'* Для блока с +5 В на pin 6; проверить относительно pin 5. Передняя ручка SPEED действует только с передними кнопками.',18,muted)
def diode_up(x,y1,y2):
    line([(x,y1),(x,y1+12)],green)
    poly([(x-10,y1+30),(x+10,y1+30),(x,y1+12)],green)
    line([(x-13,y1+10),(x+13,y1+10)],green,2)
    line([(x,y1+30),(x,y2)],green)


def render_common():
    canvas(2000,2230)
    text(50,35,'ESP32-S3 → Yaesu G-800DXA',38,b=True)
    text(52,91,'Основные GPIO и оптокаскады — по схеме UA1CFM · добавлен выход скорости 0–5 В',23,muted)
    box(50,137,1900,54,'#fff3dc','#e4bc72',10)
    text(72,151,'ESP32-S3-DevKitC-1 N16R8. Эта распиновка скорости относится к DevKit, а не к UEDX.',22,orange,b=True)

    box(50,215,420,945)
    text(75,245,'ESP32-S3-DevKitC-1',27,b=True)
    text(75,286,'N16R8 · распиновка автора',21,muted)
    for y,label,col in [(350,'GPIO16 · RIGHT / CW',blue),(550,'GPIO15 · LEFT / CCW',blue),(810,'GPIO4 · AZ / ADC',green),(950,'GPIO21 · SPEED PWM',orange),(1080,'GND',muted)]:
        text(74,y-12,label,23,col,b=True);terminal(470,y,col)
    text(75,395,'Направления: HIGH = включить',19,muted)
    text(75,425,'LOW = выключить',19,muted)
    text(75,603,'GPIO5 · EL — не подключать',19,muted)
    text(75,633,'GPIO17/18 · UP/DOWN — NC',19,muted)
    text(75,686,'Дисплей и HC-05:',20,b=True)
    text(75,718,'сохраняется разводка автора',19,muted)
    text(75,859,'Делитель S2/S3 · резисторы 1%',18,green)
    text(75,887,'ADC_11db · калибровать по мВ',18,green)
    text(75,988,'GPIO21 — добавление форка',18,orange)
    text(75,1020,'PWM: 20 кГц, 10 бит',18,orange)
    text(75,1120,'Питание: отдельные +5 В / GND',19,b=True)

    box(1530,215,420,945)
    text(1554,245,'YAESU G-800DXA',27,b=True)
    text(1554,286,'EXT CONTROL · mini-DIN 6',21,muted,b=True)
    for y,n,label,col in [(350,'1','CW · по часовой',blue),(550,'2','CCW · против часовой',blue),(810,'4','Положение · 2–4,5 В',green),(950,'3','Скорость · 0–5 В',orange),(1080,'5','Общий · GND',muted),(1130,'6','+5 В* → VR1',orange)]:
        terminal(1530,y,col);text(1550,y-17,n,28,col,b=True);text(1594,y-10,label,19,col)
    text(1555,413,'CW: замкнуть pin 1 на pin 5',18,muted)
    text(1555,614,'CCW: замкнуть pin 2 на pin 5',18,muted)
    text(1555,867,'Диапазон зависит от OUT VOL ADJ',17,muted)
    text(1555,1006,'S1: ШИМ / движок VR1',17,orange)

    # Author direction channels: GPIO16->R6->U4, GPIO15->R5->U3.
    for y,u,r in [(350,'U4','R6'),(550,'U3','R5')]:
        line([(470,y),(635,y)],blue);resistor(635,765,y,r+' · 330 Ом',blue)
        line([(765,y),(855,y)],blue)
        dot(555,y,blue);vr(555,y+15,y+70,'10 кОм',muted,16,tx=585)
        line([(555,y),(555,y+15)],muted);line([(555,y+70),(555,y+90)],muted);ground(555,y+90)
        box(855,y-48,300,137,'#eef4ff','#a9bfdf',12)
        text(905,y-41,u+' · PC817C',20,blue,b=True)
        text(870,y-8,'1 · A',17,blue);text(870,y+43,'2 · K',17,muted)
        text(1060,y-8,'4 · C',17,blue);text(1060,y+43,'3 · E',17,muted)
        # Optical coupling is implicit inside the labelled package block.
        line([(855,y+52),(815,y+52),(815,y+85)],muted);ground(815,y+85)
        line([(1155,y),(1530,y)],blue)
        line([(1155,y+52),(1310,y+52)],muted)
    line([(1310,402),(1310,602),(1310,745)],muted)
    # Cross feedback and speed with unambiguous wire jumps.
    line([(1310,745),(1310,797),(1320,805),(1310,821),(1310,938),(1320,946),(1310,962),(1310,1080)],muted)
    dot(1310,602,muted)
    text(655,666,'CW и CCW не включать одновременно; требуется проверка reset / STOP / реверса.',18,muted)

    # Universal S2/S3 divider: 6.8k series / 4.7k shunt, both 1%.
    line([(1530,810),(1260,810)],green);resistor(1120,1260,810,'R3 · 6,8 кОм',green)
    line([(1120,810),(470,810)],green)
    vr(1040,833,890,'R11 · 4,7 кОм',green,17);line([(1040,810),(1040,833)],green);dot(1040,810,green)
    line([(1040,890),(1040,910)],green);ground(1040,910)
    cap(710,835,905,'C2 · 100 нФ',green,tx=744);line([(710,810),(710,835)],green);dot(710,810,green);ground(710,905)
    diode_up(915,742,810);dot(915,810,green);line([(915,742),(915,725)],green)
    text(867,696,'+3,3 В',18,green,b=True);text(932,754,'D2 · BAT85',17,green)
    text(570,745,'Uadc ≈ 0,409 × U(pin 4)',18,green,b=True)
    text(1135,897,'4,5 В → 1,84 В',16,green)

    # Speed overview; component detail below.
    line([(470,950),(595,950)],orange)
    for x,w,label,sub in [(595,180,'74HCT245','3,3 → 5 В'),(855,160,'2 × RC','10 кОм / 1 мкФ'),(1110,155,'MCP6002','повторитель')]:
        box(x,920,w,67,'#fff5e8','#e0b98d',10);text(x+10,928,label,19,orange,b=True);text(x+10,957,sub,15,orange)
    line([(775,950),(855,950)],orange);line([(1015,950),(1110,950)],orange)
    line([(1265,950),(1360,950)],orange)
    box(1360,925,140,90,'#fff5e8','#e0b98d',10)
    text(1375,933,'S1',19,orange,b=True);text(1375,966,'см. ниже',17,orange)
    line([(1500,950),(1530,950)],orange)
    line([(1325,990),(1360,990)],orange);text(1325,1020,'VR1',16,orange)
    line([(1430,1130),(1530,1130)],orange);text(1390,1098,'к VR1',16,orange)
    line([(470,1080),(1530,1080)],muted);dot(1310,1080,muted)
    text(631,1104,'Общая земля ESP32, согласующего каскада и EXT CONTROL pin 5',19,muted)

    # Detailed speed circuit.
    box(50,1190,1900,435,'white','#c8d3e2',12)
    text(72,1210,'ВЫХОД СКОРОСТИ: СОГЛАСОВАНИЕ 3,3 → 5 В + ФИЛЬТР + АНАЛОГОВЫЙ БУФЕР',24,orange,b=True)
    text(72,1254,'Питание U8 и U9: стабилизированные +5 В. У каждой микросхемы 100 нФ между питанием и GND.',19,muted)
    text(78,1320,'GPIO21',21,orange,b=True);text(78,1352,'PWM 3,3 В',18,orange)
    line([(80,1390),(300,1390)],orange);dot(245,1390,orange)
    vr(245,1410,1465,'R23 · 100 кОм',muted,16,tx=73);line([(245,1390),(245,1410)],muted)
    line([(245,1465),(245,1485)],muted);ground(245,1485)
    box(300,1305,270,170,'#fff5e8','#e0b98d',10)
    text(317,1320,'U8 · 74HCT245N',21,orange,b=True)
    text(317,1380,'8 · A7',18,orange);text(476,1380,'12 · B7',18,orange)
    text(317,1412,'20 VCC, 1 DIR → +5 В',16,muted)
    text(317,1440,'10 GND, 19 /OE → GND',16,muted)
    line([(570,1390),(645,1390)],orange)
    resistor(645,765,1390,'R20 · 10 кОм',orange,17)
    line([(765,1390),(910,1390)],orange);dot(812,1390,orange)
    cap(812,1406,1485,'C20',orange,tx=839);line([(812,1390),(812,1406)],orange);ground(812,1485)
    text(835,1459,'1 мкФ',16,orange)
    resistor(910,1030,1390,'R21 · 10 кОм',orange,17)
    line([(1030,1390),(1180,1390)],orange);dot(1090,1390,orange)
    cap(1090,1406,1485,'C21',orange,tx=1010);line([(1090,1390),(1090,1406)],orange);ground(1090,1485)
    text(1010,1459,'1 мкФ',16,orange)
    # Op-amp DBV: IN+3, IN-4, OUT1, V-2, V+5.
    poly([(1180,1350),(1180,1470),(1310,1410)],orange,'#fff5e8')
    text(1190,1380,'+',22,orange,b=True);text(1190,1422,'−',22,orange,b=True)
    text(1149,1365,'3',16,orange);text(1149,1423,'2',16,orange)
    text(1225,1400,'U9',18,orange,b=True)
    text(1187,1305,'MCP6002T-I/SN',20,orange,b=True)
    text(1187,1331,'SOIC-8',17,muted)
    line([(1310,1410),(1410,1410)],orange);dot(1360,1410,orange)
    line([(1360,1410),(1360,1515),(1160,1515),(1160,1440),(1180,1440)],orange)
    text(1244,1480,'1 → 2',16,orange)
    text(1188,1545,'8 → +5 В; 4 → GND',17,muted)
    text(1530,1537,'U9B: 5 → GND; 6 ↔ 7',17,muted)
    resistor(1410,1520,1410,'R22 · 100 Ом',orange,17)
    line([(1520,1410),(1870,1410)],orange);terminal(1870,1410,orange)
    text(1615,1362,'к S1.A · ШИМ',21,orange,b=True)
    text(1615,1458,'Переключатель показан ниже',17,orange)
    text(320,1512,'Неиспользуемые A1…A6, A8 → GND;',16,muted)
    text(320,1537,'B1…B6, B8 оставить свободными.',16,muted)
    text(72,1584,'Uout ≈ 5 В × duty: 25% → 1,25 В; 50% → 2,50 В; 75% → 3,75 В; 100% → около 5 В.',19,orange,b=True)

    speed_selector(50,1650,1900)
    text(52,2060,'Выход 0–5 В номинальный: крайние уровни зависят от питания, нагрузки и размаха ОУ. Измерить перед подключением.',20,muted)
    text(52,2098,'ADC S2/S3: 5 В → 2,04 В; при 5,5 В и допусках резисторов 1% не более 2,275 В. Повторить калибровку.',19,muted)
    text(52,2136,'Номера EXT CONTROL не обозначают вид со стороны пайки. Точка = соединение. Схема пока не проверена на оборудовании.',18,muted)
    text(52,2174,'Лучше подобрать под МК: S2 — R3 6,8 кОм / R11 4,7 кОм; S3 — R3 4,7 кОм / R11 5,1 кОм. Оба 1%; вход ≤5,5 В.',18,green,b=True)
    svg.append('</svg>')
    p=ROOT/'docs/hardware/g800dxa-connection'
    p.with_suffix('.svg').write_text('\n'.join(svg),encoding='utf-8')
    im.resize((W,H),Image.Resampling.LANCZOS).save(p.with_suffix('.png'))
    print(p.with_suffix('.svg'));print(p.with_suffix('.png'))

def render_isolated():
    canvas(2400,1910)
    text(50,30,'G-800DXA · гальванически развязанный выход скорости 0–5 В',38,b=True)
    text(52,89,'GPIO21 сохранён · опторазвязка PWM · фильтр и буфер на стороне Yaesu · прошивку инвертировать не нужно',25,muted)
    box(50,145,2300,52,'#fff3dc','#e4bc72',10)
    text(73,159,'Две отдельные земли: GND_ESP и GND_YAESU. Между ними не должно быть проводного соединения.',23,orange,b=True)
    box(50,225,645,970,'#eef4ff','#a9bfdf')
    box(750,225,1600,970,'#edf8f5','#a9d4c9')
    text(80,247,'СТОРОНА ESP32',28,blue,b=True)
    text(80,290,'+5V_ESP / GND_ESP',24,blue)
    text(780,247,'СТОРОНА YAESU',28,green,b=True)
    text(780,290,'+5V_ISO / GND_YAESU',24,green)
    line([(725,207),(725,1210)],orange,3,True)
    text(506,342,'ОПТИЧЕСКИЙ БАРЬЕР',21,orange,b=True)

    # Fast optical signal channel.
    box(550,400,400,300,'white','#a9bfdf',14)
    text(569,420,'U8 · 6N137-L',26,b=True)
    line([(725,470),(725,665)],orange,2,True)
    text(564,469,'2 · A',22,blue);text(564,569,'3 · K',22,blue)
    text(744,486,'6 · OUT',21,green)
    text(744,570,'8, 7 → +5V_ISO',18,green)
    text(744,603,'5 → GND_YAESU',18,green)
    text(569,651,'1 и 4 · NC',19,muted)
    text(744,655,'выход инвертирует',16,muted)

    # Local LED current driver: no load or 5 V tied back into GPIO.
    text(80,426,'+5V_ESP',22,blue,b=True)
    line([(80,470),(350,470)],blue);resistor(350,470,470,'R30 · 470 Ом',blue,20);line([(470,470),(550,470)],blue)
    text(94,565,'GPIO21 · PWM 20 кГц',23,blue,b=True)
    line([(80,730),(180,730)],blue);resistor(180,290,730,'R32 · 2,2 кОм',blue,20);line([(290,730),(330,730)],blue)
    # Standard NPN symbol: base at left, collector above, emitter below.
    # The emitter arrow points away from the base (NPN).
    d.ellipse((365*S,645*S,535*S,815*S),fill='white',outline=blue,width=3*S)
    svg.append(f'<circle cx="450" cy="730" r="85" fill="white" stroke="{blue}" stroke-width="3"/>')
    text(330,615,'Q1 · BC547B',22,blue,b=True)
    text(320,700,'2 · B',18,blue)
    text(510,622,'1 · C',16,blue)
    text(510,813,'3 · E',18,blue)
    line([(330,730),(410,730)],blue)
    line([(410,700),(410,760)],blue,5)
    line([(410,715),(500,675),(500,640)],blue)
    line([(410,745),(500,785),(500,835)],blue)
    poly([(486,779),(467,778),(473,766)],blue,blue,2)
    line([(550,570),(500,570),(500,640)],blue)
    line([(500,835),(500,872)],blue);ground(500,872)
    text(420,914,'GND_ESP',22,blue,b=True)
    dot(307,730,blue);line([(307,730),(307,755)],blue);vr(307,755,825,'R33 · 100 кОм',blue,18,tx=75)
    line([(307,825),(307,872)],blue);ground(307,872)
    text(85,970,'LED ≈ 7–8 мА;',22,blue)
    text(85,1004,'при reset база на GND.',20,blue)

    # Receive pull-up + polarity restoration.
    line([(950,500),(1045,500)],green);dot(1000,500,green)
    line([(1000,470),(1000,500)],green);vr(1000,402,470,'R31 · 1 кОм',green,18,tx=1030)
    line([(1000,375),(1000,402)],green);text(938,340,'+5V_ISO',22,green,b=True)
    poly([(1045,457),(1045,543),(1142,500)],green,'white')
    terminal(1150,500,green)
    text(1030,547,'U10 · 74HC14N',21,green,b=True)
    text(1040,581,'1A = 1; 1Y = 2',18,green)
    text(1040,613,'14 → +5V_ISO',18,green)
    text(1040,644,'7 → GND_YAESU',18,green)
    line([(1156,500),(1260,500)],green)

    # Two-pole filter and output follower.
    resistor(1260,1370,500,'R20 · 10 кОм',green,19)
    line([(1370,500),(1500,500)],green);dot(1395,500,green)
    cap(1395,522,670,'C20',green,tx=1420);line([(1395,500),(1395,522)],green)
    text(1420,619,'1 мкФ',18,green);ground(1395,670)
    resistor(1500,1610,500,'R21 · 10 кОм',green,19)
    line([(1610,500),(1765,500),(1765,472),(1800,472)],green);dot(1655,500,green)
    cap(1655,522,670,'C21',green,tx=1680);line([(1655,500),(1655,522)],green)
    text(1680,619,'1 мкФ',18,green);ground(1655,670)
    poly([(1800,432),(1800,570),(1940,500)],green,'white')
    text(1811,457,'+',24,green,b=True);text(1811,528,'−',24,green,b=True)
    text(1840,487,'U9',21,green,b=True)
    text(1762,399,'MCP6002T-I/SN',22,green,b=True)
    text(1822,584,'SOIC-8',18,green)
    text(1776,459,'3',17,green);text(1776,536,'2',17,green)
    line([(1940,500),(2055,500)],green);dot(2010,500,green)
    line([(2010,500),(2010,740),(1765,740),(1765,540),(1800,540)],green)
    text(1844,697,'OUT 1 → IN− 2',19,green)
    text(1810,776,'8 → +5V_ISO; 4 → GND_YAESU',18,green)
    text(1810,753,'U9B: 5 → GND_YAESU; 6 ↔ 7',18,green)
    resistor(2055,2155,500,'R22 · 100 Ом',green,17)
    line([(2155,500),(2315,500)],green);terminal(2315,500,green)
    text(2155,553,'к S1.A · ШИМ',20,green,b=True)
    text(2110,668,'S1 и VR1:',19,green);text(2110,700,'схема ниже',19,green)
    text(1210,842,'После 6N137 и инвертора полярность PWM совпадает с GPIO21.',22,green)
    text(1210,805,'GPIO21 LOW / LED OFF → около 0 В после установления RC.',22,green)
    text(1210,879,'25 / 50 / 75 / 100% → примерно 1,25 / 2,5 / 3,75 / 5 В.',22,green,b=True)

    # Available isolated DC/DC followed by local regulation.
    box(550,1000,400,164,'white','#a9bfdf',14)
    text(568,1018,'U11 · AM1SS-0509SJZ',22,b=True)
    text(570,1063,'2 · +Vin',19,blue);text(570,1117,'1 · −Vin',19,blue)
    text(757,1063,'4 · +Vout',19,green);text(757,1117,'3 · −Vout',19,green)
    line([(725,1060),(725,1150)],orange,2,True)
    line([(430,1076),(550,1076)],blue);line([(430,1130),(550,1130)],blue)
    text(265,1057,'+5V_ESP',20,blue,b=True);text(265,1138,'GND_ESP',20,blue,b=True)
    line([(950,1076),(1420,1076)],green)
    text(971,1017,'+9V_RAW',19,green,b=True)
    dot(1090,1076,green);line([(1090,1076),(1090,1096)],green)
    vr(1090,1096,1158,'R34 · 820 Ом',green,17,tx=1120)
    line([(1090,1158),(1090,1180)],green)
    text(1120,1144,'0,5 Вт · нагрузка',16,green)
    dot(1280,1076,green);line([(1280,1076),(1280,1096)],green)
    cap(1280,1096,1180,'C34',green,tx=1300)
    text(1300,1148,'0,33 мкФ',16,green)
    box(1420,1018,230,112,'white','#a9d4c9',10)
    text(1435,1033,'U12 · L78L05ACZ',19,green,b=True)
    text(1435,1073,'3 · IN',18,green);text(1570,1073,'1 · OUT',18,green)
    text(1500,1101,'2 · GND',16,green)
    line([(1535,1130),(1535,1180)],green)
    line([(1650,1076),(1890,1076)],green)
    dot(1740,1076,green);line([(1740,1076),(1740,1096)],green)
    cap(1740,1096,1180,'C35',green,tx=1760)
    text(1760,1148,'0,1 мкФ',16,green)
    text(1910,1060,'+5V_ISO → U8/U10/U9',21,green,b=True)
    line([(950,1130),(975,1130),(975,1180),(2320,1180)],green)
    for x in [1090,1280,1535,1740]:dot(x,1180,green)
    text(1910,1140,'GND_YAESU → pin 5',20,green,b=True)
    text(805,957,'У U8/U10/U9: по 100 нФ на питание; U11: вход/выход по 4,7 мкФ.',20,green)
    text(805,924,'Изолированное питание: 5 → 9 В, затем стабилизатор +5 В.',20,green)

    speed_selector(50,1230,2300,green,'GND_YAESU')
    box(50,1640,2300,194,'#fff3dc','#e4bc72',12)
    text(75,1661,'РАЗВЯЗКА ВСЕГО УСТРОЙСТВА',24,orange,b=True)
    text(75,1700,'Прямой делитель AZ: 6,8 кОм от сигнала / 4,7 кОм на землю, оба 1%. Он соединяет земли и обходит оптопары.',22,orange)
    text(75,1737,'Подбирать под МК: S2 — 6,8 / 4,7 кОм; S3 — 4,7 / 5,1 кОм (от сигнала / на землю). Оба 1%; вход ≤5,5 В.',22,orange)
    text(75,1774,'Для полной развязки нужен изолированный канал AZ; USB, приборы и питание не должны соединять земли.',22,orange)
    text(52,1860,'Проект схемы; на оборудовании не проверен. Диапазон 0–5 В номинальный, реальные крайние уровни нужно измерить под нагрузкой.',20,muted)
    svg.append('</svg>')
    p=ROOT/'docs/hardware/g800dxa-speed-isolated'
    p.with_suffix('.svg').write_text('\n'.join(svg),encoding='utf-8')
    im.resize((W,H),Image.Resampling.LANCZOS).save(p.with_suffix('.png'))
    print(p.with_suffix('.svg'));print(p.with_suffix('.png'))

if __name__ == "__main__":
    render_common()
    render_isolated()
