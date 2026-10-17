// Live and legacy browser paths use the same portable firmware command/state core.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const net=require('node:net');
const root=process.env.ROTOR_URL;
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox','--disable-background-timer-throttling','--disable-renderer-backgrounding']});
 const errors=[];
 try{
  const pages=await Promise.all([process.env.SIM_URL,process.env.SECOND_URL,root,root].map(async url=>{
   const page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
   await page.goto(url);await page.waitForFunction(()=>live.ready&&window._forkState?.model&&(window._forkState.deviceRole!=='hud'||window._forkState.hudLive));
   return page;
  }));
  // Four effective controller consumers: two subscribed HUDs and two direct browsers.
  await pages[0].evaluate(()=>forkPost('/api/mode',{mode:'auto'}));
  const tcp=net.createConnection({host:'127.0.0.1',port:Number(process.env.ROTOR_TCP)});tcp.setNoDelay(true);
  await new Promise((resolve,reject)=>{tcp.once('connect',resolve);tcp.once('error',reject)});
  const logger=target=>new Promise((resolve,reject)=>{tcp.once('data',d=>{try{assert.equal(d.toString(),'RPRT 0\n');resolve(true)}catch(e){reject(e)}});tcp.write('P '+target+' 0\n')});
  const encoder=async targetReady=>{
   let j=await (await fetch(process.env.SIM_URL+'/api/sim/encoder',{method:'POST',body:'delta=1'})).json();
   targetReady(j.target);const id=j.requestId;
   for(let tries=0;j.result==='deferred'&&tries<200;tries++){
    await new Promise(r=>setTimeout(r,5));j=await(await fetch(process.env.SIM_URL+'/api/hud/command?id='+id)).json();
   }
   assert.equal(j.result,'ok');return true;
  };
  const routes=['hud','controller','logger','encoder'];
  const rows=[],samples=Number(process.env.LIVE_SAMPLES||200);
  for(let i=0;i<samples;i++){
   // External logger targets inside the normal 5-degree dead band are intentionally coalesced.
   const source=routes[i%4],target=100+((i*17)%160);
   let onTarget;const targetReady=source==='encoder'?new Promise(resolve=>{onTarget=resolve}):Promise.resolve(target);
   await new Promise(resolve=>setTimeout(resolve,30+(i*37)%80));
   const start=performance.now();
   const request=source==='logger'?logger(target):source==='encoder'?encoder(onTarget):pages[source==='hud'?0:2].evaluate(t=>forkPost('/api/target',{az:t,el:0}),target);
   const ack=request.then(ok=>{assert.equal(ok,true);return performance.now()-start});
   const confirmedTarget=Number((await targetReady).toFixed(1));
   try {
    await Promise.all(pages.map(p=>p.waitForFunction(t=>Number(document.getElementById('azt').textContent)===t,confirmedTarget,{polling:'raf',timeout:2000})));
   } catch(error) {
    console.error(JSON.stringify({sample:i,source,confirmedTarget,screens:await Promise.all(pages.map(p=>p.evaluate(()=>({target:document.getElementById('azt').textContent,state:window._forkState,liveReady:live.ready})))),errors}));
    throw error;
   }
   rows.push({ack:await ack,all:performance.now()-start,source});
  }
  tcp.end();
  // Existing HTML traffic must not stop live control and updates.
  const downloads=Promise.all(Array.from({length:8},()=>fetch(root).then(r=>r.arrayBuffer())));
  assert.equal(await pages[0].evaluate(()=>forkPost('/api/move',{d:'S'})),true);
  await Promise.all(pages.map(p=>p.waitForFunction(()=>!window._forkState.moving)));
  await downloads;
  // The per-server WS cap is four, including simultaneously opening sockets.
  assert.equal(await pages[2].evaluate(async()=>{
   const extras=await Promise.all([0,1].map(()=>new Promise((resolve,reject)=>{
    const s=new WebSocket(live.socket.url);s.onerror=reject;s.onmessage=e=>{if(JSON.parse(e.data).type==='state')resolve(s)};
   })));
   const rejected=await new Promise(resolve=>{
    const s=new WebSocket(live.socket.url),timer=setTimeout(()=>{s.close();resolve(false)},2000);
    s.onclose=()=>{clearTimeout(timer);resolve(true)};s.onerror=()=>{};
    s.onmessage=()=>{clearTimeout(timer);s.close();resolve(false)};
   });
   for(const s of extras)s.close();return rejected;
  }),true);
  // Old selection / boot commands never execute.
  const stale=await pages[0].evaluate(async()=>{
   const r=await sendLive('/api/target',{az:300,selection:window._forkState.hudSelectionEpoch+1});return r.json();
  });assert.equal(stale.result,'cancelled');
  const bootRejected=await pages[0].evaluate(()=>new Promise((resolve,reject)=>{
   const socket=new WebSocket(live.socket.url);const timer=setTimeout(()=>{socket.close();reject(new Error('missing boot rejection'))},2000);
   socket.onmessage=e=>{const msg=JSON.parse(e.data);if(msg.type==='state')socket.send(new URLSearchParams({v:1,cid:1,boot:'ffffffffffffffff',selection:msg.state.hudSelectionEpoch,path:'/api/target',az:300}).toString());
    else if(msg.type==='result'){clearTimeout(timer);socket.close();resolve(msg.result);}};
  }));assert.equal(bootRejected.result,'cancelled');
  // Reconnect gets a new snapshot. No queued movement is replayed.
  await pages[0].evaluate(()=>live.socket.close());
  await pages[0].waitForFunction(()=>live.ready);
  assert.equal((await (await fetch(root+'/api/status')).json()).moving,false);
  assert.deepEqual(errors,[]);
  const summary={samples,failures:0,evidence:'host simulator and Chromium; not physical Wi-Fi/LCD',routes:{}};
  function stats(list){const a=list.sort((a,b)=>a-b);return Object.fromEntries([['p50',.5],['p95',.95],['p99',.99],['max',1]].map(([name,p])=>[name,Math.round(a[Math.ceil(p*a.length)-1]*10)/10]));}
  for(const source of routes){const list=rows.filter(r=>r.source===source);summary.routes[source]={ackMs:stats(list.map(r=>r.ack)),lastScreenMs:stats(list.map(r=>r.all))};}
  console.log(JSON.stringify(summary,null,2));
  if(process.env.LIVE_REPORT)fs.writeFileSync(process.env.LIVE_REPORT,JSON.stringify({summary,rows},null,2)+'\n');
  if(process.env.LIVE_ASSERT_LATENCY==='1')for(const r of Object.values(summary.routes))assert.ok(r.lastScreenMs.p95<100,JSON.stringify(r));
 }finally{await browser.close()}
})().catch(error=>{console.error(error);process.exit(1)});
