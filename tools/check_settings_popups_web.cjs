// Save failures stay visible at the current scroll position; terminal notices expire.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/settings-popups-web');
const extract=(file,delimiter)=>fs.readFileSync(path.join(root,'G5500_Fork',file),'utf8').split(`R"${delimiter}(`)[1].split(`)${delimiter}";`)[0];
const mainHtml=extract('web_page.hpp','HTML'),hardwareHtml=extract('hardware_page.hpp','HWHTML');
const state=JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
state.deviceRole='controller';state.livePort=0;state.build.features={...state.build.features,mqtt:true,webLive:false};
const fixture=JSON.parse(fs.readFileSync(path.join(root,'build/pin-map-1.json'),'utf8'));
const hardware={token:123,generation:2,ready:true,hasConfirmed:true,sppSupported:true,configured:{driveInterface:0,simulation:1,bluetoothEnabled:0,bluetoothMode:3,buttonGpio:fixture.buttonGpio,pins:fixture.pins}};
(async()=>{
 fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const hardwarePage of [false,true])for(const width of [390,1100]){
   const page=await browser.newPage({viewport:{width,height:850}}),errors=[];
   page.setDefaultTimeout(15000);page.on('pageerror',e=>errors.push(e.message));
   await page.clock.install();
   let fail=true,posts=0;
   await page.route('http://popup.test/**',async route=>{
    const req=route.request(),url=new URL(req.url());
    if(url.pathname==='/api/hardware/activity')return route.fulfill({json:{ok:true}});
    if(req.method()==='POST'){
     ++posts;
     return route.fulfill({status:fail?503:200,json:fail?{ok:false,error:'storage_failed',result:'storage_full'}:{ok:true,result:'ok'}});
    }
    if(url.pathname==='/api/status')return route.fulfill({json:state});
    if(url.pathname==='/api/setup')return route.fulfill({json:{required:false,paused:true,storageOk:true}});
    if(url.pathname==='/api/network')return route.fulfill({json:{choice:0,connected:true,wifi:{mode:'dhcp'},eth:{mode:'dhcp'}}});
    if(url.pathname==='/api/mqtt')return route.fulfill({json:{enabled:false,host:'mqtt.local',port:1883,discoveryPrefix:'homeassistant'}});
    if(url.pathname==='/api/hardware/pins')return route.fulfill({json:fixture.metadata});
    if(url.pathname==='/api/hardware')return route.fulfill({json:hardware});
    if(url.pathname.startsWith('/api/'))return route.fulfill({json:{ok:true,items:[],samples:[]}});
    return route.fulfill({contentType:'text/html; charset=utf-8',body:hardwarePage?hardwareHtml:mainHtml});
   });
   await page.goto('http://popup.test/'+(hardwarePage?'hardware':'#settings'));
   const notice=page.locator('#settingsFeedback');
   const visible=async(expected)=>{
    await page.waitForFunction(expected=>{const box=document.getElementById('settingsFeedback');return !box.hidden&&box.dataset.state===expected;},expected);
    const b=await notice.boundingBox();assert(b&&b.x>=0&&b.y>=0&&b.x+b.width<=width&&b.y+b.height<=850,'Popup must fit the viewport');
    assert.equal(await notice.evaluate(e=>getComputedStyle(e).position),'fixed');
   };
   const save=async(selector)=>{
    await page.evaluate(()=>dismissSettingsFeedback());
    const button=page.locator(selector);await button.scrollIntoViewIfNeeded();
    const before=await button.boundingBox(),scrollBefore=await page.evaluate(()=>scrollY);
    const count=posts;await button.click();await visible(fail?'error':'success');assert.equal(posts,count+1);
    const after=await button.boundingBox(),scrollAfter=await page.evaluate(()=>scrollY);assert(hardwarePage&&Math.abs(scrollAfter-scrollBefore)<2||Math.abs(after.y-before.y)<2,'Saving must not scroll away from the button: '+JSON.stringify({before,after,scrollBefore,scrollAfter}));
   };
   if(hardwarePage){
    await page.waitForFunction(()=>!document.getElementById('save').disabled);
    await save('#save');assert.match(await notice.textContent(),/Запись не подтверждена/);
    await page.screenshot({path:path.join(out,`hardware-error-${width}.png`)});
    fail=false;await save('#save');
   }else{
    await page.waitForFunction(()=>window._forkState);
    await page.locator('#rotorSettings details').evaluate(e=>e.open=true);
    await save('#forkSettings [type=submit]');assert.match(await notice.textContent(),/Не хватает памяти/);
    await page.screenshot({path:path.join(out,`settings-error-${width}.png`)});
    await page.locator('#networkSettings > details').evaluate(e=>e.open=true);
    await page.waitForFunction(()=>mqttLoaded&&wizard.network);
    await save('#networkIpForm button');assert.match(await notice.textContent(),/Не удалось записать настройки/);
    await save('#mqttSave');assert.match(await notice.textContent(),/MQTT не сохранён/);
    fail=false;await save('#mqttSave');assert.match(await notice.textContent(),/MQTT сохранены/);
    fail=true;
    await page.evaluate(()=>{dismissSettingsFeedback();wizard.active=true;wizard.state={...wizard.state,required:true,paused:false,ready:true,step:1,role:'controller',token:123,storageOk:true};renderSetup();});
    await page.locator('#setupDeviceName').fill('Проверка уведомления');
    await page.locator('#setupNext').click();await visible('error');
    assert.match(await notice.textContent(),/Не удалось записать настройки/);
    await page.waitForFunction(()=>!wizard.busy);
    await page.evaluate(()=>{wizard.active=false;renderSetup();});
   }
   // Freeze time only after exercising real click/request/result paths.
   await page.clock.pauseAt(await page.evaluate(()=>Date.now()+1000));
   await page.evaluate(()=>settingsFeedback('Сохранено','success'));
   await page.clock.fastForward(5999);assert(await notice.isVisible());
   await page.clock.fastForward(1);assert(await notice.isHidden());
   await page.evaluate(()=>settingsFeedback('Предыдущее сохранение','success'));
   await page.clock.fastForward(5000);
   await page.evaluate(()=>settingsFeedback('Ошибка следующего сохранения'));
   await page.clock.fastForward(1000);assert(await notice.isVisible(),'Old success timer must not hide a new error');
   await page.clock.fastForward(8999);assert(await notice.isVisible());
   await page.clock.fastForward(1);assert(await notice.isHidden());
   await page.evaluate(()=>settingsFeedback('Сохранено','success'));
   await page.clock.fastForward(5000);
   await page.evaluate(()=>settingsFeedback('Ожидаем ответ','pending'));
   await page.clock.fastForward(20000);assert(await notice.isVisible(),'Pending request must not expire as if completed');
   await page.evaluate(()=>settingsFeedback('Повторная ошибка'));
   await notice.getByRole('button',{name:'Закрыть уведомление'}).dispatchEvent('click');assert(await notice.isHidden());
   await page.evaluate(()=>settingsFeedback('Новое уведомление','success'));
   await page.clock.fastForward(6000);assert(await notice.isHidden());
   assert.deepEqual(errors,[]);await page.close();
  }
  console.log('PASS: settings/network/MQTT/wizard/hardware save popups, preserved scroll, 390/1100px visibility, 6s success/10s error expiry, replacement timers, pending and manual dismissal');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
