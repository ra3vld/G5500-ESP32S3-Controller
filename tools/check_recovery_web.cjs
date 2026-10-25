const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const width of [390,1100]){
   const fixture=JSON.parse(fs.readFileSync(path.join(root,'build/pin-map-5.json')));
   const config={driveInterface:0,simulation:1,pwmInverted:0,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:-1,buttonActiveLow:1,buttonPullup:1,pins:fixture.pins};
   const state={role:'controller',token:123,generation:2,recovery:true,trial:false,ready:false,reason:'invalid_config',heapFree:35000,heapMaxBlock:16000,backend:'NONE',configured:config};
   const entries=Array.from({length:20},(_,i)=>({epoch:i===1?0:Date.UTC(2026,9,25,12-i)/1000,reason:i===0?'PANIC':i===2?'<svg onload=window.xss=1>':'SOFTWARE',code:i===0?4:3}));
   let available=true,reads=0;const posts=[],errors=[];
   const page=await browser.newPage({viewport:{width,height:950},locale:'ru-RU',timezoneId:'Europe/Moscow'});
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://recovery.test/**',route=>{
    const req=route.request(),url=new URL(req.url());
    if(url.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:html});
    if(url.pathname==='/api/hardware/reboots'){assert.equal(req.method(),'GET');++reads;return route.fulfill({json:{current:{reason:'PANIC',code:4},recoveryReason:'invalid_config',history:{available,count:20,entries}}});}
    if(req.method()==='POST'){posts.push({path:url.pathname,body:Object.fromEntries(new URLSearchParams(req.postData()))});return route.fulfill({json:{ok:true}});}
    return route.fulfill({json:url.pathname==='/api/hardware/pins'?fixture.metadata:url.pathname==='/api/hardware/modbus'?{pc:0,bus:0,address:1,node:201,first:201,count:1}:state});
   });
   await page.goto('http://recovery.test/recovery');await page.waitForFunction(()=>document.querySelectorAll('#reboot-entries tr').length===20);
   assert.match(await page.locator('#current-reboot').textContent(),/PANIC.*4/);
   assert.match(await page.locator('#reboot-entries tr').first().textContent(),/25\.10\.2026.*PANIC.*4/);
   assert.match(await page.locator('#reboot-entries tr').nth(1).textContent(),/Нет точного времени/);
   assert.equal(await page.locator('#reboot-entries svg').count(),0);assert.equal(await page.evaluate(()=>window.xss),undefined);
   assert.equal(await page.evaluate(()=>document.getElementById('save').form.id),'hardware');
   const order=await page.evaluate(()=>{const save=document.getElementById('save').getBoundingClientRect();return [...document.querySelectorAll('fieldset')].filter(e=>e.getClientRects().length).every(e=>e.getBoundingClientRect().bottom<=save.top);});
   assert(order,'Save is below every settings/history section');
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   const out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/recovery-history-web');fs.mkdirSync(out,{recursive:true});
   await page.locator('#reboot-history').evaluate(e=>window.scrollTo(0,e.getBoundingClientRect().top+scrollY-12));await page.screenshot({path:path.join(out,`history-${width}.png`)});
   await page.locator('#save').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`save-bottom-${width}.png`)});
   const before=reads;available=false;await page.locator('#load-reboots').click();await page.waitForFunction(()=>document.getElementById('reboot-message').textContent.includes('Не удалось прочитать'));
   assert.equal(reads,before+1);assert.equal(posts.length,0);
   available=true;await page.locator('#load-reboots').click();await page.waitForFunction(()=>document.getElementById('reboot-message').textContent==='');
   await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено'));
   assert.equal(posts.length,1);assert.equal(posts[0].path,'/api/hardware');assert.equal(posts[0].body.token,'123');assert.equal(posts[0].body.generation,'2');
   assert.deepEqual(errors,[]);await page.close();
  }
  console.log('PASS: recovery history, current reset, unsynchronized time, safe text, unavailable NVS and final save at 390/1100 px');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
