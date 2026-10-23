#!/usr/bin/env python3
"""Render and audit the G-800DXA ADuM1201 wiring; requires rsvg-convert.

Only the new ADuM drawing is generated. --check audits without writing files.
The SVG metadata contains the component pin/net map and the physical wires.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from html import escape
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/hardware/g800dxa/g800dxa-adum1201"
INK = "#17324d"
GRAY = "#5c6c7d"
COLORS = {"GND": GRAY, "3V3": "#8654a3", "VY": "#bc6512", "VD": "#bc6512",
          "PWM": "#bc6512", "PWM_ISO": "#bc6512", "RC1": "#bc6512",
          "RC2": "#bc6512", "BUF": "#bc6512", "SPEED_PWM": "#bc6512",
          "SPEED": "#bc6512", "VR_TOP": "#bc6512", "VR_WIPER": "#bc6512",
          "AZ": "#087f71", "AZ_RAW": "#087f71"}


class Drawing:
    def __init__(self):
        self.base, self.symbols, self.labels = [], [], []
        self.pins, self.bodies, self.polygons, self.wires = {}, [], [], []

    def text(self, x, y, value, size=23, color=INK, bold=False):
        self.labels.append(f'<text x="{x}" y="{y + size}" font-size="{size}" '
                           f'font-weight="{700 if bold else 400}" fill="{color}">{escape(value)}</text>')

    def rect(self, x, y, w, h, fill="white", stroke="#ccd6e2", base=False, radius=10):
        layer = self.base if base else self.symbols
        layer.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" '
                     f'fill="{fill}" stroke="{stroke}" stroke-width="2"/>')

    def stroke(self, points, color=INK, width=3, dash=False):
        pts = " ".join(f"{x},{y}" for x, y in points)
        self.symbols.append(f'<polyline points="{pts}" fill="none" stroke="{color}" '
                            f'stroke-width="{width}"' + (' stroke-dasharray="8 7"' if dash else '') + '/>')

    def pin(self, name, x, y, net):
        assert name not in self.pins, name
        self.pins[name] = {"point": (x, y), "net": net}
        return (x, y)

    def wire(self, net, *points):
        for a, b in zip(points, points[1:]):
            assert a != b and (a[0] == b[0] or a[1] == b[1]), (net, a, b)
            self.wires.append({"net": net, "a": a, "b": b})

    def ring(self, x, y, color=INK):
        self.symbols.append(f'<circle cx="{x}" cy="{y}" r="6" fill="white" '
                            f'stroke="{color}" stroke-width="2.5"/>')

    def resistor(self, name, a, b, nets, label, tx=None, ty=None):
        self.pin(name + ".1", *a, nets[0]); self.pin(name + ".2", *b, nets[1])
        mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        color = COLORS.get(nets[0], "#2463ce")
        if a[1] == b[1]:
            self.stroke([a, (mx - 28, my)], color); self.stroke([(mx + 28, my), b], color)
            self.rect(mx - 28, my - 11, 56, 22, stroke=color, radius=0)
            self.bodies.append((name, mx - 28, my - 11, mx + 28, my + 11))
            self.text(a[0] if tx is None else tx, my - 40 if ty is None else ty, label, 22, color)
        else:
            self.stroke([a, (mx, my - 28)], color); self.stroke([(mx, my + 28), b], color)
            self.rect(mx - 11, my - 28, 22, 56, stroke=color, radius=0)
            self.bodies.append((name, mx - 11, my - 28, mx + 11, my + 28))
            self.text(mx + 25 if tx is None else tx, my - 18 if ty is None else ty, label, 22, color)

    def capacitor(self, name, x, y1, y2, top_net, label, tx=None, ty=None):
        self.pin(name + ".1", x, y1, top_net); self.pin(name + ".2", x, y2, "GND")
        mid = (y1 + y2) / 2
        color = COLORS.get(top_net, INK)
        for pts in [[(x, y1), (x, mid - 9)], [(x - 19, mid - 9), (x + 19, mid - 9)],
                    [(x - 19, mid + 9), (x + 19, mid + 9)], [(x, mid + 9), (x, y2)]]:
            self.stroke(pts, color)
        self.bodies.append((name, x - 19, mid - 9, x + 19, mid + 9))
        self.text(x + 28 if tx is None else tx, mid - 10 if ty is None else ty, label, 21, color)

    def coupler(self, name, y, gpio, led_net, command, resistor):
        blue = "#2463ce"
        self.resistor(resistor, (680, y), (800, y), (gpio, led_net), resistor + " · 330 Ом")
        self.resistor("R" + ("7" if name == "U4" else "8"), (640, y), (640, y + 80),
                      (gpio, "GND"), "10 кОм", tx=660, ty=y + 87)
        self.wire(gpio, (500, y), (680, y))
        self.wire(led_net, (800, y), (1000, y))
        self.rect(1000, y - 70, 340, 200, "#f0f5ff", "#a9bddc")
        self.bodies.append((name, 1000, y - 70, 1340, y + 130))
        for pin, x, py, net in [(1, 1000, y, led_net), (2, 1000, y + 80, "GND"),
                                (4, 1340, y, command), (3, 1340, y + 80, "GND")]:
            self.pin(f"{name}.{pin}", x, py, net)
        self.text(1050, y - 53, name + " · PC817C", 27, blue, True)
        self.text(1017, y - 14, "1 · A", 21, blue); self.text(1017, y + 85, "2 · K", 21, GRAY)
        self.text(1250, y - 14, "4 · C", 21, blue); self.text(1250, y + 85, "3 · E", 21, GRAY)
        self.stroke([(1170, y - 10), (1170, y + 85)], "#a9bddc", 2, True)
        self.text(1105, y + 28, "LED →", 20, blue)
        self.wire("GND", (1000, y + 80), (560, y + 80))
        self.wire("GND", (1340, y + 80), (2690, y + 80))
        self.wire(command, (1340, y), (2770, y))


def on(p, a, b):
    return (a[0] == b[0] == p[0] and min(a[1], b[1]) <= p[1] <= max(a[1], b[1])) or (
        a[1] == b[1] == p[1] and min(a[0], b[0]) <= p[0] <= max(a[0], b[0]))


def crossing(a, b):
    """Intersection of two perpendicular closed segments, or None."""
    if (a["a"][0] == a["b"][0]) == (b["a"][0] == b["b"][0]):
        return None
    v, h = (a, b) if a["a"][0] == a["b"][0] else (b, a)
    p = (v["a"][0], h["a"][1])
    return p if on(p, a["a"], a["b"]) and on(p, b["a"], b["b"]) else None


def enters_polygon(a, b, vertices):
    """Clip a segment against the strict interior of a convex symbol."""
    edges = list(zip(vertices, vertices[1:] + vertices[:1]))
    area = sum(p[0] * q[1] - q[0] * p[1] for p, q in edges)
    sign = 1 if area > 0 else -1
    low, high = 0.0, 1.0
    for p, q in edges:
        dx, dy = q[0] - p[0], q[1] - p[1]
        start = sign * (dx * (a[1] - p[1]) - dy * (a[0] - p[0]))
        slope = sign * (dx * (b[1] - a[1]) - dy * (b[0] - a[0]))
        if slope == 0:
            if start <= 0:
                return False
        elif slope > 0:
            low = max(low, (1e-6 - start) / slope)
        else:
            high = min(high, (1e-6 - start) / slope)
    return low < high


def audit(d):
    expected = {
        "ESP.3V3": "3V3", "ESP.GND": "GND", "ESP.16": "CW_GPIO", "ESP.15": "CCW_GPIO",
        "ESP.21": "PWM", "ESP.4": "AZ", "J1.1": "CW", "J1.2": "CCW", "J1.3": "SPEED",
        "J1.4": "AZ_RAW", "J1.5": "GND", "J1.6": "VD", "YAESU.10V": "10V",
        "U4.1": "CW_LED", "U4.2": "GND", "U4.3": "GND", "U4.4": "CW",
        "U3.1": "CCW_LED", "U3.2": "GND", "U3.3": "GND", "U3.4": "CCW",
        "U8.1": "3V3", "U8.2": None, "U8.3": "PWM", "U8.4": "GND", "U8.5": "GND",
        "U8.6": "PWM_ISO", "U8.7": "GND", "U8.8": "VY",
        "U9.1": "BUF", "U9.2": "GND", "U9.3": "RC2", "U9.4": "BUF", "U9.5": "VY",
        "R6": ("CW_GPIO", "CW_LED"), "R5": ("CCW_GPIO", "CCW_LED"),
        "R7": ("CW_GPIO", "GND"), "R8": ("CCW_GPIO", "GND"), "R23": ("PWM", "GND"),
        "R20": ("PWM_ISO", "RC1"), "R21": ("RC1", "RC2"), "R22": ("BUF", "SPEED_PWM"),
        "R3": ("AZ", "AZ_RAW"), "R11": ("AZ", "GND"), "D2.A": "AZ", "D2.K": "3V3",
        "D7.A": "GND", "D7.K": "AZ", "R35": ("3V3", "GND"),
        "C20": ("RC1", "GND"), "C21": ("RC2", "GND"), "C2": ("AZ", "GND"),
        "C30": ("3V3", "GND"), "C31": ("VY", "GND"), "C32": ("VY", "GND"),
        "C33": ("VY", "GND"), "R1135": ("10V", "VD"), "R1136": ("VD", "GND"),
        "VR1.1": "VR_TOP", "VR1.2": "VR_WIPER", "VR1.3": "GND",
        "S1A.C": "SPEED", "S1A.A": "SPEED_PWM", "S1A.B": "VR_WIPER",
        "S1B.C": "VD", "S1B.A": "VY", "S1B.B": "VR_TOP",
    }
    for name, pair in list(expected.items()):
        if isinstance(pair, tuple):
            del expected[name]
            expected.update({name + ".1": pair[0], name + ".2": pair[1]})
    assert set(expected) == set(d.pins), (set(expected) - set(d.pins), set(d.pins) - set(expected))
    for name, net in expected.items():
        assert d.pins[name]["net"] == net, (name, net, d.pins[name])

    points = defaultdict(set)
    for w in d.wires:
        points[w["net"]].update((w["a"], w["b"]))
    bridges = set()
    for i, a in enumerate(d.wires):
        for b in d.wires[i + 1:]:
            p = crossing(a, b)
            if p:
                if a["net"] == b["net"]:
                    points[a["net"]].add(p)
                else:
                    assert p not in (a["a"], a["b"], b["a"], b["b"]), ("Cross-net contact", a, b, p)
                    h = a if a["a"][1] == a["b"][1] else b
                    bridges.add((p, h["net"]))
            elif a["net"] != b["net"]:
                assert not any(on(p, b["a"], b["b"]) for p in (a["a"], a["b"])), ("Collinear short", a, b)
                assert not any(on(p, a["a"], a["b"]) for p in (b["a"], b["b"])), ("Collinear short", a, b)
    for name, pin in d.pins.items():
        net, p = pin["net"], pin["point"]
        touches = [w for w in d.wires if on(p, w["a"], w["b"])]
        assert (not touches) if net is None else (touches and all(w["net"] == net for w in touches)), (name, touches)
        if net is not None:
            points[net].add(p)

    graphs = {}
    for net, nodes in points.items():
        graph = defaultdict(set)
        for w in (w for w in d.wires if w["net"] == net):
            split = sorted(p for p in nodes if on(p, w["a"], w["b"]))
            for a, b in zip(split, split[1:]):
                graph[a].add(b); graph[b].add(a)
        todo, seen = [next(iter(nodes))], set()
        while todo:
            p = todo.pop()
            if p not in seen:
                seen.add(p); todo.extend(graph[p] - seen)
        assert seen == nodes, ("Disconnected net", net, nodes - seen)
        graphs[net] = graph

    for w in d.wires:
        for name, x1, y1, x2, y2 in d.bodies:
            a, b = w["a"], w["b"]
            if a[0] == b[0]:
                through = x1 < a[0] < x2 and max(min(a[1], b[1]), y1) < min(max(a[1], b[1]), y2)
            else:
                through = y1 < a[1] < y2 and max(min(a[0], b[0]), x1) < min(max(a[0], b[0]), x2)
            assert not through, ("Wire through component", name, w)
        for name, vertices in d.polygons:
            assert not enters_polygon(w["a"], w["b"], vertices), ("Wire through component", name, w)

    for position, throw in [("PWM", "A"), ("MANUAL", "B")]:
        parent = {net: net for net in points}
        def root(net):
            while parent[net] != net:
                net = parent[net]
            return net
        for pole in ("S1A", "S1B"):
            parent[root(expected[pole + ".C"])] = root(expected[pole + "." + throw])
        assert root("VD") == root("VY" if position == "PWM" else "VR_TOP")
        assert root("VD") != root("VR_TOP" if position == "PWM" else "VY")
        assert root("SPEED") == root("SPEED_PWM" if position == "PWM" else "VR_WIPER")
        assert root("SPEED") != root("VR_WIPER" if position == "PWM" else "SPEED_PWM")

    return {"pins": len(d.pins), "nets": len(points), "wire_segments": len(d.wires),
            "insulated_crossings": len(bridges), "switch_positions_checked": ["PWM", "MANUAL"],
            "errors": []}, bridges, graphs


def build():
    d = Drawing()
    d.rect(0, 0, 3360, 2376, "white", "white", True, 0)
    d.text(55, 30, "G-800DXA · ADuM1201 + 2 × PC817C · без дополнительного питания каскада", 39, bold=True)
    d.text(57, 92, "ESP32-S3 DevKitC-1 N16R8 · PWM 20 кГц · ручной потенциометр и двухполюсный переключатель", 27, GRAY)
    d.rect(50, 145, 3260, 62, "#fff4df", "#e1bd7e", True)
    d.text(72, 162, "Прямой AZ требует общей земли: каналы управления развязаны компонентами, полной развязки устройства НЕТ.", 28, "#9b5612", True)
    d.rect(50, 250, 450, 1940, "#f5f8fc", base=True)
    d.rect(2770, 250, 540, 1940, "#f5f8fc", base=True)
    d.text(73, 265, "ESP32-S3 · DevKit", 30, bold=True)
    d.text(2790, 265, "YAESU · EXT CONTROL", 30, bold=True)
    d.text(2790, 307, "mini-DIN 6 · номера контактов", 24, GRAY)
    for name, y, net, label in [("3V3", 330, "3V3", "+3,3 В"), ("GND", 460, "GND", "GND_ESP"),
                                ("16", 700, "CW_GPIO", "GPIO16 · CW / RIGHT"),
                                ("15", 990, "CCW_GPIO", "GPIO15 · CCW / LEFT"),
                                ("21", 1320, "PWM", "GPIO21 · SPEED PWM"),
                                ("4", 1970, "AZ", "GPIO4 · AZ / ADC")]:
        d.pin("ESP." + name, 500, y, net); d.ring(500, y, COLORS.get(net, "#2463ce"))
        d.text(74, y - 23, label, 27, COLORS.get(net, "#2463ce"), True)
    for n, y, net, label in [(6, 460, "VD", "6 · VD"), (1, 700, "CW", "1 · CW"),
                               (2, 990, "CCW", "2 · CCW"), (3, 1480, "SPEED", "3 · SPEED"),
                               (4, 1970, "AZ_RAW", "4 · AZ"), (5, 2150, "GND", "5 · GND_YAESU")]:
        d.pin(f"J1.{n}", 2770, y, net); d.ring(2770, y, COLORS.get(net, "#2463ce"))
        d.text(2792, y - 42, label, 28, COLORS.get(net, "#2463ce"), True)
    d.text(2790, 735, "Замкнуть 1 на 5 → CW", 24, GRAY)
    d.text(2790, 1025, "Замкнуть 2 на 5 → CCW", 24, GRAY)
    d.text(2790, 1515, "Вход скорости 0…5 В", 24, GRAY)
    d.text(2790, 1552, "Здесь максимум ограничен VD.", 23, GRAY)
    d.text(2790, 2005, "Напряжение положения", 24, GRAY)
    d.text(2790, 2040, "Проверить OUT VOL ADJ.", 23, GRAY)
    d.text(75, 520, "Питание ESP: USB / штатные +5 В.", 23, GRAY)
    d.text(75, 560, "VD не питает ESP32.", 23, GRAY, True)
    d.text(75, 750, "HIGH включает оптопару.", 24, GRAY)
    d.text(75, 1040, "LOW снимает команду.", 24, GRAY)
    d.text(75, 1380, "20 кГц · 10 бит · без инвертора", 23, COLORS["PWM"])
    d.text(75, 1550, "EL / GPIO5, 17, 18 — NC", 24, GRAY)
    d.text(75, 1610, "Эта распиновка не для UEDX", 23, GRAY)
    d.text(75, 1650, "и не для CrowPanel.", 23, GRAY)
    d.text(75, 2040, "AZ: делитель 6,8 / 4,7 кОм", 23, COLORS["AZ"])
    d.text(75, 2080, "как в существующей схеме.", 23, COLORS["AZ"])

    # Actual ground wires are joined at the ADC return, not only by net labels.
    d.wire("GND", (500, 460), (560, 460), (560, 2150), (2770, 2150))
    d.wire("GND", (2690, 520), (2690, 2150))
    d.text(850, 2170, "Провод pin 5 ↔ GND_ESP необходим для прямого AZ; это обход всех барьеров развязки.", 24, GRAY, True)
    d.wire("3V3", (500, 330), (820, 330), (820, 1900), (1100, 1900))
    d.capacitor("C30", 690, 330, 460, "3V3", "C30 · 100 нФ", tx=850)
    d.wire("GND", (690, 460), (560, 460))
    d.wire("3V3", (820, 330), (1140, 330))
    d.resistor("R35", (1140, 330), (1140, 460), ("3V3", "GND"),
               "R35 · 500 Ом", tx=1170, ty=365)
    d.text(1170, 405, "0,125 Вт · 6,6 мА", 21, GRAY)
    d.wire("GND", (1140, 460), (690, 460))
    d.text(1540, 270, "V_YAESU · питание U8/U9 только в положении ШИМ", 25, COLORS["VY"], True)
    d.wire("VY", (1540, 1230), (1540, 330), (2380, 330), (2380, 1140), (2490, 1140))
    for name, x, label in [("C31", 1740, "C31 · 100 нФ"), ("C32", 1960, "C32 · 1 мкФ"),
                            ("C33", 2180, "C33 · 100 нФ")]:
        d.capacitor(name, x, 330, 520, "VY", label)
        d.wire("GND", (x, 520), (2690, 520))
    d.text(1600, 550, "C31 у U8; C33 у U9; C32 — локальная ёмкость.", 22, GRAY)
    d.text(1600, 590, "VD не нагружать светодиодом.", 22, GRAY)

    # Internal source on the particular Yaesu controller revision supplied by the user.
    d.text(2790, 365, "Источник внутри блока:", 21, GRAY)
    d.pin("YAESU.10V", 3100, 350, "10V"); d.text(3010, 340, "+10 В", 23, GRAY)
    d.resistor("R1135", (3100, 370), (3100, 450), ("10V", "VD"), "R1135", tx=3140, ty=370)
    d.text(3140, 405, "1,2 кОм", 21, GRAY)
    d.resistor("R1136", (3100, 480), (3100, 580), ("VD", "GND"), "R1136", tx=3140, ty=490)
    d.text(3140, 525, "1 кОм", 21, GRAY)
    d.wire("10V", (3100, 350), (3100, 370))
    d.wire("VD", (3100, 450), (3100, 480)); d.wire("VD", (3100, 460), (2640, 460), (2640, 1180))
    d.wire("GND", (3100, 580), (3100, 600), (2690, 600))
    d.text(2790, 625, "VD ≈ 4,55 В без нагрузки", 23, COLORS["VD"], True)

    d.coupler("U4", 700, "CW_GPIO", "CW_LED", "CW", "R6")
    d.coupler("U3", 990, "CCW_GPIO", "CCW_LED", "CCW", "R5")
    d.text(1610, 810, "STOP: оба PC817 выключены.", 25, "#2463ce", True)
    d.text(1610, 850, "Pin 1 и pin 2 разомкнуты.", 25, "#2463ce", True)
    d.text(1610, 890, "При отключении ESP скорость может быть HIGH.", 23, GRAY)

    # U8 is ADuM1201, so the forward PWM channel is input 3 -> output 6.
    d.rect(1040, 1200, 350, 290, "#fff7eb", "#d6b17e")
    d.bodies.append(("U8", 1040, 1200, 1390, 1490))
    d.text(1000, 1155, "U8 · ADuM1201ARZ · SOIC-8", 26, COLORS["PWM"], True)
    for n, x, y, net, label in [(1, 1040, 1230, "3V3", "1 VDD1"), (3, 1040, 1320, "PWM", "3 VIB →"),
                               (2, 1040, 1390, None, "2 VOA"), (4, 1040, 1460, "GND", "4 GND1"),
                               (8, 1390, 1230, "VY", "8 VDD2"), (6, 1390, 1320, "PWM_ISO", "→ 6 VOB"),
                               (7, 1390, 1390, "GND", "7 VIA"), (5, 1390, 1460, "GND", "5 GND2")]:
        d.pin(f"U8.{n}", x, y, net)
        d.text(x + 13 if x == 1040 else x - 129, y - 12, label, 21, COLORS.get(net, GRAY))
    d.stroke([(1215, 1215), (1215, 1475)], "#d6b17e", 2, True)
    d.stroke([(1040, 1390), (1000, 1390)], GRAY, 2)
    d.stroke([(993, 1383), (1007, 1397)], GRAY, 2); d.stroke([(993, 1397), (1007, 1383)], GRAY, 2)
    d.text(885, 1374, "NC", 22, GRAY)
    d.wire("3V3", (820, 1230), (1040, 1230)); d.wire("VY", (1390, 1230), (1540, 1230))
    d.wire("PWM", (500, 1320), (1040, 1320))
    d.resistor("R23", (640, 1320), (640, 1460), ("PWM", "GND"), "R23 · 100 кОм", tx=590, ty=1510)
    d.wire("GND", (1040, 1460), (560, 1460))
    d.wire("GND", (1390, 1390), (1430, 1390), (1430, 1770), (2690, 1770))
    d.wire("GND", (1390, 1460), (1430, 1460))
    d.text(1000, 1540, "7 → GND; 2 — NC.", 22, GRAY)
    d.text(1000, 1580, "Без питания VDD1:", 22, GRAY)
    d.text(1000, 1620, "выход 6 = HIGH.", 22, GRAY)
    d.text(1000, 1660, "ARZ: импульсы ≥ 1 мкс.", 22, GRAY)
    d.text(1000, 1700, "Края < 2% / > 98% —", 22, GRAY)
    d.text(1000, 1735, "точность не задана.", 22, GRAY)
    d.wire("PWM_ISO", (1390, 1320), (1490, 1320))
    d.resistor("R20", (1490, 1320), (1610, 1320), ("PWM_ISO", "RC1"), "R20 · 100 кОм", tx=1460)
    d.wire("RC1", (1610, 1320), (1760, 1320))
    d.resistor("R21", (1760, 1320), (1880, 1320), ("RC1", "RC2"), "R21 · 100 кОм", tx=1745)
    d.wire("RC2", (1880, 1320), (2050, 1320))
    for name, x, net in [("C20", 1660, "RC1"), ("C21", 1930, "RC2")]:
        d.wire(net, (x, 1320), (x, 1470))
        d.capacitor(name, x, 1470, 1600, net, name + " · 100 нФ",
                    tx=x + 27 if name == "C20" else 1730,
                    ty=None if name == "C20" else 1630)
        d.wire("GND", (x, 1600), (x, 1770))
    d.text(1510, 1810, "Два RC: τ = 10 мс; установление ≈ 0,2 с.", 23, COLORS["PWM"])

    d.symbols.append('<polygon points="2050,1280 2050,1460 2230,1370" fill="#fff7eb" '
                     f'stroke="{COLORS["PWM"]}" stroke-width="3"/>')
    d.polygons.append(("U9", [(2050, 1280), (2050, 1460), (2230, 1370)]))
    for n, x, y, net in [(1, 2230, 1370, "BUF"), (2, 2110, 1430, "GND"),
                           (3, 2050, 1320, "RC2"), (4, 2050, 1420, "BUF"), (5, 2110, 1310, "VY")]:
        d.pin(f"U9.{n}", x, y, net)
    d.text(1760, 1155, "U9 · TLV9001IDBVR", 26, COLORS["PWM"], True)
    d.text(1810, 1195, "SOT-23-5 · DBV", 21, GRAY)
    d.text(2020, 1287, "3", 21, COLORS["PWM"]); d.text(2063, 1300, "+", 25, COLORS["PWM"])
    d.text(2020, 1425, "4", 21, COLORS["PWM"]); d.text(2063, 1401, "−", 25, COLORS["PWM"])
    d.text(2119, 1300, "5", 20, COLORS["PWM"]); d.text(2119, 1433, "2", 20, GRAY)
    d.text(2237, 1337, "1", 20, COLORS["PWM"])
    d.wire("VY", (2380, 1100), (2110, 1100), (2110, 1310))
    d.wire("GND", (2110, 1430), (2110, 1770))
    d.wire("BUF", (2230, 1370), (2290, 1370))
    d.wire("BUF", (2260, 1370), (2260, 1570), (2010, 1570), (2010, 1420), (2050, 1420))
    d.text(2140, 1615, "1 ↔ 4 · до R22", 21, COLORS["PWM"])
    d.resistor("R22", (2290, 1370), (2390, 1370), ("BUF", "SPEED_PWM"), "R22 · 100 Ом", tx=2240, ty=1315)
    d.wire("SPEED_PWM", (2390, 1370), (2430, 1370), (2430, 1440), (2490, 1440))

    # Both poles and the pot are in the actual signal/power paths, not an inset.
    for pole, cy, nets in [("S1B", 1180, ("VD", "VY", "VR_TOP")),
                            ("S1A", 1480, ("SPEED", "SPEED_PWM", "VR_WIPER"))]:
        for suffix, x, y, net in [("C", 2640, cy, nets[0]), ("A", 2490, cy - 40, nets[1]),
                                   ("B", 2490, cy + 40, nets[2])]:
            d.pin(pole + "." + suffix, x, y, net); d.ring(x, y, COLORS["PWM"])
        d.stroke([(2640, cy), (2500, cy - 38)], COLORS["PWM"], 4)
        d.text(2500, cy - 100, pole.replace("S1", "S1."), 24, COLORS["PWM"], True)
    d.stroke([(2570, 1160), (2570, 1460)], GRAY, 2, True)
    d.text(2410, 820, "S1 · DPDT ON-ON", 21, COLORS["PWM"], True)
    d.text(2410, 860, "Разрыв перед", 20, GRAY)
    d.text(2410, 890, "замыканием", 20, GRAY)
    d.text(2410, 930, "Показано: ШИМ", 21, COLORS["PWM"], True)
    d.wire("SPEED", (2640, 1480), (2770, 1480))
    d.wire("VR_TOP", (2490, 1220), (2410, 1220), (2410, 1660), (2300, 1660))
    d.wire("VR_WIPER", (2490, 1520), (2450, 1520), (2450, 1720), (2350, 1720))
    d.resistor("VR1", (2300, 1660), (2300, 1780), ("VR_TOP", "GND"), "VR1 · 10 кОм", tx=2360, ty=1810)
    # Rename the bottom terminal to .3, then add wiper .2.
    d.pins["VR1.3"] = d.pins.pop("VR1.2")
    d.pin("VR1.2", 2350, 1720, "VR_WIPER")
    d.stroke([(2350, 1720), (2312, 1720)], COLORS["PWM"])
    d.symbols.append(f'<polygon points="2312,1720 2325,1713 2325,1727" fill="{COLORS["PWM"]}"/>')
    d.wire("GND", (2300, 1780), (2300, 1930), (2690, 1930))
    d.text(2360, 1850, "линейный · ручная скорость", 21, COLORS["PWM"])
    d.text(2480, 1640, "Менять при", 21, GRAY)
    d.text(2480, 1680, "STOP.", 21, GRAY)

    # Universal S2/S3 divider with upper/lower Schottky clamps.
    d.wire("AZ_RAW", (2770, 1970), (2270, 1970))
    d.resistor("R3", (2050, 1970), (2270, 1970), ("AZ", "AZ_RAW"), "R3 · 6,8 кОм · 1%", ty=2010)
    d.wire("AZ", (2050, 1970), (500, 1970))
    d.resistor("R11", (1660, 1970), (1660, 2150), ("AZ", "GND"), "R11 · 4,7 кОм · 1%", tx=1690)
    d.capacitor("C2", 1250, 1970, 2150, "AZ", "C2 · 100 нФ", tx=1280)
    d.pin("D2.A", 1100, 1970, "AZ"); d.pin("D2.K", 1100, 1900, "3V3")
    d.stroke([(1100, 1900), (1100, 1912)], COLORS["AZ"])
    d.stroke([(1084, 1912), (1116, 1912)], COLORS["AZ"])
    d.symbols.append(f'<polygon points="1100,1916 1086,1942 1114,1942" fill="white" stroke="{COLORS["AZ"]}" stroke-width="3"/>')
    d.stroke([(1100, 1942), (1100, 1970)], COLORS["AZ"])
    d.bodies.append(("D2", 1084, 1912, 1116, 1942))
    d.text(1140, 1903, "D2 · BAT85 · K → +3,3 В", 23, COLORS["AZ"])
    d.pin("D7.K", 900, 1970, "AZ"); d.pin("D7.A", 900, 2150, "GND")
    d.stroke([(900, 1970), (900, 2020)], COLORS["AZ"])
    d.stroke([(884, 2020), (916, 2020)], COLORS["AZ"])
    d.symbols.append(f'<polygon points="900,2024 886,2050 914,2050" fill="white" stroke="{COLORS["AZ"]}" stroke-width="3"/>')
    d.stroke([(900, 2050), (900, 2150)], COLORS["AZ"])
    d.bodies.append(("D7", 884, 2020, 916, 2050))
    d.text(930, 2075, "D7 · BAT85", 23, COLORS["AZ"])
    d.text(930, 2110, "A → GND; K → ADC", 21, COLORS["AZ"])
    d.text(1500, 1870, "Uadc ≈ 0,409 × U(pin 4) · 4,5 В → 1,84 В", 23, COLORS["AZ"], True)

    d.text(55, 2240, "VD: 10 В × 1 кОм / (1,2 кОм + 1 кОм) ≈ 4,55 В без нагрузки; Rвых ≈ 545 Ом. С U8/U9 ориентир ≈ 4,1 В до нагрузки pin 3.", 25, COLORS["VD"], True)
    d.text(55, 2287, "Измерить VD и pin 3 под нагрузкой; 0…5 В и максимальная скорость не гарантируются. U8: 2,7…5,5 В. AZ не изолирован.", 24, GRAY)
    d.text(55, 2325, "Точки — соединение; дуга — пересечение без контакта.", 23, GRAY)
    return d


def svg(d, bridges, graphs):
    wires = []
    for w in d.wires:
        a, b = w["a"], w["b"]
        wires.append(f'<path d="M{a[0]},{a[1]} L{b[0]},{b[1]}" stroke="{COLORS.get(w["net"], "#2463ce")}"/>')
    for (x, y), net in sorted(bridges):
        wires.append(f'<path d="M{x-8},{y} L{x+8},{y}" stroke="white" stroke-width="10"/>')
        wires.append(f'<path d="M{x-8},{y} Q{x},{y-16} {x+8},{y}" stroke="{COLORS.get(net, "#2463ce")}"/>')
    dots = []
    for net, graph in graphs.items():
        for (x, y), neighbors in graph.items():
            if len(neighbors) >= 3:
                dots.append(f'<circle cx="{x}" cy="{y}" r="5" fill="{COLORS.get(net, "#2463ce")}"/>')
    metadata = {"pins": d.pins, "wires": d.wires,
                "switch": {"PWM": ["S1A.C-A", "S1B.C-A"], "MANUAL": ["S1A.C-B", "S1B.C-B"]}}
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="420mm" height="297mm" viewBox="0 0 3360 2376">'
            '<title>G-800DXA: ADuM1201ARZ, two PC817C, pin 6 power, direct AZ</title>'
            '<desc>Wired functional schematic. Direct AZ joins grounds, so the complete interface is not galvanically isolated.</desc>'
            '<metadata>' + escape(json.dumps(metadata, ensure_ascii=False)) + '</metadata>'
            + ''.join(d.base) + '<g fill="none" stroke-width="3" stroke-linejoin="round" stroke-linecap="round">'
            + ''.join(wires) + '</g>' + ''.join(dots) + ''.join(d.symbols)
            + '<g font-family="DejaVu Sans,sans-serif">'
            + ''.join(d.labels) + '</g></svg>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="audit pins, wires, crossings and switches without writing")
    args = parser.parse_args()
    drawing = build()
    report, bridges, graphs = audit(drawing)
    if not args.check:
        OUT.with_suffix(".svg").write_text(svg(drawing, bridges, graphs), encoding="utf-8")
        for fmt in ("png", "pdf"):
            cmd = ["rsvg-convert", "--format", fmt, "--output", str(OUT.with_suffix("." + fmt))]
            if fmt == "png":
                cmd += ["--width", "3360", "--height", "2376"]
            subprocess.run(cmd + [str(OUT.with_suffix(".svg"))], check=True)
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
