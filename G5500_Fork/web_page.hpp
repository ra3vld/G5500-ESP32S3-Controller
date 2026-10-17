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
.range-editors{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.range-editor{min-width:0}.range-editor>.label{font-size:18px}.command-card h2{margin-top:0}.command-card h3{font-size:16px;margin:20px 0 8px}.control-mode{margin-bottom:16px}.target-controls{margin:16px 0}.range-editor .seg{display:flex;flex-wrap:wrap}.range-editor .seg .btn{flex:1 1 120px;font-size:14px}.range-editor input{max-width:100%}.calbox .row{align-items:center;gap:8px}.calbox .row>span{flex:1 1 150px}.calbox .row .btn{white-space:normal}@media(max-width:760px){.range-editors{grid-template-columns:1fr}}
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
.hint{color:var(--m);font-size:13px;line-height:1.5;overflow-wrap:anywhere}
.device-panel h2{font-size:20px;line-height:1.35;margin:0;overflow-wrap:anywhere}.device-panel #hudLink:empty{display:none}.device-panel #hudLink:not(:empty){margin-top:8px}
.device-section{margin-top:14px;border-top:1px solid var(--b);padding-top:14px}.device-section summary{line-height:1.4;padding:4px 0}.device-section[open] summary{margin-bottom:14px}
.device-fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.device-fields .field{min-width:0}.device-fields label{display:block;margin-bottom:7px;color:var(--m);font-size:14px;line-height:1.4}.device-fields input,.device-fields select{min-height:44px;max-width:100%}
.device-panel .btn{min-height:44px;font-size:15px;padding:10px 16px;border-radius:10px;cursor:pointer;touch-action:manipulation;overflow-wrap:anywhere}.device-actions{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px}.device-panel form{margin:0}.device-panel .hint{margin:12px 0}.device-address{display:grid;gap:12px;padding:16px 0}.device-address>.btn{justify-self:start}
.device-list{display:grid;gap:10px}.device-peer{display:grid;grid-template-columns:minmax(0,1fr) auto auto;gap:8px}.device-peer>.btn:first-child{text-align:left}.device-peer-note{grid-column:1/-1;color:var(--m);font-size:13px}.device-pagination{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:12px 0;font-size:13px;color:var(--m)}.device-pagination>span{text-align:center;overflow-wrap:anywhere}
@media(max-width:600px){.device-fields{grid-template-columns:minmax(0,1fr);gap:14px}.device-actions>.btn,.device-address>.btn{width:100%}.device-peer{grid-template-columns:repeat(2,minmax(0,1fr))}.device-peer>.btn:first-child{grid-column:1/-1}.device-pagination .btn{padding:10px 12px}}
[hidden]{display:none!important}.nav button:focus-visible,.nav a:focus-visible{outline:2px solid var(--a);outline-offset:2px}
.current-status{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;padding:12px;font-size:13px}.current-status span{display:block;color:var(--m);margin-bottom:5px}.current-status b{font-size:14px;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}.current-status .online{color:var(--a)}.current-status .offline{color:#e5a94f}
.action-feedback{position:fixed;z-index:50;bottom:max(12px,env(safe-area-inset-bottom));left:50%;transform:translateX(-50%);width:calc(100% - 24px);max-width:620px;display:flex;align-items:center;gap:12px;padding:12px 14px;background:#132532;border:2px solid #57bde8;border-radius:12px;box-shadow:0 6px 30px #0009;font-size:16px;line-height:1.4;overflow-wrap:anywhere}.action-feedback[data-state=success]{border-color:#24d18d}.action-feedback[data-state=error]{border-color:#ff7970}.action-feedback>div{flex:1;min-width:0}.action-feedback strong{display:block;margin-bottom:3px}.action-feedback button{flex:none;border:0;border-radius:8px;min-width:44px;min-height:44px;background:#294153;color:white;font-size:24px;cursor:pointer}.action-feedback[data-state=pending] button{visibility:hidden}.btn[aria-busy=true]{opacity:.8;outline:2px solid #57bde8;outline-offset:2px}.btn[aria-busy=true]::after{content:' …'}

/* Settings share one disclosure, field and action layout. */
body.settings-view{height:100dvh;overflow:hidden;display:flex;flex-direction:column}
body.settings-view>.wrap{width:100%;min-height:0;flex:1;overflow-y:auto;margin:0 auto;scrollbar-gutter:stable;scroll-padding-top:16px}
body.settings-view .nav{position:static}
#settingsPanel{display:grid;gap:12px;padding:4px 0 24px}
.settings-section.card{margin:0;padding:0;border:1px solid #294153;border-radius:14px;background:#101c27;overflow:clip}
.settings-details>summary{display:flex;align-items:center;gap:20px;padding:20px;list-style:none;cursor:pointer;user-select:none}
.settings-details>summary::-webkit-details-marker{display:none}
.settings-details>summary::after{content:'';flex:none;width:9px;height:9px;border-right:2px solid #94aab9;border-bottom:2px solid #94aab9;transform:rotate(45deg);margin:0 4px 6px auto}
.settings-details[open]>summary::after{transform:rotate(225deg);margin-bottom:-4px}
.settings-details[open]>summary{border-bottom:1px solid #294153;background:#142330}
.settings-details>summary h2{min-width:0;font-size:18px;line-height:1.35;margin:0;font-weight:600}
.section-caption{display:block;margin-top:5px;color:#9aafbf;font-size:13px;line-height:1.45;font-weight:400}
.settings-content{padding:20px}
#settingsPanel h3{font-size:16px;line-height:1.4;font-weight:600;margin:0 0 16px}
#settingsPanel h4{font-size:14px;line-height:1.45;font-weight:600;margin:0 0 12px;color:#cad9e4}
#settingsPanel p{line-height:1.55}
#settingsPanel .hint{font-size:13px;line-height:1.55;color:#9aafbf;margin:12px 0}
#settingsPanel .row,#settingsPanel .device-fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px 20px;align-items:end}
#settingsPanel .field{min-width:0;display:flex;flex-direction:column;gap:8px}
#settingsPanel .field label{display:block;font-size:14px;line-height:1.45;color:#c9d8e3;margin:0}
#settingsPanel input,#settingsPanel select{max-width:100%;min-width:0;min-height:44px;padding:10px 12px;border:1px solid #3b5365;border-radius:9px;background:#09141e;color:#f4f7fa;font:inherit;font-size:16px;line-height:1.35}
#settingsPanel .check-field{display:flex;gap:10px;align-items:flex-start;font-size:14px;line-height:1.5;margin:14px 0}
#settingsPanel .check-field input{width:18px;min-height:18px;height:18px;margin:2px 0 0;flex:none;accent-color:#24d18d}
#settingsPanel .btn{display:inline-flex;justify-content:center;align-items:center;gap:6px;max-width:100%;min-width:0;min-height:44px;height:auto;border:1px solid #365466;border-radius:9px;background:#1c3748;color:#edf5fa;font-family:inherit;font-size:14px;font-weight:600;line-height:1.4;padding:10px 14px;white-space:normal;overflow-wrap:anywhere;cursor:pointer;touch-action:manipulation;text-align:center}
#settingsPanel .btn.good{background:#176c54;border-color:#278f70}
#settingsPanel .btn.warn{background:#263641;border-color:#7a714d}
#settingsPanel .btn.done{background:#173c30;border-color:#367b5a;box-shadow:none}
#settingsPanel .btn.calreset{background:#2e2026;border-color:#7e4248;color:#ffc0c3}
#settingsPanel .btn:disabled{cursor:default}
#settingsPanel button:focus-visible,#settingsPanel summary:focus-visible,#settingsPanel input:focus-visible,#settingsPanel select:focus-visible{outline:2px solid #58b8df;outline-offset:3px}
#settingsPanel .settings-group,#forkSettings>h3{margin-top:24px;padding-top:20px;border-top:1px solid #294153}
#settingsPanel .form-actions{display:flex;align-items:center;flex-wrap:wrap;gap:12px;margin-top:20px;padding-top:16px;border-top:1px solid #294153}
#settingsPanel .form-actions .hint{margin:0;flex:1 1 150px}
#settingsPanel .range-editors,#settingsPanel .calibration-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}
#settingsPanel .settings-pane{min-width:0;padding:16px;border:1px solid #294153;background:#0c1822;border-radius:10px}
#settingsPanel .range-editor{display:flex;flex-direction:column}
#settingsPanel .range-form{margin-top:16px;padding-top:16px;border-top:1px solid #294153}
#settingsPanel .range-editor .range-form{display:flex;flex-direction:column;flex:1}
#settingsPanel .range-form .row{gap:12px}
#settingsPanel .range-form .form-actions{padding-top:0;border:0;margin-top:16px}
#settingsPanel .range-editor .form-actions{margin-top:auto;padding-top:8px}
#settingsPanel .range-form .form-actions .btn{width:100%}
#settingsPanel .rangebadge{display:inline-block;background:#203441;border-radius:6px;padding:6px 8px;font-size:12px;font-weight:500;margin:12px 0 0;line-height:1.45}
#settingsPanel .range-editor .rangebadge{align-self:flex-start}
#settingsPanel .seg{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin:0}
#settingsPanel .profile-options{grid-template-columns:repeat(3,minmax(0,1fr))}
#settingsPanel .el-setting .profile-options{grid-template-columns:repeat(2,minmax(0,1fr))}
#settingsPanel .profile-options .btn{padding:9px 8px;font-size:13px}
#settingsPanel .calibration-method{margin-bottom:20px}
#settingsPanel .calhead{align-items:center;flex-wrap:wrap;margin:0 0 16px;gap:8px}
#settingsPanel .calhead h3{margin:0}
#settingsPanel .callive{font-size:26px;line-height:1.25;font-weight:600;font-variant-numeric:tabular-nums}
#settingsPanel .callive span:last-child{font-size:16px}
#settingsPanel .endpoint-buttons{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
#settingsPanel .minirow{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}
#settingsPanel .minirow .btn{padding:9px 6px}
#settingsPanel .calvalues{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin-top:12px;font-size:12px;line-height:1.5;color:#a7bac8}
#settingsPanel .calvalues>div{min-width:0;background:#14232e;border-radius:6px;padding:8px;overflow-wrap:anywhere}
#settingsPanel .endpoint-values{grid-template-columns:repeat(2,minmax(0,1fr))}
#settingsPanel .calvalues b{color:#e4edf4;font-weight:600}
#settingsPanel .point-list{display:grid;gap:10px}
#settingsPanel .point-row{display:grid;gap:12px;padding:12px;background:#14232e;border:1px solid #294153;border-radius:9px;font-size:14px;line-height:1.5;overflow-wrap:anywhere}
#settingsPanel .point-actions{display:flex;flex-wrap:wrap;gap:8px}
#settingsPanel .point-actions .btn{flex:1 1 120px}
#settingsPanel .point-actions .btn+ .btn{flex:1 1 76px}
#settingsPanel .inline-form{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:12px;align-items:end;margin-top:16px}
#settingsPanel .inline-message:empty{display:none}
#settingsPanel .inline-message{font-size:13px;line-height:1.5;margin-top:12px;color:#b8daca;overflow-wrap:anywhere}
#settingsPanel .device-list{gap:12px}
#settingsPanel .device-peer{gap:8px;padding:12px;border:1px solid #294153;border-radius:9px;background:#0c1822}
#settingsPanel .device-pagination{margin:16px 0;gap:12px}
#settingsPanel .device-address{padding:16px 0;gap:16px}
#settingsPanel .device-peer-note{line-height:1.5}
#settingsPanel .kv{margin-top:12px;font-size:13px;line-height:1.5}
#settingsPanel .kv>div{padding:12px;background:#0c1822;border:1px solid #243b4c;border-radius:8px}
#settingsPanel #bttransport:empty{display:none}
#settingsPanel .mono{font-family:monospace;user-select:all;overflow-wrap:anywhere}
#settingsPanel .period-options{display:flex;flex-wrap:wrap;gap:8px}
#settingsPanel .period-options .btn{flex:1 1 90px;white-space:nowrap;overflow-wrap:normal}
#settingsPanel canvas{margin-top:16px}
body.settings-view>.action-feedback{position:static;transform:none;left:auto;bottom:auto;flex:none;align-self:center;width:calc(100% - 24px);margin:0 12px max(12px,env(safe-area-inset-bottom));max-width:956px;box-shadow:none;font-size:14px}
@media(max-width:760px){#settingsPanel .range-editors,#settingsPanel .calibration-grid{grid-template-columns:minmax(0,1fr)}}
@media(max-width:600px){.settings-details>summary{padding:16px;gap:16px}.settings-details>summary h2{font-size:17px}.settings-content{padding:16px}#settingsPanel .row,#settingsPanel .device-fields{grid-template-columns:minmax(0,1fr);gap:16px}#settingsPanel .settings-pane{padding:12px}#settingsPanel .form-actions>.btn{width:100%}#settingsPanel .form-actions{gap:10px}#settingsPanel .range-form .row{grid-template-columns:repeat(2,minmax(0,1fr))}#settingsPanel .profile-options{grid-template-columns:repeat(2,minmax(0,1fr))}#settingsPanel #azmc{grid-column:1/-1}#settingsPanel .inline-form{grid-template-columns:minmax(0,1fr)}#settingsPanel .inline-form>.btn{width:100%}#settingsPanel .device-peer{grid-template-columns:repeat(2,minmax(0,1fr))}}

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
<div id="selectedRotor" class="hint hide" role="status"></div>
<div class="grid">
<div class="card"><div class="label">Азимут — текущее / задание</div><div><span id="az" class="big">--.-</span><span class="sep"> / </span><span id="azt" class="targetbig">---</span><span class="unit">°</span></div><div class="bar"><div id="azbar" class="fill"></div></div><div><span id="azminlabel">0°</span> <span id="azmaxlabel" style="float:right">450°</span></div></div>
<div class="card"><div class="label">Элевация — текущее / задание</div><div><span id="el" class="big">--.-</span><span class="sep"> / </span><span id="elt" class="targetbig">---</span><span class="unit">°</span></div><div class="bar"><div id="elbar" class="fill"></div></div><div><span id="elminlabel">0°</span> <span id="elmaxlabel" style="float:right">180°</span></div></div>
</div>

<div class="card command-card"><h2>Наведение</h2>
<div class="control-mode">
<div class="label">Режим управления</div>
<div class="mode-controls" style="margin-top:8px">
<div class="field"><label for="followMode">Следование командам</label><select id="followMode" onchange="forkPost('/api/mode',{mode:this.value})"><option value="manual">Ручной</option><option value="auto">Автоматический</option><option value="auto_1h">Авто 1 ч</option></select></div></div>
<div id="hourLeft" aria-live="polite"></div>
<div id="waitingTarget" style="margin-top:8px">Нет ожидающей цели</div>
</div>

<div style="display:flex;justify-content:space-between;gap:10px;flex-wrap:wrap">
<div><div class="label">Движение</div><div id="motion" style="font-size:23px;font-weight:700">STOP</div></div>
<div><span class="status"><span id="caldot" class="dot"></span><span id="caltxt">Calibration</span></span></div></div>
<div class="target-controls"><div class="label">Задать угол</div><div class="row" style="margin-top:8px">
<div class="field"><label id="tazlbl">AZ 0..450°</label><input id="taz" type="number" step="0.1" min="0" max="450"></div>
<div class="field"><label id="tellbl">EL 0…180°</label><input id="tel" type="number" step="0.1" min="0" max="180"></div>
<button class="btn good" onclick="go()">Повернуть</button></div></div>
<h3>Ручное вращение</h3>
<div id="movementButtons" class="controls" style="margin-top:10px">
<button class="btn blank">-</button><button class="btn movebtn" data-dir="U"><span>▲ UP</span></button><button class="btn blank">-</button>
<button class="btn movebtn" data-dir="L"><span>◀ LEFT</span></button><button id="executeTarget" class="btn good" onclick="targetAction()" disabled>Повернуть</button><button class="btn movebtn" data-dir="R"><span>RIGHT ▶</span></button>
<button class="btn blank">-</button><button class="btn movebtn" data-dir="D"><span>▼ DOWN</span></button><button class="btn blank">-</button>
</div>

</div>
<div class="card"><div class="label">Предустановки</div><div id="presetButtons" class="preset-grid" style="margin-top:10px"></div>
</div>
<div class="card current-status" id="currentStatus" aria-label="Текущий статус устройства">
<div><span>Wi-Fi</span><b id="statusWifi">—</b></div>
<div><span>Bluetooth</span><b id="statusBt">—</b></div>
<div><span>Сигнал Wi-Fi</span><b id="statusSignal">—</b></div>
<div title="Свободная внутренняя RAM"><span>Свободная память</span><b id="statusMemory">—</b></div>
<div><span>Uptime</span><b id="statusUptime">—</b></div>
</div>
</main>
<section aria-labelledby="settingsTab" hidden="" id="settingsPanel" role="tabpanel"><section class="card settings-section" id="rotorSettings"><details class="settings-details"><summary><h2>Поворотка и привод<span class="section-caption">Модель, движение и подключение логгеров</span></h2></summary><div class="settings-content"><form id="forkSettings" onsubmit="saveFork(event)"><div class="row">
<div class="field"><label for="forkSettings-model">Модель</label><select id="forkSettings-model" name="model"><option value="G5500">G-5500</option><option value="G800DXA">G-800DXA</option></select></div>
</div><h3>Подключение логгеров</h3><div class="row"><div class="field"><label for="forkSettings-rotorName">Имя в N1MM</label><input id="forkSettings-rotorName" maxlength="31" name="rotorName" required=""/></div>
<div class="field"><label for="forkSettings-n1mmPort">N1MM UDP</label><input id="forkSettings-n1mmPort" max="65535" min="1" name="n1mmPort" required="" type="number"/></div>
<div class="field"><label for="pstPort">PstRotator UDP</label><input aria-describedby="pstPortHelp" id="pstPort" max="65534" min="1" name="pstPort" required="" type="number"/></div>
<div class="field"><label for="forkSettings-reportPort">Положение N1MM UDP</label><input id="forkSettings-reportPort" max="65535" min="1" name="reportPort" required="" type="number"/></div>
<div class="field"><label for="forkSettings-reportHost">Дополнительный IP N1MM</label><input id="forkSettings-reportHost" name="reportHost" placeholder="192.168.1.10"/></div></div>
<p class="hint" id="pstPortHelp">PstRotator UDP — для UR5EQF и 5MContest. В логгере выберите PstRotator/UDP; отдельно запускать программу PstRotator не требуется.<br/>Log4OM 2, DXLog.net, TR4W (с 4.148.6), Swisslog и LogHX также имеют интеграцию с PstRotator, но с нашим контроллером не тестировались.</p>
<div class="hint" id="buildInfo"></div>
<div class="hide" id="encoderSettings"><label class="check-field"><input checked="" name="encoderInverted" type="checkbox"/> Инверсия энкодера</label></div>
<div class="settings-group" id="coastSettings"><h3>Выбег и остановка</h3><div class="row">
<div class="field"><label for="coastDegrees">Угол выбега AZ, °</label><input id="coastDegrees" max="90" min="0" name="coastDegrees" required="" step="0.1" type="number" value="5"/></div>
<div class="field el-setting"><label for="elCoastDegrees">Угол выбега EL, °</label><input id="elCoastDegrees" max="90" min="0" name="elCoastDegrees" required="" step="0.1" type="number" value="5"/></div>
</div><p class="hint">Привод отключается за этот угол до цели, оставляя запас на механический выбег. 0° — отключение при достижении цели. Для короткого хода угол ограничен 10% полного хода. После отключения обратной коррекции нет.</p></div>
<div class="settings-group" id="speedSettings"><h3>Скорость и плавность движения</h3><label class="check-field" id="analogSpeedToggle"><input name="speedEnabled" type="checkbox"/> Управлять скоростью через внешний модуль 0–5 В</label>
<h4>Азимут · AZ</h4><div class="row">
<div class="field"><label for="forkSettings-speedPercent">Рабочая скорость, %</label><input id="forkSettings-speedPercent" max="100" min="25" name="speedPercent" required="" step="1" type="number" value="50"/></div>
<div class="field"><label for="forkSettings-startSpeedPercent">Скорость старта, %</label><input id="forkSettings-startSpeedPercent" max="100" min="25" name="startSpeedPercent" required="" step="1" type="number" value="25"/></div>
<div class="field"><label for="forkSettings-approachSpeedPercent">Скорость у цели, %</label><input id="forkSettings-approachSpeedPercent" max="100" min="25" name="approachSpeedPercent" required="" step="1" type="number" value="25"/></div>
<div class="field"><label for="forkSettings-accelerationMs">Время разгона 0–100% рабочей скорости, мс</label><input id="forkSettings-accelerationMs" max="10000" min="100" name="accelerationMs" required="" step="1" type="number" value="2000"/></div>
<div class="field"><label for="forkSettings-decelerationMs">Время торможения 100–0% рабочей скорости, мс</label><input id="forkSettings-decelerationMs" max="10000" min="100" name="decelerationMs" required="" step="1" type="number" value="1500"/></div>
<div class="field"><label for="forkSettings-slowdownDegrees">Начинать замедление за, °</label><input id="forkSettings-slowdownDegrees" max="90" min="1" name="slowdownDegrees" required="" step="1" type="number" value="10"/></div>
<div class="field"><label for="decelerationFactor">Коэффициент торможения AZ</label><input id="decelerationFactor" max="10" min="0.1" name="decelerationFactor" required="" step="0.1" type="number" value="1"/></div>
</div><div class="hint">Выход AZ: <b id="speedOutput">0%</b></div><div id="elSpeedSettings"><h4>Элевация · EL</h4><div class="row">
<div class="field"><label for="forkSettings-elSpeedPercent">Рабочая скорость, %</label><input id="forkSettings-elSpeedPercent" max="100" min="1" name="elSpeedPercent" required="" step="1" type="number" value="100"/></div>
<div class="field"><label for="forkSettings-elStartSpeedPercent">Скорость старта, %</label><input id="forkSettings-elStartSpeedPercent" max="100" min="1" name="elStartSpeedPercent" required="" step="1" type="number" value="18"/></div>
<div class="field"><label for="forkSettings-elApproachSpeedPercent">Скорость у цели, %</label><input id="forkSettings-elApproachSpeedPercent" max="100" min="1" name="elApproachSpeedPercent" required="" step="1" type="number" value="18"/></div>
<div class="field"><label for="forkSettings-elAccelerationMs">Время разгона 0–100% рабочей скорости, мс</label><input id="forkSettings-elAccelerationMs" max="10000" min="100" name="elAccelerationMs" required="" step="1" type="number" value="1800"/></div>
<div class="field"><label for="forkSettings-elDecelerationMs">Время торможения 100–0% рабочей скорости, мс</label><input id="forkSettings-elDecelerationMs" max="10000" min="100" name="elDecelerationMs" required="" step="1" type="number" value="1200"/></div>
<div class="field"><label for="forkSettings-elSlowdownDegrees">Начинать замедление за, °</label><input id="forkSettings-elSlowdownDegrees" max="90" min="1" name="elSlowdownDegrees" required="" step="1" type="number" value="2"/></div>
<div class="field"><label for="elDecelerationFactor">Коэффициент торможения EL</label><input id="elDecelerationFactor" max="10" min="0.1" name="elDecelerationFactor" required="" step="0.1" type="number" value="1"/></div>
</div><div class="hint">Выход EL: <b id="elSpeedOutput">0%</b></div></div><p class="hint">Проценты задают выход привода. Замедление начинается перед зоной остановки. Коэффициент 1 — обычный спад скорости; 2 — вдвое быстрее; 0,5 — вдвое медленнее. Он действует также при снижении рабочей скорости и реверсе. Реверс: торможение и пауза 300 мс. STOP выключает привод сразу.</p></div>
<div class="form-actions"><button class="btn good" type="submit">Сохранить настройки</button></div></form></div></details></section><section class="card settings-section" id="calibrationSettings"><details class="settings-details" ontoggle="if(this.open){loadAzCalibration();loadElCalibration();}"><summary id="caltoggle"><h2>Диапазоны и калибровка<span class="section-caption">Упоры и измерения датчиков AZ / EL</span></h2></summary><div class="settings-content" id="calbody">
<div class="range-editors"><section class="range-editor settings-pane"><h3>Азимут · AZ</h3>
<div class="seg profile-options">
<button class="btn" id="azm36" onclick="setAzMode(36)">P36 · 360°</button>
<button class="btn" id="azm45" onclick="setAzMode(45)">P45 · 450°</button>
<button class="btn" id="azmc" onclick="useCustomRange()">Произвольный</button>
</div>
<p class="rangebadge" id="azrange">P--</p>
<form class="range-form" id="azRangeForm" onsubmit="saveAzRange(event)"><h4>Произвольный диапазон</h4>
<div class="row"><div class="field"><label for="azRangeForm-start">Начало диапазона, °</label><input id="azRangeForm-start" max="3600" min="-3600" name="start" required="" step="0.1" type="number" value="0"/></div>
<div class="field"><label for="azRangeForm-span">Полный ход, °</label><input id="azRangeForm-span" max="3600" min="1" name="span" required="" step="0.1" type="number" value="450"/></div></div>
<p>Конец диапазона: <b id="azRangeEnd">450°</b></p>
<p class="hint">Минус сдвигает начало против часовой стрелки, плюс — по часовой. Изменение полного хода сбрасывает калибровки произвольного профиля. При изменении только начала измерения сохраняются.</p>
<div class="form-actions"><button class="btn good" type="submit">Применить диапазон</button></div><div class="inline-message" id="azRangeMessage" role="status"></div>
</form>
</section><section class="range-editor el-setting settings-pane"><h3>Элевация · EL</h3>
<div class="seg profile-options">
<button class="btn" id="elmstd" onclick="elPost('/api/elrange',{profile:'standard'})">Стандартный · 0…180°</button>
<button class="btn" id="elmc" onclick="useCustomElRange()">Произвольный</button>
</div>
<p class="rangebadge" id="elrange">P--</p>
<form class="range-form" id="elRangeForm" onsubmit="saveElRange(event)"><h4>Произвольный диапазон</h4>
<div class="row"><div class="field"><label for="elRangeForm-start">Начало диапазона, °</label><input id="elRangeForm-start" max="3600" min="-3600" name="start" required="" step="0.1" type="number" value="0"/></div>
<div class="field"><label for="elRangeForm-span">Полный ход, °</label><input id="elRangeForm-span" max="3600" min="1" name="span" required="" step="0.1" type="number" value="180"/></div></div>
<p>Конец диапазона: <b id="elRangeEnd">180°</b></p>
<p class="hint">Начало может быть ниже или выше нулевой элевации. Изменение полного хода сбрасывает калибровки произвольного профиля. При изменении только начала измерения сохраняются.</p>
<div class="form-actions"><button class="btn good" type="submit">Применить диапазон</button></div><div class="inline-message" id="elRangeMessage" role="status"></div>
</form>
</section></div>
<div class="settings-group calibration-method"><h3>Способ калибровки</h3><div class="seg">
<button class="btn" id="mode2" onclick="setCalMode('2')">Две точки · GS-232</button>
<button class="btn" id="modem" onclick="setCalMode('multi')">Несколько точек</button>
</div><p class="hint">Активно: <b id="calmode">—</b></p></div>
<div class="calibration-grid" id="panel2"><section class="settings-pane"><h3>Азимут · AZ</h3><p class="hint">Запомните начало и конец активного диапазона.</p><div class="endpoint-buttons"><button class="btn warn" id="b2_azo" onclick="cal2('o')">O · AZ 0°</button><button class="btn warn" id="b2_azf" onclick="cal2('f')">F · AZ FULL</button></div><div class="calvalues endpoint-values"><div>Начало <b id="az2p0">—</b> мВ</div><div>Конец <b id="az2pf">—</b> мВ</div></div><div class="form-actions"><button class="btn calreset" onclick="resetCal('az','2')">Сбросить AZ</button></div></section><section class="settings-pane el-setting"><h3>Элевация · EL</h3><p class="hint">Запомните начало и конец активного диапазона.</p><div class="endpoint-buttons"><button class="btn warn" id="b2_elo" onclick="cal2('o2')">O2 · EL 0°</button><button class="btn warn" id="b2_elf" onclick="cal2('f2')">F2 · EL 180°</button></div><div class="calvalues endpoint-values"><div>Начало <b id="el2p0">—</b> мВ</div><div>Конец <b id="el2pf">—</b> мВ</div></div><div class="form-actions"><button class="btn calreset" onclick="resetCal('el','2')">Сбросить EL</button></div></section></div>
<div class="calibration-grid hide" id="panelm"><section class="settings-pane"><div class="calhead"><h3>Азимут · AZ</h3><div class="callive"><span id="calaz">--.-</span><span>°</span></div></div><div id="legacyAzPoints">
<div class="minirow">
<button class="btn warn" id="b_az0" onclick="cal('az0')">0°</button>
<button class="btn warn" id="b_az90" onclick="cal('az90')">90°</button>
<button class="btn warn" id="b_az180" onclick="cal('az180')">180°</button>
<button class="btn warn" id="b_az270" onclick="cal('az270')">270°</button>
<button class="btn warn" id="b_az360" onclick="cal('az360')">360°</button>
<button class="btn warn" id="b_az450" onclick="cal('az450')">450°</button>
</div>
<div class="calvalues">
<div>0° <b id="azp0">-</b></div><div>90° <b id="azp90">-</b></div><div>180° <b id="azp180">-</b></div>
<div>270° <b id="azp270">-</b></div><div>360° <b id="azp360">-</b></div><div id="azv450">450° <b id="azp450">-</b></div>
</div>
</div><div class="hide" id="customAzPoints"><p class="hint">Запомните оба упора и все добавленные точки. Угол указывается непрерывно, например −60°, 0°, 360°.</p>
<div class="point-list" id="azPointList"></div><form class="inline-form" onsubmit="addAzPoint(event)"><div class="field"><label for="azPointAngle">Угол новой точки, °</label><input id="azPointAngle" required="" step="0.1" type="number"/></div><button class="btn" type="submit">Добавить</button></form></div><div class="form-actions"><button class="btn calreset" onclick="resetCal('az','multi')">Сбросить AZ · <span id="resetazrange">P--</span></button></div></section><section class="settings-pane el-setting"><div class="calhead"><h3>Элевация · EL</h3><div class="callive"><span id="calel">--.-</span><span>°</span></div></div><div id="legacyElPoints"><div class="minirow">
<button class="btn warn" id="b_el0" onclick="cal('el0')">0°</button>
<button class="btn warn" id="b_el45" onclick="cal('el45')">45°</button>
<button class="btn warn" id="b_el90" onclick="cal('el90')">90°</button>
<button class="btn warn" id="b_el135" onclick="cal('el135')">135°</button>
<button class="btn warn" id="b_el180" onclick="cal('el180')">180°</button>
</div>
<div class="calvalues">
<div>0° <b id="elp0">-</b></div><div>45° <b id="elp45">-</b></div><div>90° <b id="elp90">-</b></div>
<div>135° <b id="elp135">-</b></div><div>180° <b id="elp180">-</b></div>
</div></div><div class="hide el-setting" id="customElPoints"><p class="hint">Запомните оба упора и все промежуточные точки. Напряжение датчика должно последовательно расти или убывать.</p><div class="point-list" id="elPointList"></div><form class="inline-form" onsubmit="addElPoint(event)"><div class="field"><label for="elPointAngle">Угол новой точки EL, °</label><input id="elPointAngle" required="" step="0.1" type="number"/></div><button class="btn" type="submit">Добавить</button></form></div><div class="form-actions"><button class="btn calreset" onclick="resetCal('el','multi')">Сбросить EL</button></div></section></div>
</div></details></section><section class="card settings-section" id="presetSettings"><details class="settings-details"><summary><h2>Предустановки<span class="section-caption">Четыре сохранённых положения</span></h2></summary><div class="settings-content"><form id="presetEditor" onsubmit="savePreset(event)">
<div class="row"><div class="field"><label for="presetEditor-slot">Кнопка</label><select id="presetEditor-slot" name="slot" onchange="fillPreset()"><option value="1">1</option><option value="2">2</option><option value="3">3</option><option value="4">4</option></select></div>
<div class="field"><label for="presetEditor-label">Название</label><input id="presetEditor-label" maxlength="24" name="label" required=""/></div>
<div class="field"><label for="presetEditor-az">AZ, °</label><input id="presetEditor-az" max="450" min="0" name="az" required="" step="0.1" type="number"/></div>
<div class="field el-setting"><label for="presetEditor-el">EL, °</label><input id="presetEditor-el" max="180" min="0" name="el" step="0.1" type="number" value="0"/></div></div>
<div class="form-actions"><button class="btn good" type="submit">Сохранить предустановку</button></div></form></div></details></section><section class="card settings-section" id="hudPanel"><details class="settings-details"><summary><h2>Пульт и связь<span class="section-caption">Роль устройства и подключение повороток</span></h2></summary><div class="settings-content">
<h3 id="hudHeading">Контроллер поворотки</h3>
<div class="hint" id="hudLink" role="status"></div>
<div class="hide" id="hudControls">
<p class="hint">Команды программ, подключённых к HUD, идут выбранной поворотке.</p>
<div class="device-list" id="hudPeers"></div>
<div class="device-pagination"><button class="btn" id="hudPrev" onclick="hudPage(-1)">Назад</button><span id="hudPageInfo"></span><button class="btn" id="hudNext" onclick="hudPage(1)">Далее</button></div>
<section class="settings-group"><h3>Добавить поворотку</h3>
<div class="form-actions"><button class="btn" onclick="hudSearch()">Найти в сети</button><button class="btn" id="hudUartSearch" onclick="hudSearch(true)">Найти по UART</button></div>
<form class="device-address" onsubmit="event.preventDefault();hudSearch(false,true)">
<div class="device-fields"><div class="field"><label for="hudAddress">IP контроллера</label><input id="hudAddress" placeholder="192.168.4.1"/></div><div class="field"><label for="hudPeerPort">UDP-порт HUD</label><input id="hudPeerPort" max="65535" min="1" type="number" value="4534"/></div></div>
<button class="btn">Проверить адрес</button></form>
<div class="device-list" id="hudDiscovered"></div>
<div class="device-pagination"><button class="btn" onclick="hudDiscoveryPage(-1)">Назад</button><span id="hudDiscoveryInfo"></span><button class="btn" onclick="hudDiscoveryPage(1)">Далее</button></div>
</section></div>
<section class="settings-group"><h3>Роль устройства и связь</h3>
<form id="hudConfig" onsubmit="saveHudDevice(event)"><div class="device-fields">
<div class="field"><label for="deviceRole">Роль после перезагрузки</label><select id="deviceRole" name="role"><option value="controller">Контроллер поворотки</option><option value="hud">HUD — удалённый пульт</option></select></div>
<div class="field"><label for="deviceWifi">Подключение по Wi-Fi</label><select id="deviceWifi" name="wifi"><option value="existing">Через роутер / обычный режим</option><option value="host">Общая точка доступа</option><option value="client">Клиент общей точки доступа</option></select></div>
<div class="field"><label for="deviceUart">Проводной UART</label><select id="deviceUart" name="uart"><option value="0">GS-232 для программ, 9600</option><option value="1">HUD ↔ поворотка, 115200</option></select></div>
<div class="field"><label for="devicePort">UDP-порт HUD</label><input id="devicePort" max="65535" min="1" name="port" required="" type="number" value="4534"/></div>
</div><p class="hint">Для общей сети одно устройство создаёт точку доступа, остальные подключаются к ней через настройки Wi-Fi ниже. UART HUD занимает существующие провода GS-232.</p>
<div class="form-actions"><button class="btn good" type="submit">Сохранить и перезагрузить</button></div></form>
</section></div></details></section><section class="card settings-section" id="networkSettings"><details class="settings-details"><summary><h2>Сеть и подключение<span class="section-caption">Домашний Wi-Fi, точка доступа и Bluetooth</span></h2></summary><div class="settings-content"><h3>Текущее подключение</h3><div class="kv">
<div>IP в сети: <b id="ip">-</b></div><div>IP точки доступа: <b id="apip">-</b></div><div>TCP rotctld: <b>4533</b></div><div>Режим: <b id="mode">-</b></div><div>Уровень Wi-Fi: <b id="rssi">-</b></div><div>Качество: <b id="sig">-</b></div>
<div class="hide" id="btlinkrow">Bluetooth-соединение: <b id="btlinkstate" role="status">-</b></div>
<div><span id="btlabel">Bluetooth</span>: <b id="btstate" role="status">-</b></div><div id="bttransport">-</div>
<div class="hide" id="btmacrow">Bluetooth MAC: <b class="mono" id="btmac">-</b></div></div><div class="hint hide" id="bthelp">Этот MAC нужен в Look4Sat. Подключение видно и без передачи команд; сопряжение в Android само по себе не означает активное соединение.</div>
<section class="settings-group"><h3>Домашняя сеть Wi-Fi</h3><div class="row"><div class="field"><label for="ssid">Имя сети (SSID)</label><input autocomplete="off" id="ssid"/></div><div class="field"><label for="pass">Пароль сети</label><input autocomplete="new-password" id="pass" type="password"/></div></div>
<div class="form-actions"><button class="btn good" id="wifiSaveButton" onclick="wifi()">Сохранить Wi-Fi</button><span class="hint">Устройство перезагрузится.</span></div><div aria-live="polite" class="inline-message" id="wifiMessage" role="status"></div></section>
<section class="settings-group"><h3>Точка доступа устройства</h3><div class="field"><label for="appass">Пароль точки доступа</label><input autocomplete="new-password" id="appass" maxlength="63" minlength="8" placeholder="Пустое поле — открытая сеть" type="password"/></div><p class="hint">От 8 до 63 символов. Оставьте поле пустым, чтобы открыть сеть без пароля.</p><div class="form-actions"><button class="btn good" id="apSaveButton" onclick="saveAp()">Сохранить точку доступа</button><span class="hint">Устройство перезагрузится.</span></div></section></div></details></section><section class="card settings-section" id="signalSettings"><details class="settings-details" ontoggle="if(this.open)loadRssi()"><summary><h2>Уровень сигнала<span class="section-caption">История подключения к домашнему Wi-Fi</span></h2></summary><div class="settings-content">
<div class="period-options"><button class="btn rbtn active" data-r="hour">Час</button><button class="btn rbtn" data-r="day">Сутки</button><button class="btn rbtn" data-r="week">Неделя</button><button class="btn rbtn" data-r="month">Месяц</button><button class="btn rbtn" data-r="year">Год</button></div>
<canvas height="260" id="rchart" width="900"></canvas>
<div class="kv"><div>Мин.: <b id="rmin">-</b></div><div>Макс.: <b id="rmax">-</b></div><div>Среднее: <b id="ravg">-</b></div><div>Измерений: <b id="rcount">0</b></div></div></div></details></section></section></div>
<div id="settingsFeedback" class="action-feedback" hidden><div role="status" aria-live="polite" aria-atomic="true"><strong id="settingsFeedbackTitle"></strong><span id="settingsFeedbackText"></span></div><button type="button" aria-label="Закрыть уведомление" onclick="this.parentElement.hidden=true">×</button></div>
<script>
function setText(node,value){const text=String(value??'');if(node.textContent!==text)node.textContent=text;}
let settingsActionSequence=0,settingsFeedbackTimer;
const settingsRequests=new Set();
function settingsFeedback(message,state='error'){
 const box=document.getElementById('settingsFeedback');box.dataset.state=state;box.hidden=document.getElementById('settingsPanel').hidden;
 setText(document.getElementById('settingsFeedbackTitle'),state==='pending'?'Выполняется…':state==='success'?'Готово':'Не выполнено');
 setText(document.getElementById('settingsFeedbackText'),message);
 clearTimeout(settingsFeedbackTimer);if(state==='success')settingsFeedbackTimer=setTimeout(()=>{box.hidden=true;},6000);
}
function beginSettingsAction(message,key){
 if(settingsRequests.has(key))return null;
 settingsRequests.add(key);const sequence=++settingsActionSequence;
 const active=document.activeElement,source=document.getElementById('settingsPanel').contains(active)?active:null;
 const button=source?.closest('button')||source?.closest('form')?.querySelector('button[type=submit]');
 const wasDisabled=button?.disabled;
 if(button){button.disabled=true;button.setAttribute('aria-busy','true');}
 settingsFeedback(message,'pending');
 return {
  update(text){if(sequence===settingsActionSequence)settingsFeedback(text,'pending');},
  finish(ok,text){settingsRequests.delete(key);if(button){button.disabled=wasDisabled;button.removeAttribute('aria-busy');}if(sequence===settingsActionSequence)settingsFeedback(text,ok?'success':'error');}
 };
}
async function settingsRequest(path,options={}){
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),12000);
 try{const response=await fetch(path,{cache:'no-store',...options,signal:controller.signal});const text=await response.text();return {ok:response.ok,status:response.status,text,json:()=>JSON.parse(text)};}
 finally{clearTimeout(timer);}
}


function selectTab(name,updateHash=true){
 const settings=name==='settings',opening=settings&&document.getElementById('settingsPanel').hidden;
 document.body.classList.toggle('settings-view',settings);
 for(const tab of ['control','settings']){
  const active=(tab==='settings')===settings,button=document.getElementById(tab+'Tab');
  document.getElementById(tab+'Panel').hidden=!active;
  button.classList.toggle('active',active);button.setAttribute('aria-selected',String(active));button.tabIndex=active?0:-1;
 }
 if(updateHash&&location.hash!==(settings?'#settings':'#control'))location.hash=settings?'settings':'control';
 if(!settings)document.getElementById('settingsFeedback').hidden=true;
 if(opening)loadRssi();
 if(updateHash)(settings?document.querySelector('.wrap'):window).scrollTo({top:0,behavior:'instant'});
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

// Live commands are never replayed after reconnecting.
const livePaths=new Set(['/api/target','/api/move','/api/mode','/api/execute','/api/preset/run','/api/hud/select']);
const live={socket:null,ready:false,boot:null,revision:0,next:0,pending:new Map(),state:null,retry:null,last:0};
let httpState=null,liveFrame=0;
function renderLive(){liveFrame=0;if(httpState)renderStatus(live.ready&&live.state?{...httpState,...live.state}:httpState);}
function queueLiveRender(){if(!liveFrame)liveFrame=requestAnimationFrame(renderLive);}
function connectLive(port){
 if(port===0||live.socket||live.retry||window.G5500_HTTP_ONLY)return;
 const socket=new WebSocket('ws://'+location.hostname+':'+(port||81)+'/api/live');live.socket=socket;
 socket.onmessage=event=>{
  let message;try{message=JSON.parse(event.data)}catch(e){socket.close();return}
  if(message.v!==1||!message.boot)return;
  live.last=Date.now();
  if(message.type==='state'){
   if(live.boot&&message.boot!==live.boot){for(const p of live.pending.values()){clearTimeout(p.timer);p.reject(new Error('restarted'))}live.pending.clear();live.revision=0;live.state=null;}
   if(live.ready&&message.boot===live.boot&&message.revision<=live.revision)return;
   const changed=live.state&&live.state.hudSelectionEpoch!==message.state.hudSelectionEpoch;
   live.boot=message.boot;live.revision=message.revision;live.state=message.state;live.ready=true;queueLiveRender();
   if(changed)setTimeout(()=>upd(true),0);
  }else if(message.type==='result'&&message.boot===live.boot){
   const pending=live.pending.get(message.id);if(!pending)return;
   if(message.result.result==='deferred'){document.getElementById('forkMessage').textContent='Ожидаем подтверждение поворотки…';return;}
   clearTimeout(pending.timer);live.pending.delete(message.id);
   pending.resolve({status:message.result.ok?200:409,json:async()=>message.result});
  }
 };
 socket.onclose=()=>{
  if(live.socket!==socket)return;
  live.socket=null;live.ready=false;live.state=null;live.boot=null;live.revision=0;
  for(const p of live.pending.values()){clearTimeout(p.timer);p.reject(new Error('disconnected'))}live.pending.clear();
  live.retry=setTimeout(()=>{live.retry=null;connectLive(port)},1000);upd(true);
 };
 socket.onerror=()=>socket.close();
}
function sendLive(path,body){
 if(!live.ready||live.socket?.readyState!==WebSocket.OPEN)return Promise.reject(new Error('offline'));
 const stop=path==='/api/move'&&['S','A','E'].includes(body.d);
 if(live.pending.size>=(stop?9:8)||live.socket.bufferedAmount>(stop?4096:2048))return Promise.resolve({status:503,json:async()=>({ok:false,result:'busy'})});
 const id=++live.next,packet=new URLSearchParams({...body,v:1,cid:id,boot:live.boot,selection:body.selection??window._forkState?.hudSelectionEpoch??0,path}).toString();
 return new Promise((resolve,reject)=>{
  const timer=setTimeout(()=>{live.pending.delete(id);resolve({status:504,json:async()=>({ok:false,result:'timeout'})})},1500);
  live.pending.set(id,{resolve,reject,timer});
  try{live.socket.send(packet)}catch(e){clearTimeout(timer);live.pending.delete(id);reject(e)}
 });
}
setInterval(()=>{if(live.ready&&Date.now()-live.last>1500)live.socket.close()},250);
document.addEventListener('visibilitychange',()=>{if(!document.hidden){queueLiveRender();upd(true)}});
async function forkPost(path,body){
 const success={'/api/settings':'Настройки сохранены','/api/preset':'Предустановка сохранена','/api/device':'Настройки устройства сохранены','/api/hud/select':'Поворотка выбрана','/api/hud/peers':body.action==='remove'?'Поворотка удалена':body.name?'Имя поворотки сохранено':'Поворотка добавлена','/api/hud/discover':'Поиск устройств запущен'}[path];
 const notice=success?beginSettingsAction(path==='/api/hud/discover'?'Поиск устройств…':'Ожидаем подтверждение устройства…',path+JSON.stringify(body)):null;
 if(success&&!notice)return false;
 try{
  const selection=body.selection??window._forkState?.hudSelectionEpoch,name=window._forkState?.hudName;
  if(window._forkState?.deviceRole==='hud'&&['/api/target','/api/move','/api/mode','/api/execute','/api/preset','/api/preset/run'].includes(path))body={...body,selection};
  if(livePaths.has(path)&&window._forkState?.azRangeRevision!==undefined)body={...body,rangeRevision:window._forkState.azRangeRevision};
  const fastStop=path==='/api/move'&&['S','A','E'].includes(body.d);
  const usingLive=live.ready&&livePaths.has(path);
  const request=notice?settingsRequest:fetch;
  const r=usingLive?await sendLive(path,body):await request(fastStop?path+'?d='+body.d+(body.selection===undefined?'':'&selection='+body.selection):path,fastStop?{cache:'no-store'}:{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body)});
  let j=await r.json();
  if(r.status===202&&j.requestId){
    document.getElementById('forkMessage').textContent='Ожидаем подтверждение поворотки…';notice?.update('Ожидаем подтверждение поворотки…');
    const started=Date.now();
    while(j.result==='deferred'&&Date.now()-started<3000){await new Promise(resolve=>setTimeout(resolve,100));j=await (await request('/api/hud/command?id='+j.requestId,{cache:'no-store'})).json();}
    if(j.result==='deferred')j={ok:false,result:'timeout'};
  }
  const messages={deferred:'Ожидаем подтверждение',timeout:'Нет подтверждения — проверьте состояние поворотки',unavailable:'Нет связи с повороткой',busy:'Поворотка занята, повторите команду',cancelled:'Команда отменена',storage_full:'Не хватает памяти для сохранения',hud_port_conflict:'Порт HUD совпадает с портом логгера',controller_settings_only:'Эти настройки доступны на контроллере поворотки',restart:'Сохранено. Устройство перезагружается',searching:'Поиск устройств…',invalid_or_port_conflict:'Проверьте настройки и порты',uart_in_use_by_hc05:'UART занят HC-05: нужна сборка без Bluetooth',not_calibrated:'Сначала выполните калибровку',invalid:'Проверьте значения',ignored:'Сейчас выполняется предустановка',pending:'Цель ожидает подтверждения',ok:'Готово'};
  document.getElementById('forkMessage').textContent=(messages[j.result]||'Ошибка')+(path!=='/api/hud/select'&&name&&selection!==window._forkState?.hudSelectionEpoch?' · команда для '+name:'');
  const ok=r.status<400&&!!j.ok;
  notice?.finish(ok,ok&&j.result==='ok'?success:(messages[j.result]||'Не удалось выполнить действие'));
  if(!usingLive)await upd(true);return ok;
 }catch(e){const message='Подтверждение не получено. Проверьте связь и состояние устройства перед повтором.';document.getElementById('forkMessage').textContent=message;notice?.finish(false,message);return false}
}
function fillPreset(){
 const f=document.getElementById('presetEditor'),p=window._forkState?.presets[Number(f.elements.slot.value)-1];if(!p)return;
 f.elements.label.value=p.label;f.elements.az.value=p.target.az;f.elements.el.value=p.target.el;
}
async function savePreset(e){e.preventDefault();await forkPost('/api/preset',Object.fromEntries(new FormData(e.target)))}
async function saveFork(e){e.preventDefault();const body=Object.fromEntries(new FormData(e.target));if(window._forkState?.build?.drive==='YAESU')body.speedEnabled=e.target.elements.speedEnabled.checked?'true':'false';else delete body.speedEnabled;if(window._forkState?.build?.display==='CROWPANEL_21')body.encoderInverted=e.target.elements.encoderInverted.checked?'true':'false';else delete body.encoderInverted;if(window._forkState?.deviceRole==='hud')for(const key of Object.keys(body))if(!['rotorName','n1mmPort','pstPort','reportPort','reportHost','encoderInverted'].includes(key))delete body[key];if(await forkPost('/api/settings',body))delete e.target.dataset.dirty;}
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
 setText(document.getElementById('modelTitle'),j.deviceRole==='hud'?'HUD / '+(j.build?.board||'ESP32'):'YAESU '+(g800?'G-800DXA':'G-5500')+' / '+(j.build?.board||'ESP32'));
 document.getElementById('followMode').value=j.controlMode;
 setText(document.getElementById('hourLeft'),j.controlMode==='auto_1h'?'Осталось '+Math.ceil(j.remainingMs/60000)+' мин':'');
 setText(document.getElementById('waitingTarget'),j.pendingValid?'Ожидает: '+(j.pending.hasAz?'AZ '+j.pending.az.toFixed(1)+'° ':'')+(j.pending.hasEl&&!g800?'EL '+j.pending.el.toFixed(1)+'°':''):'Нет ожидающей цели');
 updateTargetButton(j);
 const buttons=document.getElementById('presetButtons');
 if(!buttons.children.length)for(let slot=1;slot<=4;slot++){const b=document.createElement('button');b.className='btn';b.onclick=()=>forkPost('/api/preset/run',{slot});buttons.append(b)}
 j.presets.forEach((p,i)=>{const b=buttons.children[i];setText(b,p.label);b.disabled=!p.valid;b.classList.toggle('active',j.presetSlot===i&&j.presetActive)});
 const elNodes=[document.getElementById('el').closest('.card'),document.getElementById('tel').closest('.field'),document.getElementById('el2p0').parentElement,document.getElementById('elp0').parentElement.parentElement,document.getElementById('calel').closest('.calhead'),document.getElementById('b_el0').parentElement,document.getElementById('b2_elo'),document.getElementById('b2_elf'),...Array.from(document.querySelectorAll('button')).filter(b=>b.textContent==='RESET EL'),...document.querySelectorAll('.el-setting,.movebtn[data-dir="U"],.movebtn[data-dir="D"]')];
 [...elNodes,...document.querySelectorAll('.el-help')].forEach(n=>{if(n)n.classList.toggle('hide',g800)});
 const independent=['SEPARATE_PWM','BTS7960','SIMULATED'].includes(j.build?.drive);
 document.getElementById('speedSettings').classList.toggle('hide',j.build?.drive==='NONE'||(!g800&&!independent));
 document.getElementById('analogSpeedToggle').classList.toggle('hide',independent);
 document.getElementById('elSpeedSettings').classList.toggle('hide',g800||!independent);
 const btBackend=j.build?.bluetoothBackend||(j.build?.bluetooth===false?'NONE':'HC05');
 const btName=btBackend==='ESP32_SPP'?'Встроенный Bluetooth SPP':btBackend==='HC05'?'HC-05':'Bluetooth отключён';
 const nativeBt=btBackend==='ESP32_SPP';
 setText(document.getElementById('buildInfo'),j.build?`${j.build.board} · ${j.build.drive} · ${j.build.display} · ${btName}`:'');
 setText(document.getElementById('btlabel'),nativeBt?'Канал SPP':btName);
 setText(document.getElementById('bttransport'),btBackend==='ESP32_SPP'?'G5500-Rotator · SPP / виртуальный COM':btBackend==='HC05'?'HC-05 UART: 9600 baud':'');
 for(const id of ['btlinkrow','btmacrow','bthelp'])document.getElementById(id).classList.toggle('hide',!nativeBt);
 const btLink=!!(j.btLinkConnected??j.btConnected),linkState=document.getElementById('btlinkstate');
 setText(linkState,btLink?'Подключено':'Нет подключения');linkState.style.color=btLink?'#24d18d':'#e5a94f';
 setText(document.getElementById('btmac'),j.btMac||'Недоступен');
 document.getElementById('movementButtons').classList.toggle('az-only',g800);
 const f=document.getElementById('forkSettings');
 for(const key of ['model','rotorName','n1mmPort','pstPort','reportPort','reportHost','speedPercent','startSpeedPercent','approachSpeedPercent','accelerationMs','decelerationMs','slowdownDegrees','coastDegrees','decelerationFactor'])if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements[key].value=j[key];
 for(const key of ['speedPercent','startSpeedPercent','approachSpeedPercent','accelerationMs','decelerationMs','slowdownDegrees','coastDegrees','decelerationFactor']){const elKey='el'+key[0].toUpperCase()+key.slice(1);if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements[elKey].value=j[elKey];if(key.endsWith('Percent'))f.elements[key].min=independent?1:25;}
 setText(document.getElementById('elSpeedOutput'),(j.elSpeedOutputPercent||0).toFixed(1)+'%');
 setText(document.getElementById('speedOutput'),j.speedOutputPercent.toFixed(1)+'%');
 if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements.speedEnabled.checked=j.speedEnabled;
 document.getElementById('encoderSettings').classList.toggle('hide',j.build?.display!=='CROWPANEL_21');
 if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements.encoderInverted.checked=j.encoderInverted??true;
 document.querySelector('#presetEditor [name=el]').min=j.elStart??0;document.querySelector('#presetEditor [name=el]').max=j.elMax??180;
 document.querySelector('#presetEditor [name=az]').min=j.azStart??0;document.querySelector('#presetEditor [name=az]').max=j.azMax??(j.azMode===36?360:450);
 if(first)fillPreset();
 const faults={calibration_storage_invalid:'Ошибка сохранённой калибровки: откройте настройки AZ',calibration_save_failed:'Не удалось сохранить калибровку',target_not_calibrated:'Сначала выполните калибровку',continuous_motion_timeout:'Превышено время непрерывного вращения',udp_bind_failed:'Не удалось открыть UDP-порт',pwm_attach_failed:'Не удалось включить регулировку скорости'};if(j.fault)setText(document.getElementById('forkMessage'),faults[j.fault]||'Ошибка управления');
}

async function q(u){try{if((live.ready||window._forkState?.deviceRole==='hud')&&(u.startsWith('/api/move?')||u.startsWith('/api/target?'))){const url=new URL(u,location.origin);await forkPost(url.pathname,Object.fromEntries(url.searchParams));return ''}return await (await fetch(u,{cache:'no-store'})).text()}catch(e){return''}}
async function mv(d){await q('/api/move?d='+d)}
async function st(){await q('/api/move?d=S')}
async function stopAxis(d,selection){
  const scope=selection===undefined?'':'&selection='+selection;
  if(d==='L'||d==='R') await q('/api/move?d=A'+scope);
  else if(d==='U'||d==='D') await q('/api/move?d=E'+scope);
}
async function go(){if(taz.value==='')return;const body={az:taz.value};if(window._forkState?.model!=='G800DXA'){if(tel.value==='')return;body.el=tel.value}await forkPost('/api/target',body)}
async function cal(p){await calibrationAction('/api/cal?p='+p,'Точка '+p.slice(0,2).toUpperCase()+' '+p.slice(2)+'° запомнена')}
async function cal2(p){await calibrationAction('/api/cal2?p='+p,(p[0]==='f'?'Конец':'Начало')+' диапазона '+(p.endsWith('2')?'EL':'AZ')+' запомнено')}
async function setCalMode(m){await calibrationAction('/api/calmode?m='+m,m==='multi'?'Выбрана многоточечная калибровка':'Выбрана двухточечная калибровка')}
async function calibrationAction(path,success){
 const notice=beginSettingsAction('Сохранение калибровки…',path);if(!notice)return false;
 try{const r=await settingsRequest(path);if(!r.ok){notice.finish(false,r.status===403?'Калибровка доступна на контроллере поворотки':'Не удалось сохранить калибровку. Повторите после проверки устройства.');return false;}
 notice.finish(true,success);await upd(true);await Promise.all([loadAzCalibration(),loadElCalibration()]);return true;
 }catch(e){notice.finish(false,'Подтверждение не получено. Проверьте связь и состояние калибровки перед повтором.');return false;}
}
async function setAzMode(m){await azPost('/api/azrange',{profile:m===36?'p36':'p45'})}
async function resetCal(axis,cm){
  const name=axis==='az'?'АЗИМУТА':'ЭЛЕВАЦИИ';
  const mn=cm==='2'?'GS-232 2-POINT':'MULTI-POINT';
  const rr=axis==='az'?' '+(window._azMode===0?'Произвольный':'P'+window._azMode):'';
  if(!confirm('Сбросить '+mn+' калибровку '+name+rr+'?')) return;
  await calibrationAction('/api/calreset?axis='+axis+'&mode='+cm,'Калибровка '+axis.toUpperCase()+' сброшена');
}
function azEndPreview(){const f=document.getElementById('azRangeForm');azRangeEnd.textContent=(Number(f.elements.start.value)+Number(f.elements.span.value)).toFixed(1)+'°'}
document.getElementById('azRangeForm').addEventListener('input',e=>{e.currentTarget.dataset.dirty='1';azEndPreview()});
async function loadAzCalibration(){
 if(window._forkState?.deviceRole==='hud')return;
 try{const r=await fetch('/api/azcal',{cache:'no-store'});if(!r.ok)return;const j=await r.json();window._azCal=j;
 const f=document.getElementById('azRangeForm');if(!f.dataset.dirty&&!f.contains(document.activeElement)){f.elements.start.value=j.customStart;f.elements.span.value=j.customSpan;azEndPreview()}
 azPointList.replaceChildren();
 if(j.profile==='custom')for(const p of j.points){const row=document.createElement('div');row.className='point-row';const label=document.createElement('span');label.textContent=p.angle+'° · '+(p.mv<0?'не записано':p.mv+' мВ');row.append(label);
 const capture=document.createElement('button');capture.className='btn '+(p.mv>=0?'good':'warn');capture.textContent='Запомнить положение';capture.onclick=()=>azPost('/api/azcal',{op:'capture',angle:p.angle});const actions=document.createElement('div');actions.className='point-actions';actions.append(capture);row.append(actions);
 if(!p.endpoint){const del=document.createElement('button');del.className='btn';del.textContent='Удалить';del.onclick=()=>azPost('/api/azcal',{op:'delete',angle:p.angle});actions.append(del)}azPointList.append(row)}
 if(j.storageFault)azRangeMessage.textContent='Ошибка сохранённой калибровки. Сбросьте калибровку AZ и настройте её заново.';
 }catch(e){azRangeMessage.textContent='Не удалось прочитать калибровку'}
}
async function axisCalibrationPost(axis,path,body,notice=null){
 const label=axis.toUpperCase();notice=notice||beginSettingsAction('Сохранение '+label+'…',path+JSON.stringify(body));if(!notice)return false;
 notice.update('Сохранение '+label+'…');
 const message=document.getElementById(axis+'RangeMessage');
 try{const r=await settingsRequest(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body)});const j=r.json();
 if(!r.ok||j.ok===false){const error=j.error==='invalid_point'?'Проверьте угол: точка должна быть внутри диапазона, без повторов; максимум 16 точек.':j.error==='invalid_range'?'Проверьте начало и полный ход.':'Не удалось сохранить настройки '+label;setText(message,error);notice.finish(false,error);return false;}
 const text=body.op?(body.op==='capture'?'Положение '+label+' '+body.angle+'° запомнено':body.op==='add'?'Точка '+label+' '+body.angle+'° добавлена':'Точка '+label+' '+body.angle+'° удалена'):'Диапазон '+label+' сохранён';
 delete document.getElementById(axis+'RangeForm').dataset.dirty;setText(message,text);notice.finish(true,text);
 await upd(true);await (axis==='az'?loadAzCalibration():loadElCalibration());return true;
 }catch(e){const error='Подтверждение не получено. Проверьте связь и настройки '+label+' перед повтором.';setText(message,error);notice.finish(false,error);return false;}
}
async function azPost(path,body){return axisCalibrationPost('az',path,body)}
async function saveAzRange(e){e.preventDefault();const f=e.target;await azPost('/api/azrange',{profile:'custom',start:f.elements.start.value,span:f.elements.span.value})}
async function useCustomAxisRange(axis){
 const notice=beginSettingsAction('Загрузка произвольного диапазона '+axis.toUpperCase()+'…',axis+'custom');if(!notice)return false;
 try{const r=await settingsRequest('/api/'+axis+'cal');if(!r.ok){notice.finish(false,'Не удалось прочитать диапазон. Повторите после проверки устройства.');return false;}
 const j=r.json();return await axisCalibrationPost(axis,'/api/'+axis+'range',{profile:'custom',start:j.customStart,span:j.customSpan},notice);
 }catch(e){notice.finish(false,'Не удалось прочитать диапазон. Проверьте связь с устройством.');return false;}
}
async function useCustomRange(){return useCustomAxisRange('az')}
async function addAzPoint(e){e.preventDefault();if(await azPost('/api/azcal',{op:'add',angle:azPointAngle.value}))azPointAngle.value=''}
function elEndPreview(){const f=document.getElementById('elRangeForm');elRangeEnd.textContent=(Number(f.elements.start.value)+Number(f.elements.span.value)).toFixed(1)+'°'}
document.getElementById('elRangeForm').addEventListener('input',e=>{e.currentTarget.dataset.dirty='1';elEndPreview()});
async function loadElCalibration(){
 if(window._forkState?.deviceRole==='hud'||window._forkState?.model==='G800DXA')return;
 try{const r=await fetch('/api/elcal',{cache:'no-store'});if(!r.ok)return;const j=await r.json();window._elCal=j;
 const f=document.getElementById('elRangeForm');if(!f.dataset.dirty&&!f.contains(document.activeElement)){f.elements.start.value=j.customStart;f.elements.span.value=j.customSpan;elEndPreview()}
 elPointList.replaceChildren();
 if(j.profile==='custom')for(const p of j.points){const row=document.createElement('div');row.className='point-row';const label=document.createElement('span');label.textContent=p.angle+'° · '+(p.mv<0?'не записано':p.mv+' мВ');row.append(label);
 const capture=document.createElement('button');capture.className='btn '+(p.mv>=0?'good':'warn');capture.textContent='Запомнить положение';capture.onclick=()=>elPost('/api/elcal',{op:'capture',angle:p.angle});const actions=document.createElement('div');actions.className='point-actions';actions.append(capture);row.append(actions);
 if(!p.endpoint){const del=document.createElement('button');del.className='btn';del.textContent='Удалить';del.onclick=()=>elPost('/api/elcal',{op:'delete',angle:p.angle});actions.append(del)}elPointList.append(row)}
 if(j.storageFault)elRangeMessage.textContent='Ошибка сохранённой калибровки. Сбросьте калибровку EL и настройте её заново.';
 }catch(e){elRangeMessage.textContent='Не удалось прочитать калибровку'}
}
async function elPost(path,body){return axisCalibrationPost('el',path,body)}
async function saveElRange(e){e.preventDefault();const f=e.target;await elPost('/api/elrange',{profile:'custom',start:f.elements.start.value,span:f.elements.span.value})}
async function useCustomElRange(){return useCustomAxisRange('el')}
async function addElPoint(e){e.preventDefault();if(await elPost('/api/elcal',{op:'add',angle:elPointAngle.value}))elPointAngle.value=''}
function markCalBtn(id, ok){
  const b=document.getElementById(id);
  if(!b) return;
  b.classList.toggle('done', !!ok);
  b.classList.toggle('warn', !ok);
}
async function wifi(){
 const message=document.getElementById('wifiMessage');
 const network=document.getElementById('ssid').value,password=document.getElementById('pass').value;
 if(!network){setText(message,'Укажите SSID сети Wi-Fi.');settingsFeedback(message.textContent);return;}
 if(!confirm('Сохранить Wi‑Fi и перезагрузить контроллер?'))return;
 const notice=beginSettingsAction('Сохранение настроек Wi-Fi…','wifi');if(!notice)return;
 setText(message,'Сохранение настроек Wi-Fi…');
 try{
  const response=await settingsRequest('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:network,pass:password})});
  const text=response.ok?'Настройки Wi-Fi сохранены. Устройство перезагружается.':'Не удалось сохранить Wi-Fi. Проверьте SSID и пароль.';
  setText(message,text);notice.finish(response.ok,text);
  if(response.ok)document.getElementById('pass').value='';
 }catch(e){const text='Подтверждение не получено. Проверьте адрес контроллера в домашней сети. Если подключения нет, подключитесь к сервисной сети G5500-HUD-… или G5500-Rotator-… и проверьте SSID.';setText(message,text);notice.finish(false,text);}
}
async function saveAp(){
 const p=appass.value;
 if(p.length>0&&(p.length<8||p.length>63)){settingsFeedback('Пароль точки доступа должен содержать 8–63 символа. Пустое поле — открытая сеть.');return;}
 if(!confirm(p.length?'Сохранить пароль точки доступа и перезагрузить контроллер?':'Сделать точку доступа открытой и перезагрузить контроллер?'))return;
 const notice=beginSettingsAction('Сохранение точки доступа…','ap');if(!notice)return;
 try{const r=await settingsRequest('/api/ap?pass='+encodeURIComponent(p));notice.finish(r.ok,r.ok?'Настройки точки доступа сохранены. Устройство перезагружается.':'Не удалось сохранить настройки точки доступа.');if(r.ok)appass.value='';}
 catch(e){notice.finish(false,'Подтверждение не получено. Проверьте связь и настройки точки доступа перед повтором.');}
}
function pct(v,m){v=Math.max(0,Math.min(m,v));return(v/m*100).toFixed(1)+'%'}
let statusPending=false,statusRefreshQueued=false;
async function upd(force=false){
if(live.ready&&!force)return;
if(statusPending){if(force)statusRefreshQueued=true;return;}
statusPending=true;
const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),5000);
try{let j=await (await fetch('/api/status',{cache:'no-store',signal:controller.signal})).json();
httpState=j;connectLive(j.livePort);queueLiveRender();
}catch(e){
 const wifi=document.getElementById('statusWifi');setText(wifi,'Нет связи');wifi.className='offline';wifi.title='Не удалось обновить статус устройства';
 for(const id of ['statusBt','statusSignal','statusMemory','statusUptime']){const node=document.getElementById(id);setText(node,'—');node.className='';}
}finally{clearTimeout(timeout);statusPending=false;if(statusRefreshQueued){statusRefreshQueued=false;queueMicrotask(()=>upd(true));}}}
function renderStatus(j){
setText(az,j.deviceRole==='hud'&&!j.hudOnline?'—':Number(j.az).toFixed(1));setText(el,j.deviceRole==='hud'&&!j.hudOnline?'—':Number(j.el).toFixed(1));
updateFork(j);
setText(azt,j.targetValid?Number(j.targetAz).toFixed(1):'---');
setText(elt,j.targetValid?Number(j.targetEl).toFixed(1):'---');
setText(calaz,Number(j.az).toFixed(1));setText(calel,Number(j.el).toFixed(1));
const azmn=j.azStart??0,azmx=j.azMax??(j.azMode===36?360:450);azbar.style.width=pct(j.az-azmn,azmx-azmn);setText(azminlabel,azmn+'°');setText(azmaxlabel,azmx+'°');taz.min=azmn;taz.max=azmx;setText(tazlbl,'AZ '+azmn+'…'+azmx+'°');const elmn=j.elStart??0,elmx=j.elMax??180;elbar.style.width=pct(j.el-elmn,elmx-elmn);setText(elminlabel,elmn+'°');setText(elmaxlabel,elmx+'°');tel.min=elmn;tel.max=elmx;setText(tellbl,'EL '+elmn+'…'+elmx+'°');
setText(motion,j.motion);
window._azMode=j.azMode??45;const rangeName=j.azMode===0?'Произвольный':'P'+j.azMode;
const selok=!!j.selectedCalOk;
setText(caltxt,(j.calMode==='multi'?'MULTI-POINT':'GS-232 2-POINT')+' · '+rangeName+(selok?' OK':' NOT CALIBRATED'));
caldot.className='dot '+(selok?'on':'');
setText(calmode,j.calMode==='multi'?'Несколько точек':'Две точки · GS-232');
mode2.classList.toggle('good',j.calMode==='2');modem.classList.toggle('good',j.calMode==='multi');
azm36.classList.toggle('good',j.azMode===36);azm45.classList.toggle('good',j.azMode===45);
setText(azrange,rangeName+' · '+azmn+'…'+azmx+'°');azmc.classList.toggle('good',j.azMode===0);
setText(b2_azo,'O · AZ '+azmn+'°');setText(b2_azf,'F · AZ '+azmx+'°');
legacyAzPoints.classList.toggle('hide',j.azMode===0);customAzPoints.classList.toggle('hide',j.azMode!==0);
azPointAngle.min=azmn;azPointAngle.max=azmx;
const elCustom=j.elProfile==='custom';setText(elrange,(elCustom?'Произвольный':'Стандартный')+' · '+elmn+'…'+elmx+'°');elmstd.classList.toggle('good',!elCustom);elmc.classList.toggle('good',elCustom);
setText(b2_elo,'O2 · EL '+elmn+'°');setText(b2_elf,'F2 · EL '+elmx+'°');legacyElPoints.classList.toggle('hide',elCustom||j.model==='G800DXA');customElPoints.classList.toggle('hide',!elCustom||j.model==='G800DXA');elPointAngle.min=elmn;elPointAngle.max=elmx;
const rangeKey=j.model+':'+j.azMode+':'+j.azRangeRevision;if(window._azCalKey!==rangeKey&&j.deviceRole!=='hud'){window._azCalKey=rangeKey;loadAzCalibration();loadElCalibration();}
setText(resetazrange,rangeName);
panel2.classList.toggle('hide',j.calMode!=='2');panelm.classList.toggle('hide',j.calMode!=='multi');
b_az450.classList.toggle('hide',j.azMode===36);azv450.classList.toggle('hide',j.azMode===36);
setText(az2p0,j.az2p0);setText(az2pf,j.az2pf);setText(el2p0,j.el2p0);setText(el2pf,j.el2pf);
markCalBtn('b2_azo',j.az2p0>=0);markCalBtn('b2_azf',j.az2pf>=0);
markCalBtn('b2_elo',j.el2p0>=0);markCalBtn('b2_elf',j.el2pf>=0);
setText(azp0,j.azp0);setText(azp90,j.azp90);setText(azp180,j.azp180);setText(azp270,j.azp270);setText(azp360,j.azp360);setText(azp450,j.azp450);
setText(elp0,j.elp0);setText(elp45,j.elp45);setText(elp90,j.elp90);setText(elp135,j.elp135);setText(elp180,j.elp180);
markCalBtn('b_az0',j.azp0>=0);markCalBtn('b_az90',j.azp90>=0);markCalBtn('b_az180',j.azp180>=0);
markCalBtn('b_az270',j.azp270>=0);markCalBtn('b_az360',j.azp360>=0);markCalBtn('b_az450',j.azMode===45&&j.azp450>=0);
markCalBtn('b_el0',j.elp0>=0);markCalBtn('b_el45',j.elp45>=0);markCalBtn('b_el90',j.elp90>=0);
markCalBtn('b_el135',j.elp135>=0);markCalBtn('b_el180',j.elp180>=0);
setText(ip,j.ip);setText(document.getElementById('apip'),j.apIp||'-');setText(mode,j.wifiMode);
setText(rssi,(j.rssi===null?'-':j.rssi+' dBm'));
setText(sig,j.signal||'-');
setText(btstate,j.build?.bluetooth===false?'Отключён в сборке':j.btConnected?'CONNECTED':'DISCONNECTED');
btstate.style.color=j.btConnected?'#24d18d':'#e5a94f';
if(document.activeElement!==ssid && !ssid.dataset.dirty) ssid.value=j.ssid||'';
updateHud(j);
compactStatus(j);
}
let rrange='hour',rssiData=null;
function drawRssi(j){
 rssiData=j;
 const c=document.getElementById('rchart'),w=c.clientWidth,h=c.clientHeight,dpr=window.devicePixelRatio||1;
 if(!w||!h)return;
 c.width=Math.round(w*dpr);c.height=Math.round(h*dpr);
 const x=c.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);
 x.clearRect(0,0,w,h);x.fillStyle='#071018';x.fillRect(0,0,w,h);x.strokeStyle='#294153';x.lineWidth=1;x.font='12px Arial';x.fillStyle='#96a9b7';
 const top=15,bottom=h-28,left=64,right=w-12;
 for(let db=-30;db>=-100;db-=10){let y=top+(-30-db)/70*(bottom-top);x.beginPath();x.moveTo(left,y);x.lineTo(right,y);x.stroke();x.fillText(db+' dBm',3,y+4);}
 let a=j.values||[],n=a.length;x.strokeStyle='#24d18d';x.lineWidth=2;x.beginPath();let pen=false;
 for(let i=0;i<n;i++){let v=a[i];if(v===null){pen=false;continue}let px=left+(n<=1?0:i/(n-1))*(right-left);let py=top+(-30-Math.max(-100,Math.min(-30,v)))/70*(bottom-top);if(!pen){x.moveTo(px,py);pen=true}else x.lineTo(px,py);}x.stroke();
 x.fillStyle='#96a9b7';x.fillText('Ранее',left,bottom+18);x.textAlign='right';x.fillText('Сейчас',right,bottom+18);
 if(!a.some(v=>v!==null)){x.textAlign='center';x.fillText('Нет измерений',(left+right)/2,(top+bottom)/2);}
 setText(rmin,j.min===null?'-':j.min+' dBm');setText(rmax,j.max===null?'-':j.max+' dBm');setText(ravg,j.avg===null?'-':j.avg+' dBm');setText(rcount,j.count||0);
}
new ResizeObserver(()=>{if(rssiData)drawRssi(rssiData);}).observe(document.getElementById('rchart'));
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
setInterval(upd,500);setInterval(()=>upd(true),5000);upd();

ssid.addEventListener('input',function(){ssid.dataset.dirty='1';});
for(const id of ['forkSettings','hudConfig'])document.getElementById(id).addEventListener('input',event=>{event.currentTarget.dataset.dirty='1';});

document.querySelectorAll('.btn').forEach(function(b){
  b.addEventListener('contextmenu',function(e){e.preventDefault();});
  b.addEventListener('dragstart',function(e){e.preventDefault();});
  b.addEventListener('selectstart',function(e){e.preventDefault();});
});

document.querySelectorAll('.movebtn').forEach(function(b){
  let active=false,selection;

  b.addEventListener('touchstart',function(e){
    e.preventDefault();
  },{passive:false});

  b.addEventListener('pointerdown',function(e){
    e.preventDefault();
    active=true;selection=window._forkState?.deviceRole==='hud'?window._forkState?.hudSelectionEpoch:undefined;
    try{ b.setPointerCapture(e.pointerId); }catch(x){}
    mv(b.dataset.dir);
  });

  function release(e){
    if(e) e.preventDefault();
    if(active){
      active=false;
      stopAxis(b.dataset.dir,selection);
    }
  }

  b.addEventListener('pointerup',release);
  b.addEventListener('pointercancel',release);
  b.addEventListener('lostpointercapture',release);
  b.addEventListener('contextmenu',function(e){e.preventDefault();});
});

let hudSelection;
let hudOffset=0,hudDiscoveredOffset=0,hudTotal=0,hudDiscoveryTotal=0,hudPollBusy=false,hudLastPoll=0;
async function hudPage(delta){hudOffset=Math.max(0,Math.min(Math.max(0,hudTotal-1),hudOffset+delta*8));await loadHudPeers()}
async function hudDiscoveryPage(delta){hudDiscoveredOffset=Math.max(0,Math.min(Math.max(0,hudDiscoveryTotal-1),hudDiscoveredOffset+delta*8));await loadHudDiscovery()}
function hudRow(peer,discovered){
 const row=document.createElement('div');row.className='device-peer';
 const select=document.createElement('button');select.className='btn'+(peer.selected?' good':'');
 const age=peer.ageMs===null?'нет данных':Math.round(peer.ageMs/1000)+' с назад';
 select.textContent=peer.name+' · '+(peer.online?'AZ '+Number(peer.az).toFixed(1)+'° / EL '+Number(peer.el).toFixed(1)+'°'+(peer.moving?' · вращение':''):'нет связи')+' · '+age;
 select.onclick=async()=>{await forkPost(discovered?'/api/hud/peers':'/api/hud/select',{id:peer.id});await loadHudPeers();if(discovered)await loadHudDiscovery()};row.append(select);
 if(!discovered){
  const rename=document.createElement('button');rename.className='btn';rename.textContent='Имя';rename.onclick=async()=>{const name=prompt('Имя поворотки',peer.name);if(name!==null){await forkPost('/api/hud/peers',{id:peer.id,name});await loadHudPeers()}};row.append(rename);
  const remove=document.createElement('button');remove.className='btn';remove.textContent='Удалить';remove.onclick=async()=>{await forkPost('/api/hud/peers',{id:peer.id,action:'remove'});await loadHudPeers()};row.append(remove);
 }else{const note=document.createElement('span');note.className='device-peer-note';note.textContent='Нажмите, чтобы сохранить';row.append(note)}
 return row;
}
async function loadHudPeers(){
 if(hudPollBusy)return;hudPollBusy=true;
 try{const data=await (await fetch('/api/hud/peers?offset='+hudOffset,{cache:'no-store'})).json();hudTotal=data.total;
 const box=document.getElementById('hudPeers');box.replaceChildren(...data.items.map(p=>hudRow(p,false)));
 document.getElementById('hudPageInfo').textContent=hudTotal?`${hudOffset+1}–${Math.min(hudOffset+8,hudTotal)} / ${hudTotal}`:'Добавьте поворотку';
 document.getElementById('hudPrev').disabled=hudOffset===0;document.getElementById('hudNext').disabled=hudOffset+8>=hudTotal;
 }catch(e){}finally{hudPollBusy=false;hudLastPoll=Date.now()}
}
async function loadHudDiscovery(){try{const data=await (await fetch('/api/hud/peers?discovered=1&offset='+hudDiscoveredOffset,{cache:'no-store'})).json();hudDiscoveryTotal=data.total;document.getElementById('hudDiscovered').replaceChildren(...data.items.map(p=>hudRow(p,true)));document.getElementById('hudDiscoveryInfo').textContent='Найдено: '+data.total}catch(e){}}
async function hudSearch(uart=false,manual=false){
 await forkPost('/api/hud/discover',{transport:uart?'uart':'wifi',address:manual?document.getElementById('hudAddress').value:'',port:manual?document.getElementById('hudPeerPort').value:window._forkState?.hudPort||4534});
 hudDiscoveredOffset=0;setTimeout(loadHudDiscovery,500);setTimeout(loadHudDiscovery,1800);
}
async function saveHudDevice(e){e.preventDefault();if(await forkPost('/api/device',Object.fromEntries(new FormData(e.target))))delete e.target.dataset.dirty;}
function updateHud(j){
 const hud=j.deviceRole==='hud';
 const selected=document.getElementById('selectedRotor');selected.classList.toggle('hide',!hud);selected.textContent=hud?(j.hudName||'Выберите поворотку в настройках')+' · '+(j.hudOnline?'На связи':'Нет связи'):'';
 if(hud&&hudSelection!==undefined&&hudSelection!==j.hudSelectionEpoch){document.getElementById('taz').value='';document.getElementById('tel').value='';fillPreset();}
 hudSelection=j.hudSelectionEpoch;
 document.getElementById('hudControls').classList.toggle('hide',!hud);
 document.getElementById('hudHeading').textContent=hud?'HUD · '+(j.hudName||'Выберите поворотку')+(j.remoteDrive?' · '+j.model+' / '+j.remoteDrive:''):'Контроллер поворотки';
 document.getElementById('hudLink').textContent=hud?(j.hudOnline?'Связь: '+j.hudTransport+' · '+j.hudAgeMs+' мс':'Нет связи с выбранной повороткой'):'';
 if(j.hudCatalogError)document.getElementById('hudLink').textContent+=' · Ошибка памяти или сохранённых настроек';
 document.getElementById('hudUartSearch').disabled=!j.hudUart;
 const f=document.getElementById('hudConfig');if(!f.dataset.dirty&&!f.contains(document.activeElement)){f.elements.role.value=j.deviceRole||'controller';f.elements.wifi.value=['existing','host','client'][j.hudWifi||0];f.elements.uart.value=j.hudUart?'1':'0';f.elements.port.value=j.hudPort||4534;}
 document.getElementById('caltoggle').closest('.card').classList.toggle('hide',hud);
 document.getElementById('speedSettings').classList.toggle('hide',hud||document.getElementById('speedSettings').classList.contains('hide'));
 document.getElementById('coastSettings').classList.toggle('hide',hud);
 for(const name of ['model'])document.querySelector('#forkSettings [name="'+name+'"]').closest('.field').classList.toggle('hide',hud);
 document.querySelector('a[href="/adc"]').classList.toggle('hide',hud);
 for(const b of document.querySelectorAll('.movebtn,#presetButtons button'))if(hud&&!j.hudOnline)b.disabled=true;else if(b.classList.contains('movebtn'))b.disabled=false;
 document.getElementById('followMode').disabled=hud&&!j.hudOnline;
 if(hud){document.getElementById('caltxt').textContent=j.hudOnline?(j.selectedCalOk?'Поворотка откалибрована':'Нужна калибровка на контроллере'):'Нет актуальных данных';if(!j.hudOnline){document.getElementById('motion').textContent='Нет связи';document.getElementById('azt').textContent='—';document.getElementById('elt').textContent='—';}if(Date.now()-hudLastPoll>1000)loadHudPeers();}
}

</script></body></html>
)HTML";
