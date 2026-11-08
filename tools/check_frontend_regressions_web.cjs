// Regression scenarios from the frontend review. Hardware/settings use production DeviceApi.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {spawn}=require('node:child_process'),readline=require('node:readline');
const root=path.resolve(__dirname,'..'),binary=process.env.DEVICE_API_TEST_BINARY;
if(!binary)throw Error('Set DEVICE_API_TEST_BINARY');
const selected=name=>!process.env.FRONTEND_SCENARIO||process.env.FRONTEND_SCENARIO.split(',').includes(name);
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const extract=(file,delimiter='HTML')=>fs.readFileSync(path.join(root,'G5500_Fork',file),'utf8').split('R"'+delimiter+'(')[1].split(')'+delimiter+'";')[0];
const main=extract('web_page.hpp'),hardware=extract('hardware_page.hpp','HWHTML'),adc=extract('diagnostic_pages.hpp');
const output=process.env.ARTIFACT_DIR;
if(output)fs.mkdirSync(output,{recursive:true});
async function instance(browser,{url='/#settings',delayPath='',reboot=false,joystick=false,step=3}={}){
 const proc=spawn(binary,['--serve-http'],{cwd:root,stdio:['pipe','pipe','inherit']}),pending=[],trace=[],errors=[];
 readline.createInterface({input:proc.stdout}).on('line',line=>{const p=pending.shift();try{p.resolve(JSON.parse(line));}catch(e){p.reject(Error(line));}});
 proc.on('exit',code=>{for(const p of pending.splice(0))p.reject(Error('Native exited '+code));});
 const started=Date.now();let queue=Promise.resolve();
 const rpc=(method,url,body='')=>{
  const task=queue.then(()=>new Promise((resolve,reject)=>{pending.push({resolve,reject});proc.stdin.write(`${10001+Date.now()-started}\t${method}\t${url}\t${body}\n`);}));
  queue=task.catch(()=>{});return task;
 };
 await rpc('POST','/__fixture/controller');await rpc('POST','/api/setup','action=pause');
 await rpc('POST','/api/settings','rotorName=Saved-antenna');
 if(joystick){
  const h=(await rpc('GET','/api/hardware')).body;
  assert.equal((await rpc('POST','/api/hardware',new URLSearchParams({token:h.token,generation:h.generation,localInputMode:2,'pin.localAzAdc':36}).toString())).status,200);
  await rpc('POST','/__fixture/reboot');await rpc('POST','/api/hardware/activity');await rpc('GET','/api/hardware');await rpc('POST','/__fixture/inputs');
 }
 if(url==='/setup')await rpc('POST','/__fixture/step',String(step));
 const page=await browser.newPage({viewport:{width:1100,height:950}});page.setDefaultTimeout(15000);
 page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
 await page.addInitScript(()=>window.G5500_HTTP_ONLY=true);
 await page.route('http://frontend.test/**',async route=>{
  const req=route.request(),u=new URL(req.url()),body=req.postData()||u.search.slice(1);
  trace.push({path:u.pathname,url:req.url(),method:req.method(),body});
  if(u.pathname==='/'||u.pathname==='/setup')return route.fulfill({contentType:'text/html',body:main});
  if(u.pathname==='/hardware')return route.fulfill({contentType:'text/html',body:hardware});
  // Text AP replies/restart semantics are covered by native tests; here inspect the actual browser request.
  if(u.pathname==='/api/ap')return route.fulfill({contentType:'text/plain',body:'AP password saved'});
  if(u.pathname==='/favicon.ico')return route.fulfill({status:204});
  if(req.method()==='POST'&&u.pathname===delayPath)await wait(1000);
  const result=await rpc(req.method(),u.pathname,body);
  if(reboot&&req.method()==='POST'&&u.pathname==='/api/hardware'&&result.status===200)await rpc('POST','/__fixture/reboot');
  await route.fulfill({status:result.status,contentType:'application/json',body:JSON.stringify(result.body)});
 });
 await page.goto('http://frontend.test'+url);
 if(url==='/hardware')await page.waitForFunction(()=>state&&metadata&&!document.getElementById('save').disabled);
 else await page.waitForFunction(()=>wizard.loaded&&window._forkState);
 return {page,rpc,trace,errors,close:async()=>{try{assert.deepEqual(errors,[]);}finally{await page.close();proc.stdin.end();proc.kill();}}};
}
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  if(selected('periodic-force')){
   const s=await instance(browser,{url:'/setup'}),p=s.page;
   try{
    await p.waitForFunction(()=>wizard.hardwareCanApply&&!wizard.polling);
    const count=await p.evaluate(async()=>{let reads=0;const real=window.fetch;window.fetch=(u,o)=>{if(new URL(u,location.href).pathname==='/api/status')++reads;return real(u,o);};wizard.busy=true;background.pause(true);try{await upd(true,true);return reads;}finally{wizard.busy=false;background.pause(false);window.fetch=real;}});
    assert.equal(count,0,'Periodic forced refresh must obey the paused background queue');
    console.log('PASS: periodic metadata refresh obeys the save transaction pause');
   }finally{await s.close();}
  }
  if(selected('late-cancel')){
   const s=await instance(browser),p=s.page;
   try{
    const name=await p.evaluate(async()=>{const real=window.fetch;let reject;window.fetch=(u,o)=>String(u)==='/__lateAbort'?new Promise((resolve,r)=>reject=r):real(u,o);try{const job=background.read('__lateAbort','/__lateAbort').catch(e=>e.name);await new Promise(r=>setTimeout(r,0));background.pause(true);background.pause(false);reject(new DOMException('aborted','AbortError'));return await job;}finally{window.fetch=real;}});
    assert.equal(name,'PollingDeferred','A local cancellation stays local after background requests resume');
    console.log('PASS: delayed cancellation after resume never becomes a network failure');
   }finally{await s.close();}
  }
  if(selected('stale-setup')){
   const s=await instance(browser,{url:'/setup'}),p=s.page;
   try{
    await p.waitForFunction(()=>wizard.hardwareCanApply&&!wizard.polling);
    let held,release;const received=new Promise(r=>held=r),gate=new Promise(r=>release=r);let first=true;
    await p.route('http://frontend.test/api/setup',async route=>{
     if(route.request().method()!=='GET'||!first)return route.fallback();first=false;
     const snapshot=await s.rpc('GET','/api/setup');held();await gate;await route.fulfill({status:200,json:snapshot.body});
    });
    await p.evaluate(()=>{const real=window.fetch;window.fetch=(url,options={})=>real(url,new URL(url,location.href).pathname==='/api/setup'&&!options.method?{...options,signal:undefined}:options);window.__stalePoll=pollSetup();});
    await received;await p.locator('#setupNext').click();await p.waitForFunction(()=>!wizard.busy&&wizard.state.step===4);
    release();await p.evaluate(()=>__stalePoll);
    assert.equal(await p.evaluate(()=>wizard.state.step),4,'A pre-navigation setup response must not move the browser back');
    console.log('PASS: delayed pre-navigation setup read cannot overwrite the acknowledged next step');
   }finally{await s.close();}
  }
  if(selected('shared-pool')){
   const s=await instance(browser,{url:'/setup'}),p=s.page;
   try{
    await p.waitForFunction(()=>wizard.hardwareCanApply&&!wizard.polling);
    await p.evaluate(async()=>{
     window.__poolFetch=window.fetch;window.fetch=(url,options={})=>String(url).startsWith('/__poolhold')?new Promise((resolve,reject)=>options.signal.addEventListener('abort',()=>reject(new DOMException('aborted','AbortError')),{once:true})):__poolFetch(url,options);
     let started=false;const shim=window.fetch;window.fetch=(url,options)=>{if(String(url).startsWith('/__poolhold'))started=true;return shim(url,options);};window.__poolJobs=[];
     for(let i=0;!started&&i<30;++i){__poolJobs.push(background.read('__pool1','/__poolhold1').catch(()=>{}));await new Promise(r=>setTimeout(r,30));}
     if(!started)throw Error('Background slot did not become available');__poolJobs.push(background.read('__pool2','/__poolhold2').catch(()=>{}));
    });
    const frame=await(await p.locator('#equipmentFrame').elementHandle()).contentFrame();
    const deferred=await frame.evaluate(()=>background.read('__framePool','/api/status?view=bootstrap').then(()=>false,e=>e.name==='PollingDeferred'));
    await p.evaluate(async()=>{background.cancel('__pool1');background.cancel('__pool2');await Promise.all(__poolJobs);window.fetch=__poolFetch;});
    assert(deferred,'Hardware iframe must share the parent background request limit');
    console.log('PASS: parent and hardware iframe share one wizard background slot');
   }finally{await s.close();}
  }
  if(selected('polling-deferred')){
   const s=await instance(browser,{url:'/setup'}),p=s.page;
   try{
    await p.waitForFunction(()=>!wizard.polling&&connection.httpOnline);
    const result=await p.evaluate(async()=>{
     const real=window.fetch;
     window.fetch=(url,options={})=>String(url).startsWith('/__hold')?new Promise((resolve,reject)=>options.signal.addEventListener('abort',()=>reject(new DOMException('aborted','AbortError')),{once:true})):real(url,options);
     const jobs=[background.read('__hold1','/__hold1').catch(()=>{}),background.read('__hold2','/__hold2').catch(()=>{})];
     try{await upd(false);await pollSetup();return {online:connection.httpOnline,message:document.getElementById('setupMessage').textContent};}
     finally{background.cancel('__hold1');background.cancel('__hold2');await Promise.all(jobs);window.fetch=real;}
    });
    assert(result.online,'A deferred local refresh must not mark a reachable controller offline');
    assert(!/Связь прервана/.test(result.message),'Local pool saturation must not be shown as a lost wizard connection');
    console.log('PASS: a full background pool defers status/setup without marking the device offline');
   }finally{await s.close();}
  }
  if(selected('read-retry')){
   const s=await instance(browser),p=s.page;
   try{
    await p.waitForFunction(()=>!wizard.polling&&connection.httpOnline);
    const result=await p.evaluate(async()=>{
     const real=window.fetch,counts={};
     window.fetch=(url,options={})=>{const key=(options.method||'GET')+' '+new URL(url,location.href).pathname;counts[key]=(counts[key]||0)+1;if(counts[key]===1&&['GET /api/status','GET /api/setup','POST /api/setup'].includes(key))return Promise.reject(new TypeError('Connection reset'));return real(url,options);};
     try{await upd(false);await pollSetup();await setupRequest('/api/setup',{action:'begin_save'}).catch(()=>{});return {counts,online:connection.httpOnline,message:document.getElementById('setupMessage').textContent};}
     finally{window.fetch=real;}
    });
    assert.equal(result.counts['GET /api/status'],2,'A failed status read is retried once');
    assert.equal(result.counts['GET /api/setup'],2,'A failed setup read is retried once');
    assert.equal(result.counts['POST /api/setup'],1,'Mutations are never replayed after a network failure');
    assert(result.online&&!/Связь прервана/.test(result.message));
    console.log('PASS: one bounded retry for failed status/setup reads; POST is sent only once');
   }finally{await s.close();}
  }
  if(selected('name-ap')){
   const s=await instance(browser),p=s.page;
   try{
    await p.locator('#hudPanel details').evaluate(e=>e.open=true);
    assert.equal(await p.locator('#setupDeviceName').inputValue(),'Saved-antenna');
    await p.locator('#setupDeviceName').fill('Unsaved name');await p.evaluate(()=>renderSetup());
    assert.equal(await p.locator('#setupDeviceName').inputValue(),'Unsaved name');
    await p.locator('#networkSettings > details').evaluate(e=>e.open=true);
    const password='DummyOnly&A+B =?';await p.locator('#appass').fill(password);
    await Promise.all([p.waitForResponse(r=>new URL(r.url()).pathname==='/api/ap'),p.locator('#apSaveButton').click()]);
    const req=s.trace.find(r=>r.path==='/api/ap');assert.equal(req.method,'POST');assert.equal(new URL(req.url).search,'');assert.equal(new URLSearchParams(req.body).get('pass'),password);
    console.log('PASS: ordinary saved name, preserved draft, AP POST body without URL password');
   }finally{await s.close();}
  }
  if(selected('settings')){
   const s=await instance(browser,{delayPath:'/api/settings'}),p=s.page;
   try{
    await p.locator('#programSettings details').evaluate(e=>e.open=true);
    const input=p.locator('#forkSettings-rotorName'),save=p.locator('#programSettings button[type=submit]');
    await input.fill('Saved-A');await save.click();await input.fill('Draft-B');await input.evaluate(e=>e.blur());
    await p.waitForFunction(()=>settingsRequests.size===0);await wait(1300);
    assert.equal(await input.inputValue(),'Draft-B');assert(await p.evaluate(()=>!!sharedForm.dataset.dirty));
    assert.equal((await s.rpc('GET','/api/status')).body.rotorName,'Saved-A');
    await save.click();await p.waitForFunction(()=>!sharedForm.dataset.dirty);
    assert.equal((await s.rpc('GET','/api/status')).body.rotorName,'Draft-B');
    console.log('PASS: ordinary settings retain newer draft and save it on the next explicit submit');
   }finally{await s.close();}
  }
  if(selected('hardware')){
   const s=await instance(browser,{url:'/hardware',delayPath:'/api/hardware',reboot:true}),p=s.page;
   try{
    const input=p.locator('[name=forceApSta]');await input.evaluate(e=>e.closest('details').open=true);await input.selectOption('1');await p.locator('#save').click();await p.waitForFunction(()=>busy);
    await input.selectOption('0');await p.waitForFunction(()=>!busy);await wait(500);
    assert.equal(await input.inputValue(),'0');assert(await p.evaluate(()=>dirty));
    assert.equal((await s.rpc('GET','/api/hardware')).body.configured.forceApSta,1);
    await p.locator('#save').click();await p.waitForFunction(()=>!busy&&!dirty);
    assert.equal((await s.rpc('GET','/api/hardware')).body.configured.forceApSta,0);
    console.log('PASS: hardware newer draft survives save/reboot/refresh and remains saveable');
   }finally{await s.close();}
  }
  if(selected('iframe')){
   const s=await instance(browser,{url:'/setup',delayPath:'/api/hardware'}),p=s.page;
   try{
    const frame=p.frameLocator('#equipmentFrame');await frame.locator('[name=forceApSta]').evaluate(e=>e.closest('details').open=true);await frame.locator('[name=forceApSta]').selectOption('1');
    await p.waitForFunction(()=>wizard.hardwareDirty&&wizard.hardwareCanApply);
    const write=p.waitForRequest(r=>r.method()==='POST'&&new URL(r.url()).pathname==='/api/hardware');
    await p.locator('#setupNext').click();await write;
    assert(await p.evaluate(()=>background.read('__duringSave','/api/status').then(()=>false,e=>e.name==='PollingDeferred')),'Background reads pause during the save transaction');
    await frame.locator('[name=forceApSta]').selectOption('0');await p.waitForFunction(()=>!wizard.busy);
    assert.equal(await p.evaluate(()=>wizard.state.step),3);assert.match(await p.locator('#setupMessage').textContent(),/новые изменения/);
    assert.equal(await frame.locator('[name=forceApSta]').inputValue(),'0');
    assert(!s.trace.some(r=>r.path==='/api/setup'&&new URLSearchParams(r.body).get('action')==='apply'));
    await p.locator('#setupNext').click();await p.waitForFunction(()=>!wizard.busy);
    const writes=s.trace.filter(r=>r.path==='/api/hardware'&&r.method==='POST');assert.equal(writes.length,2);assert.equal(new URLSearchParams(writes[1].body).get('forceApSta'),'0');
    console.log('PASS: iframe save preserves newer input and blocks wizard advancement until resaved');
   }finally{await s.close();}
  }
  if(selected('joystick'))for(const delayed of [false,true]){
   const s=await instance(browser,{url:'/setup',step:4,joystick:true,delayPath:delayed?'/api/hardware/joystick/start':''}),p=s.page;
   try{
    const f=await(await p.locator('#equipmentFrame').elementHandle()).contentFrame();
    await f.locator('#joystick-start').click();await f.waitForFunction(()=>!!capture);
    if(!delayed){await f.waitForFunction(()=>capture&&!capture.starting);await wait(200);assert(s.trace.some(r=>r.path==='/api/hardware/joystick/poll'));}
    await p.locator('#setupBack').click();await p.waitForFunction(()=>!wizard.busy&&wizard.state.step===3);
    await p.locator('#setupBack').click();await p.waitForFunction(()=>!wizard.busy&&wizard.state.step===2);
    await f.waitForFunction(()=>!capture&&!calibrationPending);
    assert(!await p.locator('#equipmentFrame').isVisible());
    const count=()=>s.trace.filter(r=>r.path==='/api/hardware/joystick/poll').length,before=count();await wait(2300);
    assert.equal(count(),before);assert.equal(s.trace.filter(r=>r.path==='/api/hardware/joystick/cancel').length,1);
    assert.equal((await s.rpc('GET','/api/status')).body.configurationLocked,false);
    console.log(`PASS: leaving calibration ${delayed?'during delayed start':'while polling'} cancels once, no hidden polls or lock`);
   }finally{await s.close();}
  }
  if(selected('visibility')){
   const s=await instance(browser,{url:'/hardware',joystick:true}),p=s.page;
   try{
    await p.locator('#joystick-start').click();await p.waitForFunction(()=>capture&&!capture.starting);
    await p.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});document.dispatchEvent(new Event('visibilitychange'));});
    await p.waitForFunction(()=>!capture&&!calibrationPending);assert.equal((await s.rpc('GET','/api/status')).body.configurationLocked,false);
    await p.evaluate(()=>{delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));});await wait(400);
    assert.equal(s.trace.filter(r=>r.path==='/api/hardware/joystick/start').length,1);
    console.log('PASS: hidden standalone calibration cancels and does not restart on visibility');
   }finally{await s.close();}
  }
  if(selected('adc')){
   const p=await browser.newPage({viewport:{width:390,height:950}});let offline=false,value=1234;
   await p.route('http://adc.test/**',r=>{
    const u=new URL(r.request().url());if(u.pathname==='/adc')return r.fulfill({contentType:'text/html',body:adc});
    if(u.pathname==='/api/adcdiag')return offline?r.abort('failed'):r.fulfill({json:{hasEl:true,azSensor:'ADC',elSensor:'ADC',azRaw:value,azMv:1000,elRaw:2345,elMv:2000}});
    return r.fulfill({json:{}});
   });
   await p.goto('http://adc.test/adc');await p.waitForFunction(()=>azraw.textContent==='1234'&&adcStatus.dataset.stale==='false');
   offline=true;await p.waitForFunction(()=>adcStatus.dataset.stale==='true');assert.match(await p.locator('#adcStatus').textContent(),/Нет свежих данных.*Последний ответ/);
   assert.equal(await p.locator('#azraw').textContent(),'1234');
   for(const width of [390,1100]){await p.setViewportSize({width,height:950});assert(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));if(output)await p.screenshot({path:path.join(output,`adc-stale-${width}.png`)});}
   value=1444;offline=false;await p.waitForFunction(()=>azraw.textContent==='1444'&&adcStatus.dataset.stale==='false');
   console.log('PASS: ADC failure marks retained readings stale, shows last response, recovers without reload; 390/1100px');await p.close();
  }
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
