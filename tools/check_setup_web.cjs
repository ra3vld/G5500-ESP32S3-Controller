// Wizard layout/interaction tests. Motion and persistence are tested in native C++.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/setup-web');fs.mkdirSync(out,{recursive:true});
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const hardware=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
const core=input=>JSON.parse(execFileSync(path.join(root,'build/simulator'),{input:input+'\n',encoding:'utf8'}).trim().split('\n').at(-1));
const initial=core('step 0').state;
const read=route=>JSON.parse(core('http '+route).body);
const motion=read('/api/motion'),azcal=read('/api/azcal'),elcal=read('/api/elcal');
const fixture=JSON.parse(fs.readFileSync(path.join(root,'build/pin-map-4.json')));
function geometry(){
 const issues=[],root=document.getElementById('settingsPanel')||document.querySelector('main'),shown=e=>e.getClientRects().length&&getComputedStyle(e).visibility!=='hidden';
 if(document.documentElement.scrollWidth>innerWidth+1)issues.push('page overflow');
 if(document.body.classList.contains('embedded')&&document.documentElement.scrollHeight>innerHeight+1)issues.push('inner vertical scrollbar '+document.documentElement.scrollHeight+' > '+innerHeight);
 for(const e of root.querySelectorAll('input[type=checkbox],input[type=radio]'))if(shown(e)){const r=e.getBoundingClientRect();if(Math.abs(r.width-18)>1||Math.abs(r.height-18)>1)issues.push('checkbox dimensions '+e.id+' '+r.width+'x'+r.height);}
 for(const e of root.querySelectorAll('button,input:not([type=checkbox]):not([type=radio]),select'))if(shown(e)){
  const r=e.getBoundingClientRect();if(r.width>(document.getElementById('settingsPanel')?innerWidth-24:innerWidth)+1)issues.push('wide control '+e.id);
  if(e.tagName==='BUTTON'&&r.height<43)issues.push('short button '+e.id);
  if(e.tagName==='BUTTON'&&(e.scrollWidth>e.clientWidth+2||e.scrollHeight>e.clientHeight+2))issues.push('clipped button '+e.id);
 }
 for(const group of root.querySelectorAll('.field-action'))if(shown(group)){
  const input=group.querySelector('input'),button=group.querySelector('button');if(!input||!button||!shown(input)||!shown(button))continue;
  const a=input.getBoundingClientRect(),b=button.getBoundingClientRect();
  if(innerWidth>600&&Math.abs(a.bottom-b.bottom)>2)issues.push('field/action misaligned '+input.id);
  if(innerWidth<=600&&b.top<a.bottom-1)issues.push('mobile field/action overlap '+input.id);
 }
 return issues;
}
function equipmentSized(step){
 const frame=document.getElementById('equipmentFrame'),doc=frame.contentDocument,main=doc?.querySelector('main');
 if(!main||!doc.body||!doc.documentElement)return false;
 const ready=!!main&&doc.body.dataset.setupStage===String(step)&&frame.contentWindow.innerWidth===frame.clientWidth&&
  doc.documentElement.scrollHeight<=frame.contentWindow.innerHeight+1&&
  Math.abs(frame.clientHeight-Math.max(400,main.getBoundingClientRect().height+24))<3;
 const signature=[step,frame.clientWidth,frame.clientHeight,main?.getBoundingClientRect().height,doc?.documentElement.scrollHeight].join(':');
 const previous=window.setupGeometryStable;
 if(!ready||previous?.signature!==signature){window.setupGeometryStable={signature,at:performance.now()};return false;}
 return performance.now()-previous.at>=150;
}
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const role of ['controller','hud']){
   const page=await browser.newPage({viewport:{width:1100,height:950}}),errors=[],posts=[];
   page.setDefaultTimeout(15000);page.on('pageerror',e=>{errors.push(e.message);console.error('PAGE ERROR:',e.message);});
   const status=structuredClone(initial);status.deviceRole=role;status.hudOnline=true;status.selectedCalOk=true;status.hudWifi=0;status.hudName='Антенна';status.hudSelected='0000000000001234';status.hudHost='192.168.1.30';status.hudUart=true;status.configurationLocked=false;
   const sequence=role==='hud'?[0,1,3,4,8,7,9]:[0,1,2,3,4,5,6,7,9];
   let forceRecovery=false,failSave=false;let state={version:1,required:true,paused:false,step:0,checked:0,generation:1,equipment:true,ready:true,role,driveInterface:0,simulation:false,token:777,testToken:0,storageOk:true};
   const config={driveInterface:role==='controller'?2:0,simulation:0,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:fixture.buttonGpio,buttonActiveLow:1,buttonPullup:1,pwmInverted:0,pins:fixture.pins};
   const hw={role,token:11,generation:1,recovery:false,trial:false,ready:true,hasConfirmed:true,configured:config,effective:config,confirmed:config,heapFree:60000,heapMaxBlock:30000,sppSupported:true,classicEsp32:true,backend:'NONE'};
   await page.route('http://setup.test/**',async route=>{
    const req=route.request(),u=new URL(req.url()),body=Object.fromEntries(new URLSearchParams(req.postData()||u.search));const json=data=>route.fulfill({json:data});
    if(u.pathname==='/setup'||u.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
    if(u.pathname==='/hardware'||u.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:hardware});
    if(req.method()==='POST')posts.push({path:u.pathname,body});
    if(u.pathname==='/api/setup'){if(forceRecovery)return route.fulfill({status:409,json:{ok:false,error:'recovery_only'}});
     if(req.method()==='POST'){
      if(body.action==='arm')state.testToken=state.testToken+1||1;
      if(body.action==='begin_save')state.testToken=0;
      if(body.action==='drive'){state.driveInterface=Number(body.driveInterface);state.simulation=body.simulation==='1';}
      if(body.action==='advance')state.step=sequence[sequence.indexOf(state.step)+1];
      if(body.action==='back')state.step=sequence[Math.max(0,sequence.indexOf(state.step)-1)];
      if(body.action==='pause')state.paused=true;
      if(body.action==='resume')state.paused=false;
      if(body.action==='complete')state.required=false;
      if(!['arm','renew','begin_save','end_save'].includes(body.action))state.generation++;if(body.action==='apply'){state.pendingNext=true;}
     }
     return json(state);
    }
    if(u.pathname==='/api/status')return json(status);
    if(u.pathname==='/api/settings'&&req.method()==='POST'){if(failSave)return route.fulfill({status:503,json:{ok:false,error:'storage_failed'}});Object.assign(status,body);return json({ok:true});}
    if(['/api/network','/api/ap','/api/wifi','/api/device','/api/hardware','/api/hardware/confirm'].includes(u.pathname)&&req.method()==='POST'){state.restartRequired=true;return json({ok:true});}
    if(u.pathname==='/api/network')return json({ethernetSupported:true,choice:0,connected:true,staIp:'192.168.1.20',apIp:'192.168.4.1',wifi:{mode:'dhcp'},eth:{mode:'dhcp'}});
    if(u.pathname==='/api/hardware/pins')return json(fixture.metadata);
    if(u.pathname==='/api/hardware/modbus')return json({pc:0,bus:0,address:1,node:201,first:201,count:1});
    if(u.pathname==='/api/hardware')return json(hw);
    if(u.pathname==='/api/motion')return json(motion);
    if(u.pathname==='/api/azcal')return json(azcal);
    if(u.pathname==='/api/elcal')return json(elcal);
    if(u.pathname==='/api/motion/coast')return json({phase:'idle',axis:0,reason:'',results:[{measured:0,degrees:[0,0],seconds:[0,0]},{measured:0,degrees:[0,0],seconds:[0,0]}]});
    if(u.pathname==='/api/hud/peers')return json({total:0,items:[]});
    if(u.pathname==='/api/rssi')return json({values:[],min:null,max:null,avg:null,count:0});
    if(u.pathname==='/api/ota')return json({available:false,reason:'flash_too_small'});
    return json({ok:true,result:'ok'});
   });
   await page.goto('http://setup.test/setup');await page.waitForFunction(()=>wizard.active&&window._forkState).catch(async e=>{console.error(await page.evaluate(()=>({html:document.getElementById('setupMessage')?.textContent,body:document.body.innerText.slice(-800),state:window._forkState})));throw e;});
   assert.deepEqual(await page.locator('#settingsPanel > .settings-section').evaluateAll(nodes=>nodes.map(n=>n.id)),['networkStatus','networkSettings','hudPanel','rotorSettings','equipmentSettings','calibrationSettings','motionSettings','peersSettings','programSettings','presetSettings','otaSettings','signalSettings','setupTests']);
   assert.equal(await page.locator('#setupChecked').count(),0);assert.equal(await page.locator('.nav a[href="/setup"]').count(),0);assert.equal(await page.locator('#settingsPanel > :first-child').getAttribute('id'),'setupEntry');
   for(const step of sequence){
    state.step=step;
    await page.evaluate(async s=>{wizard.state=s;renderSetup();if(s.step===6)await loadMotionSettings();},state);
    if(step===8){assert(await page.locator('#setupPeerLink a').isVisible(),'Controller setup link belongs to the visible peers step');assert.equal(await page.locator('#setupPeerLink a').getAttribute('href'),'http://192.168.1.30/setup');assert.match(await page.locator('#setupPeerReadiness').textContent(),/Антенна.*на связи.*AZ/);}
    if(step===9){assert.equal(await page.locator('#setupMovementCheck').isVisible(),role==='controller');assert.equal(await page.locator('#setupTargetForm').isVisible(),role==='controller');assert(await page.locator('#setupReview').isVisible());assert.match(await page.locator('#setupReviewList').textContent(),/Плата:.*Диапазон AZ:/);}
    for(const width of [320,390,600,760,1100,1440]){
     await page.setViewportSize({width,height:950});if([3,4].includes(step))await page.waitForFunction(equipmentSized,step);await page.evaluate(()=>document.querySelector('.wrap').scrollTop=0);
     assert.deepEqual(await page.evaluate(geometry),[],`${role}, step ${step}, width ${width}`);if([3,4].includes(step)){const f=page.frames().find(f=>f.url().includes('/hardware'));const issues=await f.evaluate(geometry);if(issues.length)console.error('FRAME METRICS',await f.evaluate(()=>({url:location.href,width:innerWidth,height:innerHeight,scroll:document.documentElement.scrollHeight,main:document.querySelector('main').getBoundingClientRect().toJSON(),parentWidth:parent.document.getElementById('equipmentFrame').clientWidth,groups:[...document.querySelectorAll('[data-pin-group]')].map(e=>[e.dataset.pinGroup,e.hidden])})));assert.deepEqual(issues,[],`hardware ${role} ${step} ${width}`);}
     if(step===4){const f=page.frameLocator('#equipmentFrame');assert(await f.locator('[name=buttonGpio]').isHidden());assert(await f.locator('[name=localInputMode]').isHidden());assert(await f.locator('#input-checks').isVisible());assert(await page.locator('#encoderSettings').isHidden());}
     if([390,1100].includes(width)){
      await page.screenshot({path:path.join(out,`${role}-${step}-${width}-top.png`)});
      await page.locator('#setupFooter').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${role}-${step}-${width}-bottom.png`)});
      if([3,4].includes(step)){const frame=page.frameLocator('#equipmentFrame');await frame.locator('#hardware').waitFor();for(const field of await frame.locator('fieldset:visible').all()){const label=(await field.locator('legend').first().textContent()).replace(/[^a-zа-яё0-9]+/ig,'-').slice(0,45);await field.scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${role}-${step}-${width}-hardware-${label}.png`)});}}
     }
    }
    const enlarged=await page.addStyleTag({content:'#settingsPanel label,#settingsPanel button,#settingsPanel input,#settingsPanel select,#setupHint{font-size:22px!important;line-height:1.5!important}'});
    let hardwareEnlarged;if([3,4].includes(step)){const f=page.frames().find(f=>f.url().includes('/hardware'));hardwareEnlarged=await f.addStyleTag({content:'label,input,select,button,small{font-size:22px!important;line-height:1.5!important}'});}
    for(const width of [320,390,600,760,1100,1440]){await page.setViewportSize({width,height:950});if([3,4].includes(step))await page.waitForFunction(equipmentSized,step);assert.deepEqual(await page.evaluate(geometry),[],`enlarged text ${role} ${step} ${width}`);if([390,1100].includes(width)){await page.locator('#setupFooter').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${role}-${step}-${width}-enlarged.png`)});}if([3,4].includes(step)){const f=page.frames().find(f=>f.url().includes('/hardware'));assert.deepEqual(await f.evaluate(geometry),[],`hardware enlarged ${role} ${step} ${width}`);}}
    if(hardwareEnlarged)await hardwareEnlarged.evaluate(e=>e.remove());await enlarged.evaluate(e=>e.remove());
   }
   // Combined inputs are configured once; the next step exposes only tests/calibration.
   hw.configured.driveInterface=state.driveInterface;hw.configured.simulation=Number(state.simulation);
   hw.configured.localInputMode=3;hw.configured.pins={...fixture.pins,localAzNegative:21,localAzPositive:22,localAzAdc:36,localElAdc:39};
   state.step=3;await page.evaluate(s=>{wizard.state=s;renderSetup();},state);
   await page.frameLocator('#equipmentFrame').locator('body').evaluate(()=>{dirty=false;return refresh();});
   const inputFrame=page.frameLocator('#equipmentFrame');
   await inputFrame.locator('[name=localInputMode]').selectOption('3');
   for(const [name,pin] of Object.entries({localAzNegative:21,localAzPositive:22,localAzAdc:36,localElAdc:39}))await inputFrame.locator(`[name="pin.${name}"]`).selectOption(String(pin));
   assert(await inputFrame.locator('#joystick-settings').isHidden());
   for(const axis of ['az','el']){if(role==='controller')await inputFrame.locator(`[name=${axis}SensorInverted]`).selectOption('1');}
   await page.evaluate(()=>hardwareCall('save'));const savedInputs=posts.findLast(p=>p.path==='/api/hardware');assert(savedInputs);assert.equal(savedInputs.body.localInputMode,'3');if(role==='controller'){assert.equal(savedInputs.body.azSensorInverted,'1');assert.equal(savedInputs.body.elSensorInverted,'1');}
   for(const step of [3,4]){
    state.step=step;await page.evaluate(s=>{wizard.state=s;renderSetup();},state);
    await page.waitForFunction(step=>document.getElementById('equipmentFrame').contentDocument.body.dataset.setupStage===String(step),step);
    if(step===4){assert(await inputFrame.locator('[name=localInputMode]').isHidden());assert(await inputFrame.locator('[name=localAzInvert]').isHidden());assert(await inputFrame.locator('#joystick-settings').isVisible());assert(await inputFrame.locator('#joystick-start').isEnabled());}
    for(const width of [320,390,600,760,1100,1440]){
     await page.setViewportSize({width,height:950});await page.waitForFunction(equipmentSized,step);
     const f=page.frames().find(f=>f.url().includes('/hardware'));assert.deepEqual(await f.evaluate(geometry),[]);
     if([390,1100].includes(width)){const target=step===3?'[name=localInputMode]':'#joystick-settings';await inputFrame.locator(target).scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${role}-combined-${step}-${width}.png`)});}
    }
    const combinedFrame=page.frames().find(f=>f.url().includes('/hardware'));
    const largeInputs=await combinedFrame.addStyleTag({content:'label,input,select,button,small{font-size:22px!important;line-height:1.5!important}'});
    for(const width of [320,390,600,760,1100,1440]){
     await page.setViewportSize({width,height:950});await page.waitForFunction(equipmentSized,step);
     assert.deepEqual(await combinedFrame.evaluate(geometry),[],`combined enlarged ${role} ${step} ${width}`);
     if(step===4&&[390,1100].includes(width)){await inputFrame.locator('#joystick-start').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${role}-combined-${step}-${width}-enlarged.png`)});}
    }
    await largeInputs.evaluate(e=>e.remove());
   }
   hw.configured.localInputMode=0;hw.configured.pins=fixture.pins;hw.configured.driveInterface=role==='controller'?2:0;state.restartRequired=false;state.pendingNext=false;
   await page.frameLocator('#equipmentFrame').locator('body').evaluate(()=>{dirty=false;return refresh();});
   if(role==='controller'){
    assert.deepEqual(await page.locator('#forkSettings-model option').evaluateAll(o=>o.map(e=>e.value)),['G5500','G800DXA','G800SA']);
    for(const [kind,drive,simulation,model] of [['G5500',0,false,'G5500'],['G800DXA',0,false,'G800DXA'],['G800SA',0,false,'G800SA'],['DIR_PWM',3,false],['SEPARATE_PWM',1,false],['BTS7960',2,false],['SIMULATION',2,true]]){
     state.step=2;await page.evaluate(s=>{wizard.state=s;renderSetup();},state);
     await page.locator('#setupDriveKind').selectOption(kind);
     await page.locator('#setupNext').focus();await page.evaluate(()=>pollSetup());assert.equal(await page.locator('#setupDriveKind').inputValue(),kind);
     assert.equal(await page.locator('#rotorSettings select:visible').count(),1,'One combined drive selector');assert(await page.locator('#forkSettings-model').isHidden());
     assert.deepEqual(await page.locator('#setupDriveKind option').evaluateAll(o=>o.map(e=>e.value)),['G5500','G800DXA','G800SA','DIR_PWM','SEPARATE_PWM','BTS7960','SIMULATION']);
     for(const width of [390,1100]){await page.setViewportSize({width,height:1100});await page.evaluate(()=>document.querySelector('.wrap').scrollTop=0);assert.deepEqual(await page.evaluate(geometry),[]);await page.screenshot({path:path.join(out,`drive-${kind}-${width}-choice.png`)});}
     await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===3);
     assert.equal(state.driveInterface,drive);assert.equal(state.simulation,simulation);if(model)assert.equal(status.model,model);
     await page.waitForFunction(d=>document.getElementById('equipmentFrame').contentDocument.getElementById('hardware').elements.driveInterface.value===String(d),drive);
     await page.frameLocator('#equipmentFrame').locator('body').evaluate(()=>refresh());
     const hwFrame=page.frameLocator('#equipmentFrame');assert.equal(await hwFrame.locator('[name=driveInterface]').inputValue(),String(drive),'Selected driver survives hardware refresh');
     assert.equal(await hwFrame.locator('[name=simulation]').inputValue(),simulation?'1':'0');
     if(drive===0)assert.equal(await hwFrame.locator('[name=pwmInverted]').inputValue(),'0');
     for(const name of ['azEnable','elEnable'])assert.equal(await hwFrame.locator('[name="pin.'+name+'"]').isVisible(),!simulation&&[1,3].includes(drive));
     if(!simulation&&drive===2)assert.match(await hwFrame.locator('[name="pin.azSpeedEnable"]').locator('..').textContent(),/необязательно/);
     if(drive===3){assert(await hwFrame.locator('[name="pin.azNegative"]').isHidden());assert(await hwFrame.locator('[name="pin.azPositive"]').isVisible());assert.match(await hwFrame.locator('[name="pin.azPositive"]').locator('..').textContent(),/DIR/);}
     for(const width of [390,1100]){await page.setViewportSize({width,height:1100});await page.waitForFunction(equipmentSized,3);await page.locator('#equipmentDriveSummary').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`drive-${kind}-${width}-gpio.png`)});}
     if(kind==='G800SA'){
      state.step=6;await page.evaluate(s=>{httpState.model='G800SA';renderStatus(httpState);wizard.state=s;renderSetup();},state);
      assert(await page.locator('#speedSettings').isVisible());assert.equal(await page.locator('#speedSettings').evaluate(e=>e.closest('.settings-section').id),'motionSettings');
      await page.locator('#speedEnabled').check();const beforeModule=posts.length;await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===7);
      assert(posts.slice(beforeModule).some(p=>p.path==='/api/settings'&&p.body.speedEnabled==='true'));
     }
    }
    state.step=2;await page.evaluate(s=>{wizard.state=s;renderSetup();},state);await page.locator('#setupDriveKind').selectOption('G5500');await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===3);
   }
   // A name remains a draft through blur and polling. Next writes before advancing.
   state.step=1;await page.evaluate(s=>{wizard.state=s;renderSetup();},state);
   const name='Пульт с длинным именем';await page.locator('#setupDeviceName').fill(name);
   await page.locator('#setupNext').focus();await page.evaluate(()=>pollSetup());
   assert.equal(await page.locator('#setupDeviceName').inputValue(),name);
   assert(await page.locator('#setupNameForm button').isHidden());
   const beforeName=posts.length;await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step!==1);
   const namePosts=posts.slice(beforeName);assert(namePosts.some(p=>p.path==='/api/settings'&&p.body.rotorName===name));assert(!namePosts.some(p=>p.path==='/api/device'||p.body.action==='apply'),'Name alone does not restart');
   assert(namePosts.findIndex(p=>p.path==='/api/settings')<namePosts.findIndex(p=>p.body.action==='advance'));
   // A failed save keeps the draft and step, and can be retried.
   state.step=1;await page.evaluate(s=>{wizard.state=s;renderSetup();},state);failSave=true;
   await page.locator('#setupDeviceName').fill('Черновик');await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy);
   assert.equal(await page.locator('#setupDeviceName').inputValue(),'Черновик');assert.equal(await page.evaluate(()=>wizard.state.step),1);assert.match(await page.locator('#setupMessage').textContent(),/Не удалось записать настройки/);
   failSave=false;await page.locator('#setupReset').click();await page.waitForFunction(()=>!wizard.busy);
   assert.equal(await page.locator('#setupDeviceName').inputValue(),name);assert(await page.locator('#setupReset').isDisabled());
   // Every edited network form is saved before a single restart is requested.
   state.step=0;await page.evaluate(s=>{wizard.state=s;renderSetup();},state);
   await page.locator('#networkChoice').selectOption('3');await page.locator('#ssid').fill('Новая сеть');await page.locator('#pass').fill('password123');await page.locator('#appass').fill('accesspoint123');
   const beforeNetwork=posts.length;await page.locator('#setupNext').click();await page.waitForFunction(()=>wizard.waiting);
   const networkPosts=posts.slice(beforeNetwork),paths=networkPosts.map(p=>p.path);
   assert(paths.indexOf('/api/network')<paths.indexOf('/api/wifi'));assert(paths.includes('/api/ap'));assert.equal(networkPosts.at(-1).body.action,'apply');
   assert(!('wifiAddress' in networkPosts.find(p=>p.path==='/api/network').body),'DHCP does not send empty static addresses');
   state.restartRequired=false;state.pendingNext=false;state.token++;state.step=1;await page.evaluate(s=>{wizard.state=s;wizard.restartToken=s.token-1;},state);await page.evaluate(()=>pollSetup());assert.equal(await page.evaluate(()=>wizard.waiting),false);
   // Hardware and Modbus drafts use one validated request; no inner Save button is needed.
   state.step=3;await page.evaluate(s=>{wizard.state=s;renderSetup();},state);
   await page.frameLocator('#equipmentFrame').locator('body').evaluate(async()=>{await refresh();await refreshModbus();});
   await page.waitForFunction(()=>wizard.hardwareCanApply);
   await page.frameLocator('#equipmentFrame').locator('[name=buttonActiveLow]').selectOption('0');
   await page.frameLocator('#equipmentFrame').locator('#modbus [name=address]').fill('2');
   await page.waitForFunction(()=>wizard.hardwareDirty);assert(await page.frameLocator('#equipmentFrame').locator('#save').isHidden());
   const beforeHardware=posts.length;await page.locator('#setupNext').click();await page.waitForFunction(()=>wizard.waiting);
   const hwPost=posts.slice(beforeHardware).find(p=>p.path==='/api/hardware');assert(hwPost);assert.equal(hwPost.body['mb.address'],'2');assert.equal(hwPost.body.buttonActiveLow,'0');
   assert.equal(posts.at(-1).body.action,'apply');
   state.restartRequired=false;state.pendingNext=false;state.token++;state.step=7;await page.evaluate(s=>{wizard.state=s;wizard.restartToken=s.token-1;},state);await page.evaluate(()=>pollSetup());
   if(role==='controller'){
    // Both axis forms and the common loop rate are flushed by one Next action.
    state.step=6;await page.evaluate(async s=>{wizard.state=s;renderSetup();await loadMotionSettings();},state);
    await page.locator('#coastDegrees').fill('7');await page.locator('[name=elCoastDegrees]').fill('8');await page.locator('#controlHz').selectOption('100');
    const beforeTuning=posts.length;await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===7);
    const tuning=posts.slice(beforeTuning);assert(tuning.some(p=>p.body.controlHz==='100'));assert(tuning.some(p=>p.body.coastDegrees==='7'));assert(tuning.some(p=>p.body.elCoastDegrees==='8'));
    state.step=5;await page.evaluate(s=>{wizard.state=s;renderSetup();},state);
    await page.locator('#azRangeForm [name=start]').fill('10');await page.locator('#azRangeForm [name=span]').fill('400');await page.locator('#elRangeForm [name=start]').fill('-10');await page.locator('#elRangeForm [name=span]').fill('190');
    const beforeRanges=posts.length;await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===6);
    const ranges=posts.slice(beforeRanges);assert(ranges.some(p=>p.path==='/api/azrange'&&p.body.start==='10'&&p.body.span==='400'));assert(ranges.some(p=>p.path==='/api/elrange'&&p.body.start==='-10'&&p.body.span==='190'));
   }
   state.step=7;await page.evaluate(s=>{wizard.state=s;wizard.step=null;renderSetup();},state);
   const formAssociation=await page.evaluate(()=>document.getElementById('forkSettings').elements.rotorName===document.getElementById('forkSettings-rotorName'));
   assert(formAssociation,'Moved program fields retain their form owner');
   await page.locator('#forkSettings-n1mmPort').fill('12042');await page.locator('#forkSettings-rotorName').fill('Новое имя N1MM');
   assert.equal(await page.locator('#setupNext').isEnabled(),true);
   await page.locator('#setupNext').click();await page.waitForFunction(()=>wizard.state.step===9);
   assert(posts.some(p=>p.path==='/api/setup'&&p.body.action==='advance'&&p.body.setupToken==='777'));
   assert(posts.some(p=>p.path==='/api/settings'&&p.body.rotorName==='Новое имя N1MM'&&p.body.n1mmPort==='12042'&&!('model' in p.body)));
   await page.locator('#presetEditor-label').fill('Сохранено при паузе');await page.locator('#presetEditor-az').fill('85');await page.locator('#presetEditor-el').fill('45');
   await page.locator('#setupPause').click();await page.waitForFunction(()=>location.pathname==='/');
   assert(posts.some(p=>p.path==='/api/preset'&&p.body.label==='Сохранено при паузе'&&p.body.az==='85'));
   await page.locator('#setupNotice').waitFor({state:'visible'});
   if(role==='controller'){forceRecovery=true;await page.evaluate(()=>pollSetup());await page.waitForURL('**/recovery');}
   assert.deepEqual(errors,[],role+' browser errors');
   await page.close();
  }
  console.log('PASS: controller/HUD steps, resumed state, pause, implicit multi-form save, failed-save retention, draft reset, form ownership, commissioning token, checkbox/button geometry and associated actions at 320/390/600/760/1100/1440 px, enlarged text; screenshots in '+out);
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
