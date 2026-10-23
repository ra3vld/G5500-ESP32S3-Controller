// Offline browser contract: rate controls and a WebSocket that never closes.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const sample=JSON.parse(execFileSync(path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
(async()=>{
 const browser=await chromium.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:390,height:844}}),errors=[],posts=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>{
   window.testSockets=[];
   window.WebSocket=class {static OPEN=1;readyState=1;bufferedAmount=0;constructor(){testSockets.push(this)}close(){this.readyState=2}send(){}};
  });
  const state={...sample,livePort:81,deviceRole:'controller',configurationLocked:false,moving:false,az:20,autotune:{phase:'idle',active:false},build:{...sample.build,drive:'BTS7960'}};
  let controlPeriodMs=20;
  await page.route('http://frequency.test/**',route=>{
   const req=route.request(),url=new URL(req.url());
   if(req.method()==='POST'){
    const form=Object.fromEntries(new URLSearchParams(req.postData()));posts.push({path:url.pathname,form});
    if(form.controlHz)controlPeriodMs=1000/Number(form.controlHz);
    return route.fulfill({json:{ok:true,result:'ok'}});
   }
   if(url.pathname==='/api/status')return route.fulfill({json:state});
   if(url.pathname==='/api/motion')return route.fulfill({json:{controlPeriodMs,axes:[]}});
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{items:[],values:[],points:[],peers:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://frequency.test/#settings');await page.waitForFunction(()=>window._forkState&&testSockets.length);
  await page.locator('#motionSettings summary').click();await page.waitForFunction(()=>controlHz.value==='50');
  for(const hz of ['100','200','50']){
   await page.locator('#controlHz').selectOption(hz);await page.locator('#controlFrequencyForm button').click();
   await page.waitForFunction(()=>!controlFrequencyForm.dataset.dirty);
   assert.equal(controlPeriodMs,1000/Number(hz));assert.deepEqual(posts.at(-1),{path:'/api/settings',form:{controlHz:hz}});
   await page.getByRole('button',{name:'Закрыть уведомление'}).click();
  }
  // First frame is fresh, then TCP stalls and close never emits onclose.
  await page.evaluate(state=>testSockets[0].onmessage({data:JSON.stringify({v:1,type:'state',boot:'abc',revision:1,state:{...state,az:99}})}),state);
  await page.waitForFunction(()=>live.ready&&window._forkState.az===99);
  state.az=30;
  await page.evaluate(()=>{live.last=Date.now()-1500;});
  await page.waitForFunction(()=>window._forkState.az===30);
  assert(await page.evaluate(()=>live.ready&&testSockets[0].readyState===1),'short stall keeps connection while HTTP supplies fresh state');
  await page.evaluate(()=>{live.last=Date.now()-6000;});
  await page.waitForFunction(()=>!live.ready&&window._forkState.az===30);
  assert.equal(await page.evaluate(()=>testSockets[0].readyState),2);
  await page.waitForFunction(()=>testSockets.length>=2);
  await page.evaluate(()=>testSockets[0].onmessage({data:JSON.stringify({v:1,type:'state',boot:'abc',revision:999,state:{az:999}})}));
  assert.equal(await page.evaluate(()=>window._forkState.az),30,'late old-socket frames must be ignored');
  await page.evaluate(state=>testSockets.at(-1).onmessage({data:JSON.stringify({v:1,type:'state',boot:'abc',revision:2,state:{...state,az:31}})}),state);
  await page.waitForFunction(()=>live.ready&&window._forkState.az===31);
  // Failed initial handshake must also be replaced without waiting for close.
  await page.evaluate(()=>{live.ready=false;live.last=Date.now()-6000;});
  const before=await page.evaluate(()=>testSockets.length);await page.waitForFunction(n=>testSockets.length>n,before);
  await page.locator('#controlFrequencyForm').scrollIntoViewIfNeeded();
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  const out=path.join(root,'local-notes/control-20261023');fs.mkdirSync(out,{recursive:true});
  await page.screenshot({path:path.join(out,'frequency-390.png')});
  assert.deepEqual(errors,[]);console.log('PASS: 50/100/200 Hz, save/reload, mobile layout, stalled close, fresh HTTP fallback, late frames, handshake recovery');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
