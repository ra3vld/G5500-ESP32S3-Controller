/* Fork modifications by RA3VLD, 2026-10-12. GPLv3; see docs/upstream.md. */
#pragma once
#ifndef ARDUINO
#define PROGMEM
#endif
static constexpr char WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>G5500 Fork — управление повороткой</title>
<style>
:root{--bg:#081018;--p:#111d28;--p2:#162635;--t:#f4f7fa;--m:#96a9b7;--a:#24d18d;--s:#e74c3c;--b:#2b4557}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(#081018,#0b1721);color:var(--t);font-family:Arial,sans-serif}
.wrap{max-width:980px;margin:auto;padding:12px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}h1{font-size:26px;margin:8px 0 10px}

.nav{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 12px;padding:8px;background:#0b1720;border:1px solid #294153;border-radius:12px;position:sticky;top:0;z-index:20}
.nav-tabs{display:flex;gap:8px;flex-wrap:wrap}
.nav button,.nav a{border:0;cursor:pointer;font-family:inherit;display:inline-block;padding:9px 12px;border-radius:9px;background:#173044;color:#fff;text-decoration:none;font-weight:700;font-size:14px}
.nav button.active{background:#167a5c}.nav button:hover,.nav a:hover{background:#235b78}
.card{background:var(--p);border:1px solid var(--b);border-radius:12px;padding:12px;margin-top:10px}
.big{font-size:54px;font-weight:700}.targetbig{font-size:34px;font-weight:700;color:var(--a)}.sep{font-size:30px;color:var(--m);font-weight:400}.unit{font-size:25px;color:var(--m)}.label{color:var(--m);font-size:13px;text-transform:uppercase;letter-spacing:.08em}
.bar{height:16px;border-radius:8px;background:#071018;overflow:hidden;margin-top:12px}.fill{height:100%;background:linear-gradient(90deg,#24d18d,#33b8ff)}
.controls{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none}.btn{border:0;border-radius:14px;min-height:48px;background:#235b78;color:white;font-size:19px;font-weight:700;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none;touch-action:none;-webkit-tap-highlight-color:transparent}.btn *{pointer-events:none;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none}.movebtn{cursor:pointer}
.stop{background:var(--s);font-size:23px;min-height:60px}.good{background:#167a5c}.warn{background:#81541d}.blank{visibility:hidden}
#executeTarget{min-height:60px;white-space:pre-line;font-size:clamp(16px,4vw,23px)}
.target-controls{border-top:1px solid var(--b);margin-top:10px;padding-top:10px}.target-controls .row{align-items:flex-end}.mode-controls{display:grid;grid-template-columns:minmax(0,1fr)}.controls.az-only .blank{display:none}#hourLeft:not(:empty){margin-top:8px;color:var(--a)}#forkMessage:empty{display:none}#forkMessage{margin:10px 0;padding:10px 12px;background:var(--p2);border-radius:10px}.row{display:flex;gap:10px;flex-wrap:wrap}.field{flex:1;min-width:160px}input{width:100%;padding:9px;border-radius:10px;border:1px solid #355063;background:#07121a;color:white;font-size:16px}
.kv{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;overflow-wrap:anywhere}.kv div{background:#0b1720;padding:9px;border-radius:9px}
.status{display:inline-flex;align-items:center;gap:7px;padding:7px 10px;border-radius:999px;background:#0a151e;border:1px solid #294153;font-size:13px}
.dot{width:9px;height:9px;border-radius:50%;background:#697f8e}.dot.on{background:var(--a);box-shadow:0 0 10px #24d18d}
.rbtn{min-height:38px;font-size:13px;padding:7px 10px}.rbtn.active{outline:2px solid #24d18d}canvas{width:100%;height:260px;background:#071018;border-radius:10px;margin-top:10px}
.calhead{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:10px}
.callive{font-size:34px;font-weight:700;white-space:nowrap}.callive span:last-child{font-size:18px;color:var(--m)}
.cal-toggle{width:100%;min-height:44px;background:#162635;border:1px solid #355063;border-radius:12px;color:var(--t);font-size:18px;font-weight:700;text-align:left;padding:0 14px;cursor:pointer}
.cal-toggle .arrow{float:right;transition:transform .18s ease}
.cal-toggle.open .arrow{transform:rotate(180deg)}
.cal-body{display:none;padding-top:10px}
.cal-body.open{display:block}
.btn.done{background:#1f7a46;border-color:#35b56e;color:#fff;box-shadow:0 0 10px rgba(36,209,141,.25)}
.calreset{background:#9b2c2c;min-height:46px;font-size:15px;padding:8px 12px}
select{width:100%;padding:9px;border-radius:8px;background:#071018;color:var(--t);border:1px solid #355063;font-size:16px} .preset-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.preset-grid button{overflow-wrap:anywhere}.preset-grid button.active{border-color:#24d18d} summary{cursor:pointer;font-weight:700} button:disabled{opacity:.45;cursor:default} #forkSettings input[type=checkbox]{width:auto} @media(max-width:700px){.preset-grid{grid-template-columns:repeat(2,minmax(0,1fr))}} .seg{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin:8px 0}.seg .btn{min-height:42px;font-size:14px;border-radius:10px}.minirow{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}.minirow .btn{min-height:42px;font-size:13px;border-radius:10px;padding:6px}.calbox{background:#0b1720;border:1px solid #294153;border-radius:12px;padding:10px;margin-top:8px}.calbox h3{margin:0 0 8px;font-size:15px}.calvalues{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;font-size:12px}.calvalues div{background:#071018;border-radius:8px;padding:7px}.hide{display:none!important}.rangebadge{display:inline-block;padding:4px 8px;border-radius:999px;background:#173044;color:#fff;font-size:12px;font-weight:700}
@media(max-width:700px){.grid{grid-template-columns:1fr}.big{font-size:46px}.targetbig{font-size:30px}.sep{font-size:26px}}
[hidden]{display:none!important}.nav button:focus-visible,.nav a:focus-visible{outline:2px solid var(--a);outline-offset:2px}
.current-status{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;padding:12px;font-size:13px}.current-status span{display:block;color:var(--m);margin-bottom:5px}.current-status b{font-size:14px;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}.current-status .online{color:var(--a)}.current-status .offline{color:#e5a94f}
</style></head><body><div class="wrap">
<div style="font-size:13px;color:#96a9b7;margin:2px 0 6px">G5500 Fork 0.1.0 · оригинал &copy; UA1CFM</div>
<h1 id="modelTitle">YAESU G-5500 / ESP32</h1>
<nav class="nav" aria-label="Разделы контроллера">
<div class="nav-tabs" role="tablist" aria-label="Управление и настройки">
<button id="controlTab" class="active" role="tab" aria-controls="controlPanel" aria-selected="true" onclick="selectTab('control')">Управление</button>
<button id="settingsTab" role="tab" aria-controls="settingsPanel" aria-selected="false" tabindex="-1" onclick="selectTab('settings')">Настройки</button>
</div>
<a href="/adc">ADC TEST</a>
<a href="/resetdiag">RESET DIAG</a>
<a href="/diaglog">DIAG CONSOLE</a>
</nav>
<div id="forkMessage" role="status" aria-live="polite"></div>
<main id="controlPanel" role="tabpanel" aria-labelledby="controlTab">
<div class="grid">
<div class="card"><div class="label">Азимут — текущее / задание</div><div><span id="az" class="big">--.-</span><span class="sep"> / </span><span id="azt" class="targetbig">---</span><span class="unit">°</span></div><div class="bar"><div id="azbar" class="fill"></div></div><div>0° <span id="azmaxlabel" style="float:right">450°</span></div></div>
<div class="card"><div class="label">Элевация — текущее / задание</div><div><span id="el" class="big">--.-</span><span class="sep"> / </span><span id="elt" class="targetbig">---</span><span class="unit">°</span></div><div class="bar"><div id="elbar" class="fill"></div></div><div>0° <span style="float:right">180°</span></div></div>
</div>

<div class="card">
<div style="display:flex;justify-content:space-between;gap:10px;flex-wrap:wrap">
<div><div class="label">Движение</div><div id="motion" style="font-size:23px;font-weight:700">STOP</div></div>
<div><span class="status"><span id="caldot" class="dot"></span><span id="caltxt">Calibration</span></span></div></div>
<div id="movementButtons" class="controls" style="margin-top:10px">
<button class="btn blank">-</button><button class="btn movebtn" data-dir="U"><span>▲ UP</span></button><button class="btn blank">-</button>
<button class="btn movebtn" data-dir="L"><span>◀ LEFT</span></button><button id="executeTarget" class="btn good" onclick="targetAction()" disabled>Повернуть</button><button class="btn movebtn" data-dir="R"><span>RIGHT ▶</span></button>
<button class="btn blank">-</button><button class="btn movebtn" data-dir="D"><span>▼ DOWN</span></button><button class="btn blank">-</button>
</div>
<div class="target-controls"><div class="label">Задать угол</div><div class="row" style="margin-top:8px">
<div class="field"><label id="tazlbl">AZ 0..450°</label><input id="taz" type="number" step="0.1" min="0" max="450"></div>
<div class="field"><label>EL 0..180°</label><input id="tel" type="number" step="0.1" min="0" max="180"></div>
<button class="btn good" onclick="go()">Повернуть</button></div></div>
</div>
<div class="card"><div class="label">Предустановки</div><div id="presetButtons" class="preset-grid" style="margin-top:10px"></div>
</div>
<div class="card">
<div class="label">Режим управления</div>
<div class="mode-controls" style="margin-top:8px">
<div class="field"><label for="followMode">Следование командам</label><select id="followMode" onchange="forkPost('/api/mode',{mode:this.value})"><option value="manual">Ручной</option><option value="auto">Автоматический</option><option value="auto_1h">Авто 1 ч</option></select></div></div>
<div id="hourLeft" aria-live="polite"></div>
<div id="waitingTarget" style="margin-top:8px">Нет ожидающей цели</div>
</div>
<div class="card current-status" id="currentStatus" aria-label="Текущий статус устройства">
<div><span>Wi-Fi</span><b id="statusWifi">—</b></div>
<div><span>Bluetooth</span><b id="statusBt">—</b></div>
<div><span>Сигнал Wi-Fi</span><b id="statusSignal">—</b></div>
<div title="Свободная внутренняя RAM"><span>Свободная память</span><b id="statusMemory">—</b></div>
<div><span>Uptime</span><b id="statusUptime">—</b></div>
</div>
</main>
<section id="settingsPanel" role="tabpanel" aria-labelledby="settingsTab" hidden>
<div class="card"><details><summary>Настроить предустановки</summary>
<form id="presetEditor" onsubmit="savePreset(event)">
<div class="row"><div class="field"><label>Кнопка</label><select name="slot" onchange="fillPreset()"><option value="1">1</option><option value="2">2</option><option value="3">3</option><option value="4">4</option></select></div>
<div class="field"><label>Название</label><input name="label" maxlength="24" required></div>
<div class="field"><label>AZ, °</label><input name="az" type="number" min="0" max="450" step="0.1" required></div>
<div class="field el-setting"><label>EL, °</label><input name="el" type="number" min="0" max="180" step="0.1" value="0"></div></div>
<button class="btn" type="submit">Сохранить предустановку</button></form></details></div>
<div class="card">
<button id="caltoggle" class="cal-toggle" onclick="toggleCalMenu()">Калибровка <span class="arrow">▼</span></button>
<div id="calbody" class="cal-body">
<div class="label">АЛГОРИТМ</div>
<div class="seg">
<button id="mode2" class="btn" onclick="setCalMode('2')">GS-232 2-POINT</button>
<button id="modem" class="btn" onclick="setCalMode('multi')">MULTI-POINT</button>
</div>
<div class="label">ДИАПАЗОН АЗИМУТА — таблицы P36/P45 независимы</div>
<div class="seg">
<button id="azm36" class="btn" onclick="setAzMode(36)">P36 · 360°</button>
<button id="azm45" class="btn" onclick="setAzMode(45)">P45 · 450°</button>
</div>
<div style="display:flex;justify-content:space-between;gap:8px;align-items:center;margin:6px 0 10px">
<span>Активно: <b id="calmode">-</b></span><span id="azrange" class="rangebadge">P--</span>
</div>

<div id="panel2" class="calbox">
<h3>GS-232 2-POINT</h3>
<div class="muted" style="font-size:12px;margin-bottom:8px">AZ: O = 0°, F = конец выбранного диапазона.<span class="el-help"> EL: O2 = 0°, F2 = 180°.</span></div>
<div class="minirow">
<button id="b2_azo" class="btn warn" onclick="cal2('o')">O · AZ 0°</button>
<button id="b2_azf" class="btn warn" onclick="cal2('f')">F · AZ FULL</button>
<button class="btn calreset" onclick="resetCal('az','2')">RESET AZ</button>
<button id="b2_elo" class="btn warn" onclick="cal2('o2')">O2 · EL 0°</button>
<button id="b2_elf" class="btn warn" onclick="cal2('f2')">F2 · EL 180°</button>
<button class="btn calreset" onclick="resetCal('el','2')">RESET EL</button>
</div>
<div class="calvalues" style="margin-top:8px">
<div>AZ 0<br><b id="az2p0">-</b> mV</div>
<div>AZ FULL<br><b id="az2pf">-</b> mV</div>
<div>EL 0 / 180<br><b id="el2p0">-</b> / <b id="el2pf">-</b> mV</div>
</div>
</div>

<div id="panelm" class="calbox hide">
<div class="calhead" style="margin-top:0"><h3 style="margin:0">MULTI-POINT AZ</h3><div class="callive"><span id="calaz">--.-</span><span>°</span></div></div>
<div class="minirow" style="margin-top:8px">
<button id="b_az0" class="btn warn" onclick="cal('az0')">0°</button>
<button id="b_az90" class="btn warn" onclick="cal('az90')">90°</button>
<button id="b_az180" class="btn warn" onclick="cal('az180')">180°</button>
<button id="b_az270" class="btn warn" onclick="cal('az270')">270°</button>
<button id="b_az360" class="btn warn" onclick="cal('az360')">360°</button>
<button id="b_az450" class="btn warn" onclick="cal('az450')">450°</button>
</div>
<div class="calvalues" style="margin-top:8px">
<div>0° <b id="azp0">-</b></div><div>90° <b id="azp90">-</b></div><div>180° <b id="azp180">-</b></div>
<div>270° <b id="azp270">-</b></div><div>360° <b id="azp360">-</b></div><div id="azv450">450° <b id="azp450">-</b></div>
</div>
<button class="btn calreset" style="width:100%;margin-top:8px" onclick="resetCal('az','multi')">RESET AZ <span id="resetazrange">P--</span></button>

<div class="calhead" style="margin-top:12px"><h3 style="margin:0">MULTI-POINT EL · общая</h3><div class="callive"><span id="calel">--.-</span><span>°</span></div></div>
<div class="minirow" style="margin-top:8px">
<button id="b_el0" class="btn warn" onclick="cal('el0')">0°</button>
<button id="b_el45" class="btn warn" onclick="cal('el45')">45°</button>
<button id="b_el90" class="btn warn" onclick="cal('el90')">90°</button>
<button id="b_el135" class="btn warn" onclick="cal('el135')">135°</button>
<button id="b_el180" class="btn warn" onclick="cal('el180')">180°</button>
<button class="btn calreset" onclick="resetCal('el','multi')">RESET EL</button>
</div>
<div class="calvalues" style="margin-top:8px">
<div>0° <b id="elp0">-</b></div><div>45° <b id="elp45">-</b></div><div>90° <b id="elp90">-</b></div>
<div>135° <b id="elp135">-</b></div><div>180° <b id="elp180">-</b></div>
</div>
</div>
</div></div>
<div class="card"><details><summary>Поворотка и логгеры</summary>
<form id="forkSettings" onsubmit="saveFork(event)"><div class="row">
<div class="field"><label>Модель</label><select name="model"><option value="G5500">G-5500</option><option value="G800DXA">G-800DXA</option></select></div>
<div class="field"><label>Имя в N1MM</label><input name="rotorName" maxlength="31" required></div>
<div class="field"><label>N1MM UDP</label><input name="n1mmPort" type="number" min="1" max="65535" required></div>
<div class="field"><label for="pstPort">PstRotator UDP</label><input id="pstPort" name="pstPort" type="number" min="1" max="65534" aria-describedby="pstPortHelp" required></div>
<div class="field"><label>Положение N1MM UDP</label><input name="reportPort" type="number" min="1" max="65535" required></div>
<div class="field"><label>Дополнительный IP N1MM</label><input name="reportHost" placeholder="192.168.1.10"></div></div>
<p id="pstPortHelp" style="margin:8px 0;color:var(--m);font-size:13px;line-height:1.4">PstRotator UDP — для UR5EQF и 5MContest. В логгере выберите PstRotator/UDP; отдельно запускать программу PstRotator не требуется.<br>Log4OM 2, DXLog.net, TR4W (с 4.148.6), Swisslog и LogHX также имеют интеграцию с PstRotator, но с нашим контроллером не тестировались.</p>
<div class="hint" id="buildInfo"></div>
<div id="encoderSettings" class="hide"><label><input name="encoderInverted" type="checkbox" checked> Инверсия энкодера</label></div>
<div id="speedSettings"><label id="analogSpeedToggle"><input name="speedEnabled" type="checkbox"> Управлять скоростью через внешний модуль 0–5 В</label>
<p>AZ</p><div class="row">
<div class="field"><label>Рабочая скорость, %</label><input name="speedPercent" type="number" min="25" max="100" step="1" value="50" required></div>
<div class="field"><label>Скорость старта, %</label><input name="startSpeedPercent" type="number" min="25" max="100" step="1" value="25" required></div>
<div class="field"><label>Скорость у цели, %</label><input name="approachSpeedPercent" type="number" min="25" max="100" step="1" value="25" required></div>
<div class="field"><label>Время разгона 0–100% рабочей скорости, мс</label><input name="accelerationMs" type="number" min="100" max="10000" step="1" value="2000" required></div>
<div class="field"><label>Время торможения 100–0% рабочей скорости, мс</label><input name="decelerationMs" type="number" min="100" max="10000" step="1" value="1500" required></div>
<div class="field"><label>Начинать замедление за, °</label><input name="slowdownDegrees" type="number" min="1" max="90" step="1" value="10" required></div>
</div><div class="hint">Выход AZ: <b id="speedOutput">0%</b></div><div id="elSpeedSettings"><p>EL</p><div class="row">
<div class="field"><label>Рабочая скорость, %</label><input name="elSpeedPercent" type="number" min="1" max="100" step="1" value="100" required></div>
<div class="field"><label>Скорость старта, %</label><input name="elStartSpeedPercent" type="number" min="1" max="100" step="1" value="18" required></div>
<div class="field"><label>Скорость у цели, %</label><input name="elApproachSpeedPercent" type="number" min="1" max="100" step="1" value="18" required></div>
<div class="field"><label>Время разгона 0–100% рабочей скорости, мс</label><input name="elAccelerationMs" type="number" min="100" max="10000" step="1" value="1800" required></div>
<div class="field"><label>Время торможения 100–0% рабочей скорости, мс</label><input name="elDecelerationMs" type="number" min="100" max="10000" step="1" value="1200" required></div>
<div class="field"><label>Начинать замедление за, °</label><input name="elSlowdownDegrees" type="number" min="1" max="90" step="1" value="2" required></div>
</div><div class="hint">Выход EL: <b id="elSpeedOutput">0%</b></div></div><p class="hint">Проценты задают выход привода. Замедление начинается перед зоной остановки ±5°. Реверс: торможение и пауза 300 мс. STOP выключает привод сразу.</p></div>
<button class="btn" type="submit">Сохранить настройки</button></form></details></div>
<div class="card"><div class="label">История RSSI домашнего Wi-Fi</div>
<div class="row" style="margin-top:10px"><button class="btn rbtn active" data-r="hour">1 HOUR</button><button class="btn rbtn" data-r="day">1 DAY</button><button class="btn rbtn" data-r="week">1 WEEK</button><button class="btn rbtn" data-r="month">1 MONTH</button><button class="btn rbtn" data-r="year">1 YEAR</button></div>
<canvas id="rchart" width="900" height="260"></canvas>
<div class="kv" style="margin-top:10px"><div>MIN: <b id="rmin">-</b></div><div>MAX: <b id="rmax">-</b></div><div>AVG: <b id="ravg">-</b></div><div>Points: <b id="rcount">0</b></div></div></div>
<div class="card"><div class="label">Подключение</div><div class="kv" style="margin-top:10px">
<div>Home IP: <b id="ip">-</b></div><div>AP IP: <b id="apip">-</b></div><div>TCP rotctld: <b>4533</b></div><div>Mode: <b id="mode">-</b></div><div>Home Wi-Fi RSSI: <b id="rssi">-</b></div><div>Signal: <b id="sig">-</b></div>
<div id="btlinkrow" class="hide">Bluetooth-соединение: <b id="btlinkstate" role="status">-</b></div>
<div><span id="btlabel">Bluetooth</span>: <b id="btstate" role="status">-</b></div><div id="bttransport">-</div>
<div id="btmacrow" class="hide" style="grid-column:1/-1">Bluetooth MAC: <b id="btmac" style="font-family:monospace;user-select:all;white-space:nowrap">-</b></div></div>
<div id="bthelp" class="hint hide">Этот MAC нужен в Look4Sat. Подключение видно и без передачи команд; сопряжение в Android само по себе не означает активное соединение.</div>
<div class="field" style="margin-top:10px"><label>Wi‑Fi SSID</label><input id="ssid"></div>
<div class="field" style="margin-top:8px"><label>Wi‑Fi password</label><input id="pass" type="password"></div>
<button id="wifiSaveButton" class="btn good" style="margin-top:10px" onclick="wifi()">Save Wi‑Fi + reboot</button>
<div id="wifiMessage" class="hint" role="status" aria-live="polite"></div>
<div class="field" style="margin-top:14px"><label>Access Point password (8..63 chars; empty = open AP)</label><input id="appass" type="password" minlength="8" maxlength="63" placeholder="leave empty for open AP"></div>
<button class="btn good" style="margin-top:10px" onclick="saveAp()">Save AP password + reboot</button></div>
</section></div>
<script>

function selectTab(name,updateHash=true){
 const settings=name==='settings',opening=settings&&document.getElementById('settingsPanel').hidden;
 for(const tab of ['control','settings']){
  const active=(tab==='settings')===settings,button=document.getElementById(tab+'Tab');
  document.getElementById(tab+'Panel').hidden=!active;
  button.classList.toggle('active',active);button.setAttribute('aria-selected',String(active));button.tabIndex=active?0:-1;
 }
 if(updateHash&&location.hash!==(settings?'#settings':'#control'))location.hash=settings?'settings':'control';
 if(opening)loadRssi();
}
function compactStatus(j){
 const wifi=document.getElementById('statusWifi'),bt=document.getElementById('statusBt');
 const wifiModes={'Wi-Fi Client':'Подключён','AP + Wi-Fi Client':'Подключён + AP','Access Point':'Точка доступа','Disconnected':'Нет подключения'};
 wifi.textContent=wifiModes[j.wifiMode]||j.wifiMode||'—';wifi.title=j.ssid||'';
 wifi.className=Number.isFinite(j.rssi)?'online':'offline';
 const btEnabled=j.build?.bluetooth!==false&&j.build?.bluetoothBackend!=='NONE';
 bt.textContent=!btEnabled?'Отключён':j.btConnected?'Подключён':j.btLinkConnected?'Связь без SPP':'Нет подключения';
 bt.className=btEnabled?(j.btConnected?'online':'offline'):'';
 document.getElementById('statusSignal').textContent=Number.isFinite(j.rssi)?j.rssi+' dBm':'—';
 document.getElementById('statusMemory').textContent=Number.isFinite(j.heapFree)?(j.heapFree/1024).toFixed(1)+' КиБ':'—';
 const seconds=j.uptimeSeconds;
 document.getElementById('statusUptime').textContent=Number.isFinite(seconds)?(seconds>=86400?Math.floor(seconds/86400)+' д ':'')+[Math.floor(seconds/3600)%24,Math.floor(seconds/60)%60,Math.floor(seconds)%60].map(n=>String(n).padStart(2,'0')).join(':'):'—';
}

async function forkPost(path,body){
 try{
  const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body)});
  const j=await r.json();
  const messages={not_calibrated:'Сначала выполните калибровку',invalid:'Проверьте значения',ignored:'Сейчас выполняется предустановка',pending:'Цель ожидает подтверждения',ok:'Готово'};
  document.getElementById('forkMessage').textContent=messages[j.result]||'Ошибка';
  await upd();return j.ok;
 }catch(e){document.getElementById('forkMessage').textContent='Нет связи с контроллером';return false}
}
function fillPreset(){
 const f=document.getElementById('presetEditor'),p=window._forkState?.presets[Number(f.elements.slot.value)-1];if(!p)return;
 f.elements.label.value=p.label;f.elements.az.value=p.target.az;f.elements.el.value=p.target.el;
}
async function savePreset(e){e.preventDefault();await forkPost('/api/preset',Object.fromEntries(new FormData(e.target)))}
async function saveFork(e){e.preventDefault();const body=Object.fromEntries(new FormData(e.target));if(window._forkState?.build?.drive==='YAESU')body.speedEnabled=e.target.elements.speedEnabled.checked?'true':'false';else delete body.speedEnabled;if(window._forkState?.build?.display==='CROWPANEL_21')body.encoderInverted=e.target.elements.encoderInverted.checked?'true':'false';else delete body.encoderInverted;if(await forkPost('/api/settings',body))delete e.target.dataset.dirty;}
function buttonTarget(j){return j.pendingValid?j.pending:j.lastTarget}
function targetLabel(t,j){
 if(!t)return '';
 const az=t.hasAz?Number(t.az.toFixed(1))+'°':'',el=t.hasEl&&j.model!=='G800DXA'?'EL '+Number(t.el.toFixed(1))+'°':'';
 return az&&el?'AZ '+az+'\n'+el:az||el;
}
function updateTargetButton(j){
 const b=document.getElementById('executeTarget'),stop=j.moving||j.controlMode!=='manual',label=targetLabel(buttonTarget(j),j);
 b.textContent=stop?'stop':label||'Повернуть';
 b.classList.toggle('stop',stop);b.classList.toggle('good',!stop);
 b.disabled=!stop&&!label;
 b.title=stop?'Остановить движение':label?'Повернуть к '+label.replace('\n',', '):'Нет цели для поворота';
 b.setAttribute('aria-label',stop?'stop':b.title);
}
async function targetAction(){
 const j=window._forkState;if(!j)return;
 if(j.moving||j.controlMode!=='manual'){await st();await upd();return;}
 if(j.pendingValid){await forkPost('/api/execute',{});return;}
 const t=buttonTarget(j);if(!targetLabel(t,j))return;
 const body={};if(t.hasAz)body.az=t.az;if(t.hasEl&&j.model!=='G800DXA')body.el=t.el;
 await forkPost('/api/target',body);
}
function updateFork(j){
 const first=!window._forkState;window._forkState=j;
 const g800=j.model==='G800DXA';
 document.getElementById('modelTitle').textContent='YAESU '+(g800?'G-800DXA':'G-5500')+' / '+(j.build?.board||'ESP32');
 document.getElementById('followMode').value=j.controlMode;
 document.getElementById('hourLeft').textContent=j.controlMode==='auto_1h'?'Осталось '+Math.ceil(j.remainingMs/60000)+' мин':'';
 document.getElementById('waitingTarget').textContent=j.pendingValid?'Ожидает: '+(j.pending.hasAz?'AZ '+j.pending.az.toFixed(1)+'° ':'')+(j.pending.hasEl&&!g800?'EL '+j.pending.el.toFixed(1)+'°':''):'Нет ожидающей цели';
 updateTargetButton(j);
 const buttons=document.getElementById('presetButtons');
 if(!buttons.children.length)for(let slot=1;slot<=4;slot++){const b=document.createElement('button');b.className='btn';b.onclick=()=>forkPost('/api/preset/run',{slot});buttons.append(b)}
 j.presets.forEach((p,i)=>{const b=buttons.children[i];b.textContent=p.label;b.disabled=!p.valid;b.classList.toggle('active',j.presetSlot===i&&j.presetActive)});
 const elNodes=[document.getElementById('el').closest('.card'),document.getElementById('tel').closest('.field'),document.getElementById('el2p0').parentElement,document.getElementById('elp0').parentElement.parentElement,document.getElementById('calel').closest('.calhead'),document.getElementById('b_el0').parentElement,document.getElementById('b2_elo'),document.getElementById('b2_elf'),...Array.from(document.querySelectorAll('button')).filter(b=>b.textContent==='RESET EL'),...document.querySelectorAll('.el-setting,.movebtn[data-dir="U"],.movebtn[data-dir="D"]')];
 [...elNodes,...document.querySelectorAll('.el-help')].forEach(n=>{if(n)n.classList.toggle('hide',g800)});
 const independent=['SEPARATE_PWM','BTS7960'].includes(j.build?.drive);
 document.getElementById('speedSettings').classList.toggle('hide',['NONE','SIMULATED'].includes(j.build?.drive)||(!g800&&!independent));
 document.getElementById('analogSpeedToggle').classList.toggle('hide',independent);
 document.getElementById('elSpeedSettings').classList.toggle('hide',g800||!independent);
 const btBackend=j.build?.bluetoothBackend||(j.build?.bluetooth===false?'NONE':'HC05');
 const btName=btBackend==='ESP32_SPP'?'Встроенный Bluetooth SPP':btBackend==='HC05'?'HC-05':'Bluetooth отключён';
 const nativeBt=btBackend==='ESP32_SPP';
 document.getElementById('buildInfo').textContent=j.build?`${j.build.board} · ${j.build.drive} · ${j.build.display} · ${btName}`:'';
 document.getElementById('btlabel').textContent=nativeBt?'Канал SPP':btName;
 document.getElementById('bttransport').textContent=btBackend==='ESP32_SPP'?'G5500-Rotator · SPP / виртуальный COM':btBackend==='HC05'?'HC-05 UART: 9600 baud':'';
 for(const id of ['btlinkrow','btmacrow','bthelp'])document.getElementById(id).classList.toggle('hide',!nativeBt);
 const btLink=!!(j.btLinkConnected??j.btConnected),linkState=document.getElementById('btlinkstate');
 linkState.textContent=btLink?'Подключено':'Нет подключения';linkState.style.color=btLink?'#24d18d':'#e5a94f';
 document.getElementById('btmac').textContent=j.btMac||'Недоступен';
 document.getElementById('movementButtons').classList.toggle('az-only',g800);
 const f=document.getElementById('forkSettings');
 for(const key of ['model','rotorName','n1mmPort','pstPort','reportPort','reportHost','speedPercent','startSpeedPercent','approachSpeedPercent','accelerationMs','decelerationMs','slowdownDegrees'])if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements[key].value=j[key];
 for(const key of ['speedPercent','startSpeedPercent','approachSpeedPercent','accelerationMs','decelerationMs','slowdownDegrees']){const elKey='el'+key[0].toUpperCase()+key.slice(1);if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements[elKey].value=j[elKey];if(key.endsWith('Percent'))f.elements[key].min=independent?1:25;}
 document.getElementById('elSpeedOutput').textContent=(j.elSpeedOutputPercent||0).toFixed(1)+'%';
 document.getElementById('speedOutput').textContent=j.speedOutputPercent.toFixed(1)+'%';
 if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements.speedEnabled.checked=j.speedEnabled;
 document.getElementById('encoderSettings').classList.toggle('hide',j.build?.display!=='CROWPANEL_21');
 if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements.encoderInverted.checked=j.encoderInverted??true;
 document.querySelector('#presetEditor [name=az]').max=j.azMode===36?360:450;
 if(first)fillPreset();
 const faults={target_not_calibrated:'Сначала выполните калибровку',continuous_motion_timeout:'Превышено время непрерывного вращения',udp_bind_failed:'Не удалось открыть UDP-порт',pwm_attach_failed:'Не удалось включить регулировку скорости'};if(j.fault)document.getElementById('forkMessage').textContent=faults[j.fault]||'Ошибка управления';
}

async function q(u){try{return await (await fetch(u,{cache:'no-store'})).text()}catch(e){return''}}
async function mv(d){await q('/api/move?d='+d)}
async function st(){await q('/api/move?d=S')}
async function stopAxis(d){
  if(d==='L'||d==='R') await q('/api/move?d=A');
  else if(d==='U'||d==='D') await q('/api/move?d=E');
}
async function go(){if(taz.value==='')return;const body={az:taz.value};if(window._forkState?.model!=='G800DXA'){if(tel.value==='')return;body.el=tel.value}await forkPost('/api/target',body)}
function toggleCalMenu(){
  const body=document.getElementById('calbody');
  const btn=document.getElementById('caltoggle');
  const open=!body.classList.contains('open');
  body.classList.toggle('open',open);
  btn.classList.toggle('open',open);
}
async function cal(p){await q('/api/cal?p='+p);setTimeout(upd,150)}
async function cal2(p){await q('/api/cal2?p='+p);setTimeout(upd,150)}
async function setCalMode(m){await q('/api/calmode?m='+m);setTimeout(upd,150)}
async function setAzMode(m){await q('/api/azmode?m='+m);setTimeout(upd,150)}
async function resetCal(axis,cm){
  const name=axis==='az'?'АЗИМУТА':'ЭЛЕВАЦИИ';
  const mn=cm==='2'?'GS-232 2-POINT':'MULTI-POINT';
  const rr=axis==='az'?' '+(window._azMode===36?'P36':'P45'):'';
  if(!confirm('Сбросить '+mn+' калибровку '+name+rr+'?')) return;
  await q('/api/calreset?axis='+axis+'&mode='+cm);
  setTimeout(upd,150);
}
function markCalBtn(id, ok){
  const b=document.getElementById(id);
  if(!b) return;
  b.classList.toggle('done', !!ok);
  b.classList.toggle('warn', !ok);
}
async function wifi(){
 const message=document.getElementById('wifiMessage');
 const button=document.getElementById('wifiSaveButton');
 if(button.disabled)return;
 const network=document.getElementById('ssid').value,password=document.getElementById('pass').value;
 if(!network){message.textContent='Укажите SSID сети Wi-Fi.';return;}
 if(!confirm('Сохранить Wi‑Fi и перезагрузить контроллер?'))return;
 button.disabled=true;
 const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),12000);
 message.textContent='Сохранение настроек Wi-Fi…';
 try{
  const response=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:network,pass:password}),cache:'no-store',signal:controller.signal});
  message.textContent=await response.text()||'Не удалось сохранить настройки Wi-Fi.';
  if(response.ok)document.getElementById('pass').value='';
 }catch(e){message.textContent='Ответ не получен. Проверьте адрес контроллера в домашней сети. Если подключения нет, используйте G5500-Rotator и проверьте SSID.';}
 finally{clearTimeout(timeout);button.disabled=false;}
}
async function saveAp(){
  const p=appass.value;
  if(p.length>0 && p.length<8){alert('Пароль AP должен быть минимум 8 символов или пустым для открытой точки доступа.');return;}
  if(confirm(p.length?'Сохранить пароль точки доступа и перезагрузить контроллер?':'Сделать точку доступа открытой и перезагрузить контроллер?'))
    await q('/api/ap?pass='+encodeURIComponent(p));
}
function pct(v,m){v=Math.max(0,Math.min(m,v));return(v/m*100).toFixed(1)+'%'}
let statusPending=false;
async function upd(){
if(statusPending)return;
statusPending=true;
const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),5000);
try{let j=await (await fetch('/api/status',{cache:'no-store',signal:controller.signal})).json();
az.textContent=Number(j.az).toFixed(1);el.textContent=Number(j.el).toFixed(1);
updateFork(j);
azt.textContent=j.targetValid?Number(j.targetAz).toFixed(1):'---';
elt.textContent=j.targetValid?Number(j.targetEl).toFixed(1):'---';
calaz.textContent=Number(j.az).toFixed(1);calel.textContent=Number(j.el).toFixed(1);
const azmx=j.azMode===36?360:450;azbar.style.width=pct(j.az,azmx);azmaxlabel.textContent=azmx+'°';taz.max=azmx;tazlbl.textContent='AZ 0..'+azmx+'°';elbar.style.width=pct(j.el,180);
motion.textContent=j.motion;
window._azMode=Number(j.azMode)||45;
const selok=!!j.selectedCalOk;
caltxt.textContent=(j.calMode==='multi'?'MULTI-POINT':'GS-232 2-POINT')+' · P'+j.azMode+(selok?' OK':' NOT CALIBRATED');
caldot.className='dot '+(selok?'on':'');
calmode.textContent=j.calMode==='multi'?'MULTI-POINT':'GS-232 2-POINT';
mode2.classList.toggle('good',j.calMode==='2');modem.classList.toggle('good',j.calMode==='multi');
azm36.classList.toggle('good',j.azMode===36);azm45.classList.toggle('good',j.azMode===45);
azrange.textContent='P'+j.azMode+' · '+(j.azMode===36?'360°':'450°');
resetazrange.textContent='P'+j.azMode;
panel2.classList.toggle('hide',j.calMode!=='2');panelm.classList.toggle('hide',j.calMode!=='multi');
b_az450.classList.toggle('hide',j.azMode===36);azv450.classList.toggle('hide',j.azMode===36);
az2p0.textContent=j.az2p0;az2pf.textContent=j.az2pf;el2p0.textContent=j.el2p0;el2pf.textContent=j.el2pf;
markCalBtn('b2_azo',j.az2p0>=0);markCalBtn('b2_azf',j.az2pf>=0);
markCalBtn('b2_elo',j.el2p0>=0);markCalBtn('b2_elf',j.el2pf>=0);
azp0.textContent=j.azp0;azp90.textContent=j.azp90;azp180.textContent=j.azp180;azp270.textContent=j.azp270;azp360.textContent=j.azp360;azp450.textContent=j.azp450;
elp0.textContent=j.elp0;elp45.textContent=j.elp45;elp90.textContent=j.elp90;elp135.textContent=j.elp135;elp180.textContent=j.elp180;
markCalBtn('b_az0',j.azp0>=0);markCalBtn('b_az90',j.azp90>=0);markCalBtn('b_az180',j.azp180>=0);
markCalBtn('b_az270',j.azp270>=0);markCalBtn('b_az360',j.azp360>=0);markCalBtn('b_az450',j.azMode===45&&j.azp450>=0);
markCalBtn('b_el0',j.elp0>=0);markCalBtn('b_el45',j.elp45>=0);markCalBtn('b_el90',j.elp90>=0);
markCalBtn('b_el135',j.elp135>=0);markCalBtn('b_el180',j.elp180>=0);
ip.textContent=j.ip;document.getElementById('apip').textContent=j.apIp||'-';mode.textContent=j.wifiMode;
rssi.textContent=(j.rssi===null?'-':j.rssi+' dBm');
sig.textContent=j.signal||'-';
btstate.textContent=j.build?.bluetooth===false?'Отключён в сборке':j.btConnected?'CONNECTED':'DISCONNECTED';
btstate.style.color=j.btConnected?'#24d18d':'#e5a94f';
if(document.activeElement!==ssid && !ssid.dataset.dirty) ssid.value=j.ssid||'';
compactStatus(j);
}catch(e){
 const wifi=document.getElementById('statusWifi');wifi.textContent='Нет связи';wifi.className='offline';wifi.title='Не удалось обновить статус устройства';
 for(const id of ['statusBt','statusSignal','statusMemory','statusUptime']){const node=document.getElementById(id);node.textContent='—';node.className='';}
}finally{clearTimeout(timeout);statusPending=false;}}
let rrange='hour';
function drawRssi(j){
 const c=document.getElementById('rchart'),x=c.getContext('2d'),w=c.width,h=c.height;
 x.clearRect(0,0,w,h);x.fillStyle='#071018';x.fillRect(0,0,w,h);x.strokeStyle='#294153';x.lineWidth=1;x.font='12px Arial';x.fillStyle='#96a9b7';
 const top=15,bottom=h-28,left=46,right=w-12;
 for(let db=-30;db>=-100;db-=10){let y=top+(-30-db)/70*(bottom-top);x.beginPath();x.moveTo(left,y);x.lineTo(right,y);x.stroke();x.fillText(db+' dBm',3,y+4);}
 let a=j.values||[],n=a.length;x.strokeStyle='#24d18d';x.lineWidth=2;x.beginPath();let pen=false;
 for(let i=0;i<n;i++){let v=a[i];if(v===null){pen=false;continue}let px=left+(n<=1?0:i/(n-1))*(right-left);let py=top+(-30-Math.max(-100,Math.min(-30,v)))/70*(bottom-top);if(!pen){x.moveTo(px,py);pen=true}else x.lineTo(px,py);}x.stroke();
 x.fillStyle='#96a9b7';x.fillText('oldest',left,bottom+18);x.fillText('now',right-24,bottom+18);
 rmin.textContent=j.min===null?'-':j.min+' dBm';rmax.textContent=j.max===null?'-':j.max+' dBm';ravg.textContent=j.avg===null?'-':j.avg+' dBm';rcount.textContent=j.count||0;
}
async function loadRssi(){if(document.getElementById('settingsPanel').hidden)return;try{let j=await (await fetch('/api/rssi?range='+rrange,{cache:'no-store'})).json();drawRssi(j)}catch(e){}}
document.querySelectorAll('.rbtn').forEach(b=>b.addEventListener('click',()=>{rrange=b.dataset.r;document.querySelectorAll('.rbtn').forEach(q=>q.classList.remove('active'));b.classList.add('active');loadRssi();}));
window.addEventListener('hashchange',()=>selectTab(location.hash==='#settings'?'settings':'control',false));
document.querySelectorAll('[role=tab]').forEach(tab=>tab.addEventListener('keydown',event=>{
 if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
 event.preventDefault();const next=event.key==='Home'?'control':event.key==='End'?'settings':tab.id==='controlTab'?'settings':'control';
 selectTab(next);document.getElementById(next+'Tab').focus();
}));
selectTab(location.hash==='#settings'?'settings':'control',false);
setInterval(loadRssi,30000);
setInterval(upd,500);upd();

ssid.addEventListener('input',function(){ssid.dataset.dirty='1';});
document.getElementById('forkSettings').addEventListener('input',event=>{event.currentTarget.dataset.dirty='1';});

document.querySelectorAll('.btn').forEach(function(b){
  b.addEventListener('contextmenu',function(e){e.preventDefault();});
  b.addEventListener('dragstart',function(e){e.preventDefault();});
  b.addEventListener('selectstart',function(e){e.preventDefault();});
});

document.querySelectorAll('.movebtn').forEach(function(b){
  let active=false;

  b.addEventListener('touchstart',function(e){
    e.preventDefault();
  },{passive:false});

  b.addEventListener('pointerdown',function(e){
    e.preventDefault();
    active=true;
    try{ b.setPointerCapture(e.pointerId); }catch(x){}
    mv(b.dataset.dir);
  });

  function release(e){
    if(e) e.preventDefault();
    if(active){
      active=false;
      stopAxis(b.dataset.dir);
    }
  }

  b.addEventListener('pointerup',release);
  b.addEventListener('pointercancel',release);
  b.addEventListener('lostpointercapture',release);
  b.addEventListener('contextmenu',function(e){e.preventDefault();});
});
</script></body></html>
)HTML";
