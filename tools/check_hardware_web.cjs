// GPIO editing and trial/recovery workflow, with production board capability fixtures.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined),headless:true,args:['--no-sandbox']});
 try{
  for(const profile of ['1','2','3','4','5','az',...['1','2','3','4'].flatMap(board=>[board+'-d1',board+'-d2'])]) for(const mobile of [false,true]){
   const fixture=JSON.parse(fs.readFileSync(path.join(root,`build/pin-map-${profile}.json`),'utf8'));
   const config={driveInterface:0,simulation:1,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:fixture.buttonGpio,buttonActiveLow:1,buttonPullup:1,pwmInverted:0,pins:{...fixture.pins}};
   let state={token:123,generation:2,recovery:true,trial:false,ready:false,reason:'low_memory',failureCount:2,failureLimit:3,failureResetMs:1800000,remainingMs:120000,heapFree:3000,heapMaxBlock:512,backend:'NONE',sppSupported:!!fixture.classicEsp32,classicEsp32:!!fixture.classicEsp32,crowpanel:!!fixture.crowpanel,crowpanelLocalDrive:profile==='az',separateDrive:profile!=='az',btsDrive:true,hasConfirmed:true,configured:config},probeFails=false;
   const requests=[],errors=[];
   const page=await browser.newPage({viewport:{width:mobile?360:1100,height:900},locale:'ru-RU'});
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://hardware.test/**',async route=>{
    const req=route.request(),u=new URL(req.url());
    if(u.pathname==='/hardware'||u.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:html});
    if(req.method()==='GET')return route.fulfill({contentType:'application/json',body:JSON.stringify(u.pathname==='/api/hardware/pins'?fixture.metadata:state)});
    const body=Object.fromEntries(new URLSearchParams(req.postData()));requests.push({path:u.pathname,body});
    assert.equal(body.token,'123');assert.equal(body.generation,'2');
    const failed=probeFails&&u.pathname==='/api/hardware/probe';
    return route.fulfill({status:failed?409:200,contentType:'application/json',body:JSON.stringify(failed?{error:'not_ready'}:{ok:true})});
   });
   await page.goto('http://hardware.test/recovery');
   await page.waitForFunction(()=>!document.getElementById('save').disabled);
   assert.match(await page.locator('#state').textContent(),/восстановления/);
   assert.match(await page.locator('#failures').textContent(),/Сбои: 2 \/ 3/);
   assert.match(await page.locator('#failures').textContent(),/30 минут/);
   assert(await page.locator('#confirm').isDisabled());
   // Every board accepts an incomplete motor map in simulation, rejects it in physical mode.
   await page.locator('[name="pin.azAdc"]').selectOption('-1');
   await page.evaluate(()=>refresh());
   assert.equal(await page.locator('[name="pin.azAdc"]').inputValue(),'-1');
   assert(!(await page.locator('#save').isDisabled()));
   await page.locator('[name=simulation]').selectOption('0');
   assert(await page.locator('#save').isDisabled());
   assert.match(await page.locator('#pin-validation').textContent(),/Обратная связь AZ/);
   await page.locator('[name=simulation]').selectOption('1');
   if(fixture.metadata.displayKind===1){
    await page.locator('[name="pin.tftCs"]').selectOption('-1');assert(await page.locator('#save').isDisabled());
    await page.locator('[name="pin.tftCs"]').selectOption(String(fixture.pins.tftCs));
    await page.locator('[name="pin.tftRst"]').selectOption('-1');
    await page.locator('[name="pin.touchIrq"]').selectOption('-1');
    assert(!(await page.locator('#save').isDisabled()));
   }
   if(fixture.metadata.displayKind===2){
    await page.locator('[name="pin.lcdE"]').selectOption('-1');assert(await page.locator('#save').isDisabled());
    await page.locator('[name="pin.lcdE"]').selectOption(String(fixture.pins.lcdE));
   }
   if(profile==='1'||profile.startsWith('1-d')){
    await page.locator('[name="pin.led"]').selectOption('46');
    assert(!(await page.locator('#save').isDisabled()));
    await page.locator('[name="pin.led"]').selectOption('-1');
   }
   // GPIO0 is available for an external button even on CrowPanel.
   await page.locator('[name=buttonGpio]').fill('0');
   assert(!(await page.locator('#save').isDisabled()));
   assert.equal(await page.locator('[name=buttonGpio]').getAttribute('readonly'),null);
   await page.evaluate(()=>{const el=form.elements['pin.led'];el.value='0';el.dispatchEvent(new Event('input',{bubbles:true}));});
   assert(await page.locator('#save').isDisabled());
   assert.match(await page.locator('#pin-validation').textContent(),/занят/);
   await page.locator('[name=buttonGpio]').fill('-1');
   assert(!(await page.locator('#save').isDisabled()));
   // An integrated peripheral cannot be overwritten, including by a forced form value.
   if(fixture.crowpanel){
    const occupied=await page.locator('[name="pin.led"] option[value="1"]').evaluate(el=>el.disabled);
    assert.equal(occupied,true);
    await page.evaluate(()=>{const el=form.elements['pin.led'];el.value='1';el.dispatchEvent(new Event('input',{bubbles:true}));});
    assert(await page.locator('#save').isDisabled());
    assert.match(await page.locator('#pin-validation').textContent(),/дисплей CrowPanel/);
    await page.locator('[name="pin.led"]').selectOption('0');
   }
   await page.locator('#save').click();
   await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено'));
   assert.equal(requests.at(-1).path,'/api/hardware');
   assert.equal(requests.at(-1).body['pin.led'],'0');
   assert.equal(requests.at(-1).body['pin.azAdc'],'-1');
   assert.equal(requests.at(-1).body.buttonGpio,'-1');
   assert.equal(requests.at(-1).body.simulation,'1');
   await page.evaluate(()=>{busy=false;dirty=false;});
   state={...state,recovery:false,trial:true,ready:true,failureCount:0,heapFree:40000,heapMaxBlock:16000};
   probeFails=true;await page.evaluate(()=>refresh());
   assert.match(await page.locator('#failures').textContent(),/Сбои: 0 \/ 3/);
   assert(await page.locator('#confirm').isDisabled()); // light status alone never authorizes confirmation
   probeFails=false;await page.evaluate(()=>refresh());
   assert(!(await page.locator('#confirm').isDisabled()));
   await page.locator('#confirm').click();
   await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено'));
   assert.equal(requests.at(-1).path,'/api/hardware/confirm');
   assert(requests.some(r=>r.path==='/api/hardware/probe'));
   await page.evaluate(()=>{busy=false;probe=false;});
   state={...state,recovery:true,trial:false,hasConfirmed:false};await page.evaluate(()=>refresh());
   assert(await page.locator('#rollback').isDisabled());
   state={...state,failureCount:undefined};await page.evaluate(()=>refresh());
   assert.equal(await page.locator('#failures').textContent(),''); // older firmware remains readable
   state={...state,failureCount:3};await page.evaluate(()=>refresh());
   assert.match(await page.locator('#failures').textContent(),/Сбои: 3 \/ 3/);
   await page.locator('#reset-pins').click();
   assert.equal(await page.locator('[name="pin.azAdc"]').inputValue(),String(fixture.pins.azAdc));
   assert.equal(await page.locator('[name=bluetoothMode] option[value="2"]').evaluate(el=>el.disabled),!fixture.classicEsp32);
   if(profile==='az'){
    assert.equal(await page.locator('[name=driveInterface] option[value="1"]').evaluate(el=>el.disabled),true);
    assert.equal(await page.locator('[name=bluetoothEnabled]').isDisabled(),true);
    assert.equal(await page.locator('[name=bluetoothEnabled]').inputValue(),'0');
   }
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   assert.deepEqual(errors,[]);
   state={...state,recovery:false,trial:false,reason:'none',hasConfirmed:true};await page.evaluate(()=>{dirty=false;document.getElementById('message').textContent='';});await page.evaluate(()=>refresh());
   if(process.env.HARDWARE_SCREENSHOT_DIR){fs.mkdirSync(process.env.HARDWARE_SCREENSHOT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.HARDWARE_SCREENSHOT_DIR,`hardware-${profile}-${mobile?'mobile':'desktop'}.png`),fullPage:true});}
   if(process.env.HARDWARE_DOC_SCREENSHOT&&profile==='1'&&!mobile)await page.screenshot({path:process.env.HARDWARE_DOC_SCREENSHOT,fullPage:true});
   await page.close();
  }
  console.log('PASS: 14 board/display configurations, mobile/desktop GPIO editing, TFT/LCD completeness, simulation, CrowPanel reservations, dirty fields, defaults and trial/probe/confirm');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
