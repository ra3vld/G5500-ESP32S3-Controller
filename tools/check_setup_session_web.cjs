// Browser/production DeviceApi contract: saves, tuning-step transitions and motion permits.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const {spawn}=require('node:child_process'),readline=require('node:readline');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),binary=process.env.DEVICE_API_TEST_BINARY;
if(!binary)throw Error('Set DEVICE_API_TEST_BINARY to the native device_api_tests executable');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const hardware=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
const wait=ms=>new Promise(r=>setTimeout(r,ms));
if(process.env.ARTIFACT_DIR)fs.mkdirSync(process.env.ARTIFACT_DIR,{recursive:true});
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{for(const width of [390,1100]){
  const proc=spawn(binary,['--serve-http'],{cwd:root,stdio:['pipe','pipe','inherit']}),pending=[];
  readline.createInterface({input:proc.stdout}).on('line',line=>{const p=pending.shift();if(!p)throw Error('Unexpected fixture output: '+line);try{p.resolve(JSON.parse(line));}catch(e){p.reject(e);}});
  proc.on('exit',code=>{for(const p of pending.splice(0))p.reject(Error('Native fixture exited '+code));});
  const started=Date.now(),trace=[],errors=[];let queue=Promise.resolve(),saveDelay=0,armDelay=0,renewDelay=0,holdReplyDelay=0;
  const rpc=(method,route,body='')=>{
   const task=queue.then(()=>new Promise((resolve,reject)=>{pending.push({resolve,reject});proc.stdin.write(`${10001+Date.now()-started}\t${method}\t${route}\t${body}\n`);}));
   queue=task.catch(()=>{});return task;
  };
  const hardwarePolling=process.env.SETUP_SESSION_SCENARIO==='hardware-polling',pendingPolls={},maxPolls={};let delayed=true;
  const page=await browser.newPage({viewport:{width,height:1000},locale:'ru-RU'});page.on('pageerror',e=>errors.push(e.message));
  try{
   await page.route('http://setup-session.test/**',async route=>{
    const req=route.request(),url=new URL(req.url()),body=req.postData()||url.search.slice(1),form=new URLSearchParams(body);
    if(url.pathname==='/setup'||url.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
    if(url.pathname==='/hardware')return route.fulfill({contentType:'text/html',body:hardware});
    if(url.pathname==='/favicon.ico')return route.fulfill({status:204,body:''});
    if(url.pathname==='/api/settings'&&req.method()==='POST'&&saveDelay){const delay=saveDelay;saveDelay=0;await wait(delay);}
    if(url.pathname==='/api/setup'&&form.get('action')==='arm'&&armDelay){const delay=armDelay;armDelay=0;await wait(delay);}
    if(url.pathname==='/api/setup'&&form.get('action')==='renew'&&renewDelay)await wait(renewDelay);
    const slow=hardwarePolling&&req.method()==='GET'&&req.frame().parentFrame()&&['/api/hardware','/api/hardware/modbus','/api/hardware/pins'].includes(url.pathname),key=url.pathname;
    if(slow){pendingPolls[key]=(pendingPolls[key]||0)+1;maxPolls[key]=Math.max(maxPolls[key]||0,pendingPolls[key]);}
    try{
     if(slow&&delayed)await wait(3200);
     const result=await rpc(req.method(),url.pathname,body);trace.push({path:url.pathname,query:url.search,...result,response:result.body,body:Object.fromEntries(form)});
     if(url.pathname==='/api/move'&&form.get('d')==='H'&&holdReplyDelay){
      const delay=holdReplyDelay;holdReplyDelay=0;await wait(delay);
      return route.fulfill({status:503,contentType:'application/json',body:JSON.stringify({error:'delayed_hold_failure'})});
     }
     await route.fulfill({status:result.status,contentType:'application/json',body:JSON.stringify(result.body)});
    }finally{if(slow)--pendingPolls[key];}
   });
   if(['hud-role','hud-hardware','hud-review','hud-fields'].includes(process.env.SETUP_SESSION_SCENARIO))await rpc('POST','/__fixture/hud');
   if(['simulation','motion','optional-drive','controller-fields','passive','audit-fixes'].includes(process.env.SETUP_SESSION_SCENARIO))await rpc('POST','/__fixture/controller');
   if(process.env.SETUP_SESSION_SCENARIO==='trial-inline'){
    await rpc('POST','/api/setup','action=pause');
    const stage=async()=>{const h=(await rpc('GET','/api/hardware')).body;assert.equal((await rpc('POST','/api/hardware',new URLSearchParams({token:h.token,generation:h.generation,simulation:1,buttonActiveLow:h.configured.buttonActiveLow?0:1}).toString())).status,200);await rpc('POST','/__fixture/reboot');};
    await stage();await page.goto('http://setup-session.test/');
    await page.waitForFunction(()=>!!window._forkState?.hardwareTrial&&!wizard.active);
    const banner=page.locator('#hardwareTrialNotice'),confirm=page.locator('#hardwareTrialConfirm');
    assert(await banner.isVisible());assert(await confirm.isEnabled());
    assert.match(await banner.textContent(),/Была изменена конфигурация/);assert.equal(await confirm.textContent(),'Подтверждаю, что всё ок');
    assert(!(await rpc('GET','/api/status')).body.configurationLocked);
    const before=await page.locator('#hardwareTrialCountdown').textContent();await wait(1200);assert.notEqual(await page.locator('#hardwareTrialCountdown').textContent(),before);
    assert(await page.locator('.movebtn[data-dir=R]').isEnabled());
    assert.equal((await rpc('POST','/api/move','d=R')).status,200);
    await page.route('**/api/hardware/confirm',async route=>{await page.unroute('**/api/hardware/confirm');await route.fulfill({status:503,contentType:'application/json',body:JSON.stringify({error:'storage_failed'})});});
    await confirm.click();await page.waitForFunction(()=>!hardwareTrialBusy);
    assert(await banner.isVisible());assert.match(await page.locator('#hardwareTrialMessage').textContent(),/Не удалось сохранить/);
    assert((await rpc('GET','/api/status')).moving);
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    if(process.env.ARTIFACT_DIR)await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`trial-control-${width}.png`)});
    await confirm.click();await page.waitForFunction(()=>!hardwareTrialBusy&&!window._forkState.hardwareTrial);
    assert(await banner.isHidden());let status=await rpc('GET','/api/status');assert(status.moving&&!status.body.configurationLocked);
    await stage();await page.reload();await page.waitForFunction(()=>!!window._forkState?.hardwareTrial);
    await page.locator('#settingsTab').click();assert(await banner.isVisible());
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    if(process.env.ARTIFACT_DIR)await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`trial-settings-${width}.png`)});
    await confirm.click();await page.waitForFunction(()=>!hardwareTrialBusy&&!window._forkState.hardwareTrial);assert(await banner.isHidden());
    assert.equal(new URL(page.url()).pathname,'/');assert(!page.frames().some(f=>f.url().includes('/hardware')));
    assert.deepEqual(errors,[]);console.log(`PASS: ${width}px, inline trial countdown, movement during trial, failed confirmation retry, live confirmation on control/settings without reboot or hardware page`);continue;
   }
   if(process.env.SETUP_SESSION_SCENARIO==='passive'){
    await rpc('POST','/api/setup','action=pause');
    assert.equal((await rpc('POST','/api/move','d=R')).status,200);
    await page.goto('http://setup-session.test/');
    await page.waitForFunction(()=>wizard.loaded&&!wizard.active&&!!window._forkState);
    const reads=()=>trace.filter(r=>r.path==='/api/setup'&&r.method!=='POST'&&!r.body.action).length;
    const initialReads=reads();await wait(2800);assert.equal(reads(),initialReads,'Paused wizard must not add periodic API requests');
    assert((await rpc('GET','/api/setup')).moving);
    await page.evaluate(()=>forkPost('/api/settings',{rotorName:'Ordinary settings'}));
    let current=await rpc('GET','/api/setup');assert(current.moving&&current.body.paused&&current.body.step===1);
    await page.goto('http://setup-session.test/setup');
    await page.waitForFunction(()=>wizard.active&&wizard.state?.token&&wizard.state.step===1);
    assert((await rpc('GET','/api/setup')).moving,'Opening/resuming the guide must not stop existing movement');
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===2);
    assert((await rpc('GET','/api/setup')).moving,'Next must not stop existing movement');
    await page.locator('#setupBack').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===1);
    assert((await rpc('GET','/api/setup')).moving,'Back must not stop existing movement');
    await page.locator('#setupPause').click();await page.waitForURL('http://setup-session.test/#control');
    await page.waitForFunction(()=>wizard.loaded&&!wizard.active);
    current=await rpc('GET','/api/setup');assert(current.moving&&current.body.paused&&current.body.step===1);
    const pausedReads=reads();await wait(2800);assert.equal(reads(),pausedReads);
    const warning=await page.evaluate(()=>{selectTab('control',false);renderStatus({...window._forkState,configurationLocked:false,setupRequired:true,hardwareTrial:{remainingMs:254000,ready:true}});return !!document.getElementById('hardwareTrialNotice').getClientRects().length;});
    assert(warning,'A bookmark must not hide actual hardware warnings');
    if(process.env.ARTIFACT_DIR)await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`passive-${width}.png`)});
    assert.deepEqual(errors,[]);
    assert.equal((await rpc('POST','/api/move','d=S')).status,200);
    console.log(`PASS: ${width}px, paused wizard has no background polling, normal settings preserve checkpoint, resume/Next/Back/pause preserve existing motion, hardware warnings remain visible`);continue;
   }
   await page.goto('http://setup-session.test/setup');
   await page.waitForFunction(()=>wizard.active&&wizard.state?.token&&wizard.state.step===1);
   const session=await page.evaluate(()=>wizard.state.token);
   if(process.env.SETUP_SESSION_SCENARIO==='audit-fixes'){
    await page.locator('#setupDeviceName').fill('Saved A');saveDelay=1500;
    await page.locator('#setupNext').click();await page.waitForFunction(()=>wizard.busy);await wait(200);
    assert(await page.locator('#setupDeviceName').isEnabled());
    await page.locator('#setupDeviceName').fill('New draft B');await page.locator('#setupTitle').click();
    await page.waitForFunction(()=>!wizard.busy);
    assert.equal(await page.evaluate(()=>wizard.state.step),1);
    assert.equal(await page.locator('#setupDeviceName').inputValue(),'New draft B');
    assert.equal(await page.evaluate(()=>nameForm.dataset.dirty),'1');
    assert.match(await page.locator('#setupMessage').textContent(),/новые изменения/);
    assert.equal((await rpc('GET','/api/status')).body.rotorName,'Saved A');
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===2);
    assert.equal((await rpc('GET','/api/status')).body.rotorName,'New draft B');
    await page.locator('#setupBack').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===1);
    assert.equal(await page.locator('#setupDeviceName').inputValue(),'New draft B');
    await rpc('POST','/__fixture/step','9');await page.evaluate(()=>pollSetup());await page.evaluate(()=>upd(true));
    await page.evaluate(()=>{
     window.auditTimers=new Set();
     const create=window.setInterval.bind(window),remove=window.clearInterval.bind(window);
     window.setInterval=(fn,delay,...args)=>{const id=create(fn,delay,...args);if(delay===250)window.auditTimers.add(id);return id;};
     window.clearInterval=id=>{window.auditTimers.delete(id);return remove(id);};
    });
    const button=page.locator('[data-setup-axis="0"][data-direction="1"]');
    for(const [first,second] of [['Space','Enter'],['Enter','Space']]){
     await button.focus();await page.keyboard.down(first);await wait(350);
     const gesture=await page.evaluate(()=>testStream.gesture);
     await page.keyboard.down(second);await wait(350);
     assert.equal(await page.evaluate(()=>testStream.gesture),gesture);
     assert.equal(await page.evaluate(()=>window.auditTimers.size),1);
     await page.keyboard.up(second);await wait(100);
     assert.equal(await page.evaluate(()=>testStream.active?.owner),'key:'+first);
     assert((await rpc('GET','/api/setup')).moving);
     await page.keyboard.up(first);await wait(150);
     assert.equal(await page.evaluate(()=>window.auditTimers.size),0);
     assert(!(await rpc('GET','/api/setup')).moving);
    }
    await button.scrollIntoViewIfNeeded();
    const box=await button.boundingBox();await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await wait(350);
    const pointerGesture=await page.evaluate(()=>testStream.gesture);
    await page.keyboard.down('Space');await page.keyboard.up('Space');await wait(100);
    assert.equal(await page.evaluate(()=>testStream.gesture),pointerGesture);
    assert.match(await page.evaluate(()=>testStream.active?.owner),/^pointer:/);
    assert.equal(await page.evaluate(()=>window.auditTimers.size),1);
    assert((await rpc('GET','/api/setup')).moving);
    await page.mouse.up();await wait(150);
    assert.equal(await page.evaluate(()=>window.auditTimers.size),0);
    assert(!(await rpc('GET','/api/setup')).moving);
    // A failure from an already released gesture cannot cancel the next one.
    holdReplyDelay=1300;await button.focus();await page.keyboard.down('Space');
    await page.waitForFunction(()=>!!testStream.active?.pending);await wait(150);
    const oldGesture=await page.evaluate(()=>testStream.gesture);
    await page.keyboard.up('Space');await page.keyboard.down('Enter');await wait(1600);
    assert.equal(await page.evaluate(()=>testStream.gesture),oldGesture+1);
    assert.equal(await page.evaluate(()=>testStream.active?.owner),'key:Enter');
    assert.equal(await page.evaluate(()=>window.auditTimers.size),1);
    assert((await rpc('GET','/api/setup')).moving);
    await page.keyboard.up('Enter');await wait(150);
    assert.equal(await page.evaluate(()=>window.auditTimers.size),0);
    assert(!(await rpc('GET','/api/setup')).moving);
    assert.deepEqual(errors,[]);
    console.log(`PASS: ${width}px, edits during save stay dirty and block navigation until saved; one keyboard HOLD owner/timer, unrelated key release and stale failed response preserve the active gesture`);continue;
   }
   if(process.env.SETUP_SESSION_SCENARIO==='optional-drive'){
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===2);
    await page.locator('#setupDriveKind').selectOption('G800DXA');
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===3);
    const frame=await(await page.locator('#equipmentFrame').elementHandle()).contentFrame();
    await frame.waitForFunction(()=>typeof state!=='undefined'&&!!state&&typeof metadata!=='undefined'&&!!metadata);await page.waitForFunction(()=>wizard.hardwareCanApply,{},{timeout:5000});
    assert.equal(await frame.locator('#pin-validation').textContent(),'');assert(await page.locator('#setupNext').isEnabled());
    await frame.evaluate(()=>{setPinValue(form.elements['pin.led'],form.elements.buttonGpio.value);updatePins();});
    await page.waitForFunction(()=>!wizard.hardwareCanApply&&wizard.hardwareError.includes('GPIO'));
    assert.match(await page.locator('#setupMessage').textContent(),/GPIO/);
    assert.deepEqual(errors,[]);console.log(`PASS: ${width}px, optional drive GPIO stay disconnected; active GPIO errors reach the wizard footer`);continue;
   }
   if(['controller-fields','hud-fields'].includes(process.env.SETUP_SESSION_SCENARIO)){
    await rpc('POST','/__fixture/step','3');await page.evaluate(async()=>{await upd(true);await pollSetup();});
    const frame=await(await page.locator('#equipmentFrame').elementHandle()).contentFrame();
    await frame.waitForFunction(()=>typeof state!=='undefined'&&!!state&&typeof metadata!=='undefined'&&!!metadata);
    await frame.evaluate(()=>{form.elements.bluetoothEnabled.value='0';dirty=true;updatePins();});
    for(let i=0;i<3;++i){
     await frame.evaluate(()=>refresh());await page.evaluate(()=>renderSetup());
     assert(await frame.locator('[name=bluetoothMode]').isHidden(),'Disabled Bluetooth transport must stay hidden');
     assert.equal(await frame.locator('#setup-motion').count(),0);
    }
    if(process.env.ARTIFACT_DIR)await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`${process.env.SETUP_SESSION_SCENARIO}-3-${width}.png`)});
    await rpc('POST','/__fixture/step','4');await page.evaluate(()=>pollSetup());
    await frame.waitForFunction(()=>hardwareStage===4);
    for(let i=0;i<3;++i){
     await frame.evaluate(()=>refresh());
     const shown=await frame.evaluate(()=>[...document.querySelectorAll('fieldset')].filter(f=>f.getClientRects().length).map(f=>f.querySelector('legend')?.textContent));
     assert.deepEqual(shown,['Кнопки и джойстик'],'Capabilities and stage updates must agree on visible sections');
     await page.evaluate(()=>renderSetup());
    }
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));assert.deepEqual(errors,[]);
    if(process.env.ARTIFACT_DIR)await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`${process.env.SETUP_SESSION_SCENARIO}-4-${width}.png`)});
    console.log(`PASS: ${width}px, ${process.env.SETUP_SESSION_SCENARIO}, stable Bluetooth/stage fields across polling and parent messages, no movement-permission block`);continue;
   }
   if(process.env.SETUP_SESSION_SCENARIO==='motion'){
    await rpc('POST','/__fixture/step','3');await page.evaluate(()=>pollSetup());
    const frame=await(await page.locator('#equipmentFrame').elementHandle()).contentFrame();
    await frame.waitForFunction(()=>typeof state!=='undefined'&&!!state&&typeof metadata!=='undefined'&&!!metadata);
    assert.equal(await frame.locator('#setup-motion').count(),0);
    assert(await page.evaluate(()=>wizard.state.required&&wizard.state.motionAllowed));
    assert.equal((await rpc('POST','/api/move','d=R')).status,200);assert((await rpc('GET','/api/setup')).moving);
    assert.equal((await rpc('POST','/api/move','d=S')).status,200);assert(!(await rpc('GET','/api/setup')).moving);
    assert.equal(await page.locator('#setupPause').textContent(),'Сохранить и выйти');
    if(process.env.ARTIFACT_DIR)await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`physical-motion-${width}.png`)});
    await page.locator('#setupPause').click();await page.waitForFunction(()=>location.pathname==='/');
    let state=await rpc('GET','/api/setup');assert(state.body.required&&state.body.paused&&state.body.motionAllowed);
    assert.equal((await rpc('POST','/api/move','d=R')).status,200);assert((await rpc('GET','/api/setup')).moving);
    assert.equal((await rpc('POST','/api/move','d=S')).status,200);assert(!(await rpc('GET','/api/setup')).moving);
    assert.equal(await page.locator('#setupNotice').count(),0);
    assert.equal(await page.getByRole('link',{name:'Продолжить настройку',exact:true}).count(),0);
    await page.goto('http://setup-session.test/hardware');await page.waitForFunction(()=>typeof state!=='undefined'&&!!state);
    assert.equal(await page.locator('#setup-motion').count(),0);assert.deepEqual(errors,[]);
    console.log(`PASS: ${width}px, physical motion works during and after paused wizard, STOP works, permission checkbox/button absent`);continue;
   }
   if(process.env.SETUP_SESSION_SCENARIO==='simulation'){
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===2);
    await page.locator('#setupDriveKind').selectOption('SIMULATION');
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===3);
    const frame=await(await page.locator('#equipmentFrame').elementHandle()).contentFrame();
    await frame.waitForFunction(()=>typeof state!=='undefined'&&!!state&&typeof metadata!=='undefined'&&!!metadata);
    await page.waitForFunction(()=>wizard.hardwareCanApply,{},{timeout:5000});
    assert.equal(await frame.locator('[name=simulation]').inputValue(),'1');
    assert.equal(await frame.locator('#pin-validation').textContent(),'');
    for(let attempt=0;attempt<3&&await page.evaluate(()=>wizard.state.step===3);++attempt){
     await page.locator('#setupNext').click({timeout:5000});await page.waitForFunction(()=>!wizard.busy);
     if(await page.evaluate(()=>wizard.waiting)){
      await rpc('POST','/__fixture/reboot');await page.evaluate(()=>pollSetup());await page.waitForFunction(()=>!wizard.waiting);
     }
    }
    await page.waitForFunction(()=>wizard.state.step===4,{},{timeout:5000});
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===7);
    assert(await page.evaluate(()=>wizard.state.required&&wizard.state.motionAllowed));
    await page.locator('#setupPause').click();await page.waitForFunction(()=>location.pathname==='/');
    assert.equal((await rpc('POST','/api/move','d=R')).status,200);assert((await rpc('GET','/api/setup')).moving);
    assert.equal((await rpc('POST','/api/move','d=S')).status,200);assert(!(await rpc('GET','/api/setup')).moving);
    assert.deepEqual(errors,[]);console.log(`PASS: ${width}px, simulation equipment saves, reboots and advances through inputs to programs`);continue;
   }
   if(process.env.SETUP_SESSION_SCENARIO==='hud-review'){
    await page.waitForFunction(()=>!!window._forkState?.build?.board);
    await rpc('POST','/__fixture/step','9');await page.evaluate(async()=>{await upd(true);await pollSetup();});
    assert(await page.locator('#setupReview').isVisible());
    assert(await page.locator('#setupMovementCheck').isHidden());
    assert(!/Проверьте STOP/.test(await page.locator('#setupHint').textContent()));
    assert(await page.locator('#setupTargetForm').isHidden());
    assert.equal(await page.locator('#setupNext').textContent(),'Завершить настройку');
    assert.equal(await page.locator('[data-setup-axis]:visible').count(),0);
    assert(!trace.some(r=>['/api/move','/api/target'].includes(r.path)));
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    if(process.env.ARTIFACT_DIR){await page.locator('#setupReview').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`hud-review-${width}.png`)});}
    await page.evaluate(()=>{wizard.state.role='controller';window._forkState.deviceRole='controller';renderSetup();});
    assert(await page.locator('#setupMovementCheck').isVisible());assert(await page.locator('#setupTargetForm').isVisible());
    assert.deepEqual(errors,[]);console.log(`PASS: ${width}px, HUD final review hides movement while retaining summary and finish; controller keeps movement checks`);continue;
   }
   if(process.env.SETUP_SESSION_SCENARIO==='hud-hardware'){
    await rpc('POST','/__fixture/step','3');await page.evaluate(async()=>{await upd(true);await pollSetup();});
    let frame=await(await page.locator('#equipmentFrame').elementHandle()).contentFrame();
    await frame.waitForFunction(()=>typeof state!=='undefined'&&!!state&&typeof metadata!=='undefined'&&!!metadata);await frame.locator('[name=forceApSta]').selectOption('1');
    await page.locator('#setupNext').click();await page.waitForFunction(()=>wizard.waiting);
    await rpc('POST','/__fixture/reboot');await page.evaluate(()=>pollSetup());await page.waitForFunction(()=>!wizard.waiting);
    frame=await(await page.locator('#equipmentFrame').elementHandle()).contentFrame();
    await frame.waitForFunction(()=>state?.trial&&!!probe);
    await page.locator('#setupNext').click();
    await page.waitForFunction(()=>!wizard.waiting&&!wizard.busy&&wizard.state.step===4);
    assert(trace.some(r=>r.path==='/api/hardware/confirm'&&r.status===200));
    const hw=await rpc('GET','/api/hardware');assert(!hw.body.trial&&!hw.body.recovery&&hw.body.failureCount===0);
    assert.deepEqual(errors,[]);console.log(`PASS: ${width}px, HUD hardware apply, trial probe, live confirmation without a second reboot, next step without failures`);continue;
   }
   if(process.env.SETUP_SESSION_SCENARIO==='hud-role'){
    await page.waitForFunction(()=>window._forkState?.deviceRole==='hud');
    await page.locator('#setupDeviceName').fill('Remote HUD');
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===3);
    assert.equal((await rpc('GET','/api/status')).body.rotorName,'Remote HUD');
    const saved=trace.find(r=>r.path==='/api/settings'&&r.body.rotorName==='Remote HUD');
    assert(saved&&saved.status===200&&saved.body.setupToken===String(session));
    assert(!trace.some(r=>r.body.action==='arm'||r.body.action==='apply'));
    await page.locator('#setupBack').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===1);
    assert.equal(await page.locator('#setupDeviceName').inputValue(),'Remote HUD');
    assert.deepEqual(errors,[]);
    if(process.env.ARTIFACT_DIR){await page.locator('#setupTitle').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`hud-role-${width}.png`)});fs.writeFileSync(path.join(process.env.ARTIFACT_DIR,`hud-role-${width}.json`),JSON.stringify(trace,null,2));}
    console.log(`PASS: ${width}px, HUD role step: name and setup token accepted by production API, implicit save, Next and Back without restart or motion`);continue;
   }
   if(hardwarePolling){
    await rpc('POST','/__fixture/tuning');await rpc('POST','/__fixture/step','3');await page.evaluate(async()=>{await upd(true);await pollSetup();});
    const frame=await(await page.locator('#equipmentFrame').elementHandle()).contentFrame();
    await frame.waitForFunction(()=>typeof state!=='undefined'&&!!state&&typeof metadata!=='undefined'&&!!metadata);await wait(4500);
    assert.equal(maxPolls['/api/hardware'],1,'Hardware polling must coalesce slow requests');
    assert.equal(maxPolls['/api/hardware/modbus'],1,'Modbus polling must coalesce slow requests');
    assert.equal(maxPolls['/api/hardware/pins'],1,'GPIO metadata must load once while hardware polling waits');
    const field=frame.locator('[name=forceApSta]');await field.selectOption('1');
    delayed=false;await frame.evaluate(()=>Promise.all([refresh(),refresh(),refreshModbus(),refreshModbus()]));
    assert.equal(await field.inputValue(),'1','Background reads must preserve the draft');
    const reads=trace.filter(r=>r.path==='/api/hardware'&&r.query==='?view=form');assert(reads.length>0);
    assert(reads.every(r=>r.response.configured&&!('effective'in r.response)&&!('confirmed'in r.response)));
    await frame.evaluate(()=>{pollError=msg.textContent='Не удалось получить состояние: временный обрыв';});
    await frame.evaluate(()=>refresh());assert.equal(await frame.locator('#message').textContent(),'');
    assert.deepEqual(errors,[]);
    if(process.env.ARTIFACT_DIR){await frame.locator('#effective').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`hardware-polling-${width}.png`)});fs.writeFileSync(path.join(process.env.ARTIFACT_DIR,`hardware-polling-${width}.json`),JSON.stringify({maxPolls,trace},null,2));}
    console.log(`PASS: ${width}px, embedded hardware: 3.2s responses, single pending poll per endpoint, form-only payload, preserved draft and recovered error`);continue;
   }
   if(process.env.SETUP_SESSION_SCENARIO==='tuning'){
    await rpc('POST','/__fixture/tuning');await page.evaluate(()=>pollSetup());
    await page.waitForFunction(()=>wizard.state?.step===6&&!wizard.polling);await page.evaluate(async()=>{await upd(true);await loadMotionSettings();});
    assert.equal(await page.locator('#controlHz').inputValue(),'50');
    await page.locator('#controlHz').selectOption('100');
    await page.locator('[name=azMotionVelocityKp]').fill('1.23');await page.locator('[name=elMotionVelocityKp]').fill('1.34');
    if(process.env.ARTIFACT_DIR){await page.locator('#controlFrequencyForm').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`tuning-6-${width}.png`)});}
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===7,{},{timeout:10000});
    const motion=await rpc('GET','/api/motion'),setup=await rpc('GET','/api/setup');
    assert.equal(motion.body.controlPeriodMs,10);assert.equal(motion.body.axes[0].VelocityKp,1.23);assert.equal(motion.body.axes[1].VelocityKp,1.34);
    assert(!setup.moving&&!setup.testToken&&setup.body.required&&setup.body.step===7);
    if(process.env.ARTIFACT_DIR){await page.locator('#setupTitle').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`tuning-7-${width}.png`)});}
    assert.equal(trace.filter(r=>r.body.action==='arm').length,0,'Saving regulators must not arm a movement test');
    const manual=await rpc('POST','/api/move',new URLSearchParams({d:'R',setupToken:session}).toString());assert.equal(manual.status,200);await rpc('POST','/api/move','d=S');
    await page.locator('#setupBack').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===6);
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===7);
    assert(!trace.some(r=>r.path==='/api/settings'&&r.status!==200));assert.deepEqual(errors,[]);
    if(process.env.ARTIFACT_DIR)fs.writeFileSync(path.join(process.env.ARTIFACT_DIR,`tuning-${width}.json`),JSON.stringify(trace,null,2));
    console.log(`PASS: ${width}px, BTS7960 setup with production DeviceApi: implicit frequency/PID save, Next to programs, revisit without edits, ordinary motion available`);
    continue;
   }
   await wait(1800);
   assert.equal(trace.filter(r=>r.body.action==='renew').length,0,'Editing must not send motion heartbeats');
   await page.locator('#setupDeviceName').fill('Slow save');saveDelay=2300;
   await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===2);
   assert.equal(await page.evaluate(()=>wizard.state.token),session);
   assert.equal((await rpc('GET','/api/status')).body.rotorName,'Slow save');
   assert(!trace.some(r=>r.body.action==='begin_save'&&r.status!==200));
   await page.locator('#setupBack').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===1);
   await page.locator('#setupDeviceName').fill('Draft after hidden tab');
   await page.evaluate(()=>Object.defineProperty(document,'hidden',{configurable:true,get:()=>true}));await wait(1800);
   await page.evaluate(()=>{delete document.hidden;return pollSetup();});
   assert.equal(await page.locator('#setupDeviceName').inputValue(),'Draft after hidden tab');
   assert.equal(await page.evaluate(()=>wizard.state.token),session);
   if(process.env.ARTIFACT_DIR){fs.mkdirSync(process.env.ARTIFACT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`session-name-${width}.png`)});}
   // A hidden hardware iframe must stop both hardware and Modbus HTTP polling.
   await rpc('POST','/__fixture/step','3');await page.evaluate(()=>pollSetup());
   await page.frameLocator('#equipmentFrame').locator('[name="pin.azAdc"]').waitFor({state:'attached'});
   await rpc('POST','/__fixture/step','9');await page.evaluate(()=>pollSetup());
   await wait(500);const count=()=>trace.filter(r=>r.path.startsWith('/api/hardware')).length,before=count();await wait(2300);assert.equal(count(),before,'Hidden hardware polling');
   await page.evaluate(()=>upd(true));
   const button=page.locator('[data-setup-axis="0"][data-direction="1"]');await button.scrollIntoViewIfNeeded();
   const press=async()=>{const r=await button.boundingBox();await page.mouse.move(r.x+r.width/2,r.y+r.height/2);await page.mouse.down();};
   await press();await page.waitForFunction(()=>!!wizard.state.testToken);
   await wait(350);let state=await rpc('GET','/api/setup');assert(state.moving,'Explicit HOLD must move in simulation');
   const old=trace.findLast(r=>r.path==='/api/move'&&r.body.d==='H'&&r.status===200);assert(old);
   await page.mouse.up();await wait(200);state=await rpc('GET','/api/setup');assert(!state.moving,'Release closes the gesture');
   assert(trace.some(r=>r.path==='/api/move'&&r.body.d==='S'&&r.body.client&&r.status===200));
   renewDelay=1900;await press();await wait(2500);state=await rpc('GET','/api/setup');assert(!state.moving&&!state.testToken,'Lost heartbeat cancels movement');
   const arms=trace.filter(r=>r.body.action==='arm').length;renewDelay=0;await wait(1800);
   assert.equal(trace.filter(r=>r.body.action==='arm').length,arms,'Reconnect cannot arm movement');
   assert(!(await rpc('GET','/api/setup')).moving);await page.mouse.up();
   // A new explicit press gets a new permit; an old queued packet remains rejected.
   await press();await wait(350);const current=await rpc('GET','/api/setup');assert(current.moving&&current.testToken!==Number(old.body.setupTestToken));
   const stale=await rpc('POST','/api/move',new URLSearchParams(old.body).toString());assert.equal(stale.status,503);
   await page.mouse.up();await wait(200);assert(!(await rpc('GET','/api/setup')).moving);
   // Releasing while ARM is delayed must not send the delayed initial HOLD.
   armDelay=1900;const holds=()=>trace.filter(r=>r.path==='/api/move'&&r.body.d==='H').length,startHolds=holds();
   await press();await wait(50);await page.mouse.up();await wait(2200);
   assert.equal(holds(),startHolds);assert(!(await rpc('GET','/api/setup')).moving);
   assert.equal(trace.filter(r=>r.body.action==='resume').length,1,'No automatic resume/NVS churn after network stalls');
   assert.deepEqual(errors,[]);
   if(process.env.ARTIFACT_DIR)fs.writeFileSync(path.join(process.env.ARTIFACT_DIR,`session-${width}.json`),JSON.stringify(trace,null,2));
   console.log(`PASS: ${width}px, production DeviceApi: slow save, hidden tab/draft, inactive iframe, STOP/closed HOLD, expired permit, no reconnect replay, delayed ARM release`);
  }catch(e){console.error('STATE',await page.evaluate(()=>({state:wizard.state,error:wizard.error,message:document.getElementById('setupMessage').textContent})));console.error('LAST POSTS',trace.filter(r=>r.body.action||r.path==='/api/settings'||r.path==='/api/move').slice(-12));if(process.env.ARTIFACT_DIR)fs.writeFileSync(path.join(process.env.ARTIFACT_DIR,`failed-${width}.json`),JSON.stringify(trace,null,2));throw e;}finally{await page.close();proc.stdin.end();proc.kill();}
 }}finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
