// Exercise the generated HTTP bytes with the production DeviceApi fixture.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {spawn,execFileSync}=require('node:child_process'),readline=require('node:readline'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),binary=process.env.DEVICE_API_TEST_BINARY;
const until=async(test,message)=>{const deadline=Date.now()+10000;while(!test()){if(Date.now()>deadline)throw Error(message);await new Promise(r=>setTimeout(r,20));}};
if(!binary)throw Error('Set DEVICE_API_TEST_BINARY');
const data=JSON.parse(execFileSync('python3',['-c',`import json,base64
from tools.generate_web_assets import bundle
_,_,resources,pages=bundle(True)
print(json.dumps({'pages':{n:{'gzip':base64.b64encode(p['packed']).decode(),'manifest':p['manifest']} for n,p in pages.items()},'resources':{r['url']:{'gzip':base64.b64encode(r['packed']).decode(),'type':r['type']} for r in resources}}))`],{cwd:root,maxBuffer:5e6}));
(async()=>{
 const proc=spawn(binary,['--serve-http'],{cwd:root,stdio:['pipe','pipe','inherit']}),pending=[];
 readline.createInterface({input:proc.stdout}).on('line',line=>{const p=pending.shift();if(p)p.resolve(JSON.parse(line));});
 let queue=Promise.resolve(),clock=10001;const trace=[],requests=[];
 const rpc=(method,url,body='')=>{const task=queue.then(()=>new Promise((resolve,reject)=>{pending.push({resolve,reject});proc.stdin.write(`${clock+=20}\t${method}\t${url}\t${body}\n`);}));queue=task.catch(()=>{});return task;};
 let gateResolve,gate=new Promise(r=>gateResolve=r),block=true,fail=false;
 let hardwareResolve,hardwareGate=new Promise(r=>hardwareResolve=r),hardwareHeld=true;
 const gated=data.pages.WEB_PAGE.manifest.scripts[1];
 const server=http.createServer(async(req,res)=>{
  try{
   const url=new URL(req.url,'http://localhost');requests.push(url.pathname);
   let page=url.pathname==='/hardware'||url.pathname==='/recovery'?data.pages.HARDWARE_PAGE:['/','/setup'].includes(url.pathname)?data.pages.WEB_PAGE:null;
   const asset=data.resources[url.pathname];
   if(page||asset){
    if(url.pathname===gated&&block)await gate;
    if(url.pathname===gated&&fail){fail=false;res.writeHead(503,{'Cache-Control':'no-store'});res.end('retry');return;}
    res.writeHead(200,{'Content-Type':asset?asset.type:'text/html; charset=utf-8','Content-Encoding':'gzip','Vary':'Accept-Encoding','Cache-Control':asset?'public,max-age=31536000,immutable':'no-store'});
    res.end(Buffer.from((asset||page).gzip,'base64'));return;
   }
   if(url.pathname==='/favicon.ico'){res.writeHead(204);res.end();return;}
   if(url.pathname==='/api/hardware/pins'&&hardwareHeld)await hardwareGate;
   const chunks=[];for await(const c of req)chunks.push(c);
   const body=Buffer.concat(chunks).toString()||url.search.slice(1),result=await rpc(req.method,url.pathname,body);
   trace.push({method:req.method,path:url.pathname,body,status:result.status});
   res.writeHead(result.status,{'Content-Type':'application/json','Cache-Control':'no-store'});res.end(JSON.stringify(result.body));
  }catch(e){res.writeHead(500);res.end(String(e));}
 });
 await new Promise(r=>server.listen(0,'127.0.0.1',r));const base='http://127.0.0.1:'+server.address().port;
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 const errors=[],out=process.env.ARTIFACT_DIR;
 if(out)fs.mkdirSync(out,{recursive:true});
 try{
  await rpc('POST','/__fixture/controller');
  assert.equal((await rpc('POST','/api/move','d=R')).status,200);
  const context=await browser.newContext({viewport:{width:390,height:1000}}),page=await context.newPage();
  await page.addInitScript(()=>{
   const original=window.fetch.bind(window);window.__stepReads=[];
   window.fetch=(url,options={})=>{if(!options.method)window.__stepReads.push({path:new URL(url,location.href).pathname,step:typeof wizard==='undefined'?null:wizard.state?.step});return original(url,options);};
  });
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/',{waitUntil:'commit'});
  await page.waitForFunction(()=>document.getElementById('webBootState').textContent==='Привод движется');
  assert(await page.locator('#webBootStop').isVisible());assert.equal(await page.locator('#webBootRetry').isVisible(),false);
  if(out)await page.screenshot({path:path.join(out,'bootstrap-390.png')});
  await page.locator('#webBootStop').click();await page.waitForFunction(()=>document.getElementById('webBootState').textContent==='STOP принят');
  assert(!(await rpc('GET','/api/setup')).moving,'Bootstrap STOP works before full UI');
  block=false;gateResolve();
  await page.waitForFunction(()=>document.documentElement.dataset.webReady==='1');
  await page.waitForFunction(()=>wizard.loaded&&!wizard.polling&&window._forkState);
  assert.deepEqual(errors,[]);
  assert.equal(await page.locator('#webBoot').count(),0);
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  const history=await page.locator('#historyPanel').getAttribute('data-content-url');
  assert(history&&!requests.includes(history),'History is absent from initial transfer');
  await page.evaluate(()=>{wizard.active=false;renderSetup();selectTab('history');});
  await page.waitForFunction(()=>document.querySelectorAll('#historyPanel .history-entry').length>0);
  assert(requests.includes(history));
  const assetsBefore=requests.filter(u=>u.startsWith('/ui/')).length;
  await page.reload({waitUntil:'domcontentloaded'});await page.waitForFunction(()=>document.documentElement.dataset.webReady==='1');
  assert.equal(requests.filter(u=>u.startsWith('/ui/')).length,assetsBefore,'Immutable resources use browser cache after root reload');
  await page.waitForFunction(()=>wizard.loaded&&!wizard.polling);
  await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===2);
  await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===3);
  await until(()=>requests.includes('/api/hardware/pins'),'Hardware metadata request did not start');
  assert(await page.locator('#setupNext').isDisabled(),'Next waits for required hardware data');
  hardwareHeld=false;hardwareResolve();
  await page.waitForFunction(()=>wizard.hardwareCanApply,{},{timeout:20000});
  assert(!trace.some(r=>r.path==='/api/hardware/display'),'Embedded wizard must not fetch hidden display settings');
  for(const width of [390,1100]){
   await page.setViewportSize({width,height:1100});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   if(out)await page.screenshot({path:path.join(out,`wizard-${width}.png`)});
  }
  assert.deepEqual(errors,[]);
  // Production setup state selects exactly the data needed by each step.
  const step=async number=>{await rpc('POST','/__fixture/step',String(number));await page.evaluate(()=>pollSetup());await page.waitForFunction(n=>wizard.state.step===n&&!wizard.polling,number);};
  const specialty=new Set(['/api/azcal','/api/elcal','/api/motion','/api/motion/coast','/api/mqtt','/api/network','/api/hud/peers']);
  let at=trace.length;
  await step(0);await until(()=>trace.slice(at).some(r=>r.path==='/api/network'),'Network step did not read network');
  assert(!trace.slice(at).some(r=>specialty.has(r.path)&&r.path!=='/api/network'));
  for(const n of [1,2,7]){
   at=trace.length;await step(n);await page.waitForTimeout(300);
   assert(!trace.slice(at).some(r=>specialty.has(r.path)),`Step ${n} must not preload other steps`);
  }
  await step(5);
  await page.waitForFunction(()=>!!window._azCal&&!!window._elCal);
  await page.locator('#azRangeForm [name=span]').fill('240');
  at=trace.length;await step(6);
  await until(()=>trace.slice(at).some(r=>r.path==='/api/motion'),'Motion step did not read motion');
  await page.waitForTimeout(1200);
  assert(trace.slice(at).some(r=>r.path==='/api/motion'));
  assert(await page.evaluate(()=>!window.__stepReads.some(r=>r.step===6&&['/api/azcal','/api/elcal','/api/network','/api/mqtt'].includes(r.path))),'The motion step must not dispatch other steps data');
  await step(7);at=trace.length;await page.waitForTimeout(2800);
  assert(!trace.slice(at).some(r=>specialty.has(r.path)),'Hidden motion/calibration/iframe must stop polling');
  await step(5);assert.equal(await page.locator('#azRangeForm [name=span]').inputValue(),'240','Step changes preserve drafts');
  await step(9);at=trace.length;await page.waitForTimeout(300);
  assert(!trace.slice(at).some(r=>r.path==='/api/mqtt'),'Optional MQTT stays lazy');
  await page.locator('#optionalMqtt').evaluate(e=>e.open=true);
  await until(()=>trace.slice(at).some(r=>r.path==='/api/mqtt'),'Opening optional MQTT did not read its settings');
  assert.deepEqual(errors,[]);
  await context.close();
  const retryContext=await browser.newContext(),retryPage=await retryContext.newPage();fail=true;
  await retryPage.goto(base+'/?setup=skip',{waitUntil:'domcontentloaded'});
  await retryPage.locator('#webBootRetry').waitFor({state:'visible'});
  assert(await retryPage.locator('#webBootStop').isEnabled());
  await retryPage.locator('#webBootRetry').click();await retryPage.waitForFunction(()=>document.documentElement.dataset.webReady==='1');
  await retryContext.close();
  console.log('PASS: generated gzip shell, STOP before app, async initialization, immutable cache, lazy history, retry after failed asset, real wizard + iframe, 390/1100px');
 }finally{
  if(out)fs.writeFileSync(path.join(out,'staged-browser.json'),JSON.stringify({errors,trace,requests},null,2));
  await browser.close();server.close();proc.stdin.end();
 }
})().catch(e=>{console.error(e);process.exitCode=1});
