// Regression coverage for keyboard input, wizard escape paths and guided recovery.
const root=require('node:path').resolve(__dirname,'..');
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process'),readline=require('node:readline');
const binary=process.env.DEVICE_API_TEST_BINARY||root+'/build/native-fast/bin/ee41b956d0051b4dc5d4/device_api_tests';
const out=process.env.ARTIFACT_DIR||root+'/local-notes/ux-fixes-2026-10-30';fs.mkdirSync(out,{recursive:true});
const extract=(name,tag)=>fs.readFileSync(root+'/G5500_Fork/'+name,'utf8').split('R"'+tag+'(')[1].split(')'+tag+'";')[0];
const main=extract('web_page.hpp','HTML'),hardware=extract('hardware_page.hpp','HWHTML');
const wait=ms=>new Promise(r=>setTimeout(r,ms));const results=[];
async function instance(browser,{width=390,step=1,paused=false,moving=false,recovery=false,host='ux.test'}={}){
 const proc=spawn(binary,['--serve-http'],{cwd:root,stdio:['pipe','pipe','inherit']}),pending=[],trace=[],errors=[];
 readline.createInterface({input:proc.stdout}).on('line',line=>{const p=pending.shift();if(!p)return;try{p.resolve(JSON.parse(line));}catch(e){p.reject(Error(line));}});
 proc.on('exit',code=>{for(const p of pending.splice(0))p.reject(Error('Fixture exited '+code));});
 const start=Date.now();let queue=Promise.resolve();
 const rpc=(method,url,body='')=>{const task=queue.then(()=>new Promise((resolve,reject)=>{pending.push({resolve,reject});proc.stdin.write(`${10001+Date.now()-start}\t${method}\t${url}\t${body}\n`);}));queue=task.catch(()=>{});return task;};
 await rpc('POST','/__fixture/controller');await rpc('POST','/__fixture/step',String(step));
 if(paused)await rpc('POST','/api/setup','action=pause');
 if(moving)assert.equal((await rpc('POST','/api/move','d=R')).status,200);
 if(recovery)await rpc('POST','/__fixture/recovery');
 const page=await browser.newPage({viewport:{width,height:850},locale:'ru-RU'});page.setDefaultTimeout(10000);
 page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
 await page.addInitScript(()=>window.G5500_HTTP_ONLY=true);
 await page.route('http://'+host+'/**',async route=>{
  const req=route.request(),u=new URL(req.url()),body=req.postData()||u.search.slice(1);
  if(u.pathname==='/'||u.pathname==='/setup')return route.fulfill({contentType:'text/html',body:main});
  if(u.pathname==='/hardware'||u.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:hardware});
  if(u.pathname==='/favicon.ico')return route.fulfill({status:204});
  const result=await rpc(req.method(),u.pathname,body);trace.push({path:u.pathname,method:req.method(),body,response:result});
  await route.fulfill({status:result.status,contentType:'application/json',body:typeof result.body==='string'?result.body:JSON.stringify(result.body)});
 });
 await page.goto('http://'+host+(recovery?'/recovery':paused?'/':'/setup'));
 await page.waitForFunction(recovery=>recovery?typeof state!=='undefined'&&state&&metadata:typeof wizard!=='undefined'&&wizard.loaded&&window._forkState&&(!wizard.active||wizard.state?.token),recovery);
 return {page,rpc,trace,errors,close:async()=>{await page.close();proc.stdin.end();proc.kill();}};
}
async function snapshot(s,name){
 assert.deepEqual(s.errors,[],name+' JS errors');assert(await s.page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),name+' horizontal overflow');
 await s.page.screenshot({path:out+'/'+name+'.png'});
}
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox'],timeout:15000});
 try{
  for(const width of [390,1100]){
   {
    const s=await instance(browser,{width,paused:true});try{
     await s.page.locator('#taz').fill('90');const start=s.trace.length;
     for(const key of ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'])await s.page.locator('#taz').press(key);
     await s.page.locator('#taz').press('Tab');assert.equal(await s.page.evaluate(()=>document.activeElement.id),'tel');
     await s.page.locator('#tel').press('Shift+Tab');assert.equal(await s.page.evaluate(()=>document.activeElement.id),'taz');
     await wait(500);assert(!s.trace.slice(start).some(r=>r.path==='/api/move'&&r.method==='POST'&&new URLSearchParams(r.body).get('d')==='H'));
     assert(!(await s.rpc('GET','/api/status')).moving);await snapshot(s,'keyboard-'+width);
    }finally{await s.close();}
   }
   {
    const s=await instance(browser,{width,step:0,moving:true});try{
     const button=s.page.locator('#globalStop');assert(await button.isVisible());await button.click();assert(!(await s.rpc('GET','/api/status')).moving);
     await s.page.locator('#appass').fill('short');await s.page.locator('#setupPause').click();await s.page.waitForFunction(()=>!wizard.busy);assert(await s.page.evaluate(()=>wizard.active));
     await s.page.locator('#setupExit').click();await s.page.waitForFunction(()=>!wizard.active&&!document.getElementById('controlPanel').hidden);
     assert.equal((await s.rpc('GET','/api/setup')).body.paused,true);await snapshot(s,'exit-invalid-'+width);
    }finally{await s.close();}
   }
   {
    const s=await instance(browser,{width,step:3});try{
     const frame=await(await s.page.locator('#equipmentFrame').elementHandle()).contentFrame();await frame.waitForFunction(()=>state&&metadata);
     await wait(600);assert(await frame.locator('#position-sensors').isVisible(),'Hardware fields must remain visible inside the wizard');const footer=await s.page.locator('#setupNext').boundingBox();assert(footer.y>0&&footer.y+footer.height<=850);
     assert(await s.page.locator('#globalStop').isVisible());await snapshot(s,'equipment-'+width);
     await s.rpc('POST','/__fixture/step','4');await s.page.evaluate(()=>pollSetup());await frame.waitForFunction(()=>hardwareStage===4);assert(await frame.locator('#input-checks').isVisible(),'Input checks must remain reachable');await snapshot(s,'inputs-'+width);
    }finally{await s.close();}
   }
   {
    const s=await instance(browser,{width,recovery:true});try{
     assert(await s.page.locator('#recovery-guide').isVisible());assert(await s.page.locator('#manual-settings').isHidden());
     await s.page.locator('#hardwareStop').click();assert(!(await s.rpc('GET','/api/hardware')).moving);
     await s.page.locator('#recovery-network-open').click();await s.page.waitForFunction(()=>recoveryNetworkLoaded);
     await s.page.locator('#recovery-network [name=choice]').selectOption('0');await s.page.locator('#recovery-network [name=ssid]').fill('Fixed-router');await s.page.locator('#recovery-network [name=pass]').fill('password123');
     await s.page.locator('#recovery-network button[type=submit]').click();await s.page.waitForFunction(()=>!recoveryNetworkBusy);
     assert.match(await s.page.locator('#recovery-network-message').textContent(),/Сеть сохранена/);
     assert((await s.rpc('GET','/api/hardware')).body.recovery);await snapshot(s,'recovery-'+width);
     await s.page.locator('#recovery-manual-open').click();assert(await s.page.locator('#manual-settings').isVisible());
     await s.page.locator('#recovery-resume').click();await s.page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено.'));
     await s.rpc('POST','/__fixture/reboot');assert(!(await s.rpc('GET','/api/hardware')).body.recovery);
    }finally{await s.close();}
   }
   console.log('PASS '+width+'px: arrows/Tab, persistent STOP, discard invalid form, fixed navigation, guided recovery save/resume');
  }
  {
   const s=await instance(browser,{step:0});let blocked;try{
    await s.page.route('**/api/wifi',r=>{blocked=r;});await s.page.locator('#ssid').fill('Timeout-router');await s.page.locator('#setupNext').click();
    await s.page.waitForFunction(()=>wizard.busy);await s.page.waitForFunction(()=>!wizard.busy,{},{timeout:15000});
    assert.match(await s.page.locator('#setupMessage').textContent(),/10 секунд/);assert(await s.page.locator('#setupExit').isEnabled());
    await s.page.locator('#setupExit').click();await s.page.waitForFunction(()=>!wizard.active);assert(!s.trace.some(r=>r.path==='/api/ap'&&r.method==='POST'));
    await snapshot(s,'network-timeout');
   }finally{if(blocked)await blocked.abort().catch(()=>{});await s.close();}
  }
  {
   const s=await instance(browser,{step:0});let blocked;try{
    await s.page.route('**/api/wifi',r=>{blocked=r;});await s.page.locator('#ssid').fill('Cancelled-router');await s.page.locator('#appass').fill('newpassword');await s.page.locator('#setupNext').click();
    await s.page.waitForFunction(()=>wizard.busy);await s.page.locator('#setupExit').click();await s.page.waitForFunction(()=>!wizard.active);
    if(blocked)await blocked.fulfill({body:'Saved'}).catch(()=>{});await wait(350);
    assert(!s.trace.some(r=>r.path==='/api/ap'&&r.method==='POST'),'An exited save must not continue with the AP password');assert.deepEqual(s.errors,[]);
   }finally{if(blocked)await blocked.abort().catch(()=>{});await s.close();}
  }
  {
   const s=await instance(browser,{step:0,host:'192.168.4.1'});try{
    await s.page.locator('#networkChoice').selectOption('1');await s.page.locator('#setupNext').click();await s.page.waitForFunction(()=>wizard.waiting);
    assert(await s.page.locator('#setupBack').isDisabled());assert(await s.page.locator('#setupPause').isDisabled());assert(await s.page.locator('#setupExit').isEnabled());
    await s.rpc('POST','/__fixture/reboot');await s.page.evaluate(()=>pollSetup());await s.page.waitForFunction(()=>!wizard.waiting&&wizard.network?.trialBooted);
    await s.page.locator('#networkConfirm').click();await s.page.waitForFunction(()=>!wizard.network.trial);assert(!(await s.rpc('GET','/api/network')).body.trial);await snapshot(s,'network-confirmed');
   }finally{await s.close();}
  }
  {
   const s=await instance(browser,{step:5});try{
    await s.page.locator('#azmc').click();await s.page.waitForFunction(()=>window._azCal?.profile==='custom');
    await s.page.locator('#azRangeForm-span').fill('360');assert(await s.page.locator('#b2_azf').isDisabled());
    await s.page.locator('#azRangeForm button[type=submit]').click();await s.page.waitForFunction(()=>window._azCal?.end===360&&!document.getElementById('azRangeForm').dataset.dirty);
    await s.page.locator('#b2_azf').click();await s.page.waitForFunction(()=>window._forkState.az2pf>=0);
    await s.page.locator('#setupNext').click();await s.page.waitForFunction(()=>!wizard.busy);assert((await s.rpc('GET','/api/status')).body.az2pf>=0,'Next must not erase the captured endpoint');assert.deepEqual(s.errors,[]);
   }finally{await s.close();}
  }
  {
   const s=await instance(browser,{paused:true});try{
    let reads=0,phase='idle',measured=0;
    await s.page.route('**/api/motion/coast',route=>{const req=route.request();if(req.method()==='GET')++reads;if(req.method()==='POST'&&new URLSearchParams(req.postData()).get('action')==='start'){phase='running';setTimeout(()=>{phase='complete';measured=3},150);}return route.fulfill({json:{phase,axis:0,reason:'',results:[{degrees:[1,1],seconds:[1,1],measured},{degrees:[0,0],seconds:[0,0],measured:0}]}});});
    await s.page.locator('#settingsTab').click();await s.page.locator('#motionSettings > details > summary').click();await s.page.locator('#coastForm button[value="0"]').click();
    await s.page.waitForFunction(()=>!document.getElementById('coastApply').disabled);assert(reads>0);await s.page.locator('#coastAxis').selectOption('1');assert(await s.page.locator('#coastApply').isDisabled());await snapshot(s,'coast-complete');
   }finally{await s.close();}
  }
  console.log('PASS: bounded Wi-Fi save, exit cancels remaining saves, reboot/confirmation, range-before-capture, standalone coast polling');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
