// Production orientation form: persistence, errors, manual edits and responsive layout.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const fields=['showName','showControllerAddress','showLocalAddress','showApAddress','showHeap','showAxis'];
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
const fixture=JSON.parse(fs.readFileSync(path.join(root,'build/pin-map-4.json'),'utf8'));
(async()=>{
 const browser=await chromium.launch({headless:true,args:['--no-sandbox'],executablePath:process.env.CHROMIUM_PATH});
 try {
  for(const kind of (process.env.DISPLAY_KINDS?.split(',').map(Number)||[1,3,4,5,6,0,2]))for(const mobile of [false,true]) {
   const page=await browser.newPage({viewport:{width:mobile?360:1100,height:900},locale:'ru-RU'}),errors=[],posts=[];
   page.on('pageerror',e=>errors.push(e.message));
   const config={driveInterface:0,simulation:1,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:fixture.buttonGpio,buttonActiveLow:1,buttonPullup:1,pwmInverted:0,pins:{...fixture.pins}};
   const hardware={token:123,generation:2,recovery:false,trial:false,ready:true,hasConfirmed:true,configured:config,classicEsp32:true,sppSupported:true};
   let saved={supported:[1,3,4,5,6].includes(kind),displayKind:kind,token:123,revision:7,storageOk:true,restartRequired:false,width:kind===5?480:kind===3?480:320,height:kind===3?480:240,screenRotation:0,screenMirrorX:0,screenMirrorY:0,touchRotation:0,touchMirrorX:0,touchMirrorY:0},failure=null;
   saved.defaultFields=[1,4].includes(kind)?47:63;for(const [i,name] of fields.entries())saved[name]=Number(Boolean(saved.defaultFields&(1<<i)));
   await page.route('http://display.test/**',async route=>{
    const req=route.request(),u=new URL(req.url());
    if(u.pathname==='/hardware')return route.fulfill({contentType:'text/html',body:html});
    if(u.pathname==='/api/hardware/display') {
     if(req.method()==='GET'){const w=kind===6?360:kind===5||kind===3?480:kind===4?240:320,h=kind===6?360:kind===3?480:kind===5?320:240;saved.width=saved.screenRotation%180?h:w;saved.height=saved.screenRotation%180?w:h;return route.fulfill({contentType:'application/json',body:JSON.stringify(saved)});}
     const body=Object.fromEntries(new URLSearchParams(req.postData()));posts.push(body);
     if(failure)return route.fulfill({status:failure==='storage_failed'?503:409,contentType:'application/json',body:JSON.stringify({error:failure})});
     assert.equal(body.token,'123');assert.equal(body.revision,String(saved.revision));
     for(const name of ['screenRotation','screenMirrorX','screenMirrorY','touchRotation','touchMirrorX','touchMirrorY',...fields])saved[name]=Number(body[name]);
     ++saved.revision;
     return route.fulfill({contentType:'application/json',body:'{"ok":true}'});
    }
    return route.fulfill({contentType:'application/json',body:JSON.stringify(u.pathname==='/api/hardware/pins'?fixture.metadata:u.pathname==='/api/hardware/modbus'?{}:hardware)});
   });
   await page.goto('http://display.test/hardware');
   await page.waitForFunction(()=>displayState!==null&&!displayBusy);
   const card=page.locator('#display-settings');await card.scrollIntoViewIfNeeded();
   if(!saved.supported) {
    assert(await page.locator('#display-save').isDisabled());assert(await page.locator('#display-defaults').isDisabled());
    assert.match(await page.locator('#display-info').textContent(),kind===2?/LCD1602/:/отсутствуют/);
   } else {
    assert(await page.locator('#display-save').isDisabled());
    for(const [i,name] of fields.entries()){const box=page.locator('[name='+name+']');assert.equal(await box.isChecked(),Boolean(saved.defaultFields&(1<<i)));await box.setChecked(!Boolean(saved.defaultFields&(1<<i)));}
    await page.locator('[name=screenRotation]').selectOption('90');
    await page.locator('[name=screenMirrorX]').selectOption('1');
    await page.locator('[name=touchRotation]').selectOption('270');
    await page.locator('[name=touchMirrorY]').selectOption('1');
    await page.waitForTimeout(2100);
    assert.equal(await page.locator('[name=screenRotation]').inputValue(),'90');
    for(const [i,name] of fields.entries())assert.equal(await page.locator('[name='+name+']').isChecked(),!Boolean(saved.defaultFields&(1<<i)));
    failure='storage_failed';await page.locator('#display-save').click();
    await page.waitForFunction(()=>!displayBusy);
    assert.match(await page.locator('#display-message').textContent(),/Запись не подтверждена/);
    assert.equal(await page.locator('[name=touchRotation]').inputValue(),'270');
    assert(!(await page.locator('#display-save').isDisabled()));
    failure='stale_configuration';await page.locator('#display-save').click();await page.waitForFunction(()=>!displayBusy);
    assert.match(await page.locator('#display-message').textContent(),/устарела/);
    failure=null;await page.locator('#display-save').click();await page.waitForFunction(()=>!displayBusy);
    assert.match(await page.locator('#display-message').textContent(),/Сохранено/);
    assert(await page.locator('#display-save').isDisabled());
    for(const [i,name] of fields.entries())assert.equal(saved[name],Number(!Boolean(saved.defaultFields&(1<<i))));
    assert.deepEqual([saved.screenRotation,saved.screenMirrorX,saved.touchRotation,saved.touchMirrorY],[90,1,270,1]);
    await page.locator('#display-reload').click();await page.waitForFunction(()=>!displayBusy);
    assert.equal(await page.locator('[name=screenRotation]').inputValue(),'90');
    const count=posts.length;await page.locator('#display-defaults').click();assert.equal(posts.length,count);
    assert.equal(await page.locator('[name=screenRotation]').inputValue(),'0');
    assert.equal(await page.locator('[name=touchMirrorY]').inputValue(),'0');
    for(const [i,name] of fields.entries())assert.equal(await page.locator('[name='+name+']').isChecked(),Boolean(saved.defaultFields&(1<<i)));
    saved.storageOk=false;for(const name of ['screenRotation','screenMirrorX','screenMirrorY','touchRotation','touchMirrorX','touchMirrorY',...fields])saved[name]=0;await page.locator('#display-reload').click();await page.waitForFunction(()=>!displayBusy);
    assert.match(await page.locator('#display-message').textContent(),/не удалось прочитать/);assert(!(await page.locator('#display-save').isDisabled()));saved.storageOk=true;saved.screenRotation=90;await page.locator('#display-reload').click();await page.waitForFunction(()=>!displayBusy);
   }
   assert.deepEqual(errors,[]);
   const boxes=await card.locator('input,select,button').evaluateAll(els=>els.map(e=>{const r=e.getBoundingClientRect();const hit=e.type==='checkbox'?e.closest('label').getBoundingClientRect():r;return {left:r.left,right:r.right,height:hit.height};}));
   assert(boxes.every(b=>b.left>=0&&b.right<=(mobile?360:1100)&&b.height>=40));
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   if(kind===5){const dir=path.join(root,'build/display-fields');fs.mkdirSync(dir,{recursive:true});await card.screenshot({path:path.join(dir,mobile?'web-mobile.png':'web-desktop.png')});}
   await page.close();
  }
  console.log('PASS: selected display-profile/viewport cases; six independent visibility toggles, save/reload/reset, write errors, stale state, manual edits and unsupported displays');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
