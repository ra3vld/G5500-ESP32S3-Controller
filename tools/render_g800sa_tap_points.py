#!/usr/bin/env python3
"""Precisely overlay connection points on original Yaesu document pages."""
from pathlib import Path
from html import escape
import base64
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/hardware/g800sa'
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--manual',type=Path,required=True,help='Original Yaesu G-800SA/G-1000SA manual PDF')
SOURCE=parser.parse_args().manual
OUT.mkdir(exist_ok=True)
W,H=3000,2121
RED='#c93230';BLUE='#1266bd';GREEN='#087b64'

def text(x,y,value,size=29,color='#172535',bold=False):
    return f'<text x="{x}" y="{y}" font-family="DejaVu Sans,sans-serif" font-size="{size}" fill="{color}" font-weight="{700 if bold else 400}">{escape(value)}</text>'

def path(points,color,width=5,opacity=1):
    return '<polyline points="'+' '.join(f'{x},{y}' for x,y in points)+f'" fill="none" stroke="{color}" stroke-width="{width}" opacity="{opacity}" stroke-linecap="round" stroke-linejoin="round"/>'

def ring(x,y,color,r=13):
    return f'<circle cx="{x}" cy="{y}" r="{r}" fill="none" stroke="{color}" stroke-width="4"/>'

def rect(x,y,w,h,fill='#f3f7fb',stroke='#c4d4e2'):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="{fill}" stroke="{stroke}" stroke-width="2"/>'

with tempfile.TemporaryDirectory(prefix='g800sa-document-') as tmp:
    paper={}
    for page in (14,15):
        target=Path(tmp)/f'page{page}'
        subprocess.run(['pdftoppm','-f',str(page),'-l',str(page),'-r','230','-png','-singlefile',str(SOURCE),str(target)],check=True)
        paper[page]=base64.b64encode(target.with_suffix('.png').read_bytes()).decode()

def base(page,landscape=False):
    im=f'<image width="1901" height="2690" href="data:image/png;base64,{paper[page]}"/>'
    if landscape:im=f'<g transform="translate(2690 0) rotate(90)">{im}</g>'
    return im

def header(subtitle,number):
    return [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        '<rect width="100%" height="100%" fill="white"/>',
        text(70,80,'G-800SA: точки подключения датчика положения',48,bold=True),
        text(70,132,subtitle,30),
        rect(70,165,2860,102,'#fff4db','#dfbb6b'),
        text(95,207,'A → вход +. B → вход −. Отдельная земля G → GND ESP32. B с GND не соединять.',30,bold=True),
        text(95,247,'Штатные провода сохранить. A−B: 0…+5 В; A−G и B−G: 0…+15 В. Землю G проверить по C1008−.',28),
        text(2820,80,f'{number}/2',30)]

def footer():
    return [path([(70,2015),(2930,2015)],'#b1c3d2',2),
        text(70,2052,'Основа: Yaesu G-800SA/G-1000SA User Manual, © 2003 Vertex Standard. Цветные отметки добавлены.',25),
        text(70,2090,'Нумерация как на заводской схеме; это не вид разъёма со стороны пайки. Штатные провода сохраняются.',24)]

# Wiring page: the controller P6/1 has no conductor. A and B trace to J1005/1 and /2.
overlay=''.join([
    path([(1268,820),(1411,820),(1411,1067),(1581,1067),(1581,1167),(1530,1167)],RED,6,.75),
    path([(1268,798),(1433,798),(1433,1044),(1603,1044),(1603,1201),(1530,1201)],BLUE,6,.75),
    ring(1268,820,RED,12),ring(1268,798,BLUE,12),ring(1530,1167,RED,14),ring(1530,1201,BLUE,14),
    text(1395,1230,'A',32,RED,True),text(1618,1230,'B',32,BLUE,True),
    ring(1530,1133,'#b17a14',15),
    ring(1268,644,GREEN,13),text(1198,650,'G',30,GREEN,True),
])
s=header('Монтажная схема: два сигнала датчика A/B и отдельная точка земли G.',1)
s.append(f'<svg x="50" y="300" width="2100" height="1480" viewBox="300 200 2200 1550">{base(15,True)}{overlay}</svg>')
s.append(rect(2190,300,740,1080))
for x,y,value,size,color,bold in [
    (2220,355,'A — СИГНАЛ',33,RED,True),
    (2220,405,'P6/2 ↔ J1005/1',33,RED,True),
    (2220,449,'К входу +, перед R1 = 1 МОм.',27,'#172535',False),
    (2220,525,'B — ВХОД −, НЕ ЗЕМЛЯ',33,BLUE,True),
    (2220,575,'P6/3 ↔ J1005/2',33,BLUE,True),
    (2220,619,'К входу −, перед R3 = 1 МОм.',27,'#172535',False),
    (2220,695,'G — ОТДЕЛЬНАЯ ЗЕМЛЯ',33,GREEN,True),
    (2220,745,'J1005/9 по принципиальной схеме.',27,'#172535',False),
    (2220,789,'Проверить прозвонкой до C1008−.',27,'#172535',True),
    (2220,865,'До подключения измерить во всём ходе:',27,'#172535',False),
    (2220,909,'A−B: 0…+5 В; A−G, B−G: 0…+15 В.',27,'#172535',False),
    (2220,985,'Контакт 2 не соединять с GND.',29,'#172535',True),
    (2220,1030,'На P6 отдельной земли G нет.',27,'#172535',False),
    (2220,1090,'P6/1 штатно не подключён к плате.',27,'#172535',False),
    (2220,1150,'Датчик остаётся двухпроводным.',27,'#172535',False),
    (2220,1210,'G соединяется с GND ESP32.',28,GREEN,True),
    (2220,1300,'Питание каскада: 3,3 В от ESP32.',28,'#172535',True),
]:s.append(text(x,y,value,size,color,bold))
s.append(text(2210,1435,'P6 КРУПНО: контакты 2 и 3',29,bold=True))
s.append(f'<svg x="2210" y="1460" width="690" height="500" viewBox="1360 1000 310 255">{base(15,True)}{overlay}</svg>')
s.append(rect(90,1805,2020,155,'#edf5ff','#9bbbe0'))
s.append(text(120,1850,'A/B: J1005/1 и J1005/2. G: земля платы, проверенная по C1008−.',32,bold=True))
s.append(text(120,1898,'A/B соответствуют P6/2 и P6/3; отдельную землю взять внутри пульта.',29))
s.append(text(120,1939,'PB1 CNTL-UNIT FR001290D. G — контакт 9 по принципиальной схеме, лист 2.',28))
s+=footer();s.append('</svg>')
pages=[('g800sa-tap-points-wiring','\n'.join(s))]

# Circuit page, at the same J1005 interface pins, not the front indicator's J1004.
overlay2=''.join([ring(1663,609,RED,12),ring(1663,628,BLUE,12),
    ring(1663,760,GREEN,12),path([(1645,760),(1610,760)],GREEN,6,.8),
    path([(1645,609),(1592,609)],RED,6,.8),path([(1645,628),(1570,628)],BLUE,6,.8)])
s=header('Принципиальная схема: A/B/G на разъёме J1005 платы CNTL-UNIT FR001290D.',2)
s.append(f'<svg x="50" y="310" width="2100" height="1540" viewBox="160 240 1540 1130">{base(14)}{overlay2}</svg>')
s.append(rect(2190,310,740,740))
for y,value,size,color,bold in [
    (365,'A → J1005, контакт 1',34,RED,True),
    (413,'Далее провод идёт к P6/2.',29,'#172535',False),
    (491,'B → J1005, контакт 2',34,BLUE,True),
    (539,'Далее к P6/3. Не соединять с GND.',27,'#172535',False),
    (617,'G → J1005, контакт 9',34,GREEN,True),
    (665,'К GND ESP32. Проверить по C1008−.',27,'#172535',False),
    (745,'Контакты 2 и 3 — одна цепь;',28,'#172535',True),
    (789,'R1018 1,5 кОм связывает её с землёй.',27,'#172535',False),
    (849,'Не шунтировать R1018 проводом GND.',27,'#172535',True),
    (920,'A−B: 0…+5 В; A−G, B−G: 0…+15 В.',27,'#172535',False),
    (980,'Не перепутать J1005 и J1004.',28,'#172535',True),
]:s.append(text(2220,y,value,size,color,bold))
s.append(text(2210,1110,'J1005 КРУПНО: A / B / G',30,bold=True))
s.append(f'<svg x="2230" y="1140" width="650" height="800" viewBox="1550 530 195 320">{base(14)}{overlay2}</svg>')
s+=footer();s.append('</svg>');pages.append(('g800sa-tap-points-circuit','\n'.join(s)))


from schematic_svg import render_pages
render_pages([(OUT/(name+'.svg'),svgtext) for name,svgtext in pages],OUT/'g800sa-tap-points.pdf')
