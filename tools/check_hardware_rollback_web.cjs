// A successful settings POST can still be rolled back by the next boot.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{for(const width of [390,1100])for(const outcome of ['low_memory','radio_failed','allocation_failed','unconfirmed','recovery','success']){
  const fixture=JSON.parse(fs.readFileSync(path.join(root,'build/pin-map-4.json')));
  const config={driveInterface:2,simulation:0,pwmInverted:0,bluetoothEnabled:1,bluetoothMode:1,forceApSta:0,buttonGpio:0,buttonActiveLow:1,buttonPullup:1,pins:fixture.pins};
  const state={role:'controller',token:123,generation:2,hasConfirmed:true,recovery:false,trial:false,ready:true,reason:'none',heapFree:65000,heapMaxBlock:45000,backend:'HC05',classicEsp32:true,sppSupported:true,failureCount:2,failureLimit:5,lastFailure:'low_memory',configured:config};
  let posts=0;const errors=[];
  const page=await browser.newPage({viewport:{width,height:950},locale:'ru-RU'});
  page.on('pageerror',e=>errors.push(e.message));
  try{
   await page.route('http://hardware-rollback.test/**',async route=>{
    const req=route.request(),url=new URL(req.url());
    if(url.pathname==='/hardware')return route.fulfill({contentType:'text/html',body:html});
    if(url.pathname==='/api/hardware'&&req.method()==='POST'){
     ++posts;assert.equal(new URLSearchParams(req.postData()).get('bluetoothMode'),'2');
     state.token=456;++state.generation;
     if(outcome==='success'){state.configured={...config,bluetoothMode:2};state.backend='ESP32_SPP';}
     else{++state.failureCount;state.lastFailure=outcome==='recovery'?'low_memory':outcome;if(outcome==='recovery'){state.recovery=true;state.reason='low_memory';state.backend='NONE';}}
     return route.fulfill({json:{ok:true,confirmationRequired:true}});
    }
    if(url.pathname==='/api/hardware/display')return route.fulfill({json:{supported:false}});
    if(url.pathname==='/api/hardware/reboots')return route.fulfill({json:{current:{reason:'SOFTWARE',code:3},history:{available:true,count:0,entries:[]}}});
    return route.fulfill({json:url.pathname==='/api/hardware/pins'?fixture.metadata:url.pathname==='/api/hardware/modbus'?{pc:0,bus:0,address:1,node:201,first:201,count:1}:state});
   });
   await page.goto('http://hardware-rollback.test/hardware');
   await page.waitForFunction(()=>!!state&&!!metadata&&!busy);
   assert.match(await page.locator('#failures').textContent(),/Последний сбой запуска: Недостаточно внутренней памяти/);
   assert.doesNotMatch(await page.locator('#message').textContent(),/не запустилась/,'history alone must not report a new failed save');
   for(;;){const closed=page.locator('details.settings-fold:not([open])').filter({has:page.locator('[name=bluetoothMode]')}).first();if(!await closed.count())break;await closed.locator(':scope > summary').click();}
   await page.locator('[name=bluetoothMode]').selectOption('2');
   await page.locator('#save').click();
   await page.waitForFunction(()=>state?.token===456&&!busy);
   if(outcome==='success'){
    assert.equal(await page.locator('[name=bluetoothMode]').inputValue(),'2');
    assert.doesNotMatch(await page.locator('#message').textContent(),/не запустилась/,'a previous failure must not turn a successful save into an error');
   }else{
    assert.match(await page.locator('#message').textContent(),/Сохранённая конфигурация не запустилась/);
    assert.match(await page.locator('#message').textContent(),outcome==='recovery'?/Устройство в режиме восстановления/:/Восстановлены подтверждённые настройки/);
    assert.equal(await page.locator('#settingsFeedback').getAttribute('data-state'),'error');
    assert.equal(await page.locator('[name=bluetoothMode]').inputValue(),'1');
    if(outcome==='low_memory')assert.match(await page.locator('#message').textContent(),/Недостаточно внутренней памяти/);
   }
   assert.equal(posts,1);assert.deepEqual(errors,[]);
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   if(process.env.ARTIFACT_DIR&&outcome==='low_memory'){
    fs.mkdirSync(process.env.ARTIFACT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`rollback-${width}.png`)});
   }
   console.log(`PASS: ${width}px ${outcome}`);
  }finally{await page.close();}
 }}finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
