// Browser contract: negotiated probes aid TCP recovery without faking freshness.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const sample=JSON.parse(execFileSync(path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox','--disable-background-timer-throttling']});
 try{
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>{
   window.testSockets=[];
   window.WebSocket=class {static OPEN=1;readyState=1;bufferedAmount=0;sent=[];constructor(){testSockets.push(this)}close(){this.readyState=2}send(data){this.sent.push(data)}};
  });
  const state={...sample,livePort:81,liveControlChannel:false,liveHeartbeat:true,deviceRole:'controller',configurationLocked:false,moving:false};
  await page.route('http://heartbeat.test/**',route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/api/status')return route.fulfill({json:state});
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{items:[],values:[],points:[],peers:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://heartbeat.test/');await page.waitForFunction(()=>window._forkState&&live.socket);
  const snapshot=revision=>page.evaluate(([state,revision])=>live.socket.onmessage({data:JSON.stringify({v:1,type:'state',boot:'abc',revision,state})}),[state,revision]);
  await snapshot(1);await page.evaluate(()=>live.socket.sent=[]);await page.waitForTimeout(150);
  assert.equal(await page.evaluate(()=>live.socket.sent.length),0,'fresh telemetry needs no probes');
  const staleAt=await page.evaluate(()=>{live.last=Date.now()-1200;live.probeAt=0;return live.last});
  await page.waitForFunction(()=>live.socket.sent.length>0);
  assert.deepEqual(await page.evaluate(()=>live.socket.sent),['v=1&ping=1']);
  assert.equal(await page.evaluate(()=>live.last),staleAt,'sending a probe must not refresh stale state');
  assert.equal(await page.evaluate(()=>liveFresh()),false);
  await page.evaluate(()=>live.socket.bufferedAmount=1);const count=await page.evaluate(()=>live.socket.sent.length);
  await page.waitForTimeout(600);assert.equal(await page.evaluate(()=>live.socket.sent.length),count,'do not queue probes behind pending client bytes');
  await page.evaluate(()=>{live.socket.bufferedAmount=0;httpState.liveHeartbeat=false});state.liveHeartbeat=false;
  await page.waitForTimeout(600);assert.equal(await page.evaluate(()=>live.socket.sent.length),count,'old firmware must receive no new protocol message');
  state.liveHeartbeat=true;await page.evaluate(()=>httpState.liveHeartbeat=true);await snapshot(2);
  await page.waitForTimeout(150);assert.equal(await page.evaluate(()=>live.socket.sent.length),count,'a fresh snapshot stops probes');
  await page.evaluate(()=>{live.socket.close();live.last=Date.now()-900});
  await page.waitForTimeout(400);assert.equal(await page.evaluate(()=>live.socket.sent.length),count,'closed sockets receive no probes');
  state.telemetryIntervalMs=500;state.networkMemoryLimited=true;
  await page.evaluate(()=>{live.socket.readyState=1;live.socket.sent=[];});await snapshot(3);
  await page.evaluate(()=>{live.last=Date.now()-550;live.probeAt=0;});await page.waitForTimeout(200);
  assert.equal(await page.evaluate(()=>live.socket.sent.length),0,'2Hz telemetry must not cause extra heartbeat traffic');
  await page.evaluate(()=>{
   controlLive.socket=new WebSocket('ws://heartbeat.test:81/api/control');controlLive.ready=true;controlLive.memoryLimited=true;controlLive.last=Date.now();
   window.retainedTelemetry=live.socket;live.last=Date.now()-6000;
  });await page.waitForTimeout(350);
  assert(await page.evaluate(()=>live.socket===retainedTelemetry&&!liveFresh()),'local RAM throttling keeps the socket while coordinates remain stale');
  await page.clock.install();
  const retainedControl=await page.evaluate(()=>{
   window.retainedControl=controlLive.socket;
   live.socket.readyState=3;live.socket.onclose();
   return live.socket===null&&controlLive.socket===retainedControl&&controlLive.ready;
  });
  assert(retainedControl,'server shedding telemetry must retain the independent command channel');
  await page.evaluate(()=>{
   clearTimeout(live.retry);live.retry=null;
   controlLive.socket=null;controlLive.ready=false;live.failures=0;live.stableAt=0;
   live.socket=new WebSocket('ws://heartbeat.test:81/api/live');disconnectLive(live.socket,81);
  });
  for(const delay of [1000,2000,4000,8000,15000]){
   const opened=await page.evaluate(()=>testSockets.length);
   await page.clock.runFor(delay*0.8);assert.equal(await page.evaluate(()=>testSockets.length),opened);
   await page.clock.runFor(delay*0.4);assert.equal(await page.evaluate(()=>testSockets.length),opened+1);
   if(delay!==15000)await page.evaluate(()=>disconnectLive(live.socket,81));
  }
  const opened=await page.evaluate(()=>testSockets.length);
  await page.evaluate(()=>{live.stableAt=Date.now()-11000;disconnectLive(live.socket,81);});
  await page.clock.runFor(1200);assert.equal(await page.evaluate(()=>testSockets.length),opened+1,'stable connection resets retry backoff');
  assert.deepEqual(errors,[]);
  console.log('PASS: negotiated heartbeat/freshness, 2Hz RAM throttle, critical telemetry close retains control, retry backoff/reset and old firmware compatibility');
 }finally{await browser.close()}
})().catch(error=>{console.error(error);process.exit(1)});
