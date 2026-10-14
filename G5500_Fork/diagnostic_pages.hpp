// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#ifndef ARDUINO
#define PROGMEM
#endif
static const char ADC_TEST_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>G5500 ADC TEST v1.6.23</title>
<style>
:root{--bg:#081018;--p:#111d28;--t:#f4f7fa;--m:#96a9b7;--a:#24d18d;--b:#2b4557;--az:#33b8ff;--el:#ffb347}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(#081018,#0b1721);color:var(--t);font-family:Arial,sans-serif}
.wrap{max-width:980px;margin:auto;padding:16px}.card{background:var(--p);border:1px solid var(--b);border-radius:16px;padding:16px;margin-top:14px}
h1{margin:.25em 0}.muted{color:var(--m)}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.kv{display:grid;grid-template-columns:1fr 1fr;gap:8px}.kv div{background:#0b1720;padding:10px;border-radius:9px}
.val{font-size:25px;font-weight:700}.az{color:var(--az)}.el{color:var(--el)}
.btn{border:0;border-radius:12px;min-height:42px;padding:8px 14px;background:#235b78;color:white;font-size:15px;font-weight:700;cursor:pointer}
.good{background:#167a5c}.warn{background:#81541d}
canvas{width:100%;height:360px;background:#071018;border-radius:10px;margin-top:10px}
a{color:#24d18d;text-decoration:none;font-weight:700}
@media(max-width:700px){.grid{grid-template-columns:1fr}.kv{grid-template-columns:1fr}}
.nav{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 14px;padding:8px;background:#0b1720;border:1px solid #294153;border-radius:12px;position:sticky;top:0;z-index:20}.nav a{display:inline-block;padding:9px 12px;border-radius:9px;background:#173044;color:#fff;text-decoration:none;font-weight:700;font-size:14px}.nav a.active{background:#167a5c}.nav a:hover{background:#235b78}\n</style></head><body><div class="wrap">
<div class="muted" style="font-size:13px">G5500 Controller v1.6.23 &copy; UA1CFM</div>
<h1>ADC TEST — ESP32-S3</h1>
<nav class="nav"><a href="/">УПРАВЛЕНИЕ</a><a class="active" href="/adc">ADC TEST</a><a href="/resetdiag">RESET DIAG</a><a href="/diaglog">DIAG CONSOLE</a></nav>

<div class="card">
<div><b>Назначение:</b> диагностический замер ADC. Он не меняет калибровку AZ<span id="adcElHelp">/EL</span> и не управляет реле.</div>
<div class="muted" style="margin-top:7px">RAW — обычный 12-битный analogRead(). CAL mV — калиброванное значение analogReadMilliVolts(). LINEAR mV — условная идеальная прямая 0…3100 mV для ADC_11db. Δ показывает отличие заводской калиброванной шкалы от этой прямой, а не абсолютную погрешность без внешнего эталонного вольтметра.</div>
<div style="margin-top:10px"><button id="collectBtn" class="btn good" onclick="toggleCollect()">AUTO COLLECT: ON</button> <button class="btn warn" onclick="clearData()">CLEAR GRAPH</button></div>
</div>

<div class="grid">
<div class="card"><div class="az"><b>AZ — GPIO4</b></div><div class="kv" style="margin-top:10px">
<div>RAW ADC<br><span id="azraw" class="val az">-</span></div>
<div>CAL mV<br><span id="azmv" class="val az">-</span></div>
<div>LINEAR mV<br><span id="azlin" class="val">-</span></div>
<div>Δ mV<br><span id="azdmv" class="val">-</span></div>
<div>Δ %<br><span id="azpct" class="val">-</span></div>
<div>Samples<br><span id="azn" class="val">0</span></div>
</div></div>
<div id="adcElCard" class="card"><div class="el"><b>EL — GPIO5</b></div><div class="kv" style="margin-top:10px">
<div>RAW ADC<br><span id="elraw" class="val el">-</span></div>
<div>CAL mV<br><span id="elmv" class="val el">-</span></div>
<div>LINEAR mV<br><span id="ellin" class="val">-</span></div>
<div>Δ mV<br><span id="eldmv" class="val">-</span></div>
<div>Δ %<br><span id="elpct" class="val">-</span></div>
<div>Samples<br><span id="eln" class="val">0</span></div>
</div></div>
</div>

<div class="card">
<div><b>График RAW ADC → напряжение</b></div>
<div class="muted">Синяя линия/точки — AZ, <span id="adcElLegend">оранжевая — EL, </span>серая — условная линейная шкала 0…3100 mV. Для заполнения всей кривой медленно проведите соответствующую ось по диапазону вручную; страница автоматически набирает точки.</div>
<canvas id="chart" width="920" height="360"></canvas>
</div>
</div>
<script>
let collect=true, azPts=[], elPts=[], MAXPTS=900, hasEl=true;
function toggleCollect(){collect=!collect;collectBtn.textContent='AUTO COLLECT: '+(collect?'ON':'OFF');collectBtn.className='btn '+(collect?'good':'warn')}
function clearData(){azPts=[];elPts=[];draw();azn.textContent='0';eln.textContent='0'}
function addPoint(a,raw,mv){
  if(!collect)return;
  let p={x:Number(raw),y:Number(mv)};
  if(a.length){
    let q=a[a.length-1];
    if(Math.abs(q.x-p.x)<2 && Math.abs(q.y-p.y)<2)return;
  }
  a.push(p);
  if(a.length>MAXPTS)a.shift();
}
function fmt(v,n=0){return Number(v).toFixed(n)}
function updateAxis(prefix,raw,mv,arr){
  let lin=raw*3100/4095, d=mv-lin, pct=lin>50?100*d/lin:0;
  document.getElementById(prefix+'raw').textContent=raw;
  document.getElementById(prefix+'mv').textContent=mv;
  document.getElementById(prefix+'lin').textContent=fmt(lin,1);
  document.getElementById(prefix+'dmv').textContent=(d>=0?'+':'')+fmt(d,1);
  document.getElementById(prefix+'pct').textContent=(pct>=0?'+':'')+fmt(pct,2)+'%';
  document.getElementById(prefix+'n').textContent=arr.length;
}
function draw(){
  const c=chart,x=c.getContext('2d'),w=c.width,h=c.height,left=58,right=w-18,top=18,bottom=h-38;
  x.clearRect(0,0,w,h);x.fillStyle='#071018';x.fillRect(0,0,w,h);
  x.strokeStyle='#294153';x.fillStyle='#96a9b7';x.font='12px Arial';x.lineWidth=1;
  for(let r=0;r<=4095;r+=512){let px=left+r/4095*(right-left);x.beginPath();x.moveTo(px,top);x.lineTo(px,bottom);x.stroke();if(r<4095)x.fillText(r,px-12,bottom+18)}
  for(let mv=0;mv<=3100;mv+=500){let py=bottom-mv/3100*(bottom-top);x.beginPath();x.moveTo(left,py);x.lineTo(right,py);x.stroke();x.fillText(mv+'mV',3,py+4)}
  x.fillText('4095',right-28,bottom+18);
  // Linear reference
  x.strokeStyle='#788a96';x.setLineDash([6,6]);x.beginPath();x.moveTo(left,bottom);x.lineTo(right,top);x.stroke();x.setLineDash([]);
  function series(a,color){
    if(!a.length)return;
    let s=a.slice().sort((p,q)=>p.x-q.x);
    x.strokeStyle=color;x.fillStyle=color;x.lineWidth=2;x.beginPath();
    s.forEach((p,i)=>{let px=left+p.x/4095*(right-left),py=bottom-Math.max(0,Math.min(3100,p.y))/3100*(bottom-top);if(i)x.lineTo(px,py);else x.moveTo(px,py)});
    x.stroke();
    for(let i=0;i<s.length;i+=Math.max(1,Math.floor(s.length/180))){let p=s[i],px=left+p.x/4095*(right-left),py=bottom-Math.max(0,Math.min(3100,p.y))/3100*(bottom-top);x.fillRect(px-1.5,py-1.5,3,3)}
  }
  series(azPts,'#33b8ff');if(hasEl)series(elPts,'#ffb347');
  x.fillStyle='#33b8ff';x.fillText('AZ',right-95,top+14);x.fillStyle='#ffb347';if(hasEl)x.fillText('EL',right-60,top+14);x.fillStyle='#96a9b7';x.fillText('RAW ADC',right-80,bottom+32);
}
async function upd(){
  try{
    let j=await (await fetch('/api/adcdiag',{cache:'no-store'})).json();
    hasEl=j.hasEl!==false;
    for(const id of ['adcElCard','adcElHelp','adcElLegend'])document.getElementById(id).style.display=hasEl?'':'none';
    document.querySelector('.grid').style.gridTemplateColumns=hasEl?'':'1fr';
    addPoint(azPts,j.azRaw,j.azMv); if(hasEl)addPoint(elPts,j.elRaw,j.elMv);
    updateAxis('az',j.azRaw,j.azMv,azPts); if(hasEl)updateAxis('el',j.elRaw,j.elMv,elPts);
    draw();
  }catch(e){}
}
setInterval(upd,350);upd();draw();
</script></body></html>
)HTML";

static const char RESET_DIAG_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>G5500 RESET DIAG v1.6.23</title>
<style>
:root{--bg:#081018;--p:#111d28;--t:#f4f7fa;--m:#96a9b7;--a:#24d18d;--b:#2b4557;--w:#e5a94f;--red:#d9534f}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(#081018,#0b1721);color:var(--t);font-family:Arial,sans-serif}
.wrap{max-width:920px;margin:auto;padding:16px}.card{background:var(--p);border:1px solid var(--b);border-radius:16px;padding:16px;margin-top:14px}
h1{margin:.25em 0}.muted{color:var(--m)}a{color:var(--a);text-decoration:none;font-weight:700}
.reason{font-size:38px;font-weight:800;color:var(--a);margin-top:8px}
.code{font-size:20px;font-weight:700}.kv{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}
.kv div{background:#0b1720;padding:10px;border-radius:9px}
.note{line-height:1.45}.warn{color:var(--w)}
table{width:100%;border-collapse:collapse;margin-top:10px}
th,td{padding:9px 7px;border-bottom:1px solid #294153;text-align:left;font-size:14px}
th{color:var(--m);font-weight:700}
.btn{border:0;border-radius:9px;padding:10px 14px;font-weight:700;cursor:pointer}
.danger{background:#8f2e2b;color:#fff}
@media(max-width:650px){.kv{grid-template-columns:1fr}.reason{font-size:32px}th,td{font-size:12px;padding:7px 4px}}
.nav{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 14px;padding:8px;background:#0b1720;border:1px solid #294153;border-radius:12px;position:sticky;top:0;z-index:20}.nav a{display:inline-block;padding:9px 12px;border-radius:9px;background:#173044;color:#fff;text-decoration:none;font-weight:700;font-size:14px}.nav a.active{background:#167a5c}.nav a:hover{background:#235b78}\n</style></head><body><div class="wrap">
<div class="muted" style="font-size:13px">G5500 Controller v1.6.23 &copy; UA1CFM</div>
<h1>RESET DIAG</h1>
<nav class="nav"><a href="/">УПРАВЛЕНИЕ</a><a href="/adc">ADC TEST</a><a class="active" href="/resetdiag">RESET DIAG</a><a href="/diaglog">DIAG CONSOLE</a></nav>

<div class="card">
<div class="muted">Причина последней загрузки ESP32-S3</div>
<div id="reason" class="reason">--</div>
<div class="kv">
<div>Reset code<br><span id="code" class="code">--</span></div>
<div>Обновление<br><span id="stamp" class="code">--</span></div>
</div>
</div>

<div class="card">
<div style="display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap">
  <div>
    <b>История перезагрузок</b>
    <div class="muted">Последние 20 запусков. Время хранится как UTC и показывается браузером в вашем местном часовом поясе.</div>
  </div>
  <button class="btn danger" onclick="clearHist()">ОЧИСТИТЬ ИСТОРИЮ</button>
</div>
<div style="overflow-x:auto">
<table>
<thead><tr><th>#</th><th>Дата и время</th><th>Причина</th><th>Код</th></tr></thead>
<tbody id="hist"><tr><td colspan="4" class="muted">Загрузка...</td></tr></tbody>
</table>
</div>
<div id="timeNote" class="muted" style="margin-top:8px"></div>
</div>

<div class="card note">
<b>Расшифровка:</b><br><br>
<b>POWERON</b> — обычное включение питания.<br>
<b>BROWNOUT</b> — просадка питания ESP32.<br>
<b>EXTERNAL</b> — внешний аппаратный reset.<br>
<b>SOFTWARE</b> — программный перезапуск.<br>
<b>PANIC</b> — аварийное исключение программы.<br>
<b>INT_WDT / TASK_WDT / WDT</b> — сработал watchdog.<br>
<b>DEEPSLEEP</b> — выход из deep sleep.<br><br>
<span class="warn">Если домашний Wi‑Fi недоступен при загрузке, NTP-время получить нельзя — такая запись будет показана как «Время не синхронизировано».</span>
</div>
</div>
<script>
async function upd(){
  try{
    let j=await (await fetch('/api/resetreason',{cache:'no-store'})).json();
    reason.textContent=j.reason||'UNKNOWN';
    code.textContent=j.code;
    stamp.textContent=new Date().toLocaleTimeString();
  }catch(e){reason.textContent='NO CONNECTION'}
}
async function histUpd(){
  try{
    let j=await (await fetch('/api/resethistory',{cache:'no-store'})).json();
    let h='';
    let unsynced=0;
    (j.entries||[]).forEach((e,i)=>{
      let when='Время не синхронизировано';
      if(Number(e.epoch)>0) when=new Date(Number(e.epoch)*1000).toLocaleString();
      else unsynced++;
      h+='<tr><td>'+(i+1)+'</td><td>'+when+'</td><td><b>'+e.reason+'</b></td><td>'+e.code+'</td></tr>';
    });
    hist.innerHTML=h||'<tr><td colspan="4" class="muted">История пуста</td></tr>';
    timeNote.textContent=unsynced?('Без точного времени: '+unsynced+' записей'):'';
  }catch(e){hist.innerHTML='<tr><td colspan="4">Ошибка загрузки истории</td></tr>'}
}
async function clearHist(){
  if(!confirm('Очистить всю историю перезагрузок?')) return;
  await fetch('/api/resethistory/clear',{cache:'no-store'});
  histUpd();
}
setInterval(upd,1000);upd();histUpd();
</script></body></html>
)HTML";

static const char DIAG_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>G5500 Diagnostic Console v1.6.23</title>
<style>
:root{--bg:#071018;--border:#294153;--text:#d8f7df;--muted:#91a5b2}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:white;font-family:Arial,sans-serif}
.wrap{max-width:1200px;margin:auto;padding:14px}
.top{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:10px}
h2{margin:0 14px 0 0;font-size:21px}
button,a.btn{border:1px solid var(--border);background:#173044;color:white;border-radius:9px;padding:9px 13px;font-weight:700;text-decoration:none;cursor:pointer}
button.good{background:#126b50}
button.danger{background:#a93d3d}
label{color:var(--muted);font-size:14px;display:flex;gap:7px;align-items:center}
#state{color:var(--muted);margin-left:auto;font-size:13px}
#term{height:calc(100vh - 92px);min-height:420px;overflow:auto;white-space:pre-wrap;word-break:break-word;background:#020608;color:var(--text);border:1px solid var(--border);border-radius:10px;padding:12px;font:14px/1.42 Consolas,"Courier New",monospace}
@media(max-width:700px){#state{width:100%;margin-left:0}#term{height:calc(100vh - 140px);font-size:12px}}
.nav{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 14px;padding:8px;background:#0b1720;border:1px solid #294153;border-radius:12px;position:sticky;top:0;z-index:20}.nav a{display:inline-block;padding:9px 12px;border-radius:9px;background:#173044;color:#fff;text-decoration:none;font-weight:700;font-size:14px}.nav a.active{background:#167a5c}.nav a:hover{background:#235b78}\n</style></head><body><div class="wrap">
<div class="top">
<h2>G5500 Web Diagnostic Console</h2>
<a class="btn" href="/">УПРАВЛЕНИЕ</a>
<a class="btn" href="/adc">ADC TEST</a>
<a class="btn" href="/resetdiag">RESET DIAG</a>
<a class="btn" href="/diaglog" style="background:#167a5c">DIAG CONSOLE</a>
<button class="good" onclick="refreshNow()">Refresh</button>
<button class="danger" onclick="clearLog()">Clear RAM log</button>
<label><input id="auto" type="checkbox" checked> Auto scroll</label>
<label><input id="pause" type="checkbox"> Pause</label>
<span id="state">connecting...</span>
</div>
<pre id="term">Loading...</pre>
</div>
<script>
let last='';
let prevRawLines=[];
let stampedLines=[];

function pcStamp(){
  const d=new Date();
  const p=n=>String(n).padStart(2,'0');
  const ms=String(d.getMilliseconds()).padStart(3,'0');
  return '['+p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds())+'.'+ms+']';
}

function stampSnapshot(raw){
  if(!raw)return '';

  let lines=raw.replace(/\r/g,'').split('\n');
  if(lines.length && lines[lines.length-1]==='')lines.pop();

  // Preserve time labels for lines still present after the ESP32 RAM ring-buffer shifts.
  let overlap=0;
  const max=Math.min(prevRawLines.length,lines.length);
  for(let k=max;k>=0;k--){
    let ok=true;
    for(let i=0;i<k;i++){
      if(prevRawLines[prevRawLines.length-k+i]!==lines[i]){ok=false;break;}
    }
    if(ok){overlap=k;break;}
  }

  const nextStamped=[];
  for(let i=0;i<overlap;i++){
    nextStamped.push(stampedLines[stampedLines.length-overlap+i]);
  }
  for(let i=overlap;i<lines.length;i++){
    nextStamped.push(pcStamp()+' | '+lines[i]);
  }

  prevRawLines=lines;
  stampedLines=nextStamped;
  return nextStamped.join('\n');
}

async function loadLog(){
  if(pause.checked)return;
  try{
    const r=await fetch('/api/diaglog',{cache:'no-store'});
    if(!r.ok)throw new Error('HTTP '+r.status);
    const t=await r.text();
    if(t!==last){
      const nearBottom=(term.scrollHeight-term.scrollTop-term.clientHeight)<80;
      term.textContent=t ? stampSnapshot(t) : '(RAM diagnostic log is empty)';
      if(auto.checked && nearBottom)term.scrollTop=term.scrollHeight;
      last=t;
    }
    state.textContent='LIVE • PC '+new Date().toLocaleTimeString();
  }catch(e){
    state.textContent='ERROR: '+e.message;
  }
}
async function refreshNow(){await loadLog();if(auto.checked)term.scrollTop=term.scrollHeight}
async function clearLog(){
  if(!confirm('Очистить только диагностический RAM-буфер?'))return;
  await fetch('/api/diaglog/clear',{cache:'no-store'});
  last='';
  prevRawLines=[];
  stampedLines=[];
  await loadLog();
}
setInterval(loadLog,500);
loadLog();
</script></body></html>
)HTML";
