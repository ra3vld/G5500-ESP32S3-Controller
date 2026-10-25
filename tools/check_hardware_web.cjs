// GPIO editing and trial/recovery workflow, with production board capability fixtures.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined),headless:true,args:['--no-sandbox']});
 try{
  for(const profile of (process.env.HARDWARE_PROFILES?.split(',')||['1','2','3','4','5','az',...['1','2','3','4'].flatMap(board=>[board+'-d1',board+'-d2'])])) for(const mobile of [false,true]){
   const fixture=JSON.parse(fs.readFileSync(path.join(root,`build/pin-map-${profile}.json`),'utf8'));
   const config={driveInterface:0,simulation:1,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:fixture.buttonGpio,buttonActiveLow:1,buttonPullup:1,pwmInverted:0,pins:{...fixture.pins}};
   let state={token:123,generation:2,recovery:true,trial:false,ready:false,reason:'low_memory',failureCount:2,failureLimit:3,failureResetMs:1800000,remainingMs:120000,heapFree:3000,heapMaxBlock:512,backend:'NONE',sppSupported:!!fixture.classicEsp32,classicEsp32:!!fixture.classicEsp32,crowpanel:!!fixture.crowpanel,crowpanelLocalDrive:profile==='az',separateDrive:profile!=='az',btsDrive:true,hasConfirmed:true,configured:config},probeFails=false;
   const requests=[],errors=[];const mbState={pc:0,bus:0,address:1,node:201,first:201,count:1,owner:0,uart0Baud:9600,uart0Parity:0,uart0StopBits:1,uart1Baud:115200,uart1Parity:0,uart1StopBits:1};
   const page=await browser.newPage({viewport:{width:mobile?360:1100,height:900},locale:'ru-RU'});
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://hardware.test/**',async route=>{
    const req=route.request(),u=new URL(req.url());
    if(u.pathname==='/hardware'||u.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:html});
    if(req.method()==='GET')return route.fulfill({contentType:'application/json',body:JSON.stringify(u.pathname==='/api/hardware/pins'?fixture.metadata:u.pathname==='/api/hardware/modbus'?mbState:state)});
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
   // Only selected local devices request GPIO; switching does not discard assignments.
   for(const mode of [0,1,2,3]){
    await page.locator('[name=localInputMode]').selectOption(String(mode));
    assert.equal(await page.locator('[data-pin-group=buttons]').isVisible(),!!(mode&1));
    assert.equal(await page.locator('[data-pin-group=joystick]').isVisible(),!!(mode&2));
    assert.equal(await page.locator('[data-pin-group=sensorAz]').isHidden(),true);
    assert.equal(await page.locator('[data-pin-group=sensorEl]').isHidden(),true);
    assert.equal(await page.locator('[data-pin-group=hc05]').isHidden(),true);
    assert.equal(await page.locator('[data-pin-group=drive]').isHidden(),true);
    assert.equal(await page.locator('#save').isDisabled(),mode!==0);
    if(profile==='1'&&process.env.HARDWARE_SCREENSHOT_DIR){fs.mkdirSync(process.env.HARDWARE_SCREENSHOT_DIR,{recursive:true});await page.locator(`[data-pin-group=${mode&1?'buttons':mode&2?'joystick':'hudRadio'}]`).scrollIntoViewIfNeeded();await page.screenshot({path:path.join(process.env.HARDWARE_SCREENSHOT_DIR,`gpio-mode-${mode}-${mobile?'mobile':'desktop'}.png`)});}
   }
   await page.locator('[name=localInputMode]').selectOption('0');
   if(profile==='1') {
    await page.locator('[name="pin.hudRx"]').selectOption('38');
    assert(await page.locator('#save').isDisabled());
    assert.match(await page.locator('#pin-validation').textContent(),/все три/);
    await page.locator('[name="pin.hudTx"]').selectOption('39');
    await page.locator('[name="pin.hudDe"]').selectOption('40');
    await page.locator('[name="pin.uartDe"]').selectOption('41');
    assert(!(await page.locator('#save').isDisabled()));
    await page.locator('[name=bluetoothEnabled]').selectOption('1');
    await page.locator('[name=bluetoothMode]').selectOption('1');
    assert(await page.locator('#save').isDisabled());
    assert.match(await page.locator('#pin-validation').textContent(),/HC-05/);
    await page.locator('[name=bluetoothEnabled]').selectOption('0');
    for(const name of ['hudRx','hudTx','hudDe','uartDe'])await page.locator('[name="pin.'+name+'"]').selectOption('-1');
   }
   if(!fixture.crowpanel){
    await page.locator('[name=driveInterface]').selectOption('3');await page.locator('[name=simulation]').selectOption('0');
    assert(await page.locator('[name="pin.azNegative"]').isHidden());assert(await page.locator('[name="pin.azPositive"]').isVisible());
    assert.match(await page.locator('[name="pin.azPositive"]').locator('..').textContent(),/DIR/);
    await page.locator('[name=driveInterface]').selectOption('1');assert(await page.locator('[name="pin.azNegative"]').isVisible());
    await page.locator('[name=driveInterface]').selectOption('0');await page.locator('[name=simulation]').selectOption('1');
   }
   if(profile==='1') {
    const original=await page.evaluate(()=>draft());
    for(const driver of [1,2,3]) {
     await page.evaluate(c=>{for(const [name,value]of Object.entries(c.pins))if(form.elements['pin.'+name])form.elements['pin.'+name].value=value;},original);
     await page.locator('[name=driveInterface]').selectOption(String(driver));await page.locator('[name=simulation]').selectOption('0');
     const en=driver===2?['azSpeedEnable','elSpeedEnable']:['azEnable','elEnable'];
     for(const name of en) {
      const field=page.locator('[name="pin.'+name+'"]');assert(await field.isVisible());
      assert.match(await field.locator('..').textContent(),/ENABLE.*необязательно/);
      await field.selectOption('-1');
     }
     assert(!(await page.locator('#save').isDisabled()),'No ENABLE is required for PWM driver '+driver);
     assert.equal(await page.locator('[name="pin.azEnable"]').isVisible(),driver!==2);
     if(process.env.HARDWARE_SCREENSHOT_DIR)await page.locator('[data-pin-group=drive]').screenshot({path:path.join(process.env.HARDWARE_SCREENSHOT_DIR,`enable-${driver}-off-${mobile?'mobile':'desktop'}.png`)});
     for(let i=0;i<2;++i)await page.locator('[name="pin.'+en[i]+'"]').selectOption(String(38+i));
     assert(!(await page.locator('#save').isDisabled()));
     const fields=await page.locator('[data-pin-group=drive] select:visible').evaluateAll(els=>els.map(el=>{const r=el.getBoundingClientRect();return {width:r.width,height:r.height,left:r.left,right:r.right};}));
     assert(fields.every(r=>r.width>0&&r.height>=40&&r.left>=0&&r.right<=(mobile?360:1100)));
     if(process.env.HARDWARE_SCREENSHOT_DIR)await page.locator('[data-pin-group=drive]').screenshot({path:path.join(process.env.HARDWARE_SCREENSHOT_DIR,`enable-${driver}-on-${mobile?'mobile':'desktop'}.png`)});
     await page.evaluate(name=>{const el=form.elements['pin.'+name];el.value=form.elements['pin.azPositive'].value;el.dispatchEvent(new Event('input',{bubbles:true}));},en[0]);
     assert(await page.locator('#save').isDisabled());assert.match(await page.locator('#pin-validation').textContent(),/занят/);
     await page.locator('[name="pin.'+en[0]+'"]').selectOption('38');
     const before=requests.length;await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено'));
     assert.equal(requests.length,before+1);
     for(let i=0;i<2;++i)assert.equal(requests.at(-1).body['pin.'+en[i]],String(38+i));
     await page.evaluate(()=>{busy=false;dirty=false;return refresh();});
    }
    await page.evaluate(c=>{for(const [name,value]of Object.entries(c.pins))if(form.elements['pin.'+name])form.elements['pin.'+name].value=value;for(const name of names)form.elements[name].value=c[name];busy=false;dirty=true;updatePins();},original);
   }
   assert.equal(await page.locator('[name=forceApSta]').isEnabled(),true);assert.match(await page.locator('[name=forceApSta]').locator('..').textContent(),/^Точка доступа/);assert.equal(await page.locator('input[name=buttonGpio]').count(),0);
   assert(!await page.locator('#bts-settings').isVisible());
   await page.locator('[name=simulation]').selectOption('0');
   await page.locator('[name=driveInterface]').selectOption('2');
   assert(await page.locator('#bts-settings').isVisible());
   await page.locator('[name=pwmInverted]').selectOption('1');
   await page.locator('[name=driveInterface]').selectOption('0');
   assert(!await page.locator('#bts-settings').isVisible());
   assert.equal(await page.locator('[name=pwmInverted]').inputValue(),'0');
   await page.locator('[name=simulation]').selectOption('0');
   // Every board accepts an incomplete motor map in simulation, rejects it in physical mode.
   await page.locator('[name="pin.azAdc"]').selectOption('-1');
   await page.locator('[name=simulation]').selectOption('1');
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
   await page.locator('[name=buttonGpio]').selectOption('0');
   assert(!(await page.locator('#save').isDisabled()));
   assert.equal(await page.locator('[name=buttonGpio]').getAttribute('readonly'),null);
   await page.evaluate(()=>{const el=form.elements['pin.led'];el.value='0';el.dispatchEvent(new Event('input',{bubbles:true}));});
   assert(await page.locator('#save').isDisabled());
   assert.match(await page.locator('#pin-validation').textContent(),/занят/);
   await page.locator('[name=buttonGpio]').selectOption('-1');
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
   if(profile==='1'){
    for(const [name,value] of Object.entries({uart0Baud:9600,uart0Parity:0,uart0StopBits:1,uart1Baud:115200,uart1Parity:0,uart1StopBits:1}))
     assert.equal(await page.locator(`#modbus [name=${name}]`).inputValue(),String(value));
    for(const suffix of ['Baud','Parity','StopBits'])assert.deepEqual(
     await page.locator(`[name=uart0${suffix}] option`).allTextContents(),await page.locator(`[name=uart1${suffix}] option`).allTextContents());
    for(const [name,value] of Object.entries({uart0Baud:38400,uart0Parity:2,uart0StopBits:2,uart1Baud:19200,uart1Parity:1,uart1StopBits:1}))
     await page.locator(`#modbus [name=${name}]`).selectOption(String(value));
    await page.locator('#modbus [name=pc]').selectOption('1');await page.locator('#modbus [name=bus]').selectOption('1');
    await page.locator('#modbus [name=count]').fill('2');await page.locator('#modbus [name=node]').fill('202');
    await page.locator('#modbus [name=address]').fill('201');const before=requests.length;
    await page.locator('#modbus-save').click();assert.equal(requests.length,before);assert.match(await page.locator('#message').textContent(),/вне диапазона/);
    await page.locator('#modbus [name=address]').fill('1');await page.evaluate(()=>refreshModbus());
    assert.equal(await page.locator('#modbus [name=node]').inputValue(),'202');
    for(const mode of ['0','1']){
     await page.locator('#modbus [name=pc]').selectOption(mode);await page.locator('#modbus [name=bus]').selectOption(mode);
     assert.equal(await page.locator('[name=uart0Baud]').inputValue(),'38400');assert.equal(await page.locator('[name=uart1Baud]').inputValue(),'19200');
     assert.equal(await page.locator('[name=uart0Parity]').inputValue(),'2');assert.equal(await page.locator('[name=uart0StopBits]').inputValue(),'2');
    }
    if(process.env.HARDWARE_SCREENSHOT_DIR){fs.mkdirSync(process.env.HARDWARE_SCREENSHOT_DIR,{recursive:true});await page.locator('#uart-settings').screenshot({path:path.join(process.env.HARDWARE_SCREENSHOT_DIR,`uart-${mobile?'mobile':'desktop'}.png`)});}

    await page.locator('#modbus-save').click();await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено')).catch(async e=>{throw new Error(e.message+' '+JSON.stringify({mobile,errors,requests:requests.slice(-2),form:await page.evaluate(()=>({message:msg.textContent,dirty:mbDirty,busy,values:Object.fromEntries(new FormData(mbForm))}))}));});
    assert.equal(requests.at(-1).path,'/api/hardware/modbus');assert.equal(requests.at(-1).body.node,'202');assert.equal(requests.at(-1).body.pc,'1');
    for(const [name,value] of Object.entries({uart0Baud:38400,uart0Parity:2,uart0StopBits:2,uart1Baud:19200,uart1Parity:1,uart1StopBits:1}))assert.equal(requests.at(-1).body[name],String(value));
    assert(await page.locator('#modbus-save').isDisabled());await page.evaluate(()=>{busy=false;buttons();});
    mbState.fault=true;await page.evaluate(()=>refreshModbus());assert.match(await page.locator('#modbus-status').textContent(),/параметры кольца/);
   }
   if(process.env.HARDWARE_SCREENSHOT_DIR){fs.mkdirSync(process.env.HARDWARE_SCREENSHOT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.HARDWARE_SCREENSHOT_DIR,`hardware-${profile}-${mobile?'mobile':'desktop'}.png`),fullPage:true});}
   if(process.env.HARDWARE_DOC_SCREENSHOT&&profile==='1'&&!mobile)await page.screenshot({path:process.env.HARDWARE_DOC_SCREENSHOT,fullPage:true});
   if(profile==='1'&&!mobile){
    await page.goto('http://hardware.test/hardware?setup=1');
    await page.waitForFunction(()=>typeof mbNames!=='undefined'&&!!state&&!!metadata);
    await page.locator('[name=uart0Baud]').selectOption('57600');await page.locator('[name=uart1StopBits]').selectOption('2');
    const before=requests.length;
    await page.evaluate(()=>window.postMessage({type:'hardwareAction',id:71,action:'save'},location.origin));
    await page.waitForFunction(()=>!mbDirty);
    assert.equal(requests.length,before+1);assert.equal(requests.at(-1).path,'/api/hardware');
    assert.equal(requests.at(-1).body['mb.uart0Baud'],'57600');assert.equal(requests.at(-1).body['mb.uart1StopBits'],'2');
    assert(new URLSearchParams(requests.at(-1).body).toString().length<=1536,'combined hardware form capacity');assert(Object.keys(requests.at(-1).body).length<=96);
   }
   await page.close();
  }
  console.log('PASS: selected board/display configurations, independent UART line parameters, mobile/desktop GPIO editing, TFT/LCD completeness, simulation, CrowPanel reservations, dirty fields, defaults and trial/probe/confirm');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
