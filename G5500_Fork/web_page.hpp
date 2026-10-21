/* Fork modifications by RA3VLD, 2026-10-12. GPLv3; see docs/upstream.md. */
#pragma once
#ifndef ARDUINO
#define PROGMEM
#endif
static constexpr char WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>G5500 Fork — управление повороткой</title>
<!-- Rotation favicon: Lucide rotate-cw, ISC; see docs/licenses/lucide-favicon.md. -->
<link id="connectionFavicon" rel="icon" type="image/svg+xml" data-state="offline" href="data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20viewBox%3D%220%200%2032%2032%22%3E%3Crect%20width%3D%2232%22%20height%3D%2232%22%20rx%3D%227%22%20fill%3D%22%23081018%22%2F%3E%3Cg%20transform%3D%22translate(4%204)%22%20fill%3D%22none%22%20stroke%3D%22%23e74c3c%22%20stroke-width%3D%222.5%22%20stroke-linecap%3D%22round%22%20stroke-linejoin%3D%22round%22%3E%3Cpath%20d%3D%22M21%2012a9%209%200%201%201-9-9c2.52%200%204.93%201%206.74%202.74L21%208M21%203v5h-5%22%2F%3E%3C%2Fg%3E%3Cpath%20d%3D%22m7%207%2018%2018%22%20stroke%3D%22%23081018%22%20stroke-width%3D%226%22%20stroke-linecap%3D%22round%22%2F%3E%3Cpath%20d%3D%22m7%207%2018%2018%22%20stroke%3D%22%23e74c3c%22%20stroke-width%3D%223%22%20stroke-linecap%3D%22round%22%2F%3E%3C%2Fsvg%3E">
<style>
:root{--bg:#081018;--p:#111d28;--p2:#162635;--t:#f4f7fa;--m:#96a9b7;--a:#24d18d;--s:#e74c3c;--b:#2b4557}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(#081018,#0b1721);color:var(--t);font-family:Arial,sans-serif}
.wrap{max-width:980px;margin:auto;padding:12px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}h1{font-size:26px;margin:8px 0 10px}
.page-header{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px 20px;margin:8px 0 16px}.page-header h1{flex:1 1 350px;min-width:0;margin:0;line-height:1.3;overflow-wrap:anywhere}
.connection-status{overflow-wrap:anywhere;min-width:0;display:inline-flex;align-items:center;gap:8px;max-width:100%;padding:9px 12px;border:1px solid #3b5365;border-radius:999px;background:#101c27;font-size:13px;line-height:1.4;color:#c9d8e3}.connection-status::before{content:'';flex:none;width:8px;height:8px;border-radius:50%;background:currentColor}.connection-status[data-state=online]{color:#62dfa9;border-color:#316b56}.connection-status[data-state=offline]{color:#ff9690;border-color:#814b4b}.connection-status[data-state=unselected]{color:#efc07a;border-color:#78603d}
.drive-status{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.motion-fault{color:#ff9690;white-space:pre-line;overflow-wrap:anywhere}
.page-footer{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px 20px;margin-top:24px;padding:16px 0 4px;border-top:1px solid #294153;color:var(--m);font-size:13px;line-height:1.6}.footer-links{display:flex;flex-wrap:wrap;gap:4px 20px}.page-footer a{display:inline-flex;align-items:center;min-height:44px;color:#acd3e8;text-underline-offset:3px}.page-footer a:hover{color:#fff}.page-footer a:focus-visible{outline:2px solid var(--a);outline-offset:3px}

.nav{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 12px;padding:8px;background:#0b1720;border:1px solid #294153;border-radius:12px;position:sticky;top:0;z-index:20}
.nav-tabs{display:flex;gap:8px;flex-wrap:wrap}
.nav button,.nav a{border:0;cursor:pointer;font-family:inherit;display:inline-block;padding:9px 12px;border-radius:9px;background:#173044;color:#fff;text-decoration:none;font-weight:700;font-size:14px}
.range-editors{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.range-editor{min-width:0}.range-editor>.label{font-size:18px}.command-card h2{margin-top:0}.command-card h3{font-size:16px;margin:20px 0 8px}.control-mode{margin-top:16px}.target-controls{margin:16px 0}.range-editor .seg{display:flex;flex-wrap:wrap}.range-editor .seg .btn{flex:1 1 120px;font-size:14px}.range-editor input{max-width:100%}.calbox .row{align-items:center;gap:8px}.calbox .row>span{flex:1 1 150px}.calbox .row .btn{white-space:normal}@media(max-width:760px){.range-editors{grid-template-columns:1fr}}
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
.manual-drive{display:grid;grid-template-columns:minmax(0,1fr);gap:18px;align-items:center}.manual-drive.has-speeds{grid-template-columns:minmax(0,1.4fr) minmax(180px,1fr)}.manual-speeds{display:grid;gap:14px;min-width:0}.manual-speed label{display:flex;justify-content:space-between;gap:12px;font-size:14px}.manual-speed output{font-variant-numeric:tabular-nums;white-space:nowrap}.manual-speed input{width:100%;min-height:44px;padding:0;accent-color:var(--a);touch-action:pan-y}.manual-speeds .hint{margin:0}@media(max-width:600px){.manual-drive.has-speeds{grid-template-columns:minmax(0,1fr);gap:12px}}
.hint{color:var(--m);font-size:13px;line-height:1.5;overflow-wrap:anywhere}
.device-panel h2{font-size:20px;line-height:1.35;margin:0;overflow-wrap:anywhere}.device-panel #hudLink:empty{display:none}.device-panel #hudLink:not(:empty){margin-top:8px}
.device-section{margin-top:14px;border-top:1px solid var(--b);padding-top:14px}.device-section summary{line-height:1.4;padding:4px 0}.device-section[open] summary{margin-bottom:14px}
.device-fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.device-fields .field{min-width:0}.device-fields label{display:block;margin-bottom:7px;color:var(--m);font-size:14px;line-height:1.4}.device-fields input,.device-fields select{min-height:44px;max-width:100%}
.device-panel .btn{min-height:44px;font-size:15px;padding:10px 16px;border-radius:10px;cursor:pointer;touch-action:manipulation;overflow-wrap:anywhere}.device-actions{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px}.device-panel form{margin:0}.device-panel .hint{margin:12px 0}.device-address{display:grid;gap:12px;padding:16px 0}.device-address>.btn{justify-self:start}
.device-list{display:grid;gap:10px}.device-peer{display:grid;grid-template-columns:minmax(0,1fr) auto auto;gap:8px}.device-peer>.btn:first-child{text-align:left}.device-peer-note{grid-column:1/-1;color:var(--m);font-size:13px}.device-pagination{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:12px 0;font-size:13px;color:var(--m)}.device-pagination>span{text-align:center;overflow-wrap:anywhere}
@media(max-width:600px){.device-fields{grid-template-columns:minmax(0,1fr);gap:14px}.device-actions>.btn,.device-address>.btn{width:100%}.device-peer{grid-template-columns:repeat(2,minmax(0,1fr))}.device-peer>.btn:first-child{grid-column:1/-1}.device-pagination .btn{padding:10px 12px}}
[hidden]{display:none!important}.nav button:focus-visible,.nav a:focus-visible{outline:2px solid var(--a);outline-offset:2px}
.history-intro{margin:20px 0}.history-intro h2{font-size:24px;margin:0 0 8px}.history-list{list-style:none;margin:0;padding:0;display:grid;gap:12px}.history-entry.card{margin:0;padding:18px 20px}.history-entry h3{font-size:16px;color:var(--a);margin:0 0 12px}.history-entry ul{margin:0;padding-left:20px;line-height:1.6;font-size:15px}.history-entry li+li{margin-top:8px}#historyPanel{padding-bottom:16px}
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
#settingsPanel .field-help{font-size:12px;line-height:1.5;color:#9aafbf;margin:0}
#settingsPanel .check-field + .field-help{margin:-6px 0 16px}
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
#settingsPanel .inline-form{display:grid;grid-template-columns:minmax(0,.85fr) minmax(0,1.15fr);gap:8px 12px;align-items:stretch;margin-top:16px}
#settingsPanel .inline-form>label{grid-column:1/-1;font-size:14px;line-height:1.45;color:#c9d8e3}
#settingsPanel .inline-form>.field-help{grid-column:1/-1}
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
@media(max-width:600px){.settings-details>summary{padding:16px;gap:16px}.settings-details>summary h2{font-size:17px}.settings-content{padding:16px}#settingsPanel .row,#settingsPanel .device-fields{grid-template-columns:minmax(0,1fr);gap:16px}#settingsPanel .settings-pane{padding:12px}#settingsPanel .form-actions>.btn{width:100%}#settingsPanel .form-actions{gap:10px}#settingsPanel .range-form .row{grid-template-columns:repeat(2,minmax(0,1fr))}#settingsPanel .profile-options{grid-template-columns:repeat(2,minmax(0,1fr))}#settingsPanel #azmc{grid-column:1/-1}#settingsPanel .device-peer{grid-template-columns:repeat(2,minmax(0,1fr))}}

</style></head><body><div class="wrap">
<header class="page-header">
<h1 id="modelTitle">YAESU G-5500 / ESP32</h1>
<div id="rotatorConnection" class="connection-status" data-state="pending" role="status" aria-live="polite" aria-atomic="true">Проверяем связь…</div>
</header>
<nav class="nav" aria-label="Разделы контроллера">
<div class="nav-tabs" role="tablist" aria-label="Управление, настройки и история">
<button id="controlTab" class="active" role="tab" aria-controls="controlPanel" aria-selected="true" onclick="selectTab('control')">Управление</button>
<button id="settingsTab" role="tab" aria-controls="settingsPanel" aria-selected="false" tabindex="-1" onclick="selectTab('settings')">Настройки</button>
<button id="historyTab" role="tab" aria-controls="historyPanel" aria-selected="false" tabindex="-1" onclick="selectTab('history')">История</button>
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
<div id="waitingTarget" style="margin-bottom:16px">Нет ожидающей цели</div>

<div style="display:flex;justify-content:space-between;gap:10px;flex-wrap:wrap">
<div><div class="label">Состояние привода</div><div class="drive-status"><div id="motion" role="status" aria-live="polite" style="font-size:23px;font-weight:700">STOP</div><button id="resetDriveError" class="btn hide" type="button" onclick="resetMotion('all')">Сбросить ошибку</button></div></div>
<div><span class="status"><span id="caldot" class="dot"></span><span id="caltxt">Calibration</span></span></div></div>
<h3>Ручное вращение</h3>
<div class="manual-drive">
<div id="movementButtons" class="controls" style="margin-top:10px">
<button class="btn blank">-</button><button class="btn movebtn" data-dir="U"><span>▲ UP</span></button><button class="btn blank">-</button>
<button class="btn movebtn" data-dir="L"><span>◀ LEFT</span></button><button id="executeTarget" class="btn good" onclick="targetAction()" disabled>Повернуть</button><button class="btn movebtn" data-dir="R"><span>RIGHT ▶</span></button>
<button class="btn blank">-</button><button class="btn movebtn" data-dir="D"><span>▼ DOWN</span></button><button class="btn blank">-</button>
</div>
<div id="manualSpeeds" class="manual-speeds hide">
<div id="azManualSpeedControl" class="manual-speed hide"><label for="azManualSpeed"><span>Скорость AZ</span><output id="azManualSpeedValue" for="azManualSpeed">—</output></label><input id="azManualSpeed" type="range" min="1" max="100" value="50" step="1" aria-describedby="manualSpeedHelp" oninput="queueManualSpeed('az',this.value)" onchange="flushManualSpeed('az')"></div>
<div id="elManualSpeedControl" class="manual-speed hide"><label for="elManualSpeed"><span>Скорость EL</span><output id="elManualSpeedValue" for="elManualSpeed">—</output></label><input id="elManualSpeed" type="range" min="1" max="100" value="50" step="1" aria-describedby="manualSpeedHelp" oninput="queueManualSpeed('el',this.value)" onchange="flushManualSpeed('el')"></div>
<p id="manualSpeedHelp" class="hint">Меняет скорость текущего и следующего движения. После перезапуска возвращается сохранённая настройка.</p>
</div></div>
<div class="target-controls"><div class="label">Задать угол</div><div class="row" style="margin-top:8px">
<div class="field"><label id="tazlbl">AZ 0..450°</label><input id="taz" type="number" step="0.1" min="0" max="450"></div>
<div class="field"><label id="tellbl">EL 0…180°</label><input id="tel" type="number" step="0.1" min="0" max="180"></div>
<button class="btn good" onclick="go()">Повернуть</button></div></div>
<div class="control-mode">
<div class="label">Режим управления</div>
<div class="mode-controls" style="margin-top:8px">
<div class="field"><label for="followMode">Следование командам</label><select id="followMode" onchange="forkPost('/api/mode',{mode:this.value})"><option value="manual">Ручной</option><option value="auto">Автоматический</option><option value="auto_1h">Авто 1 ч</option></select></div></div>
<div id="hourLeft" aria-live="polite"></div>
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
<div class="field"><label for="forkSettings-model">Модель</label><select aria-describedby="forkSettings-model-help" id="forkSettings-model" name="model"><option value="G5500">G-5500</option><option value="G800DXA">G-800DXA</option></select><p class="field-help" id="forkSettings-model-help">G-5500 управляет AZ и EL, G-800DXA — только AZ. У каждой модели свои настройки и калибровки.</p></div>
</div><h3>Подключение логгеров</h3><div class="row"><div class="field"><label for="forkSettings-rotorName">Имя в N1MM</label><input aria-describedby="forkSettings-rotorName-help" id="forkSettings-rotorName" maxlength="31" name="rotorName" required=""/><p class="field-help" id="forkSettings-rotorName-help">Имя должно совпадать с именем поворотки в N1MM. По нему контроллер выбирает адресованные ему команды.</p></div>
<div class="field"><label for="forkSettings-n1mmPort">N1MM UDP</label><input aria-describedby="forkSettings-n1mmPort-help" id="forkSettings-n1mmPort" max="65535" min="1" name="n1mmPort" required="" type="number"/><p class="field-help" id="forkSettings-n1mmPort-help">Порт приёма команд N1MM. Укажите такой же порт в логгере; он должен отличаться от других UDP-портов устройства.</p></div>
<div class="field"><label for="pstPort">PstRotator UDP</label><input aria-describedby="pstPortHelp pstPort-help" id="pstPort" max="65534" min="1" name="pstPort" required="" type="number"/><p class="field-help" id="pstPort-help">Порт приёма PstRotator, в том числе от UR5EQF и 5MContest. Ответы отправляются на следующий порт: выбранный + 1.</p></div>
<div class="field"><label for="forkSettings-reportPort">Положение N1MM UDP</label><input aria-describedby="forkSettings-reportPort-help" id="forkSettings-reportPort" max="65535" min="1" name="reportPort" required="" type="number"/><p class="field-help" id="forkSettings-reportPort-help">Порт, на котором N1MM принимает сообщения о текущем положении. Укажите его в настройках приёма логгера.</p></div>
<div class="field"><label for="forkSettings-reportHost">Дополнительный IP N1MM</label><input aria-describedby="forkSettings-reportHost-help" id="forkSettings-reportHost" name="reportHost" placeholder="192.168.1.10"/><p class="field-help" id="forkSettings-reportHost-help">Необязательный IPv4-адрес для дополнительной отправки положения. Пустое поле — ответы только известным отправителям N1MM.</p></div></div>
<p class="hint" id="pstPortHelp">PstRotator UDP — для UR5EQF и 5MContest. В логгере выберите PstRotator/UDP; отдельно запускать программу PstRotator не требуется.<br/>Log4OM 2, DXLog.net, TR4W (с 4.148.6), Swisslog и LogHX также имеют интеграцию с PstRotator, но с нашим контроллером не тестировались.</p>
<div class="settings-group" id="externalAzSettings"><h3>Угол AZ от программ</h3><div class="row"><div class="field"><label for="externalAzMode">Выбор пути</label><select aria-describedby="externalAzMode-help" id="externalAzMode" name="externalAzMode"><option value="shortest">Кратчайший путь</option><option value="mechanical">Механический угол</option></select><p class="field-help" id="externalAzMode-help">Кратчайший путь — контроллер выбирает ближайший доступный оборот. Механический угол — использует заданную программой координату без пересчёта: 10° остаётся 10°, 370° — 370°. Цель должна попадать в диапазон хода.</p></div></div><p class="hint">Общий выбор для N1MM, PstRotator, одиночных команд GS-232 и rotctld / Hamlib во всех режимах управления. На HUD действует для программ, подключённых к этому пульту.</p></div>
<div class="settings-group" id="manualProtocolSettings"><h3>Протоколы в ручном режиме</h3><div class="row">
<div class="field"><label for="n1mmManualFollow">N1MM UDP</label><select aria-describedby="n1mmManualFollow-help" id="n1mmManualFollow" name="n1mmManualFollow"><option value="false">С подтверждением</option><option value="true">Автоследование</option></select><p class="field-help" id="n1mmManualFollow-help">В ручном режиме цели N1MM ожидают подтверждения либо выполняются сразу при выборе автоследования. Настройка действует только на этот протокол.</p></div>
<div class="field"><label for="pstManualFollow">PstRotator UDP</label><select aria-describedby="pstManualFollow-help" id="pstManualFollow" name="pstManualFollow"><option value="false">С подтверждением</option><option value="true">Автоследование</option></select><p class="field-help" id="pstManualFollow-help">В ручном режиме цели PstRotator ожидают подтверждения либо выполняются сразу при выборе автоследования. Настройка действует только на этот протокол.</p></div>
<div class="field"><label for="gs232ManualFollow">GS-232 · UART, Bluetooth, TCP</label><select aria-describedby="gs232ManualFollow-help" id="gs232ManualFollow" name="gs232ManualFollow"><option value="false">С подтверждением</option><option value="true">Автоследование</option></select><p class="field-help" id="gs232ManualFollow-help">В ручном режиме цели GS-232 ожидают подтверждения либо выполняются сразу при выборе автоследования. Настройка действует только на этот протокол.</p></div>
<div class="field"><label for="rotctldManualFollow">rotctld / Hamlib</label><select aria-describedby="rotctldManualFollow-help" id="rotctldManualFollow" name="rotctldManualFollow"><option value="false">С подтверждением</option><option value="true">Автоследование</option></select><p class="field-help" id="rotctldManualFollow-help">В ручном режиме цели rotctld ожидают подтверждения либо выполняются сразу при выборе автоследования. Настройка действует на команды положения Hamlib по всем доступным подключениям.</p></div>
</div><p class="hint">Автоследование сразу выполняет новые числовые цели выбранного протокола, сохраняя ручной режим. «С подтверждением» оставляет цель для запуска кнопкой. В автоматическом режиме цели всех протоколов выполняются сразу. Направления и STOP выполняются сразу в обоих режимах; при активной предустановке внешние команды движения отклоняются.</p></div>
<p class="hint hide" id="hardwareTrialNotice">Движение заблокировано до подтверждения оборудования. <a href="/hardware">Открыть настройки оборудования</a></p>
<div class="hint" id="buildInfo"></div>
<div class="settings-group" id="hardwareSettings"><h3>Оборудование</h3><p class="hint">Привод, имитация, Bluetooth, точка доступа и кнопка настраиваются с проверкой после перезапуска.</p><a class="btn" href="/hardware">Настроить оборудование</a> <a href="/recovery">Восстановление доступа</a></div>
<div class="hide" id="encoderSettings"><label class="check-field"><input checked="" id="encoderInverted" aria-describedby="encoderInverted-help" name="encoderInverted" type="checkbox"/> Инверсия энкодера</label><p class="field-help" id="encoderInverted-help">Меняет направление изменения цели при вращении энкодера. На направление вращения двигателя не влияет.</p></div>
<div class="settings-group" id="speedSettings"><h3>Скорость и плавность движения</h3><label class="check-field" id="analogSpeedToggle"><input id="speedEnabled" aria-describedby="speedEnabled-help" name="speedEnabled" type="checkbox"/> Управлять скоростью через внешний модуль 0–5 В</label><p class="field-help" id="speedEnabled-help">Включите только при подключённом модуле регулировки скорости 0–5 В. Изменение этого режима останавливает привод.</p>
<h4>Азимут · AZ</h4><div class="row">

<div class="field"><label for="coastDegrees">Угол выбега AZ, °</label><input aria-describedby="coastDegrees-help" id="coastDegrees" max="90" min="0" name="coastDegrees" required="" step="0.1" type="number" value="5"/><p class="field-help" id="coastDegrees-help">На сколько градусов раньше цели отключать AZ для остановки по инерции. 0 — отключение на цели; максимум для короткого хода — 10% диапазона.</p></div>
<div class="field el-setting"><label for="elCoastDegrees">Угол выбега EL, °</label><input aria-describedby="elCoastDegrees-help" id="elCoastDegrees" max="90" min="0" name="elCoastDegrees" required="" step="0.1" type="number" value="5"/><p class="field-help" id="elCoastDegrees-help">На сколько градусов раньше цели отключать EL для остановки по инерции. 0 — отключение на цели; максимум для короткого хода — 10% диапазона.</p></div>
<div class="field"><label for="forkSettings-speedPercent">Рабочая скорость, %</label><input aria-describedby="forkSettings-speedPercent-help" id="forkSettings-speedPercent" max="100" min="1" name="speedPercent" required="" step="1" type="number" value="50"/><p class="field-help" id="forkSettings-speedPercent-help">Рабочий уровень выхода AZ, а не измеренная скорость вращения. Уменьшайте его для более медленного хода.</p></div>
<div class="field hide"><label for="forkSettings-startSpeedPercent">Скорость старта, %</label><input aria-describedby="forkSettings-startSpeedPercent-help" id="forkSettings-startSpeedPercent" max="100" min="1" name="startSpeedPercent" required="" step="1" type="number" value="25"/><p class="field-help" id="forkSettings-startSpeedPercent-help">Начальный выход AZ при запуске. Должен обеспечивать уверенный старт; не превышает рабочую скорость.</p></div>
<div class="field"><label for="forkSettings-approachSpeedPercent">Скорость у цели, %</label><input aria-describedby="forkSettings-approachSpeedPercent-help" id="forkSettings-approachSpeedPercent" max="100" min="1" name="approachSpeedPercent" required="" step="1" type="number" value="25"/><p class="field-help" id="forkSettings-approachSpeedPercent-help">Минимальный выход AZ перед остановкой у цели. Не превышает рабочую скорость; подберите для плавного подхода.</p></div>
<div class="field"><label for="forkSettings-accelerationMs">Время разгона 0–100% рабочей скорости, мс</label><input aria-describedby="forkSettings-accelerationMs-help" id="forkSettings-accelerationMs" max="10000" min="100" name="accelerationMs" required="" step="1" type="number" value="2000"/><p class="field-help" id="forkSettings-accelerationMs-help">Задаёт темп нарастания выхода AZ от 0 до рабочей скорости. Большее время — более плавный разгон.</p></div>
<div class="field"><label for="forkSettings-decelerationMs">Время торможения 100–0% рабочей скорости, мс</label><input aria-describedby="forkSettings-decelerationMs-help" id="forkSettings-decelerationMs" max="10000" min="100" name="decelerationMs" required="" step="1" type="number" value="1500"/><p class="field-help" id="forkSettings-decelerationMs-help">Задаёт темп снижения выхода AZ от рабочей скорости до 0. Большее время — более плавное торможение; учитывается коэффициент ниже.</p></div>
<div class="field"><label for="forkSettings-slowdownDegrees">Начинать замедление за, °</label><input aria-describedby="forkSettings-slowdownDegrees-help" id="forkSettings-slowdownDegrees" max="90" min="1" name="slowdownDegrees" required="" step="1" type="number" value="10"/><p class="field-help" id="forkSettings-slowdownDegrees-help">Расстояние до зоны отключения AZ, с которого начинается снижение скорости. Увеличьте, если привод не успевает замедлиться.</p></div>
<div class="field"><label for="decelerationFactor">Коэффициент торможения AZ</label><input aria-describedby="decelerationFactor-help" id="decelerationFactor" max="10" min="0.1" name="decelerationFactor" required="" step="0.1" type="number" value="1"/><p class="field-help" id="decelerationFactor-help">Множитель темпа снижения выхода AZ: 1 — обычный, 2 — вдвое быстрее, 0,5 — вдвое медленнее. STOP отключает выход сразу.</p></div>
</div><div class="hint">Выход AZ: <b id="speedOutput">0%</b></div><div id="elSpeedSettings"><h4>Элевация · EL</h4><div class="row">
<div class="field"><label for="forkSettings-elSpeedPercent">Рабочая скорость, %</label><input aria-describedby="forkSettings-elSpeedPercent-help" id="forkSettings-elSpeedPercent" max="100" min="1" name="elSpeedPercent" required="" step="1" type="number" value="100"/><p class="field-help" id="forkSettings-elSpeedPercent-help">Рабочий уровень выхода EL, а не измеренная скорость вращения. Уменьшайте его для более медленного хода.</p></div>
<div class="field hide"><label for="forkSettings-elStartSpeedPercent">Скорость старта, %</label><input aria-describedby="forkSettings-elStartSpeedPercent-help" id="forkSettings-elStartSpeedPercent" max="100" min="1" name="elStartSpeedPercent" required="" step="1" type="number" value="18"/><p class="field-help" id="forkSettings-elStartSpeedPercent-help">Начальный выход EL при запуске. Должен обеспечивать уверенный старт; не превышает рабочую скорость.</p></div>
<div class="field"><label for="forkSettings-elApproachSpeedPercent">Скорость у цели, %</label><input aria-describedby="forkSettings-elApproachSpeedPercent-help" id="forkSettings-elApproachSpeedPercent" max="100" min="1" name="elApproachSpeedPercent" required="" step="1" type="number" value="18"/><p class="field-help" id="forkSettings-elApproachSpeedPercent-help">Минимальный выход EL перед остановкой у цели. Не превышает рабочую скорость; подберите для плавного подхода.</p></div>
<div class="field"><label for="forkSettings-elAccelerationMs">Время разгона 0–100% рабочей скорости, мс</label><input aria-describedby="forkSettings-elAccelerationMs-help" id="forkSettings-elAccelerationMs" max="10000" min="100" name="elAccelerationMs" required="" step="1" type="number" value="1800"/><p class="field-help" id="forkSettings-elAccelerationMs-help">Задаёт темп нарастания выхода EL от 0 до рабочей скорости. Большее время — более плавный разгон.</p></div>
<div class="field"><label for="forkSettings-elDecelerationMs">Время торможения 100–0% рабочей скорости, мс</label><input aria-describedby="forkSettings-elDecelerationMs-help" id="forkSettings-elDecelerationMs" max="10000" min="100" name="elDecelerationMs" required="" step="1" type="number" value="1200"/><p class="field-help" id="forkSettings-elDecelerationMs-help">Задаёт темп снижения выхода EL от рабочей скорости до 0. Большее время — более плавное торможение; учитывается коэффициент ниже.</p></div>
<div class="field"><label for="forkSettings-elSlowdownDegrees">Начинать замедление за, °</label><input aria-describedby="forkSettings-elSlowdownDegrees-help" id="forkSettings-elSlowdownDegrees" max="90" min="1" name="elSlowdownDegrees" required="" step="1" type="number" value="2"/><p class="field-help" id="forkSettings-elSlowdownDegrees-help">Расстояние до зоны отключения EL, с которого начинается снижение скорости. Увеличьте, если привод не успевает замедлиться.</p></div>
<div class="field"><label for="elDecelerationFactor">Коэффициент торможения EL</label><input aria-describedby="elDecelerationFactor-help" id="elDecelerationFactor" max="10" min="0.1" name="elDecelerationFactor" required="" step="0.1" type="number" value="1"/><p class="field-help" id="elDecelerationFactor-help">Множитель темпа снижения выхода EL: 1 — обычный, 2 — вдвое быстрее, 0,5 — вдвое медленнее. STOP отключает выход сразу.</p></div>
</div><div class="hint">Выход EL: <b id="elSpeedOutput">0%</b></div></div><p class="hint">Реверс: торможение и пауза 300 мс перед сменой направления. STOP выключает привод сразу.</p></div>
<div class="form-actions"><button class="btn good" type="submit">Сохранить настройки</button></div></form></div></details></section><section class="card settings-section" id="motionSettings"><details class="settings-details" ontoggle="if(this.open)loadMotionSettings()"><summary><h2>Регуляторы движения<span class="section-caption">S-профиль, страгивание и удержание AZ / EL</span></h2></summary><div class="settings-content"><p class="hint">Простой регулятор задаёт выход в процентах и отключает привод по ручному выбегу. Каскад регулирует положение и скорость в °/с; удержание доступно только в нём. Изменение настроек останавливает ось.</p><div id="motionForms"></div></div></details></section><section class="card settings-section" id="calibrationSettings"><details class="settings-details" ontoggle="if(this.open){loadAzCalibration();loadElCalibration();}"><summary id="caltoggle"><h2>Диапазоны и калибровка<span class="section-caption">Упоры и измерения датчиков AZ / EL</span></h2></summary><div class="settings-content" id="calbody">
<h3>1. Выберите диапазоны</h3><div class="range-editors"><section class="range-editor settings-pane"><h3>Азимут · AZ</h3>
<div class="seg profile-options">
<button class="btn" id="azm36" aria-describedby="azProfileHelp" onclick="setAzMode(36)">P36 · 360°</button>
<button class="btn" id="azm45" aria-describedby="azProfileHelp" onclick="setAzMode(45)">P45 · 450°</button>
<button class="btn" id="azmc" aria-describedby="azProfileHelp" onclick="useCustomRange()">Произвольный</button>
</div>
<p class="field-help" id="azProfileHelp">P36 — 0…360°, P45 — 0…450°. Произвольный — начало и ход из формы ниже. Калибровки профилей независимы.</p><p class="rangebadge" id="azrange">P--</p>
<form class="range-form" id="azRangeForm" onsubmit="saveAzRange(event)"><h4>Произвольный диапазон</h4>
<div class="row"><div class="field"><label for="azRangeForm-start">Начало диапазона, °</label><input aria-describedby="azRangeForm-start-help" id="azRangeForm-start" max="3600" min="-3600" name="start" required="" step="0.1" type="number" value="0"/><p class="field-help" id="azRangeForm-start-help">Угол левого упора AZ. Минус — смещение против часовой стрелки, плюс — по часовой. Сдвиг начала сохраняет измерения датчика.</p></div>
<div class="field"><label for="azRangeForm-span">Полный ход, °</label><input aria-describedby="azRangeForm-span-help" id="azRangeForm-span" max="3600" min="1" name="span" required="" step="0.1" type="number" value="450"/><p class="field-help" id="azRangeForm-span-help">Полный ход AZ между упорами, 1…3600°. Конец равен началу плюс ход. Изменение хода сбрасывает обе калибровки произвольного AZ.</p></div></div>
<p>Конец диапазона: <b id="azRangeEnd">450°</b></p>

<div class="form-actions"><button class="btn good" type="submit">Применить диапазон</button></div><div class="inline-message" id="azRangeMessage" role="status"></div>
</form>
</section><section class="range-editor el-setting settings-pane"><h3>Элевация · EL</h3>
<div class="seg profile-options">
<button class="btn" id="elmstd" aria-describedby="elProfileHelp" onclick="elPost('/api/elrange',{profile:'standard'})">Стандартный · 0…180°</button>
<button class="btn" id="elmc" aria-describedby="elProfileHelp" onclick="useCustomElRange()">Произвольный</button>
</div>
<p class="field-help" id="elProfileHelp">Стандартный — 0…180°. Произвольный — начало и ход из формы ниже. Калибровки профилей независимы.</p><p class="rangebadge" id="elrange">P--</p>
<form class="range-form" id="elRangeForm" onsubmit="saveElRange(event)"><h4>Произвольный диапазон</h4>
<div class="row"><div class="field"><label for="elRangeForm-start">Начало диапазона, °</label><input aria-describedby="elRangeForm-start-help" id="elRangeForm-start" max="3600" min="-3600" name="start" required="" step="0.1" type="number" value="0"/><p class="field-help" id="elRangeForm-start-help">Угол нижнего упора EL. Допускается смещение ниже или выше нуля. Сдвиг начала сохраняет измерения датчика.</p></div>
<div class="field"><label for="elRangeForm-span">Полный ход, °</label><input aria-describedby="elRangeForm-span-help" id="elRangeForm-span" max="3600" min="1" name="span" required="" step="0.1" type="number" value="180"/><p class="field-help" id="elRangeForm-span-help">Полный ход EL между упорами, 1…3600°. Конец равен началу плюс ход. Изменение хода сбрасывает обе калибровки произвольного EL.</p></div></div>
<p>Конец диапазона: <b id="elRangeEnd">180°</b></p>

<div class="form-actions"><button class="btn good" type="submit">Применить диапазон</button></div><div class="inline-message" id="elRangeMessage" role="status"></div>
</form>
</section></div>
<div class="settings-group calibration-method"><h3>2. Выберите способ калибровки</h3><div class="seg">
<button class="btn" aria-describedby="calMethodHelp" id="mode2" onclick="setCalMode('2')">Две точки</button>
<button class="btn" aria-describedby="calMethodHelp" id="modem" onclick="setCalMode('multi')">Несколько точек</button>
</div><p class="field-help" id="calMethodHelp">Две точки — запись обоих упоров для линейного датчика. Несколько точек — запись упоров и промежуточных углов для поправки нелинейности. Способ общий для AZ и EL, измерения раздельные.</p><p class="hint">Активно: <b id="calmode">—</b></p></div>
<div class="settings-group"><h3>3. Запишите положения</h3><p class="hint">Для каждой оси двигайтесь от начала диапазона к концу по списку точек. Установите антенну на указанный угол ручным управлением, остановите её и нажмите «Запомнить положение». Ориентируйтесь на физическое положение антенны. NC (not calibrated) вместо текущего угла означает, что ось не откалибрована.</p><p class="hint" id="multiCalHint">В произвольном профиле можно добавить промежуточные точки. Крайние точки обязательны. Запишите все точки; напряжение датчика должно последовательно расти или убывать.</p></div>
<div class="calibration-grid" id="panel2">
<section class="settings-pane"><div class="calhead"><h3>Азимут · AZ</h3><div class="callive"><span data-cal-position="az">--.-</span><span>°</span></div></div>
<p class="hint" id="az2CalProgress" role="status"></p><div class="point-list" id="az2PointList"></div>
<div class="form-actions"><button class="btn calreset" onclick="resetCal('az','2')">Сбросить AZ</button></div></section>
<section class="settings-pane el-setting"><div class="calhead"><h3>Элевация · EL</h3><div class="callive"><span data-cal-position="el">--.-</span><span>°</span></div></div>
<p class="hint" id="el2CalProgress" role="status"></p><div class="point-list" id="el2PointList"></div>
<div class="form-actions"><button class="btn calreset" onclick="resetCal('el','2')">Сбросить EL</button></div></section>
</div>
<div class="calibration-grid hide" id="panelm">
<section class="settings-pane"><div class="calhead"><h3>Азимут · AZ</h3><div class="callive"><span data-cal-position="az">--.-</span><span>°</span></div></div>
<p class="hint" id="azCalProgress" role="status"></p><div class="point-list" id="azPointList"></div>
<div class="hide" id="customAzPoints"><form class="inline-form" onsubmit="addAzPoint(event)"><label for="azPointAngle">Угол новой точки, °</label><input aria-describedby="azPointAngle-help" id="azPointAngle" required="" step="0.1" type="number"/><button class="btn" type="submit">Добавить и запомнить</button><p class="field-help" id="azPointAngle-help">Установите антенну на нужный угол AZ и остановите её. Введите этот угол с шагом 0,1°: кнопка добавит точку и сразу запомнит текущее показание ADC.</p></form></div>
<div class="form-actions"><button class="btn calreset" onclick="resetCal('az','multi')">Сбросить AZ</button></div></section>
<section class="settings-pane el-setting"><div class="calhead"><h3>Элевация · EL</h3><div class="callive"><span data-cal-position="el">--.-</span><span>°</span></div></div>
<p class="hint" id="elCalProgress" role="status"></p><div class="point-list" id="elPointList"></div>
<div class="hide" id="customElPoints"><form class="inline-form" onsubmit="addElPoint(event)"><label for="elPointAngle">Угол новой точки, °</label><input aria-describedby="elPointAngle-help" id="elPointAngle" required="" step="0.1" type="number"/><button class="btn" type="submit">Добавить и запомнить</button><p class="field-help" id="elPointAngle-help">Установите антенну на нужный угол EL и остановите её. Введите этот угол с шагом 0,1°: кнопка добавит точку и сразу запомнит текущее показание ADC.</p></form></div>
<div class="form-actions"><button class="btn calreset" onclick="resetCal('el','multi')">Сбросить EL</button></div></section>
</div>
</div></details></section><section class="card settings-section" id="presetSettings"><details class="settings-details"><summary><h2>Предустановки<span class="section-caption">Четыре сохранённых положения</span></h2></summary><div class="settings-content"><form id="presetEditor" onsubmit="savePreset(event)">
<div class="row"><div class="field"><label for="presetEditor-slot">Кнопка</label><select aria-describedby="presetEditor-slot-help" id="presetEditor-slot" name="slot" onchange="fillPreset()"><option value="1">1</option><option value="2">2</option><option value="3">3</option><option value="4">4</option></select><p class="field-help" id="presetEditor-slot-help">Выберите одну из четырёх кнопок, которую хотите изменить. Сохранённое положение появится на вкладке управления.</p></div>
<div class="field"><label for="presetEditor-label">Название</label><input aria-describedby="presetEditor-label-help" id="presetEditor-label" maxlength="24" name="label" required=""/><p class="field-help" id="presetEditor-label-help">Подпись кнопки предустановки, до 24 символов. Например, название антенны или направления.</p></div>
<div class="field"><label for="presetEditor-az">AZ, °</label><input aria-describedby="presetEditor-az-help" id="presetEditor-az" max="450" min="0" name="az" required="" step="0.1" type="number"/><p class="field-help" id="presetEditor-az-help">Непрерывная цель AZ в активном диапазоне. Значения 0° и 360° задают разные механические положения.</p></div>
<div class="field el-setting"><label for="presetEditor-el">EL, °</label><input aria-describedby="presetEditor-el-help" id="presetEditor-el" max="180" min="0" name="el" step="0.1" type="number" value="0"/><p class="field-help" id="presetEditor-el-help">Непрерывная цель EL в активном диапазоне. Сохраняется вместе с AZ выбранной предустановки.</p></div></div>
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
<div class="device-fields"><div class="field"><label for="hudAddress">IP контроллера</label><input aria-describedby="hudAddress-help" id="hudAddress" placeholder="192.168.4.1"/><p class="field-help" id="hudAddress-help">IPv4-адрес контроллера, если поиск в сети его не нашёл. Адрес указан в веб-интерфейсе самого контроллера.</p></div><div class="field"><label for="hudPeerPort">UDP-порт HUD</label><input aria-describedby="hudPeerPort-help" id="hudPeerPort" max="65535" min="1" type="number" value="4534"/><p class="field-help" id="hudPeerPort-help">UDP-порт HUD на добавляемом контроллере. Он должен совпадать с портом в его настройках связи.</p></div></div>
<button class="btn">Проверить адрес</button></form>
<div class="device-list" id="hudDiscovered"></div>
<div class="device-pagination"><button class="btn" onclick="hudDiscoveryPage(-1)">Назад</button><span id="hudDiscoveryInfo"></span><button class="btn" onclick="hudDiscoveryPage(1)">Далее</button></div>
</section></div>
<section class="settings-group"><h3>Роль устройства и связь</h3>
<form id="hudConfig" onsubmit="saveHudDevice(event)"><div class="device-fields">
<div class="field"><label for="deviceRole">Роль после перезагрузки</label><select aria-describedby="deviceRole-help" id="deviceRole" name="role"><option value="controller">Контроллер поворотки</option><option value="hud">HUD — удалённый пульт</option></select><p class="field-help" id="deviceRole-help">Контроллер управляет приводом и читает датчики. HUD передаёт команды выбранному контроллеру; его моторные выводы отключены.</p></div>
<div class="field"><label for="deviceWifi">Подключение по Wi-Fi</label><select aria-describedby="deviceWifi-help" id="deviceWifi" name="wifi"><option value="existing">Через роутер / обычный режим</option><option value="host">Общая точка доступа</option><option value="client">Клиент общей точки доступа</option></select><p class="field-help" id="deviceWifi-help">Обычный режим — существующая сеть. Для общей сети одно устройство создаёт точку доступа, остальные работают её клиентами.</p></div>
<div class="field"><label for="deviceUart">Проводной UART</label><select aria-describedby="deviceUart-help" id="deviceUart" name="uart"><option value="0">GS-232 для программ, 9600</option><option value="1">HUD ↔ поворотка, 115200</option></select><p class="field-help" id="deviceUart-help">GS-232 — подключение программ на 9600 бод. HUD — проводная связь пульта и контроллера на 115200 бод; режим выбирается на обоих устройствах.</p></div>
<div class="field"><label for="devicePort">UDP-порт HUD</label><input aria-describedby="devicePort-help" id="devicePort" max="65535" min="1" name="port" required="" type="number" value="4534"/><p class="field-help" id="devicePort-help">Порт обмена между пультом и контроллерами. Используйте одинаковый порт на связанных устройствах, отличный от портов логгеров.</p></div>
</div><p class="hint">Для общей сети одно устройство создаёт точку доступа, остальные подключаются к ней через настройки Wi-Fi ниже. UART HUD занимает существующие провода GS-232.</p>
<div class="form-actions"><button class="btn good" type="submit">Сохранить и перезагрузить</button></div></form>
</section></div></details></section><section class="card settings-section" id="networkSettings"><details class="settings-details" open><summary><h2>Сеть и подключение<span class="section-caption">Домашний Wi-Fi, точка доступа и Bluetooth</span></h2></summary><div class="settings-content"><h3>Текущее подключение</h3><div class="kv">
<div>IP клиента (STA): <b id="ip">-</b></div><div>IP точки доступа: <b id="apip">-</b></div><div>TCP rotctld: <b>4533</b></div><div>Режим: <b id="mode">-</b></div><div>Уровень Wi-Fi: <b id="rssi">-</b></div><div>Качество: <b id="sig">-</b></div>
<div class="hide" id="btlinkrow">Bluetooth-соединение: <b id="btlinkstate" role="status">-</b></div>
<div><span id="btlabel">Bluetooth</span>: <b id="btstate" role="status">-</b></div><div id="bttransport">-</div>
<div class="hide" id="btmacrow">Bluetooth MAC: <b class="mono" id="btmac">-</b></div></div><div class="hint hide" id="bthelp">Этот MAC нужен в Look4Sat. Подключение видно и без передачи команд; сопряжение в Android само по себе не означает активное соединение.</div>
<section class="settings-group"><h3>Домашняя сеть Wi-Fi</h3><div class="row"><div class="field"><label for="ssid">Имя сети (SSID)</label><input autocomplete="off" aria-describedby="ssid-help" id="ssid"/><p class="field-help" id="ssid-help">Точное имя сети Wi-Fi, к которой подключится устройство после сохранения.</p></div><div class="field"><label for="pass">Пароль сети</label><input autocomplete="new-password" aria-describedby="pass-help" id="pass" type="password"/><p class="field-help" id="pass-help">Пароль выбранной сети Wi-Fi. Для открытой сети оставьте пустым. Сохранённый пароль не отображается в этом поле.</p></div></div>
<div class="form-actions"><button class="btn good" id="wifiSaveButton" onclick="wifi()">Сохранить и подключиться</button><span class="hint">Полученный IP появится ниже. Точка доступа останется доступной до следующей перезагрузки.</span></div><div aria-live="polite" class="inline-message" id="wifiMessage" role="status"></div><a id="wifiClientLink" class="hint" hidden target="_blank" rel="noopener noreferrer"></a></section>
<section class="settings-group"><h3>Точка доступа устройства</h3><div class="field"><label for="appass">Пароль точки доступа</label><input autocomplete="new-password" aria-describedby="appass-help" id="appass" maxlength="63" minlength="8" placeholder="Пустое поле — открытая сеть" type="password"/><p class="field-help" id="appass-help">Пароль собственной точки доступа устройства: 8–63 символа. Пустое поле отключает защиту паролем. Изменение применяется после перезагрузки.</p></div><p class="hint">От 8 до 63 символов. Оставьте поле пустым, чтобы открыть сеть без пароля.</p><div class="form-actions"><button class="btn good" id="apSaveButton" onclick="saveAp()">Сохранить точку доступа</button><span class="hint">Устройство перезагрузится.</span></div></section></div></details></section><section class="card settings-section" id="otaSettings"><details class="settings-details" ontoggle="if(this.open)loadOta()"><summary><h2>Обновление прошивки<span class="section-caption">OTA через браузер или Arduino IDE</span></h2></summary><div class="settings-content">
<p id="otaCapacity" role="status">Откройте раздел, чтобы проверить доступность OTA.</p><div class="kv"><div>Flash: <b id="otaFlash">—</b></div><div>Текущая прошивка: <b id="otaImage">—</b></div><div>Раздел OTA: <b id="otaSlot">—</b></div></div>
<p class="field-help">Выберите файл приложения .bin для той же платы и подключения. Настройки и калибровки сохраняются. Во время обновления движение будет остановлено; не отключайте питание.</p>
<form id="otaForm" onsubmit="uploadOta(event)"><div class="field"><label for="otaFile">Файл прошивки</label><input id="otaFile" type="file" accept=".bin,application/octet-stream" onchange="validateOtaFile()" disabled/><p id="otaFileHelp" class="field-help">Нужен firmware.bin из PlatformIO или файл приложения из Arduino IDE.</p></div>
<div class="form-actions"><button id="otaUpload" class="btn good" type="submit" disabled>Обновить прошивку</button><button id="otaCancel" class="btn" type="button" onclick="cancelOta()" hidden>Отменить</button><button id="otaReboot" class="btn" type="button" onclick="rebootOta()" hidden>Перезагрузить устройство</button></div></form>
<progress id="otaProgress" max="100" value="0" hidden style="width:100%" aria-label="Загрузка прошивки"></progress><p id="otaMessage" role="status" aria-live="polite"></p>
<section class="settings-group"><h3>ArduinoOTA</h3><p class="field-help">Включите сетевой порт на 10 минут, затем выберите его в Arduino IDE. Пароль меняется при каждом включении порта.</p><div class="form-actions"><button id="otaArduino" class="btn" onclick="armArduinoOta()" disabled>Включить ArduinoOTA на 10 минут</button></div><p id="otaArduinoInfo" class="field-help"></p></section>
</div></details></section><section class="card settings-section" id="signalSettings"><details class="settings-details" ontoggle="if(this.open)loadRssi()"><summary><h2>Уровень сигнала<span class="section-caption">История подключения к домашнему Wi-Fi</span></h2></summary><div class="settings-content">
<div class="period-options" aria-describedby="signalPeriodHelp"><button class="btn rbtn active" data-r="hour">Час</button><button class="btn rbtn" data-r="day">Сутки</button><button class="btn rbtn" data-r="week">Неделя</button><button class="btn rbtn" data-r="month">Месяц</button><button class="btn rbtn" data-r="year">Год</button></div>
<p class="field-help" id="signalPeriodHelp">Выберите период истории Wi-Fi. Чем ближе значение dBm к нулю, тем сильнее сигнал; пробелы на графике означают отсутствие измерений.</p><canvas height="260" id="rchart" width="900"></canvas>
<div class="kv"><div>Мин.: <b id="rmin">-</b></div><div>Макс.: <b id="rmax">-</b></div><div>Среднее: <b id="ravg">-</b></div><div>Измерений: <b id="rcount">0</b></div></div></div></details></section></section>
<section id="historyPanel" role="tabpanel" aria-labelledby="historyTab" hidden>
<div class="history-intro"><h2>История разработки</h2><p class="hint">Основные возможности форка RA3VLD по датам их появления.</p></div>
<ol class="history-list">
<li class="card history-entry"><h3><time datetime="2026-10-18">18 октября 2026</time></h3><ul>
<li>Отдельный выбор подтверждения цели или автоследования в ручном режиме для N1MM, PstRotator, GS-232 и rotctld / Hamlib.</li>
<li>Подсказки к настройкам, отметка NC для некалиброванных осей и статус связи с повороткой в заголовке.</li>
<li>Обновление прошивки по сети из браузера и через ArduinoOTA.</li>
</ul></li>
<li class="card history-entry"><h3><time datetime="2026-10-17">17 октября 2026</time></h3><ul>
<li>Режим выносного пульта HUD: выбор поворотки и управление по Wi-Fi или UART.</li>
<li>Тач-управление обеими осями на CrowPanel: выбор цели на кольце и ведение пальцем.</li>
<li>Управление и обновление положения через WebSocket.</li>
<li>Произвольные диапазоны AZ и EL, включая отрицательные углы, и калибровка по точкам внутри диапазона.</li>
<li>Настройка выбега и темпа торможения отдельно для каждой оси.</li>
<li>Отдельная вкладка настроек и компактный статус устройства.</li>
</ul></li>
<li class="card history-entry"><h3><time datetime="2026-10-16">16 октября 2026</time></h3><ul>
<li>Круглый дисплей CrowPanel 2.1 с энкодером: выбор цели, режимов и пауза движения.</li>
<li>Имитация привода для проверки управления без моторов и датчиков.</li>
<li>Диагностика памяти, отказов выделения и времени работы основного цикла.</li>
</ul></li>
<li class="card history-entry"><h3><time datetime="2026-10-15">15 октября 2026</time></h3><ul>
<li>Общая конфигурация сборки с выбором платы, дисплея, привода и Bluetooth.</li>
</ul></li>
<li class="card history-entry"><h3><time datetime="2026-10-14">14 октября 2026</time></h3><ul>
<li>Поддержка приводов с раздельным PWM и модулей BTS7960 / IBT-2.</li>
<li>Классический ESP32 и встроенный Bluetooth SPP для GS-232.</li>
</ul></li>
<li class="card history-entry"><h3><time datetime="2026-10-13">13 октября 2026</time></h3><ul>
<li>Сборки с экраном TFT и тач-панелью или без дисплея.</li>
</ul></li>
<li class="card history-entry"><h3><time datetime="2026-10-12">12 октября 2026</time></h3><ul>
<li>Публикация форка для G-5500 и G-800DXA на ESP32-S3 и ESP32-S2: веб-управление, калибровка, ручной и автоматические режимы, четыре предустановки.</li>
<li>Управление из логгеров по N1MM / PstRotator UDP, GS-232 через UART и HC-05, rotctld и GS-232 через TCP.</li>
<li>Регулировка скорости G-800DXA с плавным разгоном, замедлением у цели и торможением перед реверсом.</li>
</ul></li>
</ol>
</section>
<footer class="page-footer">
<span>G5500 Fork 0.1.0 · оригинал &copy; UA1CFM</span>
<div class="footer-links"><a href="https://github.com/ra3vld/G5500-ESP32S3-Controller" target="_blank" rel="noopener noreferrer">Репозиторий RA3VLD</a><a href="https://github.com/ua1cfm/G5500-ESP32S3-Controller" target="_blank" rel="noopener noreferrer">Оригинал UA1CFM</a></div>
</footer></div>
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


const pageTabs=['control','settings','history'];
function selectTab(name,updateHash=true){
 if(!pageTabs.includes(name))name='control';
 const settings=name==='settings',opening=settings&&document.getElementById('settingsPanel').hidden;
 document.body.classList.toggle('settings-view',settings);
 for(const tab of pageTabs){
  const active=tab===name,button=document.getElementById(tab+'Tab');
  document.getElementById(tab+'Panel').hidden=!active;
  button.classList.toggle('active',active);button.setAttribute('aria-selected',String(active));button.tabIndex=active?0:-1;
 }
 if(updateHash&&location.hash!=='#'+name)location.hash=name;
 if(!settings)document.getElementById('settingsFeedback').hidden=true;
 if(opening)loadRssi();
 if(updateHash)(settings?document.querySelector('.wrap'):window).scrollTo({top:0,behavior:'instant'});
}

let otaState=null,otaBusy=false,otaCancelled=false,otaTimer;
const otaBytes=n=>Number.isFinite(n)?n.toLocaleString('ru-RU')+' байт ('+(n/1048576).toFixed(2)+' МиБ)':'—';
const otaErrors={image_size:'Файл не помещается в раздел OTA.',wrong_chip:'Файл предназначен для другого чипа или не является прошивкой ESP32.',invalid_image:'Прошивка не прошла проверку целостности.',begin_failed:'Не удалось начать запись: проверьте свободную RAM и разметку flash.',write_failed:'Ошибка записи во flash.',incomplete_image:'Файл передан не полностью.',upload_timeout:'Загрузка прервана: данные не поступали 30 секунд.',cancelled:'Обновление отменено.',wrong_offset:'Нарушен порядок передачи файла.',ota_token:'Устройство перезагрузилось. Обновите состояние OTA.',update_locked:'Управление заблокировано до перезагрузки.',connect_failed:'ESP32 не смог подключиться к отправителю ArduinoOTA.',arduino_failed:'Ошибка передачи ArduinoOTA.',firmware_only:'Можно обновлять только приложение.',no_update:'Нет активной загрузки.'};
function otaMessage(text){setText(document.getElementById('otaMessage'),text);}
function validateOtaFile(){
 const file=document.getElementById('otaFile').files[0],state=otaState;
 const valid=state?.available&&!state.locked&&file&&file.size>=32&&file.size<=state.slotBytes&&/\.bin$/i.test(file.name);
 document.getElementById('otaUpload').disabled=otaBusy||!valid;
 let help=file?(!valid?'Выберите файл приложения .bin для этой платы.':file.name+' · '+otaBytes(file.size)):'Нужен firmware.bin из PlatformIO или файл приложения из Arduino IDE.';
 if(file&&file.size>state?.slotBytes){
  const minimum=131072+2*Math.ceil(file.size/65536)*65536;let flash=1048576;while(flash<minimum)flash*=2;
  help='Файл слишком большой. Максимум: '+otaBytes(state.slotBytes)+'. '+(state.flashBytes<minimum?'Для этой прошивки нужен другой модуль ESP32 с flash не менее '+flash/1048576+' МиБ (минимум '+otaBytes(minimum)+').':'Для этого файла нужна разметка с двумя разделами не менее '+otaBytes(file.size)+', установленная через USB.');
 }
 setText(document.getElementById('otaFileHelp'),help);
 return !!valid;
}
async function loadOta(){
 clearTimeout(otaTimer);if(otaBusy)return;
 try{
  const r=await settingsRequest('/api/ota');if(!r.ok)throw new Error('Не удалось проверить OTA.');const s=r.json();otaState=s;
  for(const [id,key] of [['otaFlash','flashBytes'],['otaImage','imageBytes'],['otaSlot','slotBytes']])setText(document.getElementById(id),otaBytes(s[key]));
  let text='OTA доступно. Максимальный размер файла: '+otaBytes(s.slotBytes)+'.';
  if(!s.available){
   if(s.reason==='flash_too_small')text='OTA недоступно: недостаточно flash-памяти. Нужен другой модуль ESP32 с flash не менее '+(s.recommendedFlashBytes/1048576)+' МиБ. Для текущей прошивки требуется минимум '+otaBytes(s.minimumFlashBytes)+' и два раздела приложения. PSRAM не заменяет flash.';
   else if(s.reason==='partition_layout'||s.reason==='slot_too_small')text='OTA недоступно: '+(s.reason==='slot_too_small'?'раздел OTA меньше текущей прошивки.':'в разметке нет двух подходящих разделов OTA и служебного раздела.')+' Установите разметку с двумя разделами приложения через USB. Требуется не менее '+otaBytes(s.minimumFlashBytes)+' flash; каждый раздел — не менее '+otaBytes(s.imageBytes)+'.';
   else text='Не удалось определить объём flash и размер прошивки. OTA недоступно.';
  }
  setText(document.getElementById('otaCapacity'),text);
  document.getElementById('otaFile').disabled=!s.available||s.locked;
  document.getElementById('otaArduino').disabled=!s.available||s.locked;
  document.getElementById('otaReboot').hidden=!s.locked||s.complete;
  if(s.locked)otaMessage(s.complete?'Прошивка проверена. Устройство перезагружается.':(otaErrors[s.error]||'Обновление начато.')+' Управление остановлено до перезагрузки.');
  setText(document.getElementById('otaArduinoInfo'),s.arduinoEnabled?'Сетевой порт: '+s.hostname+'.local:3232 · пароль: '+s.password:'Сетевой порт ArduinoOTA выключен.');
  validateOtaFile();
 }catch(e){setText(document.getElementById('otaCapacity'),'Не удалось проверить OTA. Повторите после восстановления связи.');}
 if(document.querySelector('#otaSettings details').open)otaTimer=setTimeout(loadOta,5000);
}
async function otaPost(path,body){
 const r=await settingsRequest(path,{method:'POST',headers:{'X-OTA-Token':otaState.token,'Content-Type':'application/octet-stream'},...(body?{body}:{})});
 const j=r.json();if(!r.ok||!j.ok)throw new Error(otaErrors[j.error]||j.error||'Ошибка OTA.');return j;
}
async function uploadOta(event){
 event.preventDefault();if(!validateOtaFile())return;
 const file=document.getElementById('otaFile').files[0];
 otaBusy=true;otaCancelled=false;clearTimeout(otaTimer);
 document.getElementById('otaUpload').disabled=true;document.getElementById('otaFile').disabled=true;document.getElementById('otaArduino').disabled=true;
 const progress=document.getElementById('otaProgress');progress.hidden=false;progress.value=0;
 document.getElementById('otaCancel').hidden=false;document.getElementById('otaCancel').disabled=false;document.getElementById('otaReboot').hidden=true;
 let started=false;
 try{
  const header=new Uint8Array(await file.slice(0,24).arrayBuffer());
  if(header[0]!==0xe9||(header[12]|header[13]<<8)!==otaState.chipId)throw new Error(otaErrors.wrong_chip);
  otaMessage('Остановка привода и подготовка обновления…');await otaPost('/api/ota/start?size='+file.size);started=true;
  for(let offset=0;offset<file.size;offset+=otaState.chunkBytes){
   if(otaCancelled)throw new Error('Обновление отменено.');
   const chunk=file.slice(offset,offset+otaState.chunkBytes);await otaPost('/api/ota/write?offset='+offset,chunk);
   progress.value=Math.floor((offset+chunk.size)/file.size*100);otaMessage('Передано '+progress.value+'%. Управление остановлено.');
  }
  if(otaCancelled)throw new Error('Обновление отменено.');
  otaMessage('Проверка прошивки…');await otaPost('/api/ota/finish');otaMessage('Прошивка проверена. Устройство перезагружается. После подключения обновите страницу.');
 }catch(e){
  if(started)try{await otaPost('/api/ota/cancel');}catch(_){}
  otaMessage((e.name==='AbortError'?'Устройство не ответило за 12 секунд.':e instanceof TypeError?'Связь с устройством прервана.':e.message||'Ошибка обновления.')+(started?' Управление остановлено до перезагрузки.':''));document.getElementById('otaReboot').hidden=!started;
 }finally{
  otaBusy=false;document.getElementById('otaCancel').hidden=true;
  // Preserve the completion/error message; refresh controls after reconnect.
  otaTimer=setTimeout(loadOta,5000);
 }
}
function cancelOta(){otaCancelled=true;document.getElementById('otaCancel').disabled=true;}
async function rebootOta(){try{await otaPost('/api/ota/reboot');otaMessage('Устройство перезагружается. После подключения обновите страницу.');document.getElementById('otaReboot').hidden=true;}catch(e){otaMessage(e.message);}}
async function armArduinoOta(){try{await otaPost('/api/ota/arduino');await loadOta();}catch(e){otaMessage(e.message);}}

function networkAddresses(j){
 const valid=ip=>typeof ip==='string'&&/^\d{1,3}(\.\d{1,3}){3}$/.test(ip)&&ip!=='0.0.0.0';
 const sta=j?.staIp??(Number.isFinite(j?.rssi)?j.ip:null),ap=j?.apIp??(j?.wifiMode==='Access Point'?j.ip:null);
 return [valid(sta)?'STA: '+sta:'',valid(ap)?'AP: '+ap:''].filter(Boolean);
}
function compactStatus(j){
 const wifi=document.getElementById('statusWifi'),bt=document.getElementById('statusBt');
 const wifiModes={'Wi-Fi Client':'Подключён','AP + Wi-Fi Client':'Подключён + AP','Access Point':'Точка доступа','Disconnected':'Нет подключения'};
 wifi.textContent=[wifiModes[j.wifiMode]||j.wifiMode||'—',...networkAddresses(j)].join('\n');wifi.style.whiteSpace='pre-line';wifi.title=j.ssid||'';
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
const connection={httpOnline:null,httpLast:0,state:null};
const connectionFavicon=document.getElementById('connectionFavicon'),offlineFavicon=connectionFavicon.href;
const onlineFavicon='data:image/svg+xml,'+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"><rect width="32" height="32" rx="7" fill="#081018"/><g transform="translate(4 4)" fill="none" stroke="#24d18d" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8M21 3v5h-5"/></g></svg>');
function updateConnectionFavicon(online){
 const state=online?'online':'offline';
 if(connectionFavicon.dataset.state===state)return;
 connectionFavicon.href=online?onlineFavicon:offlineFavicon;connectionFavicon.dataset.state=state;
}
let httpState=null,liveFrame=0;
function updateConnection(){
 const node=document.getElementById('rotatorConnection'),j=connection.state,now=Date.now();
 const reachable=(live.ready&&now-live.last<=1500)||(connection.httpOnline&&now-connection.httpLast<=6500);
 let state='pending',text='Проверяем связь…',hint='Ожидаем состояние устройства.';
 if(reachable&&j){
  const hud=j.deviceRole==='hud',selected=j.hudSelected&&!/^0+$/.test(j.hudSelected);
  if(hud&&!selected){state='unselected';text='Поворотка не выбрана';hint='Выберите поворотку в настройках пульта.';}
  else if(hud&&!j.hudOnline){state='offline';text='Нет связи с повороткой';hint='Пульт на связи, но выбранная поворотка '+(j.hudName||j.hudSelected)+' недоступна.';}
  else{state='online';text='Поворотка на связи';hint=hud?'Выбрана '+(j.hudName||j.hudSelected)+' · '+(j.hudTransport==='uart'?'UART':'Wi-Fi'):'Браузер получает состояние контроллера поворотки.';}
 }else if(connection.httpOnline!==null){state='offline';text='Нет связи с устройством';hint='Браузер не получает актуальное состояние '+(j?.deviceRole==='hud'?'пульта.':'контроллера поворотки.');}
 const addresses=networkAddresses(j);
 if(addresses.length)text+=' · '+(reachable?'':'Последние адреса: ')+addresses.join(' · ');
 node.dataset.state=state;node.title=hint;setText(node,text);
 updateConnectionFavicon(state==='online');
}
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
   connection.state={...httpState,...message.state};updateConnection();
   if(changed)setTimeout(()=>upd(true),0);
  }else if(message.type==='result'&&message.boot===live.boot){
   if(message.cancelledFrom&&message.cancelledThrough){for(const [id,p] of live.pending)if(id>=message.cancelledFrom&&id<=message.cancelledThrough){clearTimeout(p.timer);live.pending.delete(id);p.resolve({status:409,json:async()=>message.result})}return;}
   const pending=live.pending.get(message.id);if(!pending)return;
   if(message.result.result==='deferred'){document.getElementById('forkMessage').textContent='Ожидаем подтверждение поворотки…';return;}
   clearTimeout(pending.timer);live.pending.delete(message.id);
   pending.resolve({status:message.result.ok?200:409,json:async()=>message.result});
  }
 };
 socket.onclose=()=>{
  if(live.socket!==socket)return;
  live.socket=null;live.ready=false;live.state=null;live.boot=null;live.revision=0;
  updateConnection();
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
setInterval(()=>{if(live.ready&&Date.now()-live.last>1500)live.socket.close();updateConnection()},250);
document.addEventListener('visibilitychange',()=>{if(!document.hidden){queueLiveRender();upd(true)}});
async function forkPost(path,body){
 const success={'/api/settings':'Настройки сохранены','/api/preset':'Предустановка сохранена','/api/device':'Настройки устройства сохранены','/api/hud/select':'Поворотка выбрана','/api/hud/peers':body.action==='remove'?'Поворотка удалена':body.name?'Имя поворотки сохранено':'Поворотка добавлена','/api/hud/discover':'Поиск устройств запущен'}[path];
 const notice=success?beginSettingsAction(path==='/api/hud/discover'?'Поиск устройств…':'Ожидаем подтверждение устройства…',path+JSON.stringify(body)):null;
 if(success&&!notice)return false;
 try{
  const selection=body.selection??window._forkState?.hudSelectionEpoch,name=window._forkState?.hudName;
  if(window._forkState?.deviceRole==='hud'&&['/api/target','/api/move','/api/mode','/api/execute','/api/preset','/api/preset/run','/api/motion/reset'].includes(path))body={...body,selection};
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
  const missingCalibration=path==='/api/target'?['az','el'].filter((axis,i)=>body[axis]!==undefined&&window._forkState?.axes?.[i]?.calibrated===false).map(axis=>axis.toUpperCase()):[];
  if(j.superseded)return false;
  const calibrationMessage='Сначала выполните калибровку'+(missingCalibration.length?' '+missingCalibration.join(' и '):'');
  const messages={deferred:'Ожидаем подтверждение',timeout:'Нет подтверждения — проверьте состояние поворотки',unavailable:'Проверьте связь, параметры движения и ошибку оси',busy:'Поворотка занята, повторите команду',cancelled:'Команда отменена',storage_full:'Не хватает памяти для сохранения',motion_storage_failed:'Не удалось сохранить параметры движения',hud_port_conflict:'Порт HUD совпадает с портом логгера',controller_settings_only:'Эти настройки доступны на контроллере поворотки',restart:'Сохранено. Устройство перезагружается',searching:'Поиск устройств…',invalid_or_port_conflict:'Проверьте настройки и порты',uart_in_use_by_hc05:'UART занят HC-05: выключите Bluetooth в настройках оборудования',uart_disabled:'Назначьте RX и TX UART в настройках оборудования',not_calibrated:calibrationMessage,invalid:'Проверьте значения',ignored:'Сейчас выполняется предустановка',pending:'Цель ожидает подтверждения',ok:'Готово'};
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
async function saveFork(e){e.preventDefault();const body=Object.fromEntries(new FormData(e.target));if(window._forkState?.build?.drive==='YAESU')body.speedEnabled=e.target.elements.speedEnabled.checked?'true':'false';else delete body.speedEnabled;if(window._forkState?.build?.display==='CROWPANEL_21')body.encoderInverted=e.target.elements.encoderInverted.checked?'true':'false';else delete body.encoderInverted;if(window._forkState?.deviceRole==='hud')for(const key of Object.keys(body))if(!['rotorName','n1mmPort','pstPort','reportPort','reportHost','encoderInverted','n1mmManualFollow','pstManualFollow','gs232ManualFollow','rotctldManualFollow','externalAzMode'].includes(key))delete body[key];if(await forkPost('/api/settings',body))delete e.target.dataset.dirty;}

const simpleMotionKeys=['speedPercent','approachSpeedPercent','accelerationMs','decelerationMs','slowdownDegrees','coastDegrees','decelerationFactor'];
const motionFields=[["OutputLimitPercent","Максимальный выход, %","Общий предел выхода, включая пусковой импульс. Проверьте допустимый уровень подключённого каскада.",1.0,100.0,"any","common"],["KickPercent","Пусковой импульс, %","Может превышать рабочий выход, но не общий предел. Например, старт 50%, движение 5%.",1.0,100.0,"any","kick"],["MovementDegrees","Подтверждение движения, °","Изменение угла для обнаружения страгивания. Выберите выше шума датчика.",0.001,10.0,"any","common"],["SustainingPercent","Минимальный выход движения, %","Поддерживает движение после страгивания в каскаде. 0 — без ограничения снизу. При торможении, достижении позиции, STOP и защите выход отключается.",0,100,"1","cascade"],["Velocity","Скорость, °/с","Предельная скорость профиля движения в каскадном режиме.",0.001,360.0,"any","cascade"],["Acceleration","Ускорение, °/с²","Ограничение ускорения профиля в обоих направлениях.",0.001,1000.0,"any","cascade"],["Deceleration","Торможение, °/с²","Допустимое замедление профиля. Подберите по инерции механизма.",0.001,1000.0,"any","cascade"],["Jerk","Рывок, °/с³","Ограничение изменения ускорения S-профиля. Меньше — плавнее.",0.001,10000.0,"any","cascade"],["PositionKp","Положение · P","Пропорциональная составляющая внешнего контура.",0.0,1000.0,"any","cascade"],["PositionKi","Положение · I","Интегральная составляющая внешнего контура. 0 — отключена.",0.0,1000.0,"any","cascade"],["PositionKd","Положение · D","Гашение движения по производной измеренного положения. 0 — отключено.",0.0,1000.0,"any","cascade"],["VelocityKp","Скорость · P","Пропорциональная составляющая внутреннего контура.",0.0,10000.0,"any","cascade"],["VelocityKi","Скорость · I","Интегральная составляющая внутреннего контура. 0 — отключена.",0.0,10000.0,"any","cascade"],["VelocityKd","Скорость · D","Производная измеренной скорости с фильтрацией. 0 — отключена.",0.0,10000.0,"any","cascade"],["Feedforward","Упреждение выхода, % на °/с","Приближённый выход на единицу скорости; ПИД исправляет остаточную ошибку.",0.0,100000.0,"any","cascade"],["PositionTolerance","Допуск положения, °","Допустимая ошибка при завершении наведения. Выберите по разрешению и шуму датчика.",0.001,90.0,"any","cascade"],["HoldResumeDegrees","Возобновлять удержание за, °","Порог коррекции отклонения; не меньше допуска положения.",0.001,180.0,"any","cascade"],["StoppedVelocity","Порог остановки, °/с","Измеренная скорость, ниже которой механизм считается остановленным.",0.001,10.0,"any","cascade"],["KickMs","Предел пускового импульса, мс","Задаёт нарастание пускового выхода. В обычном движении подтверждение ожидается не меньше срока контроля заклинивания.",20.0,10000.0,"1","kick"],["KickReleaseMs","Снятие пускового импульса, мс","Плавный переход от пускового выхода к рабочему после обнаружения движения.",20.0,2000.0,"1","kick"],["VelocityWindowMs","Окно оценки скорости, мс","Большее окно уменьшает шум, но увеличивает задержку. Не больше срока свежести.",40.0,600.0,"1","common"],["DerivativeFilterMs","Фильтр D, мс","Сглаживание производных обоих ПИД. Большее время сильнее подавляет шум.",20.0,2000.0,"1","cascade"],["FeedbackTimeoutMs","Срок свежести датчика, мс","При отсутствии новых измерений дольше этого срока выход отключается.",100.0,5000.0,"1","common"],["SettleMs","Подтверждение позиции, мс","Положение и скорость должны оставаться в допуске всё это время.",20.0,5000.0,"1","cascade"],["StallMs","Контроль заклинивания, мс","Отключение при отсутствии движения под ненулевым выходом. Смена цели таймер не сбрасывает.",500.0,30000.0,"1","common"]];
function initMotionForms(){
 const root=document.getElementById('motionForms');
 for(const axis of ['az','el']){
  const f=document.createElement('form');f.id=axis+'MotionForm';f.className='settings-pane';f.dataset.axis=axis;
  f.innerHTML=`<h3>${axis.toUpperCase()}</h3><div class="row" data-motion-group="variable"><div class="field"><label for="${axis}RegulatorMode">Регулятор</label><select id="${axis}RegulatorMode" name="${axis}RegulatorMode" aria-describedby="${axis}MotionModeHelp"><option value="simple">Простой · без ПИД</option><option value="cascade">Каскадный ПИД</option></select><p class="field-help" id="${axis}MotionModeHelp">Параметры простого режима находятся ниже: скорость, выбег и S-образный разгон/торможение.</p></div></div><label class="check-field" data-motion-group="variable"><input type="checkbox" name="${axis}MotionKickEnabled"> Пусковой импульс для страгивания</label><label class="check-field" data-motion-group="cascade"><input type="checkbox" name="${axis}MotionHoldEnabled"> Активно удерживать заданную позицию</label><div class="row motion-fields"></div><p class="hint motion-status" role="status"></p><div class="form-actions"><button class="btn good" type="submit">Сохранить ${axis.toUpperCase()}</button><button class="btn" type="button" onclick="resetMotion('${axis}')">Сбросить ошибку</button></div>`;
  const simple=document.createElement('div');simple.className='settings-group';simple.dataset.motionGroup='simpleSection';
  simple.innerHTML='<h4>Параметры простого режима</h4><p class="hint" data-motion-group="simple">Разгон и торможение S-образные. Их плавность задаётся временем перехода; коэффициент торможения изменяет темп замедления. STOP отключает выход сразу.</p><div class="row simple-motion-fields"></div>';
  for(const key of simpleMotionKeys){
   const name=axis==='az'?key:'el'+key[0].toUpperCase()+key.slice(1),input=document.getElementById('forkSettings').elements[name],field=input.closest('.field');
   field.dataset.motionGroup=key==='coastDegrees'?'coast':'simple';simple.querySelector('.simple-motion-fields').append(field);
  }
  f.insertBefore(simple,f.querySelector('.check-field'));
  for(const [name,label,help,min,max,step,group] of motionFields){
   const key=axis+'Motion'+name,field=document.createElement('div');field.className='field';field.dataset.motionGroup=group;
   field.innerHTML=`<label for="${key}">${label}</label><input id="${key}" name="${key}" type="number" min="${min}" max="${max}" step="${step}" required aria-describedby="${key}Help"><p class="field-help" id="${key}Help">${help}</p>`;f.querySelector('.motion-fields').append(field);
  }
  const tuner=document.createElement('div');tuner.className='settings-group';tuner.dataset.motionGroup='variable';
  tuner.innerHTML=`<h3>Автотюн</h3><div class="kv"><div>Положение<br><strong id="${axis}TunePosition">—</strong></div><div>Измеренная скорость<br><strong id="${axis}TuneVelocity">—</strong></div><div>Заданная скорость<br><strong id="${axis}TuneRequestedVelocity">—</strong></div><div>Выход привода<br><strong id="${axis}TuneOutput">—</strong></div><div>Диапазон<br><strong id="${axis}TuneRange">—</strong></div><div>Свободный ход − / +<br><strong id="${axis}TuneRoom">—</strong></div></div><p id="${axis}TuneDriveState" role="status" aria-live="polite">Нет данных о приводе</p><div class="form-actions"><button class="btn" type="button" id="${axis}TuneCenter" onclick="autoTunePrepare('center','${axis}')">В середину диапазона</button><button class="btn" type="button" id="${axis}TuneReset" onclick="autoTunePrepare('reset','${axis}')" disabled>Сбросить ошибки</button><button class="btn stop" type="button" id="${axis}TuneStop" onclick="autoTunePrepare('stop','${axis}')" disabled>STOP</button></div><p class="hint">К середине — с сохранёнными настройками, без удержания после достижения. Предел пробного выхода действует только во время автотюна.</p><p class="hint">Пробные движения в обе стороны и проверочное наведение. Сначала выберите ручной режим и освободите ход вокруг текущей позиции. STOP отменяет процедуру. Предложенные параметры сохраняются только кнопкой «Применить»; удержание остаётся выключенным.</p><div class="row"><div class="field"><label for="${axis}TuneTravel">Допустимый ход в каждую сторону, °</label><input id="${axis}TuneTravel" type="number" min="10" max="90" step="1" value="60"><p class="field-help">Весь выбранный ход должен находиться внутри откалиброванного диапазона с запасом до упоров.</p></div><div class="field"><label for="${axis}TuneLimit">Предел пробного выхода, %</label><input id="${axis}TuneLimit" type="number" min="10" max="100" step="1" value="50"><p class="field-help">Не выше сохранённого максимального выхода оси.</p></div></div><p id="${axis}TuneStatus" role="status" aria-live="polite">Готов к запуску</p><div id="${axis}TuneResult" class="hint"></div><div class="form-actions"><button class="btn" type="button" id="${axis}TuneStart" onclick="autoTuneAction('start','${axis}')">Запустить автотюн</button><button class="btn" type="button" id="${axis}TuneCancel" onclick="autoTuneAction('cancel','${axis}')" disabled>Отменить</button><button class="btn good" type="button" id="${axis}TuneApply" onclick="autoTuneAction('apply','${axis}')" disabled>Применить результат</button></div>`;
  tuner.addEventListener('input',e=>e.stopPropagation());f.append(tuner);
  f.addEventListener('input',()=>{f.dataset.dirty='1';motionVisibility(f)});f.onsubmit=saveMotion;root.append(f);motionVisibility(f);
 }
}
function motionVisibility(f){
 const axis=f.dataset.axis,variable=f.dataset.variable!=='false',cascade=variable&&f.elements[axis+'RegulatorMode'].value==='cascade',kick=f.elements[axis+'MotionKickEnabled'].checked;
 for(const field of f.querySelectorAll('[data-motion-group]')){
  const group=field.dataset.motionGroup,visible=group==='simpleSection'||group==='coast'?!cascade:group==='simple'?variable&&!cascade:group==='common'||group==='variable'?variable:group==='cascade'?cascade:variable&&kick;
  field.classList.toggle('hide',!visible);for(const input of field.querySelectorAll('input,select'))input.disabled=!visible;
 }
}
async function loadMotionSettings(){
 if(window._forkState?.deviceRole==='hud')return;
 try{const r=await fetch('/api/motion',{cache:'no-store'});if(!r.ok)return;const config=await r.json();
  for(const [i,axis] of ['az','el'].entries()){
   const f=document.getElementById(axis+'MotionForm'),m=config.axes[i];if(!m||f.dataset.dirty)continue;
   f.elements[axis+'RegulatorMode'].value=m.regulatorMode;
   for(const name of ['KickEnabled','HoldEnabled'])f.elements[axis+'Motion'+name].checked=!!m[name[0].toLowerCase()+name.slice(1)];
   for(const [name] of motionFields)f.elements[axis+'Motion'+name].value=m[name]??(name==='SustainingPercent'?0:'');motionVisibility(f);
  }
 }catch(e){settingsFeedback('Не удалось прочитать параметры движения. Проверьте связь.')}
}
async function saveMotion(e){
 e.preventDefault();const f=e.target,axis=f.dataset.axis,body=Object.fromEntries(new FormData(f));
 for(const name of ['KickEnabled','HoldEnabled'])body[axis+'Motion'+name]=f.dataset.variable==='false'?'false':f.elements[axis+'Motion'+name].checked?'true':'false';
 if(body[axis+'RegulatorMode']==='simple')body[axis+'MotionHoldEnabled']='false';
 if(await forkPost('/api/settings',body)){delete f.dataset.dirty;await loadMotionSettings()}
}
async function resetMotion(axis){
 const button=document.getElementById('resetDriveError');if(button.disabled)return;
 button.disabled=true;try{await forkPost('/api/motion/reset',{axis})}finally{button.disabled=false}
}
let autoTuneRequest=false,autoTuneResultKey='',autoTuneDetails=null;
function autoTuneSetupProblem(j,i){
 return j.deviceRole!=='controller'||j.configurationLocked?'Управление сейчас недоступно.':j.controlMode!=='manual'?'Выберите ручной режим.':
  j.autotune?.active||j.moving||j.pendingValid||j.presetActive||j.axes?.some(a=>a.direction||a.tracking||a.holdArmed)?'Остановите движение и снимите ожидающую цель.':
  !j.axes?.[i]?.calibrated?'Сначала выполните калибровку оси.':driveFaultMessages(j).length?'Сначала устраните и сбросьте ошибку привода.':'';
}
function autoTuneMiddle(j,axis){
 const low=j[axis+'Start'],high=j[axis+'Max'];
 return typeof low==='number'&&typeof high==='number'&&Number.isFinite(low)&&Number.isFinite(high)&&high>low?low+(high-low)/2:null;
}
async function autoTunePrepare(action,axis){
 // STOP remains available while another preparation request awaits its reply.
 if(action==='stop'){await forkPost('/api/move',{d:'S'});await upd(true);return}
 if(autoTuneRequest)return;
 const j=window._forkState||{},i=axis==='az'?0:1;
 if(j.deviceRole!=='controller'||j.autotune?.active)return;
 if(action==='center'){
  const problem=autoTuneSetupProblem(j,i)||(document.getElementById(axis+'MotionForm').dataset.dirty?'Сначала сохраните параметры оси.':'')||(autoTuneMiddle(j,axis)===null?'Нет данных о диапазоне оси.':'');
  if(problem){settingsFeedback(problem);return}
 }else if(action!=='reset'||j.moving||j.axes?.some(a=>a.direction||a.tracking||a.holdArmed))return;
 autoTuneRequest=true;renderAutoTune(j);
 try{await forkPost(action==='center'?'/api/motion/autotune':'/api/motion/reset',action==='center'?{action:'center',axis,rangeRevision:j.azRangeRevision}:{axis:'all'});await upd(true)}
 finally{autoTuneRequest=false;renderAutoTune(window._forkState||{})}
}

async function autoTuneAction(action,axis){
 if(autoTuneRequest)return;
 const f=document.getElementById(axis+'MotionForm'),body={action};
 if(action==='start'){
  if(f.dataset.dirty){settingsFeedback('Сначала сохраните параметры оси.');return}
  const j=window._forkState||{},i=axis==='az'?0:1,travel=Number(document.getElementById(axis+'TuneTravel').value),limit=Number(document.getElementById(axis+'TuneLimit').value);
  const margin=Math.max(2,Number(f.elements[axis==='az'?'coastDegrees':'elCoastDegrees'].value)),position=j.axes?.[i]?.current;
  let problem=j.controlMode!=='manual'?'Выберите ручной режим.':j.moving||j.pendingValid||j.axes?.some(a=>a.tracking||a.holdArmed)?'Остановите движение и снимите ожидающую цель.':!j.axes?.[i]?.calibrated?'Сначала выполните калибровку оси.':j.fault||j.axes?.some(a=>a.motionFault)?'Сначала устраните и сбросьте ошибку привода.':
   limit>Number(f.elements[axis+'MotionOutputLimitPercent'].value)?'Пробный выход превышает сохранённый максимум.':travel<6*Number(f.elements[axis+'MotionMovementDegrees'].value)?'Ход должен быть не меньше шести порогов подтверждения движения.':
   position-travel<Number(j[axis+'Start'])+margin||position+travel>Number(j[axis==='az'?'azMax':'elMax'])-margin?'Недостаточно места: уменьшите пробный ход или переместите ось ближе к середине диапазона.':'';
  if(problem){setText(document.getElementById(axis+'TuneStatus'),problem);settingsFeedback(problem);return}
  for(const suffix of ['TuneTravel','TuneLimit'])if(!document.getElementById(axis+suffix).reportValidity())return;
  body.axis=axis;body.mode=f.elements[axis+'RegulatorMode'].value;
  body.id=window._forkState?.autotune?.id||0;
  body.travel=document.getElementById(axis+'TuneTravel').value;body.outputLimit=document.getElementById(axis+'TuneLimit').value;
  body.rangeRevision=window._forkState?.azRangeRevision;
 }
 if(action==='apply')body.id=window._forkState?.autotune?.id;
 autoTuneRequest=true;renderAutoTune(window._forkState||{});
 try{if(await forkPost('/api/motion/autotune',body)){autoTuneResultKey='';autoTuneDetails=null;await loadMotionSettings();await upd(true)}}
 finally{autoTuneRequest=false;renderAutoTune(window._forkState||{})}
}
function renderAutoTune(j){
 if(j.deviceRole==='hud')return;
 const t=j.autotune||{phase:'idle',active:false,axis:-1};
 if(autoTuneDetails&&autoTuneDetails.id!==t.id)autoTuneDetails=null;
 const phases={idle:t.reason==='applied'?'Результат сохранён':'Готов к запуску',quiet:'Оценка шума датчика',start:'Поиск уровня страгивания',low:'Проверка рабочего выхода',high:'Измерение отклика',coast:'Измерение выбега',reposition:'Подготовка следующего пробного хода',validate:'Проверочное наведение',complete:'Готово. Проверьте и примените результат',cancelled:'Автотюн отменён',failed:'Автотюн остановлен'};
 const reasons={stopped:'STOP',cancelled:'по команде пользователя',mode_changed:'изменён режим',hardware_changed:'изменено оборудование',calibration_changed:'изменена калибровка',feedback_or_configuration:'нет достоверных свежих измерений или изменён диапазон',timeout:'превышено время процедуры',other_axis_moved:'изменилась неподвижная ось',travel_limit:'выход за безопасную границу пробного участка',insufficient_travel:'привод не успевает безопасно завершить измерение на этом участке; увеличьте разрешённый ход',feedback_noise:'слишком шумный датчик',no_start:'не удалось тронуться в пределах заданного выхода',start_rejected:'команда движения отклонена',no_sustained_motion:'нет устойчивого рабочего выхода',unstable_response:'неустойчивая измеренная скорость',insufficient_response:'слишком мал отклик на изменение выхода',settle_timeout:'не удалось подтвердить остановку или достижение',validation_error:'слишком большая ошибка проверочного наведения',model_invalid:'не удалось получить допустимые параметры'};
 reasons.control_timing='слишком большая задержка расчёта';reasons.motion_storage_failed='не удалось сохранить параметры';
 for(const [i,axis] of ['az','el'].entries()){
  const own=t.axis===i,status=document.getElementById(axis+'TuneStatus');
  const a=j.axes?.[i]||{},number=(v,unit)=>typeof v==='number'&&Number.isFinite(v)?v.toFixed(2)+unit:'—';
  const sensed=a.calibrated&&!['feedback_stale','feedback_invalid'].includes(a.motionFault);
  const middle=autoTuneMiddle(j,axis),errors=driveFaultMessages(j),busy=!!(j.moving||j.axes?.some(v=>v.direction||v.tracking||v.holdArmed));
  setText(document.getElementById(axis+'TunePosition'),a.calibrated?number(a.current,'°'):'—');
  setText(document.getElementById(axis+'TuneVelocity'),sensed?number(a.measuredVelocity,' °/с'):'—');
  setText(document.getElementById(axis+'TuneRequestedVelocity'),a.regulatorMode==='cascade'&&(!own||!t.active||t.phase==='validate')?number(a.requestedVelocity,' °/с'):'—');
  setText(document.getElementById(axis+'TuneOutput'),number(a.speedOutputPercent,'%'));
  setText(document.getElementById(axis+'TuneRange'),middle===null?'—':number(j[axis+'Start'],'°')+' … '+number(j[axis+'Max'],'°'));
  setText(document.getElementById(axis+'TuneRoom'),middle===null||!a.calibrated||typeof a.current!=='number'?'—':number(Math.max(0,a.current-j[axis+'Start']),'°')+' / '+number(Math.max(0,j[axis+'Max']-a.current),'°'));
  const drivePhases={idle:'Остановлен',kick:'Страгивание',run:'Движение',braking:'Торможение',holding:'Удержание',fault:'Ошибка привода'};
  setText(document.getElementById(axis+'TuneDriveState'),errors.length?'Ошибка: '+errors.join(' · '):!a.calibrated?'Нет калибровки':drivePhases[a.motionPhase]||'Нет данных о приводе');
  const center=document.getElementById(axis+'TuneCenter');setText(center,middle===null?'В середину диапазона':'В середину ('+Number(middle.toFixed(2))+'°)');
  center.disabled=autoTuneRequest||!!autoTuneSetupProblem(j,i)||middle===null||!!document.getElementById(axis+'MotionForm').dataset.dirty;
  document.getElementById(axis+'TuneReset').disabled=autoTuneRequest||t.active||busy||!errors.length;
  document.getElementById(axis+'TuneStop').disabled=!busy&&!t.active&&!autoTuneRequest;

  setText(status,own?(phases[t.phase]||t.phase)+(t.active?' · '+(t.direction>0?'вперёд':'назад')+' · '+Number(t.output).toFixed(0)+'%':'')+(t.reason&&t.reason!=='applied'?' · '+(reasons[t.reason]||motionFaultText(t.reason)):''):'Готов к запуску');
  document.getElementById(axis+'TuneStart').disabled=autoTuneRequest||t.active||busy;
  document.getElementById(axis+'TuneCancel').disabled=autoTuneRequest||!t.active||!own;
  document.getElementById(axis+'TuneApply').disabled=autoTuneRequest||t.phase!=='complete'||!own||autoTuneDetails?.id!==t.id;
  document.querySelector('#'+axis+'MotionForm button[type=submit]').disabled=!!t.active;
  const result=document.getElementById(axis+'TuneResult');
  if(!own||t.phase!=='complete')setText(result,'');
  else if(autoTuneDetails){const r=autoTuneDetails.result;result.style.whiteSpace='pre-line';setText(result,r?autoTuneSummary(t,r):'')}
 }
 if(t.phase==='complete'&&autoTuneResultKey!==String(t.id)){
  autoTuneResultKey=String(t.id);
  fetch('/api/motion/autotune',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error();return r.json()}).then(details=>{if(details.id!==window._forkState?.autotune?.id||details.phase!=='complete'||window._forkState?.autotune?.phase!=='complete')return;autoTuneDetails=details;renderAutoTune(window._forkState)}).catch(()=>{autoTuneResultKey='';settingsFeedback('Не удалось прочитать результат автотюна.')});
 }
}
function autoTuneSummary(t,r){
 const n=value=>Number(value).toPrecision(4);
 const selected=t.mode==='simple'?`Рабочий выход / подход: ${r.speedPercent} / ${r.approachSpeedPercent}%\nВыбег: ${n(r.coastDegrees)}°; замедление за ${r.slowdownDegrees}°\nРазгон / торможение: ${r.accelerationMs} / ${r.decelerationMs} мс; коэффициент торможения ${r.decelerationFactor}`:
  `Положение P / I / D: ${n(r.PositionKp)} / ${n(r.PositionKi)} / ${n(r.PositionKd)}\nСкорость P / I / D: ${n(r.VelocityKp)} / ${n(r.VelocityKi)} / ${n(r.VelocityKd)}\nСкорость: ${n(r.Velocity)} °/с; ускорение / торможение: ${n(r.Acceleration)} / ${n(r.Deceleration)} °/с²\nМинимальный выход движения: ${r.SustainingPercent??0}%\nРывок: ${n(r.Jerk)} °/с³; упреждение: ${n(r.Feedforward)} % на °/с\nДопуск позиции: ${n(r.PositionTolerance)}°; гистерезис удержания: ${n(r.HoldResumeDegrees)}°`;
 return selected+`\nМаксимальный выход: ${n(r.OutputLimitPercent)}%; пуск: ${n(r.KickPercent)}%, нарастание ${r.KickMs} мс, ожидание до ${Math.max(r.KickMs,r.StallMs||0)} мс; снятие ${r.KickReleaseMs} мс\nПодтверждение движения: ${n(r.MovementDegrees)}°; порог остановки: ${n(r.StoppedVelocity)} °/с\nОшибка проверочного наведения: ${Number(t.validationError).toFixed(2)}°. Удержание выключено.`;
}
function renderMotionStatus(j){
 const hud=j.deviceRole==='hud';document.getElementById('motionSettings').classList.toggle('hide',hud||j.build?.drive==='NONE');
 const phases={idle:'Остановлен',kick:'Страгивание',run:'Движение',braking:'Торможение',holding:'Удержание',fault:'Ошибка'};
 for(const [i,axis] of ['az','el'].entries()){
  const f=document.getElementById(axis+'MotionForm'),a=j.axes?.[i];if(!a)continue;
  f.classList.toggle('hide',axis==='el'&&j.model==='G800DXA');f.dataset.variable=String(!!a.speedEnabled);motionVisibility(f);
  const state=!a.motionReady?'Ошибка сохранённых параметров движения':a.motionFault?'Выход отключён: '+motionFaultText(a.motionFault):phases[a.motionPhase]||'Остановлен';
  f.querySelector('.motion-status').textContent=state+' · '+Number(a.measuredVelocity||0).toFixed(2)+' °/с';

 }
}
function motionFaultText(code){return ({kick_start_timeout:'привод не тронулся',feedback_stale:'нет свежих показаний датчика',feedback_invalid:'неверные показания датчика',stalled:'движение прекратилось',planner_error:'ошибка профиля движения'})[code]||code}
function driveFaultMessages(j){
 if(j.deviceRole==='hud'&&!j.hudOnline)return [];
 const messages=[],codes=new Set();
 for(const [i,name] of ['AZ','EL'].entries()){
  if(i===1&&j.model==='G800DXA')continue;
  const a=j.axes?.[i];if(!a)continue;
  if(a.motionFault){messages.push(name+': '+motionFaultText(a.motionFault));codes.add(name.toLowerCase()+'_'+a.motionFault)}
  else if(a.motionReady===false)messages.push(name+': неверные сохранённые параметры движения');
  else if(a.motionPhase==='fault')messages.push(name+': ошибка привода');
 }
 if(j.fault&&!codes.has(j.fault)){
  const axis=/^(az|el)_(.+)$/.exec(j.fault);
  const faults={calibration_storage_invalid:'ошибка сохранённой калибровки',calibration_save_failed:'не удалось сохранить калибровку',target_not_calibrated:'сначала выполните калибровку',sequence_calibration:'потеря калибровки в последовательности',continuous_motion_timeout:'превышено время непрерывного вращения',udp_bind_failed:'не удалось открыть UDP-порт',pwm_attach_failed:'не удалось включить регулировку скорости',motion_storage_failed:'не удалось сохранить параметры движения',crowpanel_no_drive:'привод не подключён',pin_configuration_invalid:'неполная или конфликтующая распиновка GPIO',crowpanel_drive_failed:'ошибка выходов привода CrowPanel',hud_offline:'нет связи с повороткой'};
  messages.push(axis?axis[1].toUpperCase()+': '+motionFaultText(axis[2]):faults[j.fault]||j.fault);
 }
 return messages;
}
function renderDriveStatus(j){
 const errors=driveFaultMessages(j),motion=document.getElementById('motion'),reset=document.getElementById('resetDriveError');
 setText(motion,errors.length?'Ошибка: '+errors.join('\n'):j.motion);motion.classList.toggle('motion-fault',!!errors.length);motion.title=errors.join('\n');
 reset.classList.toggle('hide',!errors.length);
}
initMotionForms();

function buttonTarget(j){return j.pendingValid?j.pending:j.lastTarget}
function targetLabel(t,j){
 if(!t)return '';
 const az=t.hasAz?Number(t.az.toFixed(1))+'°':'',el=t.hasEl&&j.model!=='G800DXA'?'EL '+Number(t.el.toFixed(1))+'°':'';
 return az&&el?'AZ '+az+'\n'+el:az||el;
}
function targetCanStop(j){return j.moving||j.controlMode!=='manual'||j.axes?.some(a=>a.holdArmed||(a.regulatorMode==='cascade'&&a.tracking));}
function updateTargetButton(j){
 const b=document.getElementById('executeTarget'),stop=targetCanStop(j),label=targetLabel(buttonTarget(j),j);
 b.textContent=stop?'stop':label||'Повернуть';
 b.classList.toggle('stop',stop);b.classList.toggle('good',!stop);
 b.disabled=!stop&&!label;
 b.title=stop?'Остановить движение':label?'Повернуть к '+label.replace('\n',', '):'Нет цели для поворота';
 b.setAttribute('aria-label',stop?'stop':b.title);
}
async function targetAction(){
 const j=window._forkState;if(!j)return;
 if(targetCanStop(j)){await st();await upd();return;}
 if(j.pendingValid){await forkPost('/api/execute',{});return;}
 const t=buttonTarget(j);if(!targetLabel(t,j))return;
 const body={};if(t.hasAz)body.az=t.az;if(t.hasEl&&j.model!=='G800DXA')body.el=t.el;
 await forkPost('/api/target',body);
}
const manualSpeedRequests={az:{pending:null,timer:null,busy:false,epoch:0},el:{pending:null,timer:null,busy:false,epoch:0}};
function manualSpeedLabel(axis,value){
 const cascade=window._forkState?.axes?.[axis==='az'?0:1]?.regulatorMode==='cascade';
 return Number(Number(value).toFixed(cascade?3:0))+(cascade?' °/с':'%');
}
function queueManualSpeed(axis,value){
 const p=manualSpeedRequests[axis],input=document.getElementById(axis+'ManualSpeed');if(input.disabled)return;
 p.pending=Number(value);document.getElementById(axis+'ManualSpeedValue').textContent=manualSpeedLabel(axis,value);
 clearTimeout(p.timer);p.timer=setTimeout(()=>flushManualSpeed(axis),100);
}
async function flushManualSpeed(axis){
 const p=manualSpeedRequests[axis];clearTimeout(p.timer);p.timer=null;if(p.busy||p.pending===null)return;
 const value=p.pending,epoch=p.epoch;p.pending=null;p.busy=true;
 try{const ok=await forkPost('/api/speed',{axis,value,rangeRevision:window._forkState?.azRangeRevision});if(!ok&&p.epoch===epoch&&p.pending===null){const current=window._forkState?.axes?.[axis==='az'?0:1]?.speedSetpoint;if(Number.isFinite(current))document.getElementById(axis+'ManualSpeed').value=current;}}finally{p.busy=false;if(p.pending!==null)flushManualSpeed(axis);else renderManualSpeeds(window._forkState);}
}
function renderManualSpeeds(j){
 let visible=false;
 for(const [i,axis] of ['az','el'].entries()){
  const a=j?.axes?.[i],input=document.getElementById(axis+'ManualSpeed'),control=document.getElementById(axis+'ManualSpeedControl'),p=manualSpeedRequests[axis];
  const available=j?.deviceRole!=='hud'&&!!a?.speedAdjustable&&(i===0||j.model!=='G800DXA'),cascade=a?.regulatorMode==='cascade';
  control.classList.toggle('hide',!available);visible|=available;
  const context=[j?.deviceRole,j?.model,j?.hudSelectionEpoch,a?.regulatorMode,available,j?.autotune?.active,j?.configurationLocked].join(':');
  if(p.context!==context){p.context=context;p.pending=null;clearTimeout(p.timer);p.timer=null;++p.epoch;}
  input.disabled=!available||!!j?.autotune?.active||!!j?.configurationLocked;
  if(!available)continue;
  const max=Number(a.speedLimit),value=Number(a.speedSetpoint);if(!Number.isFinite(max)||!Number.isFinite(value))continue;
  input.min=cascade?'0.001':'1';input.max=max;input.step=cascade?'0.001':'1';
  if(!p.busy&&p.pending===null&&document.activeElement!==input)input.value=value;
  document.getElementById(axis+'ManualSpeedValue').textContent=manualSpeedLabel(axis,input.value);
  input.setAttribute('aria-valuetext',manualSpeedLabel(axis,input.value));
 }
 document.getElementById('manualSpeeds').classList.toggle('hide',!visible);
 document.querySelector('.manual-drive').classList.toggle('has-speeds',!!visible);
}
function updateFork(j){
 const first=!window._forkState;window._forkState=j;
 const g800=j.model==='G800DXA';
 document.getElementById('forkSettings-model').options[0].disabled=!!j.build?.singleAxis;
 setText(document.getElementById('modelTitle'),j.deviceRole==='hud'?'HUD / '+(j.build?.board||'ESP32'):'YAESU '+(g800?'G-800DXA':'G-5500')+' / '+(j.build?.board||'ESP32'));
 document.getElementById('followMode').value=j.controlMode;
 setText(document.getElementById('hourLeft'),j.controlMode==='auto_1h'?'Осталось '+Math.ceil(j.remainingMs/60000)+' мин':'');
 setText(document.getElementById('waitingTarget'),j.pendingValid?'Ожидает: '+(j.pending.hasAz?'AZ '+j.pending.az.toFixed(1)+'° ':'')+(j.pending.hasEl&&!g800?'EL '+j.pending.el.toFixed(1)+'°':''):'Нет ожидающей цели');
 updateTargetButton(j);
 renderManualSpeeds(j);
 const buttons=document.getElementById('presetButtons');
 if(!buttons.children.length)for(let slot=1;slot<=4;slot++){const b=document.createElement('button');b.className='btn';b.onclick=()=>forkPost('/api/preset/run',{slot});buttons.append(b)}
 j.presets.forEach((p,i)=>{const b=buttons.children[i];setText(b,p.label);b.disabled=!p.valid;b.classList.toggle('active',j.presetSlot===i&&j.presetActive)});
 const elNodes=[document.getElementById('el').closest('.card'),document.getElementById('tel').closest('.field'),...document.querySelectorAll('.el-setting,.movebtn[data-dir="U"],.movebtn[data-dir="D"]')];
 [...elNodes,...document.querySelectorAll('.el-help')].forEach(n=>{if(n)n.classList.toggle('hide',g800)});
 const independent=['SEPARATE_PWM','BTS7960','SIMULATED'].includes(j.build?.drive);
 document.getElementById('speedSettings').classList.toggle('hide',j.build?.drive!=='YAESU'||!g800);
 document.getElementById('analogSpeedToggle').classList.toggle('hide',independent);
 document.getElementById('speedEnabled-help').classList.toggle('hide',independent);
 document.getElementById('elSpeedSettings').classList.toggle('hide',g800||!independent);
 const btBackend=j.build?.bluetoothBackend||(j.build?.bluetooth===false?'NONE':'HC05');
 const btName=btBackend==='ESP32_SPP'?'Встроенный Bluetooth SPP':btBackend==='HC05'?'HC-05':'Bluetooth отключён';
 const nativeBt=btBackend==='ESP32_SPP';
 document.getElementById('hardwareTrialNotice').classList.toggle('hide',!j.configurationLocked);
 setText(document.getElementById('buildInfo'),j.build?`${j.build.board} · ${j.build.drive} · ${j.build.display} · ${btName}`:'');
 setText(document.getElementById('btlabel'),nativeBt?'Канал SPP':btName);
 setText(document.getElementById('bttransport'),btBackend==='ESP32_SPP'?'G5500-Rotator · SPP / виртуальный COM':btBackend==='HC05'?'HC-05 UART: 9600 baud':'');
 for(const id of ['btlinkrow','btmacrow','bthelp'])document.getElementById(id).classList.toggle('hide',!nativeBt);
 const btLink=!!(j.btLinkConnected??j.btConnected),linkState=document.getElementById('btlinkstate');
 setText(linkState,btLink?'Подключено':'Нет подключения');linkState.style.color=btLink?'#24d18d':'#e5a94f';
 setText(document.getElementById('btmac'),j.btMac||'Недоступен');
 document.getElementById('movementButtons').classList.toggle('az-only',g800);
 const f=document.getElementById('forkSettings');
 for(const key of ['model','rotorName','n1mmPort','pstPort','reportPort','reportHost','n1mmManualFollow','pstManualFollow','gs232ManualFollow','rotctldManualFollow','externalAzMode','startSpeedPercent'])if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements[key].value=j[key];
 for(const axis of ['az','el'])for(const key of [...simpleMotionKeys,'startSpeedPercent']){const name=axis==='az'?key:'el'+key[0].toUpperCase()+key.slice(1),input=document.querySelector(`[name="${name}"]`),owner=input.closest('form');if(first||(!owner.dataset.dirty&&!owner.contains(document.activeElement)))input.value=j[axis==='az'?key:name];if(key.endsWith('Percent'))input.min=1;}
 setText(document.getElementById('elSpeedOutput'),(j.elSpeedOutputPercent||0).toFixed(1)+'%');
 setText(document.getElementById('speedOutput'),j.speedOutputPercent.toFixed(1)+'%');
 if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements.speedEnabled.checked=j.speedEnabled;
 document.getElementById('encoderSettings').classList.toggle('hide',j.build?.display!=='CROWPANEL_21');
 if(first||(!f.dataset.dirty&&!f.contains(document.activeElement)))f.elements.encoderInverted.checked=j.encoderInverted??true;
 document.querySelector('#presetEditor [name=el]').min=j.elStart??0;document.querySelector('#presetEditor [name=el]').max=j.elMax??180;
 document.querySelector('#presetEditor [name=az]').min=j.azStart??0;document.querySelector('#presetEditor [name=az]').max=j.azMax??(j.azMode===36?360:450);
 if(first)fillPreset();
 renderMotionStatus(j);
 renderAutoTune(j);
}

async function q(u){try{if((live.ready||window._forkState?.deviceRole==='hud')&&(u.startsWith('/api/move?')||u.startsWith('/api/target?'))){const url=new URL(u,location.origin);await forkPost(url.pathname,Object.fromEntries(url.searchParams));return ''}return await (await fetch(u,{cache:'no-store'})).text()}catch(e){return''}}
async function mv(d){await q('/api/move?d='+d)}
async function st(){await q('/api/move?d=S')}
async function stopAxis(d,selection){
  const scope=selection===undefined?'':'&selection='+selection;
  if(d==='L'||d==='R') await q('/api/move?d=A'+scope);
  else if(d==='U'||d==='D') await q('/api/move?d=E'+scope);
}
async function go(){
 const body={},g800=window._forkState?.model==='G800DXA';
 for(const [key,id] of g800?[['az','taz']]:[['az','taz'],['el','tel']]){
  const input=document.getElementById(id);
  if(!input.reportValidity())return;
  if(input.value!=='')body[key]=input.value;
 }
 if(!Object.keys(body).length){
  document.getElementById('forkMessage').textContent=g800?'Введите угол AZ':'Введите угол AZ или EL';
  document.getElementById('taz').focus();return;
 }
 await forkPost('/api/target',body);
}
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
  const mn=cm==='2'?'двухточечную':'многоточечную';
  if(!confirm('Сбросить '+mn+' калибровку '+name+' активного профиля?')) return;
  await calibrationAction('/api/calreset?axis='+axis+'&mode='+cm,'Калибровка '+axis.toUpperCase()+' сброшена');
}
function azEndPreview(){const f=document.getElementById('azRangeForm');azRangeEnd.textContent=(Number(f.elements.start.value)+Number(f.elements.span.value)).toFixed(1)+'°'}
document.getElementById('azRangeForm').addEventListener('input',e=>{e.currentTarget.dataset.dirty='1';azEndPreview()});
function calibrationContext(axis){const j=window._forkState;return j?[j.deviceRole,j.model,j[axis+'Profile'],j[axis+'RangeRevision']].join(':'):''}
const calibrationLoads={};
async function loadAxisCalibration(axis){
 if(window._forkState?.deviceRole==='hud'||(axis==='el'&&window._forkState?.model==='G800DXA'))return;
 const context=calibrationContext(axis),token={context};
 if(calibrationLoads[axis]?.context===context){calibrationLoads[axis].refresh=true;return;}
 calibrationLoads[axis]=token;
 const message=document.getElementById(axis+'RangeMessage');
 try{const r=await settingsRequest('/api/'+axis+'cal');if(!r.ok)throw Error('read');const j=r.json();
 if(context!==calibrationContext(axis)||calibrationLoads[axis]!==token)return;
 window['_'+axis+'Cal']=j;window['_'+axis+'CalContext']=context;
 const f=document.getElementById(axis+'RangeForm');if(!f.dataset.dirty&&!f.contains(document.activeElement)){f.elements.start.value=j.customStart;f.elements.span.value=j.customSpan;(axis==='az'?azEndPreview:elEndPreview)()}
 renderAxisCalibration(axis,window._forkState);
 if(j.storageFault)message.textContent='Ошибка сохранённой калибровки. Сбросьте калибровку и настройте её заново.';
 }catch(e){message.textContent='Не удалось прочитать калибровку';}
 finally{if(calibrationLoads[axis]===token){delete calibrationLoads[axis];if(token.refresh)loadAxisCalibration(axis);}}
}
async function loadAzCalibration(){return loadAxisCalibration('az')}
function renderCalibrationPoints(axis,mode,points,custom,storageFault){
 const prefix=axis+(mode==='2'?'2':''),list=document.getElementById(prefix+'PointList');
 const key=JSON.stringify([points,custom,storageFault]);if(list.dataset.points===key)return;
 list.dataset.points=key;list.replaceChildren();
 const captured=points.filter(p=>p.mv>=0).length;
 const differences=points.slice(1).map((p,i)=>p.mv-points[i].mv);
 const valid=captured===points.length&&(mode==='2'?Math.abs(differences[0])>100:differences.every(v=>v>0)||differences.every(v=>v<0));
 document.getElementById(prefix+'CalProgress').textContent='Записано '+captured+' из '+points.length+'. '+(storageFault?'Ошибка сохранённой калибровки.':captured<points.length?'Запомните оставшиеся положения.':valid?'Калибровка готова.':mode==='2'?'Разница напряжений должна превышать 100 мВ.':'Напряжения должны последовательно расти или убывать.');
 for(const [i,p] of points.entries()){
  const row=document.createElement('div');row.className='point-row';row.dataset.angle=p.angle;
  const label=document.createElement('span');label.textContent=(i===0?'Начало':i===points.length-1?'Конец':'Точка '+(i+1))+' · '+p.angle+'° · '+(p.mv<0?'не записано':p.mv+' мВ');row.append(label);
  const capture=document.createElement('button');capture.className='btn '+(p.mv>=0?'good':'warn');capture.textContent='Запомнить положение';
  if(mode==='2'){const command=(i===0?'o':'f')+(axis==='el'?'2':'');capture.id='b2_'+axis+(i===0?'o':'f');capture.onclick=()=>cal2(command)}
  else capture.onclick=()=>custom?axisCalibrationPost(axis,'/api/'+axis+'cal',{op:'capture',angle:p.angle}):cal(axis+p.angle);
  const actions=document.createElement('div');actions.className='point-actions';actions.append(capture);row.append(actions);
  if(mode==='multi'&&custom&&!p.endpoint){const del=document.createElement('button');del.className='btn';del.textContent='Удалить';del.onclick=()=>axisCalibrationPost(axis,'/api/'+axis+'cal',{op:'delete',angle:p.angle});actions.append(del)}
  list.append(row);
 }
}
function renderAxisCalibration(axis,j){
 if(!j||j.deviceRole==='hud'||(axis==='el'&&j.model==='G800DXA'))return;
 const cal=window['_'+axis+'Cal'],start=j[axis+'Start'],end=j[axis+'Max'];
 renderCalibrationPoints(axis,'2',[{angle:start,mv:j[axis+'2p0']??-1},{angle:end,mv:j[axis+'2pf']??-1}],false,cal?.storageFault);
 if(cal&&window['_'+axis+'CalContext']===calibrationContext(axis)&&cal.profile===j[axis+'Profile']&&cal.start===start&&cal.end===end){
  const custom=cal.profile==='custom';
  const points=custom?cal.points:cal.points.map(p=>({...p,mv:j[axis+'p'+p.angle]??p.mv}));
  renderCalibrationPoints(axis,'multi',points,custom,cal.storageFault);
 }else{const list=document.getElementById(axis+'PointList');list.replaceChildren();delete list.dataset.points;document.getElementById(axis+'CalProgress').textContent='Загрузка точек…';}
}
async function axisCalibrationPost(axis,path,body,notice=null){
 const label=axis.toUpperCase();notice=notice||beginSettingsAction('Сохранение '+label+'…',path+JSON.stringify(body));if(!notice)return false;
 notice.update('Сохранение '+label+'…');
 const message=document.getElementById(axis+'RangeMessage');
 try{const r=await settingsRequest(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body)});const j=r.json();
 if(!r.ok||j.ok===false){const error=j.error==='invalid_point'?'Проверьте угол: точка должна быть внутри диапазона, без повторов; максимум 16 точек.':j.error==='invalid_range'?'Проверьте начало и полный ход.':'Не удалось сохранить настройки '+label;setText(message,error);notice.finish(false,error);return false;}
 const text=body.op?(body.op==='add_capture'?'Точка '+label+' '+body.angle+'° добавлена, текущее показание ADC запомнено':body.op==='capture'?'Положение '+label+' '+body.angle+'° запомнено':body.op==='add'?'Точка '+label+' '+body.angle+'° добавлена':'Точка '+label+' '+body.angle+'° удалена'):'Диапазон '+label+' сохранён';
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
async function addAzPoint(e){e.preventDefault();if(await azPost('/api/azcal',{op:'add_capture',angle:azPointAngle.value}))azPointAngle.value=''}
function elEndPreview(){const f=document.getElementById('elRangeForm');elRangeEnd.textContent=(Number(f.elements.start.value)+Number(f.elements.span.value)).toFixed(1)+'°'}
document.getElementById('elRangeForm').addEventListener('input',e=>{e.currentTarget.dataset.dirty='1';elEndPreview()});
async function loadElCalibration(){return loadAxisCalibration('el')}
async function elPost(path,body){return axisCalibrationPost('el',path,body)}
async function saveElRange(e){e.preventDefault();const f=e.target;await elPost('/api/elrange',{profile:'custom',start:f.elements.start.value,span:f.elements.span.value})}
async function useCustomElRange(){return useCustomAxisRange('el')}
async function addElPoint(e){e.preventDefault();if(await elPost('/api/elcal',{op:'add_capture',angle:elPointAngle.value}))elPointAngle.value=''}
let wifiSetup=null;
function updateWifiSetup(j){
 const link=document.getElementById('wifiClientLink'),sta=networkAddresses(j).find(ip=>ip.startsWith('STA: '));
 link.hidden=!sta;
 if(sta){link.href='http://'+sta.slice(5)+'/';link.textContent='Открыть устройство: '+link.href;}
 if(!wifiSetup)return;
 const message=document.getElementById('wifiMessage');
 if(!j.wifiConnecting&&sta&&j.connectedSsid===wifiSetup){
  const text='Подключено к «'+wifiSetup+'». Полученный IP: '+sta.slice(5)+'.'+(j.apIp&&j.apIp!=='-'?' Точка доступа: '+j.apIp+'.':'');
  setText(message,text);settingsFeedback(text,'success');wifiSetup=null;
 }else if(j.ssid===wifiSetup&&j.wifiConnecting===false&&!sta){
  setText(message,'Подключиться к «'+wifiSetup+'» пока не удалось. Проверьте SSID и пароль. Повторная попытка выполняется автоматически; точка доступа остаётся доступной.');
 }
}
async function wifi(){
 const message=document.getElementById('wifiMessage');
 const network=document.getElementById('ssid').value,password=document.getElementById('pass').value;
 if(!network){setText(message,'Укажите SSID сети Wi-Fi.');settingsFeedback(message.textContent);return;}
 if(!confirm('Сохранить настройки и подключиться к Wi-Fi?'))return;
 const notice=beginSettingsAction('Сохранение настроек Wi-Fi…','wifi');if(!notice)return;
 setText(message,'Сохранение настроек Wi-Fi…');
 try{
  const response=await settingsRequest('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:network,pass:password})});
  const detail=response.text;
  const text=response.ok?'Настройки Wi-Fi сохранены. Подключаемся к «'+network+'»… Полученный IP появится здесь.':'Не удалось сохранить Wi-Fi. '+(detail||'Проверьте SSID и пароль.');
  setText(message,text);notice.finish(response.ok,text);
  if(response.ok){document.getElementById('pass').value='';wifiSetup=network;void upd(true);}
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
if(otaBusy)return;
if(live.ready&&!force)return;
if(statusPending){if(force)statusRefreshQueued=true;return;}
statusPending=true;
const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),5000);
try{const response=await fetch('/api/status',{cache:'no-store',signal:controller.signal});if(!response.ok)throw new Error('status unavailable');const j=await response.json();if(!j||typeof j!=='object'||!['controller','hud'].includes(j.deviceRole))throw new Error('invalid status');
connection.httpOnline=true;connection.httpLast=Date.now();connection.state=live.ready&&live.state?{...j,...live.state}:j;updateConnection();
httpState=j;connectLive(j.livePort);queueLiveRender();
if(document.querySelector('#calibrationSettings details').open){loadAzCalibration();loadElCalibration();}
}catch(e){
 connection.httpOnline=false;updateConnection();
 const wifi=document.getElementById('statusWifi'),addresses=networkAddresses(httpState);setText(wifi,['Нет связи',...(addresses.length?['Последние адреса:',...addresses]:[])].join('\n'));wifi.className='offline';wifi.title='Не удалось обновить статус устройства';
 for(const id of ['statusBt','statusSignal','statusMemory','statusUptime']){const node=document.getElementById(id);setText(node,'—');node.className='';}
}finally{clearTimeout(timeout);statusPending=false;if(statusRefreshQueued){statusRefreshQueued=false;queueMicrotask(()=>upd(true));}}}
function currentPosition(j,axis){
 if(j.deviceRole==='hud'&&!j.hudOnline)return '—';
 if(j.axes?.[axis==='az'?0:1]?.calibrated===false)return 'NC';
 return Number.isFinite(j[axis])?Number(j[axis]).toFixed(1):'—';
}
function renderStatus(j){
setText(az,currentPosition(j,'az'));setText(el,currentPosition(j,'el'));
for(const axis of ['az','el'])document.getElementById(axis).title=currentPosition(j,axis)==='NC'?'NC — not calibrated: ось не откалибрована':'';
updateFork(j);
setText(azt,j.targetValid?Number(j.targetAz).toFixed(1):'---');
setText(elt,j.targetValid?Number(j.targetEl).toFixed(1):'---');
for(const axis of ['az','el'])document.querySelectorAll('[data-cal-position='+axis+']').forEach(n=>{const value=currentPosition(j,axis);setText(n,value);n.nextElementSibling.hidden=!Number.isFinite(Number(value));n.title=value==='NC'?'NC — not calibrated: ось не откалибрована':'';});
const azmn=j.azStart??0,azmx=j.azMax??(j.azMode===36?360:450);azbar.style.width=currentPosition(j,'az')==='NC'?'0%':pct(j.az-azmn,azmx-azmn);setText(azminlabel,azmn+'°');setText(azmaxlabel,azmx+'°');taz.min=azmn;taz.max=azmx;setText(tazlbl,'AZ '+azmn+'…'+azmx+'°');const elmn=j.elStart??0,elmx=j.elMax??180;elbar.style.width=currentPosition(j,'el')==='NC'?'0%':pct(j.el-elmn,elmx-elmn);setText(elminlabel,elmn+'°');setText(elmaxlabel,elmx+'°');tel.min=elmn;tel.max=elmx;setText(tellbl,'EL '+elmn+'…'+elmx+'°');
renderDriveStatus(j);
window._azMode=j.azMode??45;const rangeName=j.azMode===0?'Произвольный':'P'+j.azMode;
const selok=!!j.selectedCalOk;
setText(caltxt,(j.calMode==='multi'?'MULTI-POINT':'GS-232 2-POINT')+' · '+rangeName+(selok?' OK':' NOT CALIBRATED'));
caldot.className='dot '+(selok?'on':'');
setText(calmode,j.calMode==='multi'?'Несколько точек':'Две точки');
mode2.classList.toggle('good',j.calMode==='2');modem.classList.toggle('good',j.calMode==='multi');
azm36.classList.toggle('good',j.azMode===36);azm45.classList.toggle('good',j.azMode===45);
setText(azrange,rangeName+' · '+azmn+'…'+azmx+'°');azmc.classList.toggle('good',j.azMode===0);
customAzPoints.classList.toggle('hide',j.azMode!==0);azPointAngle.min=azmn;azPointAngle.max=azmx;
const elCustom=j.elProfile==='custom';setText(elrange,(elCustom?'Произвольный':'Стандартный')+' · '+elmn+'…'+elmx+'°');elmstd.classList.toggle('good',!elCustom);elmc.classList.toggle('good',elCustom);
customElPoints.classList.toggle('hide',!elCustom);elPointAngle.min=elmn;elPointAngle.max=elmx;
for(const axis of ['az','el']){const key=calibrationContext(axis);if(window['_'+axis+'CalKey']!==key){window['_'+axis+'CalKey']=key;loadAxisCalibration(axis)}renderAxisCalibration(axis,j)}
panel2.classList.toggle('hide',j.calMode!=='2');panelm.classList.toggle('hide',j.calMode!=='multi');multiCalHint.classList.toggle('hide',j.calMode!=='multi');
setText(ip,j.staIp??(Number.isFinite(j.rssi)?j.ip:'-'));setText(document.getElementById('apip'),j.apIp||'-');setText(mode,j.wifiMode);
setText(rssi,(j.rssi===null?'-':j.rssi+' dBm'));
setText(sig,j.signal||'-');
setText(btstate,j.build?.bluetooth===false?'Выключен в настройках':j.btConnected?'CONNECTED':'DISCONNECTED');
btstate.style.color=j.btConnected?'#24d18d':'#e5a94f';
if(document.activeElement!==ssid && !ssid.dataset.dirty) ssid.value=j.ssid||'';
updateHud(j);
compactStatus(j);
updateWifiSetup(j);
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
async function loadRssi(){if(otaBusy||document.getElementById('settingsPanel').hidden)return;try{let j=await (await fetch('/api/rssi?range='+rrange,{cache:'no-store'})).json();drawRssi(j)}catch(e){}}
document.querySelectorAll('.rbtn').forEach(b=>b.addEventListener('click',()=>{rrange=b.dataset.r;document.querySelectorAll('.rbtn').forEach(q=>q.classList.remove('active'));b.classList.add('active');loadRssi();}));
window.addEventListener('hashchange',()=>selectTab(location.hash.slice(1),false));
document.querySelectorAll('[role=tab]').forEach(tab=>tab.addEventListener('keydown',event=>{
 if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
 event.preventDefault();const index=pageTabs.indexOf(tab.id.slice(0,-3));
 const next=event.key==='Home'?pageTabs[0]:event.key==='End'?pageTabs[pageTabs.length-1]:pageTabs[(index+(event.key==='ArrowRight'?1:-1)+pageTabs.length)%pageTabs.length];
 selectTab(next);document.getElementById(next+'Tab').focus();
}));
selectTab(location.hash.slice(1),false);
setInterval(loadRssi,30000);
setInterval(()=>upd(!!wifiSetup),1000);setInterval(()=>upd(true),15000);upd();

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
 const position=axis=>peer[axis+'Calibrated']===false?'NC':Number.isFinite(peer[axis])?Number(peer[axis]).toFixed(1)+'°':'—';
 select.textContent=peer.name+' · '+(peer.online?'AZ '+position('az')+(peer.hasEl===false?'':' / EL '+position('el'))+(peer.moving?' · вращение':''):'нет связи')+' · '+age;
 select.onclick=async()=>{await forkPost(discovered?'/api/hud/peers':'/api/hud/select',{id:peer.id});await loadHudPeers();if(discovered)await loadHudDiscovery()};row.append(select);
 if(!discovered){
  const rename=document.createElement('button');rename.className='btn';rename.textContent='Имя';rename.onclick=async()=>{const name=prompt('Имя поворотки',peer.name);if(name!==null){await forkPost('/api/hud/peers',{id:peer.id,name});await loadHudPeers()}};row.append(rename);
  const remove=document.createElement('button');remove.className='btn';remove.textContent='Удалить';remove.onclick=async()=>{await forkPost('/api/hud/peers',{id:peer.id,action:'remove'});await loadHudPeers()};row.append(remove);
 }else{const note=document.createElement('span');note.className='device-peer-note';note.textContent='Нажмите, чтобы сохранить';row.append(note)}
 return row;
}
async function loadHudPeers(){
 if(otaBusy||hudPollBusy)return;hudPollBusy=true;
 try{const data=await (await fetch('/api/hud/peers?offset='+hudOffset,{cache:'no-store'})).json();hudTotal=data.total;
 const box=document.getElementById('hudPeers');box.replaceChildren(...data.items.map(p=>hudRow(p,false)));
 document.getElementById('hudPageInfo').textContent=hudTotal?`${hudOffset+1}–${Math.min(hudOffset+8,hudTotal)} / ${hudTotal}`:'Добавьте поворотку';
 document.getElementById('hudPrev').disabled=hudOffset===0;document.getElementById('hudNext').disabled=hudOffset+8>=hudTotal;
 }catch(e){}finally{hudPollBusy=false;hudLastPoll=Date.now()}
}
async function loadHudDiscovery(){if(otaBusy)return;try{const data=await (await fetch('/api/hud/peers?discovered=1&offset='+hudDiscoveredOffset,{cache:'no-store'})).json();hudDiscoveryTotal=data.total;document.getElementById('hudDiscovered').replaceChildren(...data.items.map(p=>hudRow(p,true)));document.getElementById('hudDiscoveryInfo').textContent='Найдено: '+data.total}catch(e){}}
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
 for(const name of ['model'])document.querySelector('#forkSettings [name="'+name+'"]').closest('.field').classList.toggle('hide',hud);
 document.querySelector('a[href="/adc"]').classList.toggle('hide',hud);
 for(const b of document.querySelectorAll('.movebtn,#presetButtons button'))if(hud&&!j.hudOnline)b.disabled=true;else if(b.classList.contains('movebtn'))b.disabled=false;
 document.getElementById('followMode').disabled=hud&&!j.hudOnline;
 if(hud){document.getElementById('caltxt').textContent=j.hudOnline?(j.selectedCalOk?'Поворотка откалибрована':'Нужна калибровка на контроллере'):'Нет актуальных данных';if(!j.hudOnline){document.getElementById('motion').textContent='Нет связи';document.getElementById('azt').textContent='—';document.getElementById('elt').textContent='—';}if(Date.now()-hudLastPoll>1000)loadHudPeers();}
}

</script></body></html>
)HTML";
