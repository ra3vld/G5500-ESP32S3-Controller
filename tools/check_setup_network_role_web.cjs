// Network/role visibility and final-step MQTT against the embedded page.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/setup-network-role');fs.mkdirSync(out,{recursive:true});
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const core=input=>JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:input+'\n',encoding:'utf8'}).trim().split('\n').at(-1));
const initial=core('step 0').state,read=route=>JSON.parse(core('http '+route).body);
const motion=read('/api/motion'),azcal=read('/api/azcal'),elcal=read('/api/elcal');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{for(const role of ['controller','hud'])for(const width of [390,1100]){
  const page=await browser.newPage({viewport:{width,height:1000},locale:'ru-RU'}),errors=[],posts=[];
  page.on('pageerror',e=>errors.push(e.message));let reads=0,fail=false;
  const status=structuredClone(initial);Object.assign(status,{deviceRole:role,hudUart:false,hudOnline:true,configurationLocked:true});
  status.build.features={...status.build.features,mqtt:true,homeAssistantDiscovery:true,wifi:true,hud:true,hudUart:true,webLive:false,rssiHistory:false,ota:false};status.livePort=0;
  const state={required:true,paused:false,step:0,checked:0,generation:1,equipment:true,ready:true,role,simulation:true,token:777,testToken:0,storageOk:true,restartRequired:false};
  const mqtt={enabled:false,host:'broker.local',port:1883,username:'radio',hasPassword:true,discovery:true,discoveryPrefix:'homeassistant',connected:false};
  await page.route('http://wizard-fields.test/**',async route=>{
   const req=route.request(),url=new URL(req.url()),body=Object.fromEntries(new URLSearchParams(req.postData()||url.search)),json=data=>route.fulfill({json:data});
   if(url.pathname==='/setup'||url.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
   if(req.method()==='POST')posts.push({path:url.pathname,body});
   if(url.pathname==='/api/status')return json(status);
   if(url.pathname==='/api/setup'){
    if(req.method()==='POST'){
     if(body.action==='complete'){state.required=false;state.step=10;}
     if(body.action==='pause')state.paused=true;
     if(!['begin_save','end_save','renew','arm'].includes(body.action))state.generation++;
    }
    return json(state);
   }
   if(url.pathname==='/api/mqtt'){
    if(req.method()==='GET'){++reads;return json(mqtt);}
    if(fail)return route.fulfill({status:503,json:{ok:false,error:'mqtt_storage_failed'}});
    Object.assign(mqtt,{enabled:body.enabled==='1',host:body.host,port:Number(body.port),username:body.username});state.restartRequired=true;return json({ok:true,restart:true});
   }
   if(url.pathname==='/api/network')return json({choice:1,apIp:'192.168.4.1',ethernetSupported:false,wifi:{mode:'dhcp'},eth:{mode:'dhcp'}});
   if(url.pathname==='/api/motion')return json(motion);
   if(url.pathname==='/api/azcal')return json(azcal);
   if(url.pathname==='/api/elcal')return json(elcal);
   if(url.pathname==='/api/hud/peers')return json({total:0,items:[]});
   if(url.pathname==='/favicon.ico')return route.fulfill({status:204,body:''});
   return json({ok:true,items:[],values:[],samples:[]});
  });
  await page.goto('http://wizard-fields.test/setup');await page.waitForFunction(()=>wizard.active&&wizard.state?.token&&window._forkState);
  assert(await page.locator('#networkSettings').isVisible());assert(await page.locator('#mqttSettings').isHidden());
  await page.evaluate(async()=>{await upd(true);await pollSetup();});assert.equal(reads,0,'Network step must not fetch MQTT');
  await page.screenshot({path:path.join(out,`${role}-network-${width}.png`)});
  state.step=1;await page.evaluate(()=>pollSetup());await page.waitForFunction(()=>wizard.state.step===1);
  await page.locator('#deviceUart').selectOption('1');await page.locator('#hudRefreshHz').fill('4.2');
  const stable=await page.evaluate(async()=>{
   const seen=[];for(let i=0;i<4;++i){
    renderFeatures(window._forkState.build.features);await new Promise(requestAnimationFrame);
    seen.push(document.getElementById('deviceWifi').closest('.field').hidden);
    renderSetup();updateHud(window._forkState);await new Promise(requestAnimationFrame);
    seen.push(document.getElementById('deviceWifi').closest('.field').hidden);
   }return seen;
  });assert(stable.every(Boolean),'Wi-Fi must stay hidden across both polling/render paths');
  assert.equal(await page.locator('#deviceUart').inputValue(),'1');assert.equal(await page.locator('#hudRefreshHz').inputValue(),'4.2');
  assert(await page.locator('#hudUartSettings').isVisible());assert.equal(reads,0);
  await page.evaluate(()=>{window._forkState.build.features.hudUart=false;renderFeatures(window._forkState.build.features);renderSetup();});
  assert(await page.locator('#deviceUart').isHidden());assert(await page.locator('#hudUartSettings').isHidden());assert(await page.locator('#hudRefreshHz').isDisabled());
  await page.evaluate(()=>{window._forkState.build.features.hudUart=true;renderFeatures(window._forkState.build.features);renderSetup();});
  await page.screenshot({path:path.join(out,`${role}-role-${width}.png`)});
  if(role==='controller'){
   state.step=2;await page.evaluate(()=>pollSetup());await page.locator('#setupDriveKind').selectOption('DIR_PWM');
   const models=await page.evaluate(async()=>{const seen=[];for(let i=0;i<4;++i){
    updateFork(window._forkState);renderFeatures(window._forkState.build.features);await new Promise(requestAnimationFrame);
    seen.push([!!document.getElementById('setupDriveKind').getClientRects().length,driveForm.elements.kind.value]);
    renderSetup();await new Promise(requestAnimationFrame);
    seen.push([!!document.getElementById('setupDriveKind').getClientRects().length,driveForm.elements.kind.value]);
   }return seen;});
   assert(models.every(([visible,value])=>visible&&value==='DIR_PWM'),'Drive step must stay visible and preserve its draft');
   await page.screenshot({path:path.join(out,`${role}-drive-${width}.png`)});
  }
  state.step=9;await page.evaluate(()=>pollSetup());await page.waitForFunction(()=>mqttLoaded);
  assert(await page.locator('#setupReview #mqttSettings').isVisible());assert.equal(await page.locator('#mqttForm').count(),1);
  assert(await page.locator('#mqttSave').isHidden());assert.equal(await page.locator('#setupMovementCheck').isVisible(),role==='controller');
  assert.equal(reads,1);assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  await page.locator('#mqttHost').fill('draft.local');await page.locator('#mqttPassword').fill('discard');
  await page.locator('#setupReset').click();await page.waitForFunction(()=>!wizard.busy&&mqttHost.value==='broker.local');
  assert.equal(await page.locator('#mqttPassword').inputValue(),'');assert.equal(reads,2);
  await page.locator('#mqttEnabled').check();await page.locator('#mqttHost').fill('new-broker.local');await page.locator('#mqttPassword').fill('секрет + &');
  await page.evaluate(()=>upd(true));assert.equal(await page.locator('#mqttHost').inputValue(),'new-broker.local');
  fail=true;await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&document.getElementById('setupMessage').textContent.includes('mqtt_storage_failed'));
  assert(!posts.some(p=>p.path==='/api/setup'&&p.body.action==='complete'));assert(await page.evaluate(()=>!!document.getElementById('mqttForm').dataset.dirty));
  assert.equal(await page.locator('#mqttPassword').inputValue(),'секрет + &');
  await page.locator('#mqttSettings').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${role}-mqtt-${width}.png`)});
  fail=false;await page.locator('#setupNext').click();await page.waitForURL('http://wizard-fields.test/#settings');
  const saved=posts.findLast(p=>p.path==='/api/mqtt'),finish=posts.findLastIndex(p=>p.path==='/api/setup'&&p.body.action==='complete');
  assert.equal(saved.body.host,'new-broker.local');assert.equal(saved.body.password,'секрет + &');assert.equal(saved.body.setupToken,'777');
  assert(finish>posts.findLastIndex(p=>p.path==='/api/mqtt'));assert(!posts.some(p=>p.path==='/api/setup'&&p.body.action==='apply'));
  await page.waitForFunction(()=>wizard.loaded&&!wizard.active);assert.equal(await page.locator('#networkSettings #mqttSettings').count(),1);
  state.required=true;state.paused=false;state.step=9;state.restartRequired=false;
  await page.goto('http://wizard-fields.test/setup');await page.waitForFunction(()=>wizard.active&&mqttLoaded);
  await page.locator('#mqttHost').fill('later.local');await page.locator('#setupPause').click();
  await page.waitForURL('http://wizard-fields.test/#settings');
  assert.equal(posts.findLast(p=>p.path==='/api/mqtt').body.host,'later.local');assert(state.required&&state.paused);
  assert.deepEqual(errors,[]);console.log(`PASS: ${role} ${width}px, network-only first step, stable role fields, final MQTT reset/failure/save/finish`);
  await page.close();
 }}finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
