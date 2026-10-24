#!/usr/bin/env python3
"""Generate the self-contained, explicitly wired KiCad module installation."""
from pathlib import Path
from collections import defaultdict
import json
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'hardware/ESP32-S3-ETH-CAM-MAST'
NAME = 'ESP32-S3-ETH-CAM-MAST'
NS = uuid.UUID('0f7bbb26-5c93-4cb6-bc6d-087ff7c55cef')
SCALE = 1.016  # A 2.5-unit step is the normal 100 mil schematic grid.


def uid(key):
    return str(uuid.uuid5(NS, str(key)))


def q(s):
    return json.dumps(str(s), ensure_ascii=False)


def n(x):
    return f'{x * SCALE:.5f}'.rstrip('0').rstrip('.') or '0'


def effects(size=1.15, hide=False, justify=''):
    return f'(effects (font (size {size} {size}))' + (f' (justify {justify})' if justify else '') + (' hide' if hide else '') + ')'


class Symbol:
    def __init__(self, name, box=None, hide_numbers=False):
        self.name, self.pins, self.graphics = name, {}, []
        self.hide_numbers = hide_numbers
        if box:
            x1, y1, x2, y2 = box
            self.graphics.append(f'(rectangle (start {n(x1)} {n(-y1)}) (end {n(x2)} {n(-y2)}) (stroke (width 0.254) (type default)) (fill (type background)))')

    def pin(self, number, label, x, y, side, length=5):
        self.pins[str(number)] = (label, x, y, side, length)
        return self

    def line(self, points):
        self.graphics.append('(polyline (pts ' + ' '.join(f'(xy {n(x)} {n(-y)})' for x, y in points) + ') (stroke (width 0.254) (type default)) (fill (type none)))')

    def definition(self, full=False):
        name = f'Mast:{self.name}' if full else self.name
        pins = []
        for number, (label, x, y, side, length) in self.pins.items():
            angle = {'L': 0, 'R': 180, 'T': 270, 'B': 90}[side]
            pins.append(f'(pin passive line (at {n(x)} {n(-y)} {angle}) (length {n(length)}) (name {q(label)} {effects(.95)}) (number {q(number)} {effects(.8)}))')
        return f'''(symbol {q(name)}
          {'(pin_numbers hide)' if self.hide_numbers else ''}
          (pin_names (offset 0.8)) (in_bom yes) (on_board yes)
          (property "Reference" "U" (at 0 0 0) {effects()})
          (property "Value" {q(self.name)} (at 0 0 0) {effects()})
          (symbol {q(self.name+'_0_1')} {' '.join(self.graphics)})
          (symbol {q(self.name+'_1_1')} {' '.join(pins)}))'''


class Drawing:
    def __init__(self):
        self.symbols, self.parts, self.wires, self.items = {}, {}, [], []
        self.root_uuid = uid('root')

    def add(self, symbol, ref, value, x, y, ref_at=None, value_at=None):
        self.symbols[symbol.name] = symbol
        self.parts[ref] = (symbol, value, x, y)
        if ref_at is None: ref_at = (x, y-10)
        if value_at is None: value_at = (x, y-6)
        self.items.append(f'''(symbol (lib_id {q('Mast:'+symbol.name)}) (at {n(x)} {n(y)} 0)
          (unit 1) (in_bom yes) (on_board yes) (dnp no) (uuid {uid(ref)})
          (property "Reference" {q(ref)} (at {n(ref_at[0])} {n(ref_at[1])} 0) {effects(1.3)})
          (property "Value" {q(value)} (at {n(value_at[0])} {n(value_at[1])} 0) {effects(1.2)})
          {' '.join(f'(pin {q(p)} (uuid {uid(ref+":"+p)}))' for p in symbol.pins)}
          (instances (project {q(NAME)} (path {q('/'+self.root_uuid)} (reference {q(ref)}) (unit 1)))))''')

    def p(self, ref, pin):
        symbol, _, x, y = self.parts[ref]
        _, dx, dy, _, _ = symbol.pins[str(pin)]
        return x+dx, y+dy

    def wire(self, *points):
        for a, b in zip(points, points[1:]):
            assert a[0] == b[0] or a[1] == b[1], (a, b)
            if a != b: self.wires.append((a, b))

    def nc(self, ref, pin):
        x, y = self.p(ref, pin)
        self.items.append(f'(no_connect (at {n(x)} {n(y)}) (uuid {uid("nc:"+ref+":"+str(pin))}))')

    def text(self, x, y, text, size=1.5):
        self.items.append(f'(text {q(text)} (at {n(x)} {n(y)} 0) {effects(size, justify="left")} (uuid {uid((x,y,text))}))')

    def write(self):
        # Split at actual wire endpoints, never at unrelated crossing midpoints.
        points = {p for w in self.wires for p in w}
        points.update(self.p(ref, pin) for ref, (symbol, _, _, _) in self.parts.items() for pin in symbol.pins)
        segments = set()
        for a, b in self.wires:
            on = sorted(p for p in points if (a[0] == b[0] == p[0] and min(a[1], b[1]) <= p[1] <= max(a[1], b[1])) or
                        (a[1] == b[1] == p[1] and min(a[0], b[0]) <= p[0] <= max(a[0], b[0])))
            segments.update(tuple(sorted((p1, p2))) for p1, p2 in zip(on, on[1:]))
        degree = defaultdict(int)
        for a, b in sorted(segments):
            self.items.append(f'(wire (pts (xy {n(a[0])} {n(a[1])}) (xy {n(b[0])} {n(b[1])})) (stroke (width 0) (type default)) (uuid {uid((a,b))}))')
            degree[a] += 1; degree[b] += 1
        for (x, y), count in sorted(degree.items()):
            if count > 2:
                self.items.append(f'(junction (at {n(x)} {n(y)}) (diameter 0) (color 0 0 0 0) (uuid {uid(("junction",x,y))}))')
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / (NAME+'.kicad_sch')).write_text(f'''(kicad_sch (version 20230121) (generator g5500_waveshare_mast)
          (uuid {self.root_uuid}) (paper "A2")
          (title_block (title "Waveshare mast controller / AZ + EL") (date "2026-10-24") (rev "1.0"))
          (lib_symbols {' '.join(s.definition(True) for s in self.symbols.values())})
          {' '.join(self.items)}
          (sheet_instances (path "/" (page "1"))))\n''')
        (OUT / 'ESP32-S3-ETH-CAM-MAST.kicad_sym').write_text('(kicad_symbol_lib (version 20220914) (generator g5500_waveshare_mast)\n' + '\n'.join(s.definition() for s in self.symbols.values()) + ')\n')
        (OUT / 'sym-lib-table').write_text('(sym_lib_table (version 7)\n (lib (name "Mast") (type "KiCad") (uri "${KIPRJMOD}/ESP32-S3-ETH-CAM-MAST.kicad_sym") (options "") (descr "Mast controller module connections")))\n')
        pro = {'meta': {'filename': NAME+'.kicad_pro', 'version': 1},
               'schematic': {'page_layout_descr_file': '${KIPRJMOD}/ESP32-S3-ETH-CAM-MAST.kicad_wks'},
               'sheets': [[self.root_uuid, '']], 'text_variables': {}}
        (OUT / (NAME+'.kicad_pro')).write_text(json.dumps(pro, indent=2)+'\n')
        (OUT / 'ESP32-S3-ETH-CAM-MAST.kicad_wks').write_text('(page_layout (setup (textsize 1.5 1.5) (linewidth 0.15) (textlinewidth 0.15) (left_margin 10) (right_margin 10) (top_margin 10) (bottom_margin 10)) (rect (name "border") (start 0 0 ltcorner) (end 0 0) (linewidth 0.15)))\n')
        (OUT / '.gitignore').write_text('*.kicad_prl\n*.lck\n~*\n*-backups/\n')
        for path in [OUT / (NAME+'.kicad_sch'), OUT / 'ESP32-S3-ETH-CAM-MAST.kicad_sym']:
            path.write_text('\n'.join(line.rstrip() for line in path.read_text().splitlines()).rstrip()+'\n')
        print(f'{OUT}: {len(self.parts)} components, {len(segments)} wire segments, no net labels')


d = Drawing()

# Header and ordinary graphic notes do not connect nets.
d.text(18, 16, 'Waveshare ESP32-S3-ETH на мачте: камера, два BTS7960, общая I²C и RS-485', 2.4)
d.text(18, 22, 'Монтаж между модулями. Все внешние соединения показаны проводами.  +U по напряжению двигателей.', 1.35)

# Controller symbol: external headers, complete DVP connector and RJ45 contacts.
board = Symbol('Waveshare_ESP32_S3_ETH', (-35, -115, 35, 120), True)
cam_names = ['NC', 'AGND', 'SDA / GPIO48', 'AVDD', 'SCL / GPIO47', 'RESET', 'VSYNC / GPIO1', 'PWDN',
             'HREF / GPIO2', 'DVDD', 'DOVDD', 'Y9 / GPIO18', 'XCLK / GPIO3', 'Y8 / GPIO15', 'DGND',
             'Y7 / GPIO38', 'PCLK / GPIO39', 'Y6 / GPIO40', 'Y2 / GPIO41', 'Y5 / GPIO42',
             'Y3 / GPIO45', 'Y4 / GPIO46', 'Y1', 'Y0']
for i, name in enumerate(cam_names, 1): board.pin(f'FPC{i}', f'FPC{i} {name}', -40, 50+(i-1)*5-155, 'L')
for i in range(1, 9): board.pin(f'LAN{i}', f'RJ45 {i}', -40, 185+(i-1)*5-155, 'L')
for pin, y, label in [(17,75,'AZ RPWM'), (16,90,'AZ LPWM'), (6,105,'AZ EN / TF1-3'),
                      (44,190,'EL RPWM'), (43,205,'EL LPWM'), (7,220,'EL EN / TF1-5'),
                      (19,237.5,'UART TX / USB D-'), (20,247.5,'UART RX / USB D+'), (21,257.5,'RS485 DIR / RGB')]:
    board.pin(str(pin), f'GPIO{pin} {label}', 40, y-155, 'R')
board.pin('5V','5V / VSYS',-40,95,'L').pin('3V3','3V3',0,125,'B').pin('GND','GND',-25,125,'B')
board.pin('4','SDA GPIO4 / TF1-2',15,125,'B').pin('5','SCL GPIO5 / TF1-7',25,125,'B')
d.add(board,'A1','Waveshare ESP32-S3-ETH',210,155,(210,35),(210,43))
d.text(198, 115, 'W5500\nWi-Fi\nDVP', 1.4)
d.text(196, 170, 'microSD\nне ставить', 1.2)

cam = Symbol('DVP_Camera_24',(-30,-65,25,65), True)
for i, name in enumerate(cam_names,1): cam.pin(str(i),f'{i} {name.split(" / ")[0]}',30,50+(i-1)*5-110,'R')
d.add(cam,'A2','OV2640 / OV5640, FPC24',90,110,(85,35),(85,41))
for i in range(1,25): d.wire(d.p('A2',i),d.p('A1',f'FPC{i}'))
d.text(125, 173, 'Штатный FFC24', 1.3)

lan = Symbol('LAN_RJ45',(-20,-22.5,25,22.5))
for i in range(1,9):lan.pin(str(i),str(i),30,185+(i-1)*5-202.5,'R')
d.add(lan,'J2','LAN / коммутатор',90,202.5,(90,177.5),(90,183))
for i in range(1,9): d.wire(d.p('J2',i),d.p('A1',f'LAN{i}'))
d.text(125, 225, 'Кабель Ethernet 8 жил',1.25)

# Passive component symbols use the same grid as all module terminals.
rh = Symbol('R_H',(-2.5,-1,2.5,1),True).pin('1','',-5,0,'L',2.5).pin('2','',5,0,'R',2.5)
rv = Symbol('R_V',(-1,-2.5,1,2.5),True).pin('1','',0,-5,'T',2.5).pin('2','',0,5,'B',2.5)
cv = Symbol('C_V',hide_numbers=True).pin('1','',0,-5,'T',4).pin('2','',0,5,'B',4)
cv.line([(-2,-1),(2,-1)]);cv.line([(-2,1),(2,1)])
fv = Symbol('Fuse_V',(-1,-2.5,1,2.5),True).pin('1','',0,-5,'T',2.5).pin('2','',0,5,'B',2.5)
fv.line([(0,-2.5),(0,2.5)])
fh = Symbol('Fuse_H',(-2.5,-1,2.5,1),True).pin('1','',-5,0,'L',2.5).pin('2','',5,0,'R',2.5)
fh.line([(-2.5,0),(2.5,0)])
jp = Symbol('Service_Shunt',hide_numbers=False).pin('1','',-5,0,'L',2.5).pin('2','',5,0,'R',2.5)
jp.line([(-2.5,0),(2.5,0)])

driver = Symbol('IBT2_HC_AHC244',(-25,-35,25,35))
for pin,label,y in [('1','RPWM',-25),('2','LPWM',-10),('3','R_EN',5),('4','L_EN',15)]:driver.pin(pin,label,-30,y,'L')
driver.pin('7','VCC 3V3',-15,-40,'T').pin('8','GND',-15,40,'B')
driver.pin('B+','B+',0,-40,'T').pin('B-','B-',15,40,'B')
for pin,y in [('M+',-10),('M-',10),('5',20),('6',30)]:driver.pin(pin,{'5':'R_IS','6':'L_IS'}.get(pin,pin),30,y,'R')
rn = Symbol('Pulldown_3x10k',(-15,-5,15,10),True)
for i,x in enumerate([-10,0,10],2):
    rn.pin(str(i),str(i),x,-10,'T')
    rn.line([(x,-5),(x,0),(x+1,1),(x-1,2),(x+1,3),(x-1,4),(x,5),(x,7.5),(15,7.5)])
rn.pin('1','GND',20,7.5,'R')
motor = Symbol('DC_Motor',(-10,-15,10,15)).pin('1','M+',-15,-10,'L').pin('2','M-',-15,10,'L')
motor.graphics.append('(circle (center 0 0) (radius 7.62) (stroke (width 0.254) (type default)) (fill (type none)))')

for axis, y, start, pins in [('AZ',100,1,[17,16,6]),('EL',215,4,[44,43,7])]:
    ref='A4' if axis=='AZ' else 'A5'; network='RN1' if axis=='AZ' else 'RN2'; cy=145 if axis=='AZ' else 270
    d.add(driver,ref,'IBT-2 '+axis+' / HC-AHC244',375,y,(420,y-52.5),(420,y-47.5))
    d.add(rn,network,'3 x 10k',315,cy,(310,cy+15),(310,cy+20))
    for i,(gpio,offset) in enumerate(zip(pins,[-25,-10,5])):
        yy=y+offset;r='R'+str(start+i)
        d.add(rh,r,'47',290,yy,(290,yy-4),(290,yy+4))
        d.wire(d.p('A1',gpio),d.p(r,'1'))
        d.wire(d.p(r,'2'),d.p(ref,str(i+1)))
        x=305+i*10
        d.wire((x,yy),d.p(network,str(i+2)))
    d.wire(d.p(ref,'4'),(340,y+15),(340,y+5),d.p(ref,'3'))
    d.wire(d.p(network,'1'),(570,cy+7.5))
    d.wire(d.p(ref,'8'),(360,cy+7.5))
    d.wire(d.p(ref,'B-'),(570,y+40))
    d.wire((337.5,y-40),d.p(ref,'7'))
    d.nc(ref,'5');d.nc(ref,'6')
    m='M1' if axis=='AZ' else 'M2'
    d.add(motor,m,'Двигатель '+axis,460,y,(460,y-23),(460,y+23))
    d.text(457,y,'M',2)
    d.wire(d.p(ref,'M+'),d.p(m,'1'));d.wire(d.p(ref,'M-'),d.p(m,'2'))

# Power source, independent fused motor branches and local 5 V regulator.
source=Symbol('DC_Source',(-15,-20,10,20)).pin('1','+U',15,-10,'R').pin('2','0V',15,10,'R')
d.add(source,'PS1','БП двигателей',30,250,(27.5,220),(27.5,225))
buck=Symbol('Buck_5V',(-25,-15,25,15)).pin('IN+','IN+',-30,-10,'L').pin('IN-','IN-',-30,10,'L').pin('OUT+','5V',30,-10,'R').pin('OUT-','GND',30,10,'R')
d.add(buck,'A3','DC/DC +U -> 5V',100,250,(100,227.5),(100,232.5))
d.add(fh,'F3','По DC/DC',60,240,(60,235),(60,245))
d.wire(d.p('PS1','1'),d.p('F3','1'));d.wire(d.p('F3','2'),d.p('A3','IN+'))
d.wire((55,240),(55,30),(505,30))
for ref,x in [('F1',300),('F2',505)]:
    d.add(fv,ref,'По мотору',x,45,(x+8,42.5),(x+10,47.5));d.wire((x,30),d.p(ref,'1'))
d.wire(d.p('F1','2'),(300,55),(375,55),d.p('A4','B+'))
d.wire(d.p('F2','2'),(505,170),(375,170),d.p('A5','B+'))
d.wire(d.p('PS1','2'),d.p('A3','IN-'))
d.wire(d.p('PS1','2'),(45,390),(570,390),(570,140))
d.wire(d.p('A3','OUT-'),(170,260),(170,390))
d.wire(d.p('A3','OUT+'),(150,240),(150,250),d.p('A1','5V'))
d.wire(d.p('A1','GND'),(185,390))
d.wire(d.p('A1','3V3'),(210,295))
d.wire((75,295),(340,295));d.wire((337.5,295),(337.5,60))
d.text(18,26,'0V / GND*: общий возврат к источнику; силовой ток двигателей не проходит через плату ESP32.',1.15)
d.text(295,285,'3V3',1.1)

# One I2C bus, separate supply decoupling at both remote encoder modules.
encoder=Symbol('MT6701_Module',(-25,-20,25,20)).pin('VDD','3V3',0,-25,'T').pin('GND','GND',0,25,'B').pin('SDA','SDA / A',-30,-5,'L').pin('SCL','SCL / B',-30,10,'L')
for ref,x,address,side in [('U1',115,'0x06','AZ'),('U2',250,'0x46','EL')]:
    d.add(encoder,ref,'MT6701 '+side+' / '+address,x,340,(x+10,330),(x,340))
    d.wire((x,295),d.p(ref,'VDD'));d.wire(d.p(ref,'GND'),(x,390))
    c='C1' if side=='AZ' else 'C2';cx=150 if side=='AZ' else 290
    d.add(cv,c,'100n',cx,335,(cx+7.5,332.5),(cx+7.5,337.5))
    d.wire((cx,295),d.p(c,'1'));d.wire(d.p(c,'2'),(cx,390))
d.wire(d.p('A1','4'),(225,305),(75,305));d.wire(d.p('A1','5'),(235,310),(80,310),(260,310))
for ref,xx,yy in [('U1',75,335),('U2',200,335)]:d.wire((xx,305),(xx,yy),d.p(ref,'SDA'))
for ref,xx,yy in [('U1',80,350),('U2',205,350)]:d.wire((xx,310),(xx,yy),d.p(ref,'SCL'))
for ref,x,yy in [('R13',75,305),('R14',260,310)]:
    d.add(rv,ref,'4k7',x,300,(x-7.5,297.5),(x-7.5,302.5));d.wire((x,295),d.p(ref,'1'));d.wire(d.p(ref,'2'),(x,yy))
d.text(20,397,'Одна I2C: SDA=GPIO4, SCL=GPIO5. EL заранее настроить на 0x46.',1.3)
d.text(20,402,'TF1: GPIO4=контакт 2; GPIO5=7; EN AZ GPIO6=3; EN EL GPIO7=5. Карта microSD отсутствует.',1.15)

# Optional directly wired isolated RS485 link, with removable USB service shunts.
iso=Symbol('Isolated_RS485_Module_3V3',(-25,-30,25,30))
for pin,label,yy in [('DI','DI',-20),('RO','RO',-10),('DE','DE',0),('nRE','/RE',10)]:iso.pin(pin,label,-30,yy,'L')
iso.pin('VCC','3V3',-15,35,'B').pin('GND','GND',5,35,'B')
for pin,yy in [('A',-15),('B',0),('COM',15)]:iso.pin(pin,pin,30,yy,'R')
d.add(iso,'A6','RS485: изоляция + DC/DC',410,330,(410,288),(410,294))
for i,gpio in enumerate([19,20,21],1):
    yy=227.5+i*10;ref='JP'+str(i);xx={1:347.5,2:345,3:342.5}[i];dest=['DI','RO','DE'][i-1]
    d.add(jp,ref,'USB SERVICE',270,yy,(270,yy-3),(270,yy+3))
    d.wire(d.p('A1',gpio),d.p(ref,'1'))
    d.wire(d.p(ref,'2'),(xx,yy),(xx,300+i*10),d.p('A6',dest))
d.wire(d.p('A6','nRE'),(370,340),(370,330))
d.add(rv,'R15','10k',360,355,(367.5,352.5),(367.5,357.5))
d.wire((360,330),d.p('R15','1'));d.wire(d.p('R15','2'),(360,390))
d.wire((340,295),(340,380),(395,380),d.p('A6','VCC'))
d.wire(d.p('A6','GND'),(415,390))
remote=Symbol('RS485_Remote',(-20,-30,25,30))
for pin,yy in [('A',-15),('B',0),('COM',15)]:remote.pin(pin,pin,-25,yy,'L')
d.add(remote,'J4','HUD / USB-RS485 / шлюз',540,330,(540,288),(540,294))
for pin in ['A','B','COM']:d.wire(d.p('A6',pin),d.p('J4',pin))
for ref,x in [('R16',455),('R17',500)]:
    d.add(rv,ref,'120',x,322.5,(x+7.5,320),(x+7.5,325))
    d.wire((x,315),d.p(ref,'1'));d.wire(d.p(ref,'2'),(x,330))
d.text(443,360,'A/B: витая пара + COM',1.2)
d.text(350,402,'RS485: отдельный профиль, USB и RGB отключены. Для USB снять JP1-JP3.',1.15)
d.text(350,397,'A6: гальваническая развязка сигнала И питания; COM не соединять с GND*.',1.15)
d.write()
