// Browser checks for the shared normal/recovery hardware page.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
const config={driveInterface:0,simulation:0,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:0,buttonActiveLow:1,buttonPullup:1,pwmInverted:0};
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const mobile of [false,true]){
   let state={token:123,generation:2,recovery:true,trial:false,ready:false,reason:'low_memory',failureCount:2,failureLimit:3,failureResetMs:1800000,remainingMs:120000,heapFree:3000,heapMaxBlock:512,backend:'NONE',sppSupported:true,classicEsp32:true,crowpanel:false,independentDrive:true,hasConfirmed:true,configured:{...config}},probeFails=false;
   const requests=[],errors=[];
   const page=await browser.newPage({viewport:{width:mobile?360:1100,height:900},locale:'ru-RU'});
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://hardware.test/**',async route=>{
    const req=route.request(),u=new URL(req.url());
    if(u.pathname==='/hardware'||u.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:html});
    if(req.method()==='GET')return route.fulfill({contentType:'application/json',body:JSON.stringify(state)});
    const body=Object.fromEntries(new URLSearchParams(req.postData()));
    requests.push({path:u.pathname,body});
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
   await page.locator('[name=buttonGpio]').fill('38');
   await page.evaluate(()=>refresh());
   assert.equal(await page.locator('[name=buttonGpio]').inputValue(),'38'); // polling preserves edits
   await page.locator('[name=driveInterface]').selectOption('2');
   await page.locator('[name=pwmInverted]').selectOption('1');
   await page.locator('#save').click();
   await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено'));
   assert.equal(requests.at(-1).path,'/api/hardware');
   assert.deepEqual(requests.at(-1).body,{token:'123',generation:'2',...Object.fromEntries(Object.entries({...config,driveInterface:2,pwmInverted:1,buttonGpio:38}).map(([k,v])=>[k,String(v)]))});
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
   state={...state,recovery:true,trial:false,sppSupported:false,classicEsp32:false,crowpanel:true,independentDrive:false,hasConfirmed:false,configured:{...config,buttonGpio:-1}};
   await page.evaluate(()=>refresh());
   assert(await page.locator('#confirm').isDisabled());
   assert(await page.locator('#rollback').isDisabled());
   assert(await page.locator('[name=buttonGpio]').getAttribute('readonly')!==null);
   assert.equal(await page.locator('[name=buttonGpio]').inputValue(),'-1');
   assert.equal(await page.locator('[name=bluetoothMode] option[value="2"]').evaluate(el=>el.disabled),true);
   assert.equal(await page.locator('[name=driveInterface] option[value="2"]').evaluate(el=>el.disabled),true);
   state={...state,crowpanelLocalDrive:true,separateDrive:false,btsDrive:true,configured:{...state.configured,bluetoothEnabled:1}};
   await page.evaluate(()=>refresh());
   assert.equal(await page.locator('[name=driveInterface] option[value="1"]').evaluate(el=>el.disabled),true);
   assert.equal(await page.locator('[name=driveInterface] option[value="2"]').evaluate(el=>el.disabled),false);
   assert.equal(await page.locator('[name=bluetoothEnabled]').isDisabled(),true);
   assert.equal(await page.locator('[name=bluetoothEnabled]').inputValue(),'0');
   state={...state,failureCount:undefined};await page.evaluate(()=>refresh());
   assert.equal(await page.locator('#failures').textContent(),''); // older firmware remains readable
   state={...state,failureCount:3};await page.evaluate(()=>refresh());
   assert.match(await page.locator('#failures').textContent(),/Сбои: 3 \/ 3/);
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   assert.deepEqual(errors,[]);
   if(process.env.HARDWARE_SCREENSHOT_DIR){fs.mkdirSync(process.env.HARDWARE_SCREENSHOT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.HARDWARE_SCREENSHOT_DIR,mobile?'hardware-mobile.png':'hardware-desktop.png'),fullPage:true});}
   await page.close();
  }
  console.log('PASS: recovery editing, dirty fields, full runtime form, parser/NVS probe before confirmation, board capabilities, mobile layout');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
