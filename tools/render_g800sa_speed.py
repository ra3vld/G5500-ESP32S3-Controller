#!/usr/bin/env python3
"""G-800SA ready-made speed module, isolated command and dry-contact directions."""
from pathlib import Path
from html import escape
import argparse
import base64
import subprocess
import tempfile
from schematic_svg import render_pages

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/hardware/g800sa'
W,H=3000,2121
INK='#172535';GRAY='#526779';RED='#ba3833';BLUE='#2164b7';GREEN='#087b64';ORANGE='#a25c0a'

class Drawing:
    def __init__(self,title,subtitle,page,total=3):
        self.s=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">','<rect width="100%" height="100%" fill="white"/>']
        self.t(70,80,title,46,b=True);self.t(70,132,subtitle,29);self.t(2820,80,f'{page}/{total}',29)
    def t(self,x,y,text,size=28,c=INK,b=False):
        self.s.append(f'<text x="{x}" y="{y}" font-family="DejaVu Sans,sans-serif" font-size="{size}" fill="{c}" font-weight="{700 if b else 400}">{escape(str(text))}</text>')
    def wire(self,*points,c=INK,w=3):
        self.s.append('<polyline points="'+' '.join(f'{x},{y}' for x,y in points)+f'" fill="none" stroke="{c}" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"/>')
    def dot(self,x,y,c=INK,r=6):self.s.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{c}"/>')
    def terminal(self,x,y,c=INK):self.s.append(f'<circle cx="{x}" cy="{y}" r="8" fill="white" stroke="{c}" stroke-width="3"/>')
    def box(self,x,y,w,h,fill='#f2f6fa',stroke='#bfd0df'):
        self.s.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
    def gnd(self,x,y,net='GND_M'):
        self.wire((x,y),(x,y+15));
        for dx,dy in [(22,15),(14,24),(6,33)]:self.wire((x-dx,y+dy),(x+dx,y+dy))
        self.t(x-45,y+66,net,22,GRAY)
    def rh(self,x,y,name,value):
        self.wire((x,y),(x+25,y));self.s.append(f'<rect x="{x+25}" y="{y-14}" width="100" height="28" fill="white" stroke="{INK}" stroke-width="3"/>');self.wire((x+125,y),(x+150,y))
        self.t(x+15,y-60,name,25,b=True);self.t(x+15,y-27,value,24)
    def rv(self,x,y,name,value,labels=True):
        self.wire((x,y),(x,y+25));self.s.append(f'<rect x="{x-14}" y="{y+25}" width="28" height="90" fill="white" stroke="{INK}" stroke-width="3"/>');self.wire((x,y+115),(x,y+140))
        if labels:self.t(x+30,y+57,name,25,b=True);self.t(x+30,y+91,value,23)
    def cap(self,x,y,name,value):
        self.wire((x,y),(x,y+55));self.wire((x-23,y+55),(x+23,y+55));self.wire((x-23,y+71),(x+23,y+71));self.wire((x,y+71),(x,y+140));self.t(x+32,y+53,name,24,b=True);self.t(x+32,y+90,value,23)
    def label(self,x,y,name,c=INK):
        self.terminal(x,y,c);self.t(x+17,y-14,name,28,c,True)
    def footer(self,text):
        self.wire((70,2020),(2930,2020),c='#b4c8d7',w=2);self.t(70,2063,text,25);self.t(70,2100,'G-800SA • Штатные кнопки, реле реверса, концевики и кабель ротатора сохраняются.',24,GRAY)
    def finish(self):return '\n'.join(self.s+['</svg>'])


def power(manual):
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'page14'
        subprocess.run(['pdftoppm','-f','14','-l','14','-r','230','-png','-singlefile',str(manual),str(p)],check=True)
        image=base64.b64encode(p.with_suffix('.png').read_bytes()).decode()
    d=Drawing('G-800SA: подключение готового ШИМ-регулятора','M1: модуль DC 6–60 В для коллекторного двигателя, внешний аналоговый вход 0–5 В.',1)
    d.box(70,165,2860,90,'#fff4db','#dfbb6b');d.t(95,204,'Только моторная ветвь контактов RL2001. Питание катушек реле и измерительного узла не изменять.',30,b=True);d.t(95,239,'Модуль с общим плюсом: POWER+ = MOTOR+; POWER− = GND входа управления. Проверить без питания.',26)
    # Only the short negative return branch feeding the two relay contacts is cut.
    original=f'<image width="1901" height="2690" href="data:image/png;base64,{image}"/>'
    marks='<rect x="1018" y="1753" width="16" height="18" fill="white"/>'
    marks+='<path d="M1016 1755 L1036 1769 M1036 1755 L1016 1769" stroke="#bb3830" stroke-width="3"/>'
    for x,y,label,color,tx,ty in [(932,1724,'P',RED,945,1711),(932,1835,'N',BLUE,945,1867),(1026,1745,'K',GREEN,1043,1748)]:
        marks+=f'<circle cx="{x}" cy="{y}" r="7" fill="white" stroke="{color}" stroke-width="3"/><text x="{tx}" y="{ty}" font-family="DejaVu Sans" font-size="22" font-weight="700" fill="{color}">{label}</text>'
    d.s.append(f'<svg x="70" y="300" width="1710" height="1140" viewBox="510 1400 900 600">{original}{marks}</svg>')
    d.box(1910,310,1020,1130)
    d.t(1940,365,'M1 • 6–60 В • вход SIG 0–5 В',33,b=True)
    d.box(2070,420,780,830,'white','#879fb4')
    for y,net,label,col in [(500,'P','POWER+ / IN+',RED),(640,'N','POWER− / IN−',BLUE),(780,'P','MOTOR+ / OUT+',RED),(920,'K','MOTOR− / OUT−',GREEN)]:
        d.wire((1960,y),(2070,y),c=col,w=4);d.label(1960,y,net,col);d.t(2150,y+10,label,30,col,True)
    d.wire((2070,500),(2120,500),(2120,780),(2070,780),c=RED,w=3)
    d.t(2150,1060,'SIG ← переключатель S1 (лист 2)',29,b=True)
    d.t(2150,1130,'GND_S → N / IN−',30,BLUE,True)
    d.t(2150,1190,'+5V_REF → только штатный VR модуля',25,GRAY)
    d.t(1940,1320,'Не соединять MOTOR− с IN− проводом.',28,GREEN,True)
    d.t(1940,1370,'Это обойдёт регулировку скорости.',27)
    d.box(70,1490,1730,430)
    d.t(100,1540,'Три электрические точки на PS-UNIT FR001360E',34,b=True)
    for y,line in [(1600,'P: плюс C2003 → POWER+; MOTOR+ остаётся на той же шине P.'),(1653,'N: минус C2003 → POWER− и сигнальная земля GND_M.'),(1706,'K: отделённый минус силовых контактов RL2001 → MOTOR−.'),(1759,'Разрыв — между ветвью контактов и узлом анодов D2005/D2006.'),(1812,'C2003/C2004, D2005/D2006, F2001 и катушки остаются на своих цепях.'),(1870,'Штатный F2001 3 А сохраняется; предохранитель модуля его не заменяет.')]:d.t(100,y,line,27)
    d.box(1910,1490,1020,430)
    d.t(1940,1540,'J0 — обход на штатную скорость',32,b=True)
    d.label(2000,1660,'K',GREEN);d.wire((2000,1660),(2210,1660),c=GREEN,w=5)
    d.terminal(2210,1660);d.terminal(2470,1660);d.wire((2210,1660),(2470,1660),w=6)
    d.wire((2470,1660),(2730,1660),c=BLUE,w=5);d.label(2730,1660,'N',BLUE)
    d.t(1980,1735,'Съёмная силовая перемычка K–N, не менее 3 А.',27)
    d.t(1980,1790,'Установлена: штатная полная скорость без ESP32.',27)
    d.t(1980,1845,'Снята: скорость задаёт M1. Переставлять без питания.',26)
    d.footer('Основа силовой схемы: Yaesu G-800SA/G-1000SA User Manual, © 2003 Vertex Standard. Отметки P/N/K добавлены.')
    return d.finish()


def analog():
    d=Drawing('G-800SA: PWM контроллера → вход 0–5 В модуля','Раздельные земли: GND_ESP / J1005/9 и GND_M / IN− силового модуля.',2)
    d.box(70,165,2860,90,'#edf5ff','#9bbbe0');d.t(95,204,'Режим YAESU, PWM 20 кГц. Внешний модуль сам формирует силовой ШИМ двигателя.',30,b=True);d.t(95,240,'Сигнал скорости передаётся через ADuM1201; GND_ESP и GND_M в этом узле не соединяются.',27)
    d.label(100,500,'GPIO21 • PWM',ORANGE);d.wire((100,500),(750,500))
    d.dot(500,500);d.wire((500,500),(500,600));d.rv(500,600,'R23','100 кОм');d.gnd(500,740,'GND_ESP')
    d.box(750,320,470,440,'white','#8ba8c6');d.t(785,370,'U8 ADuM1201ARZ',30,b=True)
    for x,y,label in [(750,430,'1 VDD1'),(750,500,'3 VIB'),(750,590,'2 VOA → NC'),(750,690,'4 GND1'),(1220,430,'8 VDD2'),(1220,500,'6 VOB'),(1220,590,'7 VIA → GND_M'),(1220,690,'5 GND2')]:
        d.terminal(x,y);d.t(x+12 if x==750 else x-250,y-15,label,22)
    d.wire((750,430),(650,430),(650,350),(420,350));d.t(410,330,'+3,3 В ESP',24,RED,True)
    d.wire((1220,430),(1300,430),(1300,350),(1490,350));d.t(1340,330,'+5V_M',24,RED,True)
    d.wire((750,690),(690,690),(690,810));d.gnd(690,810,'GND_ESP')
    d.wire((1220,590),(1280,590),(1280,810));d.wire((1220,690),(1280,690));d.dot(1280,690);d.gnd(1280,810)
    d.wire((1220,500),(1340,500));d.rh(1340,500,'R20','10 кОм, 1%');d.wire((1490,500),(1620,500));d.dot(1510,500)
    d.cap(1510,580,'C20','1 мкФ');d.wire((1510,500),(1510,580),(1510,580));d.wire((1510,720),(1510,770))
    d.rh(1620,500,'R21','10 кОм, 1%');d.wire((1770,500),(2070,500));d.dot(1820,500);d.dot(1970,500)
    d.cap(1820,580,'C21','1 мкФ');d.wire((1820,500),(1820,580));d.wire((1820,720),(1820,770))
    d.rv(1970,580,'R24','300 кОм, 1%',False);d.wire((1970,500),(1970,580));d.wire((1970,720),(1970,770));d.t(1770,850,'R24',25,b=True);d.t(1770,890,'300 кОм, 1%',23)
    d.wire((1510,770),(1970,770));d.gnd(1700,770)
    d.wire((2070,430),(2070,720),(2290,575),(2070,430));d.t(2080,512,'+',30);d.t(2080,662,'−',30)
    d.t(2025,484,'3',23);d.t(2025,637,'2',23);d.t(2300,560,'1',23);d.t(2090,390,'U9A MCP6002',29,b=True)
    d.wire((2290,575),(2400,575));d.dot(2340,575);d.wire((2340,575),(2340,835),(2020,835),(2020,650),(2070,650))
    d.rh(2400,575,'R22','100 Ом');d.wire((2550,575),(2780,575));d.label(2780,575,'CMD',GREEN)
    d.t(2100,790,'U9: 8 → +5V_M; 4 → GND_M',24)
    d.t(2100,875,'U9B: 5 → GND_M; 6 соединить с 7.',24)
    d.t(95,934,'U8: C30 100 нФ между 1–4; C31 100 нФ между 8–5. U9: C32 100 нФ между 8–4, у корпусов.',27)
    # Isolated supply, all four converter and three regulator pins explicit.
    d.box(70,1000,1810,750)
    d.t(100,1055,'Питание выходной стороны: 5 → 9 → 5 В',32,b=True)
    d.label(120,1190,'+5V_ESP',RED);d.wire((120,1190),(540,1190))
    d.cap(300,1250,'C33','4,7 мкФ');d.wire((300,1190),(300,1250));d.gnd(300,1390,'GND_ESP')
    d.box(540,1130,370,390,'white','#8ba8c6');d.t(565,1170,'U11 B0509S-1WR3',25,b=True)
    d.t(555,1225,'2 +Vin',24);d.t(555,1470,'1 −Vin',24);d.t(755,1225,'4 +Vout',24);d.t(755,1470,'3 −Vout',24)
    d.wire((540,1450),(470,1450));d.gnd(470,1450,'GND_ESP')
    d.wire((910,1190),(1450,1190));d.wire((910,1450),(1820,1450));d.gnd(1820,1450)
    d.cap(1050,1250,'C34','4,7 мкФ / 25 В');d.wire((1050,1190),(1050,1250));d.wire((1050,1390),(1050,1450));d.dot(1050,1450)
    d.rv(1290,1250,'R34','820 Ом / 1 Вт');d.wire((1290,1190),(1290,1250));d.wire((1290,1390),(1290,1450));d.dot(1290,1450)
    d.box(1450,1120,260,180,'white','#8ba8c6');d.t(1470,1160,'U12 L78L05ACZ',24,b=True);d.t(1465,1225,'3 IN',22);d.t(1590,1225,'1 OUT',22);d.t(1530,1280,'2 GND',22)
    d.wire((1580,1300),(1580,1450));d.dot(1580,1450)
    d.wire((1710,1190),(1820,1190));d.terminal(1820,1190,RED);d.t(1735,1150,'+5V_M',24,RED,True)
    d.wire((1770,1190),(1770,1250));d.dot(1770,1190);d.rv(1770,1250,'R35','1 кОм',False);d.wire((1770,1390),(1770,1450));d.dot(1770,1450)
    d.t(1610,1540,'R35 1 кОм',24)
    d.t(100,1585,'C35 0,33 мкФ: U12 IN–GND. C36 0,1 мкФ: U12 OUT–GND.',27)
    d.t(100,1635,'U11: сверить распиновку 1 −IN, 2 +IN, 3 −OUT, 4 +OUT по паспорту.',26)
    d.t(100,1685,'L78L05ACZ ST, TO-92: 1 OUT, 2 GND, 3 IN. GND_M → M1 POWER−.',26)
    # A real change-over contact, no parallel connection of the two command sources.
    d.box(1930,1000,1000,750)
    d.t(1960,1055,'S1 • SPDT ON–ON • ШИМ / РУЧНОЙ',30,b=True)
    d.label(2010,1190,'CMD',GREEN);d.wire((2010,1190),(2240,1190));d.terminal(2240,1190)
    d.label(2010,1430,'VR_W',ORANGE);d.wire((2010,1430),(2240,1430));d.terminal(2240,1430)
    d.terminal(2430,1310);d.wire((2430,1310),(2240,1190));d.wire((2430,1310),(2770,1310));d.label(2770,1310,'SIG',GREEN)
    d.dot(2590,1310);d.rv(2590,1360,'R25','100 кОм');d.wire((2590,1310),(2590,1360));d.gnd(2590,1500)
    d.t(1960,1605,'VR_W — движок штатного потенциометра M1.',26)
    d.t(1960,1650,'Крайние выводы VR: +5V_REF и GND_S модуля.',26)
    d.t(1960,1695,'Переключать при STOP. Источники не запараллеливать.',25)
    d.box(70,1800,2860,155,'#edf5ff','#9bbbe0')
    d.t(100,1846,'При +5V_M = 5,00 В: CMD ≈ 4,69 × duty В. При 100% получается около 4,69 В.',30,b=True)
    d.t(100,1892,'R24 ограничивает верхний уровень. Перед подключением SIG проверить CMD ≤5 В при 0/25/50/75/100%.',28)
    d.t(100,1934,'Полная штатная скорость доступна с J0; ручная регулировка — потенциометром M1 через S1.',27)
    d.footer('Выводы: ADuM1201ARZ Analog Devices; MCP6002 Microchip; L78L05ACZ ST. Номиналы резисторов, кроме R34: 0,25 Вт. R34: 1 Вт.')
    return d.finish()


def directions():
    d=Drawing('G-800SA: команды RIGHT / LEFT и подключение ESP32','Два сухих нормально разомкнутых контакта подключаются параллельно штатным кнопкам.',3)
    d.box(70,165,2860,90,'#edf5ff','#9bbbe0');d.t(95,204,'K1/K2: реле 5 В с контактом COM/NO. Q1/Q2: BC337-40. На GPIO подаётся только логика 3,3 В.',30,b=True);d.t(95,240,'Не подавать напряжение из ESP32 на контакты S2/S3. Используются только изолированные контакты реле.',27)
    for offset,ref,gpio,button,direction in [(0,'1','GPIO16','S2','RIGHT / CW'),(1430,'2','GPIO15','S3','LEFT / CCW')]:
        x=70+offset
        d.box(x,320,1360,1120)
        d.t(x+30,375,f'Канал K{ref} • {direction}',34,b=True)
        d.label(x+40,720,gpio,BLUE);d.wire((x+40,720),(x+310,720));d.rh(x+310,720,'R'+('10' if ref=='1' else '12'),'330 Ом')
        bx=x+570;cx=x+680
        d.wire((x+460,720),(bx,720));d.dot(bx,720)
        d.rv(bx,820,'R'+('11' if ref=='1' else '13'),'10 кОм');d.wire((bx,720),(bx,820));d.gnd(bx,960,'GND_ESP')
        d.wire((bx,680),(bx,760));d.wire((bx,695),(cx,635),(cx,530));d.wire((bx,745),(cx,810),(cx,960));d.gnd(cx,960,'GND_ESP')
        d.wire((cx-30,768),(cx,810),(cx-48,798));d.t(bx-25,666,'B',23);d.t(cx+16,805,'E',23);d.t(cx+16,620,'C',23)
        d.t(x+850,860,f'Q{ref} BC337-40',27,b=True)
        d.s.append(f'<rect x="{cx-30}" y="430" width="60" height="100" fill="white" stroke="{INK}" stroke-width="3"/>')
        d.t(cx+50,485,f'K{ref} • катушка 5 В',25,b=True);d.wire((cx,430),(cx,400));d.t(cx-70,395,'+5V_ESP',24,RED,True)
        dx=x+1020
        d.wire((cx,420),(dx,420),(dx,455));d.dot(cx,420)
        # Flyback diode: cathode to +5 V, anode to transistor collector.
        d.wire((dx-22,455),(dx+22,455));d.wire((dx,455),(dx-22,500),(dx+22,500),(dx,455));d.wire((dx,500),(dx,550),(cx,550));d.dot(cx,550)
        d.t(dx+35,465,'K',22);d.t(dx+35,515,'A',22);d.t(x+1000,610,'D'+ref+' 1N4007',24)
        d.t(x+35,1100,f'K{ref}.1 • только COM и NO',29,b=True)
        d.terminal(x+340,1210);d.terminal(x+650,1210);d.wire((x+340,1210),(x+590,1160));d.wire((x+650,1210),(x+1150,1210));d.wire((x+340,1210),(x+80,1210))
        d.t(x+70,1270,f'{button}A',29,b=True);d.t(x+1050,1270,f'{button}B',29,b=True)
        d.t(x+35,1350,f'{button}A–{button}B: пара, замыкающаяся при {direction.split(" / ")[0]}.',27)
        d.t(x+35,1400,'Штатный переключатель и его остальные соединения сохранить.',24)
    d.box(70,1500,2860,430)
    d.t(100,1555,'Подключение к нашему контроллеру • начальные GPIO',34,b=True)
    columns=[(100,'Сигнал'),(1100,'ESP32-S3'),(1580,'S2 Mini'),(2170,'ESP32 WROOM/WROVER')]
    for x,label in columns:d.t(x,1615,label,28,b=True)
    for y,row in [(1670,['LEFT / CCW → Q2','15','34','25']),(1725,['RIGHT / CW → Q1','16','16','26']),(1780,['PWM → U8 ADuM1201','21','21','32']),(1835,['AZ ← U1B MCP6002','4','4','34'])]:
        for (x,_),value in zip(columns,row):d.t(x,y,value,28)
    d.t(100,1895,'Модель G-800DXA (одна AZ), привод YAESU, физический режим; EL не подключать. Без скорости: флажок 0–5 В выключен.',26)
    d.footer('Для K1/K2 подходят SRD-05VDC-SL-C; используйте распиновку своего реле. NC свободен. Питание ESP/катушек: стабилизированные 5 В.')
    return d.finish()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--manual',type=Path,required=True);args=parser.parse_args()
    pages=[('g800sa-speed-power',power(args.manual)),('g800sa-speed-command',analog()),('g800sa-directions',directions())]
    render_pages([(OUT/(name+'.svg'),svg) for name,svg in pages],OUT/'g800sa-speed-module.pdf')
