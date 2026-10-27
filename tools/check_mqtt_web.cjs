// MQTT form and HTTP-only control regression against the embedded page.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const state=JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
state.deviceRole='controller';state.build.features={...state.build.features,mqtt:true,webLive:false};state.livePort=0;
(async()=>{
 const browser=await chromium.launch({...(process.env.CHROMIUM_PATH?{executablePath:process.env.CHROMIUM_PATH}:{}),headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage(),errors=[],posts=[],moves=[],targets=[];let reads=0,sockets=0,fail=false;
  page.on('pageerror',e=>errors.push(e.message));page.on('websocket',()=>++sockets);
  await page.route('**/*',route=>{
   const req=route.request(),url=new URL(req.url());
   if(url.pathname==='/api/status')return route.fulfill({json:state});
   if(url.pathname==='/api/move'&&req.method()==='POST')moves.push(new URLSearchParams(req.postData()));
   if(url.pathname==='/api/target'&&req.method()==='POST')targets.push(new URLSearchParams(req.postData()));
   if(url.pathname==='/api/mqtt'){
    if(req.method()==='GET'){++reads;return route.fulfill({json:{enabled:false,host:'mqtt.local',port:1883,username:'radio',hasPassword:true,discovery:true,discoveryPrefix:'homeassistant',error:'disabled'}});}
    posts.push(new URLSearchParams(req.postData()));return route.fulfill({status:fail?503:200,json:fail?{ok:false,error:'mqtt_storage_failed'}:{ok:true,restart:true}});
   }
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{ok:true,items:[],values:[],samples:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://controller.test/');await page.waitForFunction(()=>window._forkState);
  assert.equal(reads,0);assert.equal(sockets,0);
  await page.locator('#taz').fill('100');await page.locator('#taz').press('Enter');await page.waitForTimeout(100);
  assert.equal(targets.at(-1)?.get('az'),'100');
  await page.keyboard.down('ArrowRight');await page.waitForTimeout(450);await page.keyboard.up('ArrowRight');await page.waitForTimeout(100);
  assert(moves.some(p=>p.get('d')==='H'&&p.get('az')==='1'),'HTTP manual HOLD');
  assert(moves.some(p=>p.get('d')==='S'),'HTTP release STOP');
  await page.getByRole('tab',{name:'Настройки',exact:true}).click();await page.locator('#networkSettings > details').evaluate(e=>e.open=true);
  await page.waitForFunction(()=>document.getElementById('mqttHost').value==='mqtt.local');
  assert.equal(await page.locator('#mqttPassword').inputValue(),'');
  for(const width of [1100,390,320]){
   await page.setViewportSize({width,height:900});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'overflow '+width);
   fs.mkdirSync(path.join(root,'local-notes/mqtt-web'),{recursive:true});await page.screenshot({path:path.join(root,`local-notes/mqtt-web/network-${width}.png`),fullPage:true});
   await page.locator('#mqttSettings').scrollIntoViewIfNeeded();
   assert(await page.locator('#mqttSettings').evaluate(e=>e.scrollWidth<=e.clientWidth),'MQTT form overflow '+width);
   await page.locator('#mqttSettings').screenshot({path:path.join(root,`local-notes/mqtt-web/mqtt-${width}.png`)});
  }
  await page.locator('#mqttHost').fill('unsaved.local');await page.evaluate(()=>upd(true));assert.equal(await page.locator('#mqttHost').inputValue(),'unsaved.local');assert.equal(reads,1);
  await page.locator('#mqttSave').click();await page.waitForFunction(()=>mqttMessage.textContent.includes('перезапускается'));assert(!posts.at(-1).has('password'));
  await page.locator('#mqttPassword').fill('пароль & +');await page.locator('#mqttSave').click();await page.waitForTimeout(100);assert.equal(posts.at(-1).get('password'),'пароль & +');assert.equal(await page.locator('#mqttPassword').inputValue(),'');
  fail=true;await page.locator('#mqttClearPassword').check();await page.locator('#mqttSave').click();await page.waitForFunction(()=>mqttMessage.textContent.includes('mqtt_storage_failed'));assert.equal(posts.at(-1).get('password'),'');
  state.build.features.mqtt=false;state.build.features.rssiHistory=false;state.build.features.hud=false;state.build.features.ota=false;
  await page.evaluate(()=>upd(true));await page.waitForFunction(()=>mqttSettings.hidden);assert(await page.locator('#signalSettings').isHidden());assert(await page.locator('#otaSettings').isHidden());
  assert.equal(sockets,0);assert.deepEqual(errors,[]);console.log('PASS: MQTT form, secret preservation, failure, feature hiding, desktop/mobile and HTTP-only target/HOLD/STOP');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
