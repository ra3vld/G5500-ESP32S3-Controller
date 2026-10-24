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
 const issues=[],root=document.getElementById('settingsPanel'),shown=e=>e.getClientRects().length&&getComputedStyle(e).visibility!=='hidden';
 if(document.documentElement.scrollWidth>innerWidth+1)issues.push('page overflow');
 for(const e of root.querySelectorAll('input[type=checkbox],input[type=radio]'))if(shown(e)){const r=e.getBoundingClientRect();if(Math.abs(r.width-18)>1||Math.abs(r.height-18)>1)issues.push('checkbox dimensions '+e.id+' '+r.width+'x'+r.height);}
 for(const e of root.querySelectorAll('button,input:not([type=checkbox]):not([type=radio]),select'))if(shown(e)){
  const r=e.getBoundingClientRect();if(r.width>innerWidth-24+1)issues.push('wide control '+e.id);
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
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const role of ['controller','hud']){
   const page=await browser.newPage({viewport:{width:1100,height:950}}),errors=[],posts=[];
   page.setDefaultTimeout(15000);page.on('pageerror',e=>{errors.push(e.message);console.error('PAGE ERROR:',e.message);});
   const status=structuredClone(initial);status.deviceRole=role;status.hudOnline=true;status.selectedCalOk=true;status.hudWifi=0;status.hudName='Антенна';status.hudSelected='0000000000001234';status.hudHost='192.168.1.30';status.hudUart=true;status.configurationLocked=false;
   const sequence=role==='hud'?[0,1,3,4,8,7,9]:[0,1,2,3,4,5,6,7,9];
   let forceRecovery=false;let state={version:1,required:true,paused:false,step:0,checked:0,generation:1,equipment:true,ready:true,role,simulation:false,token:777,storageOk:true};
   const config={driveInterface:0,simulation:0,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:fixture.buttonGpio,buttonActiveLow:1,buttonPullup:1,pwmInverted:0,pins:fixture.pins};
   const hw={role,token:11,generation:1,recovery:false,trial:false,ready:true,hasConfirmed:true,configured:config,effective:config,confirmed:config,heapFree:60000,heapMaxBlock:30000,sppSupported:true,classicEsp32:true,backend:'NONE'};
   await page.route('http://setup.test/**',async route=>{
    const req=route.request(),u=new URL(req.url()),body=Object.fromEntries(new URLSearchParams(req.postData()||u.search));const json=data=>route.fulfill({json:data});
    if(u.pathname==='/setup'||u.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
    if(u.pathname==='/hardware'||u.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:hardware});
    if(req.method()==='POST')posts.push({path:u.pathname,body});
    if(u.pathname==='/api/setup'){if(forceRecovery)return route.fulfill({status:409,json:{ok:false,error:'recovery_only'}});
     if(req.method()==='POST'){
      if(body.action==='advance')state.step=sequence[sequence.indexOf(state.step)+1];
      if(body.action==='back')state.step=sequence[Math.max(0,sequence.indexOf(state.step)-1)];
      if(body.action==='pause')state.paused=true;
      if(body.action==='resume')state.paused=false;
      if(body.action==='complete')state.required=false;
      if(body.action!=='renew')state.generation++;
     }
     return json(state);
    }
    if(u.pathname==='/api/status')return json(status);
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
   assert.deepEqual(await page.locator('#settingsPanel > .settings-section').evaluateAll(nodes=>nodes.map(n=>n.id)),['networkSettings','hudPanel','rotorSettings','equipmentSettings','calibrationSettings','motionSettings','peersSettings','programSettings','presetSettings','otaSettings','signalSettings','setupTests']);
   assert.equal(await page.locator('#setupNext').isDisabled(),true);
   for(const step of sequence){
    state.step=step;
    await page.evaluate(async s=>{wizard.state=s;renderSetup();if(s.step===6)await loadMotionSettings();},state);
    if(step===8){assert(await page.locator('#setupPeerLink a').isVisible(),'Controller setup link belongs to the visible peers step');assert.equal(await page.locator('#setupPeerLink a').getAttribute('href'),'http://192.168.1.30/setup');assert.match(await page.locator('#setupPeerReadiness').textContent(),/Антенна.*на связи.*AZ/);}
    if(step===9){assert(await page.locator('#setupReview').isVisible());assert.match(await page.locator('#setupReviewList').textContent(),/Плата:.*Диапазон AZ:/);}
    for(const width of [320,390,600,760,1100,1440]){
     await page.setViewportSize({width,height:950});if([3,4].includes(step))await page.waitForFunction(step=>{const f=document.getElementById('equipmentFrame'),d=f.contentDocument;const main=d?.querySelector('main');return !!main&&d?.body?.dataset.setupStage===String(step)&&Math.abs(f.getBoundingClientRect().height-(main.getBoundingClientRect().height+24))<3;},step);await page.evaluate(()=>document.querySelector('.wrap').scrollTop=0);
     assert.deepEqual(await page.evaluate(geometry),[],`${role}, step ${step}, width ${width}`);
     if(step===4)assert(await page.frameLocator('#equipmentFrame').getByRole('group',{name:'Кнопка подтверждения цели',exact:true}).isVisible());
     if([390,1100].includes(width)){
      await page.screenshot({path:path.join(out,`${role}-${step}-${width}-top.png`)});
      await page.locator('#setupFooter').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${role}-${step}-${width}-bottom.png`)});
      if([3,4].includes(step)){const frame=page.frameLocator('#equipmentFrame');await frame.locator('#hardware').waitFor();for(const field of await frame.locator('fieldset:visible').all()){const label=(await field.locator('legend').first().textContent()).replace(/[^a-zа-яё0-9]+/ig,'-').slice(0,45);await field.scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${role}-${step}-${width}-hardware-${label}.png`)});}}
     }
    }
    await page.setViewportSize({width:390,height:950});const enlarged=await page.addStyleTag({content:'#settingsPanel label,#settingsPanel button,#setupHint{font-size:22px!important;line-height:1.5!important}'});assert.deepEqual(await page.evaluate(geometry),[],`enlarged text ${role} ${step}`);await enlarged.evaluate(e=>e.remove());
   }
   state.step=7;await page.evaluate(s=>{wizard.state=s;wizard.step=null;renderSetup();},state);
   const formAssociation=await page.evaluate(()=>document.getElementById('forkSettings').elements.rotorName===document.getElementById('forkSettings-rotorName'));
   assert(formAssociation,'Moved program fields retain their form owner');
   await page.locator('#setupChecked').check();assert.equal(await page.locator('#setupNext').isEnabled(),true);
   await page.locator('#setupNext').click();await page.waitForFunction(()=>wizard.state.step===9);
   assert(posts.some(p=>p.path==='/api/setup'&&p.body.action==='advance'&&p.body.setupToken==='777'));
   await page.locator('#setupPause').click();await page.waitForFunction(()=>location.pathname==='/');
   await page.locator('#setupNotice').waitFor({state:'visible'});
   if(role==='controller'){forceRecovery=true;await page.evaluate(()=>pollSetup());await page.waitForURL('**/recovery');}
   assert.deepEqual(errors,[],role+' browser errors');
   await page.close();
  }
  console.log('PASS: controller/HUD steps, resumed state, pause, form ownership, commissioning token, checkbox/button geometry and associated actions at 320/390/600/760/1100/1440 px, enlarged text; screenshots in '+out);
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
