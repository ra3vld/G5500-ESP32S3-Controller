// One recovery network form, reached through the guide or manual settings.
// Run after check_native.py device_api_tests; all writes use its host fixture.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const {spawn}=require('node:child_process'),readline=require('node:readline');
const fs=require('node:fs'),path=require('node:path'),zlib=require('node:zlib'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),binary=process.env.DEVICE_API_TEST_BINARY;
if(!binary)throw Error('Set DEVICE_API_TEST_BINARY to the native device_api_tests executable');
const header=fs.readFileSync(process.env.HARDWARE_PAGE_ASSET_SOURCE||path.join(root,'G5500_Fork/web_assets.hpp'),'utf8');
const array=header.match(/HARDWARE_PAGE_GZIP\[\] PROGMEM = \{([\s\S]*?)\};/)[1];
const html=zlib.gunzipSync(Buffer.from([...array.matchAll(/0x([0-9a-f]{2})/g)].map(m=>parseInt(m[1],16)))).toString();
const out=process.env.ARTIFACT_DIR;if(out)fs.mkdirSync(out,{recursive:true});

async function instance(browser,width,recovery=true){
 const proc=spawn(binary,['--serve-http'],{cwd:root,stdio:['pipe','pipe','inherit']}),pending=[],trace=[],errors=[];
 readline.createInterface({input:proc.stdout}).on('line',line=>{
  const task=pending.shift();if(!task)return;
  try{task.resolve(JSON.parse(line));}catch(e){task.reject(e);}
 });
 proc.on('exit',code=>{for(const task of pending.splice(0))task.reject(Error('Native fixture exited '+code));});
 const started=Date.now();let queue=Promise.resolve(),failNetwork=false,blockNetwork=false,releaseNetwork=null;
 const rpc=(method,url,body='')=>{
  const task=queue.then(()=>new Promise((resolve,reject)=>{
   pending.push({resolve,reject});proc.stdin.write(`${10001+Date.now()-started}\t${method}\t${url}\t${body}\n`);
  }));queue=task.catch(()=>{});return task;
 };
 await rpc('POST','/__fixture/controller');
 if(recovery)await rpc('POST','/__fixture/recovery');
 const page=await browser.newPage({viewport:{width,height:950},locale:'ru-RU'});page.setDefaultTimeout(10000);
 page.on('pageerror',e=>errors.push(e.message));
 await page.route('http://recovery-network.test/**',async route=>{
  const req=route.request(),url=new URL(req.url()),body=req.method()==='GET'?url.search.slice(1):req.postData()||'';
  if(['/hardware','/recovery'].includes(url.pathname))return route.fulfill({contentType:'text/html',body:html});
  if(url.pathname==='/favicon.ico')return route.fulfill({status:204});
  if(req.method()==='POST'&&url.pathname==='/api/hardware/network'){
   if(failNetwork){failNetwork=false;return route.fulfill({status:503,json:{ok:false,error:'network_storage_failed'}});}
   if(blockNetwork){blockNetwork=false;await new Promise(resolve=>{releaseNetwork=resolve;});releaseNetwork=null;}
  }
  const result=await rpc(req.method(),url.pathname,body);trace.push({path:url.pathname,method:req.method(),body,response:result});
  await route.fulfill({status:result.status,contentType:'application/json',body:JSON.stringify(result.body)});
 });
 await page.goto('http://recovery-network.test/'+(recovery?'recovery':'hardware'));
 await page.waitForFunction(()=>typeof state!=='undefined'&&state&&metadata&&!busy);
 return {page,rpc,trace,errors,fail:()=>{failNetwork=true;},block:()=>{blockNetwork=true;},blocked:()=>!!releaseNetwork,
  release:()=>releaseNetwork?.(),close:async()=>{releaseNetwork?.();await page.close();proc.stdin.end();proc.kill();}};
}

(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{for(const width of [390,1100]){
  const s=await instance(browser,width);const {page}=s;
  try{
   const manualToggle=page.locator('#recovery-manual-open'),networkToggle=page.locator('#recovery-network-open');
   assert.equal(await manualToggle.getAttribute('aria-expanded'),'false');
   assert.equal(await networkToggle.getAttribute('aria-expanded'),'false');
   assert(await page.locator('#recovery-resume-reason').isHidden());
   const noConfirmed=await page.evaluate(()=>{
    const original=state;state={...state,hasConfirmed:false};buttons();
    const result={disabled:document.getElementById('recovery-resume').disabled,reason:document.getElementById('recovery-resume-reason').textContent,visible:!document.getElementById('recovery-resume-reason').hidden};
    state=original;buttons();return result;
   });
   assert(noConfirmed.disabled&&noConfirmed.visible);assert.match(noConfirmed.reason,/Нет подтверждённых настроек/);
   await page.locator('#recovery-manual-open').click();
   assert.equal(await manualToggle.getAttribute('aria-expanded'),'true');
   await manualToggle.click();assert(await page.locator('#manual-settings').isHidden());
   assert.equal(await manualToggle.getAttribute('aria-expanded'),'false');
   await manualToggle.click();
   const fold=page.locator('details.settings-fold').filter({has:page.locator('[name=bluetoothEnabled]')});
   await fold.locator(':scope > summary').click();
   assert.equal(await fold.locator('#recovery-network').count(),1,'Wi-Fi settings must also be in manual Network and Bluetooth');
   await page.waitForFunction(()=>recoveryNetworkLoaded);
   assert.equal(await networkToggle.getAttribute('aria-expanded'),'true');
   assert(await page.locator('#recovery-network').isVisible());
   assert.equal(await page.locator('#recovery-network').count(),1);
   assert(await page.evaluate(()=>form.elements.bluetoothEnabled.form===form&&recoveryNetwork.elements.ssid.form===recoveryNetwork));
   assert.equal(await page.locator('form form').count(),0,'Separate forms must not become nested');
   await page.locator('#recovery-network [name=choice]').selectOption('0');
   await page.locator('#recovery-network [name=ssid]').fill('Draft-router');
   await page.locator('#recovery-network [name=pass]').fill('password123');
   assert.equal(await page.evaluate(()=>dirty),false,'Editing Wi-Fi must not mark hardware as edited');
   const reads=s.trace.filter(r=>r.path==='/api/hardware/network'&&r.method==='GET').length;
   await fold.locator(':scope > summary').click();
   await page.locator('#recovery-network-open').click();
   assert(await page.locator('#recovery-network').isVisible());
   assert.equal(await page.locator('#recovery-network [name=ssid]').inputValue(),'Draft-router');
   assert.equal(s.trace.filter(r=>r.path==='/api/hardware/network'&&r.method==='GET').length,reads,'Shortcut must reuse the loaded draft');
   await networkToggle.click();assert(await page.locator('#recovery-network').isHidden());
   assert.equal(await networkToggle.getAttribute('aria-expanded'),'false');
   await networkToggle.click();assert(await page.locator('#recovery-network').isVisible());
   assert.equal(await page.locator('#recovery-network [name=ssid]').inputValue(),'Draft-router');
   await manualToggle.click();assert(await page.locator('#manual-settings').isHidden());
   assert.equal(await networkToggle.getAttribute('aria-expanded'),'false');
   await manualToggle.click();assert(await page.locator('#recovery-network').isVisible());
   await page.locator('[name=forceApSta]').selectOption('1');
   assert.equal(await page.evaluate(()=>dirty),true,'External hardware controls must still edit hardware');
   assert(await page.locator('#recovery-resume').isDisabled(),'Start must not discard unsaved hardware changes');
   assert.match(await page.locator('#recovery-resume-reason').textContent(),/несохранённые изменения оборудования/);
   assert(await page.locator('#recovery-resume-reason').isVisible());
   assert(!(await page.evaluate(()=>new FormData(form).has('ssid'))));
   s.block();await page.locator('#recovery-network button[type=submit]').click();
   await page.waitForFunction(()=>recoveryNetworkBusy);
   const deadline=Date.now()+5000;while(!s.blocked()&&Date.now()<deadline)await new Promise(r=>setTimeout(r,10));
   assert(s.blocked());assert(await page.locator('#save').isDisabled());assert(await page.locator('#recovery-resume').isDisabled());
   assert.match(await page.locator('#recovery-resume-reason').textContent(),/Сохраняются настройки сети/);
   await page.locator('#hardwareStop').click();
   await page.waitForFunction(()=>document.getElementById('hardwareStopStatus').textContent==='STOP принят');
   s.release();await page.waitForFunction(()=>!recoveryNetworkBusy);
   assert.match(await page.locator('#recovery-network-message').textContent(),/Сеть сохранена/);
   const network=(await s.rpc('GET','/api/hardware/network')).body;
   assert.equal(network.ssid,'Draft-router');assert.equal(network.choice,0);assert.equal(network.wifi.mode,'dhcp');
   assert((await s.rpc('GET','/api/hardware')).body.recovery,'Saving network alone must keep recovery');
   assert.equal(s.trace.filter(r=>r.path==='/api/hardware'&&r.method==='POST').length,0,'Network submit must not submit hardware');
   assert.equal(await page.locator('[name=forceApSta]').inputValue(),'1','Hardware draft must survive network save');
   await page.locator('#recovery-network [name=choice]').selectOption('1');
   await page.locator('#recovery-network [name=apPass]').fill('newap123');
   s.fail();await page.locator('#recovery-network button[type=submit]').click();
   await page.waitForFunction(()=>!recoveryNetworkBusy);
   assert.match(await page.locator('#recovery-network-message').textContent(),/Исправление не завершено/);
   assert.equal(await page.locator('#recovery-network [name=apPass]').inputValue(),'newap123');
   await networkToggle.click();await networkToggle.click();
   await page.locator('#recovery-network button[type=submit]').click();
   await page.waitForFunction(()=>!recoveryNetworkBusy);
   assert.equal((await s.rpc('GET','/api/hardware/network')).body.choice,1);
   assert.match(await page.locator('#recovery-network-message').textContent(),/Сеть сохранена/);
   assert.deepEqual(s.errors,[]);assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   if(out)await fold.screenshot({path:path.join(out,`network-${width}.png`)});
   assert.equal(await page.evaluate(()=>form.elements.bluetoothEnabled.value),'0');
   await page.locator('#save').click();
   await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено.'));
   const saved=s.trace.filter(r=>r.path==='/api/hardware'&&r.method==='POST');assert.equal(saved.length,1);
   assert.equal(new URLSearchParams(saved[0].body).get('bluetoothEnabled'),'0');
   assert.equal(new URLSearchParams(saved[0].body).get('forceApSta'),'1');
   await page.waitForFunction(()=>!busy);
   assert(await page.locator('#recovery-resume').isDisabled());
   assert.match(await page.locator('#recovery-resume-reason').textContent(),/Ожидаем перезапуск/);
   console.log(`PASS ${width}px: disclosure toggles/ARIA, visible block reasons, shared draft, DHCP/AP/errors, independent hardware form and STOP`);
  }finally{await s.close();}
 }
 const normal=await instance(browser,1100,false);
 try{
  const {page}=normal,fold=page.locator('details.settings-fold').filter({has:page.locator('[name=bluetoothEnabled]')});
  await fold.locator(':scope > summary').click();assert(await page.locator('#recovery-network').isHidden());
  assert(await page.evaluate(()=>form.contains(hardwareNetwork)),'Normal hardware layout must retain its section order');
  await page.locator('[name=forceApSta]').selectOption('1');
  assert.equal(await page.evaluate(()=>dirty),true);assert.equal(await page.evaluate(()=>form.elements.forceApSta.value),'1');
  assert.deepEqual(normal.errors,[]);console.log('PASS normal hardware: external control ownership and input handling');
 }finally{await normal.close();}
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
