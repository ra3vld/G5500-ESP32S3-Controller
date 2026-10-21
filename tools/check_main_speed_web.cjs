// Main speed controls against an in-memory API; never connects to hardware.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/main-speed-web');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const sample=JSON.parse(process.env.SIM_STATE?fs.readFileSync(process.env.SIM_STATE,'utf8'):execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8',timeout:5000}).trim()).state;
(async()=>{
 fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:1000}}),posts=[],errors=[];
  page.on('pageerror',e=>errors.push(e.message));await page.addInitScript(()=>window.G5500_HTTP_ONLY=true);
  const state={...sample,deviceRole:'controller',model:'G5500',configurationLocked:false,moving:true,
   build:{...sample.build,drive:'BTS7960'},autotune:{phase:'idle',active:false},
   axes:sample.axes.map((a,i)=>({...a,speedEnabled:true,speedAdjustable:true,regulatorMode:i?'cascade':'simple',speedSetpoint:i?24.5:50,speedLimit:i?37.26:90,direction:1,tracking:true,motionReady:true,motionPhase:'run'}))};
  let release=null,blockNext=false,active=0,maxActive=0;const moves=[];
  await page.route('http://speed.test/**',async route=>{
   const request=route.request(),url=new URL(request.url());
   if(url.pathname==='/api/move')moves.push(request.method()==='POST'?new URLSearchParams(request.postData()).get('d'):url.searchParams.get('d'));
   if(request.method()==='POST'){
    const form=Object.fromEntries(new URLSearchParams(request.postData()));posts.push({path:url.pathname,form});
    if(url.pathname==='/api/speed'){
     ++active;maxActive=Math.max(maxActive,active);if(blockNext){blockNext=false;await new Promise(resolve=>release=resolve);}
     state.axes[form.axis==='az'?0:1].speedSetpoint=Number(form.value);--active;
    }
    return route.fulfill({json:{ok:true,result:'ok'}});
   }
   if(url.pathname==='/api/status')return route.fulfill({json:state});
   if(url.pathname==='/api/motion')return route.fulfill({json:{axes:[]}});
   if(url.pathname==='/api/rssi')return route.fulfill({json:{values:[],count:0,min:null,max:null,avg:null}});
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{ok:true,items:[],points:[],peers:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://speed.test/');await page.locator('#azManualSpeed').waitFor({state:'visible'});
  assert.equal(await page.locator('#azManualSpeedValue').textContent(),'50%');
  assert.equal(await page.locator('#elManualSpeedValue').textContent(),'24.5 °/с');
  assert.equal(await page.locator('#azManualSpeed').getAttribute('max'),'90');
  assert.equal(await page.locator('#elManualSpeed').getAttribute('max'),'37.26');
  assert.equal(Number(await page.locator('#azManualSpeed').getAttribute('min')),4.5);
  assert.equal(Number(await page.locator('#elManualSpeed').getAttribute('min')),1.863);
  const beforeDrag=posts.length;
  for(let value=30;value<=40;++value){
   await page.locator('#azManualSpeed').evaluate((e,value)=>{e.value=value;e.dispatchEvent(new Event('input',{bubbles:true}));},String(value));
   await page.waitForTimeout(20);
  }
  assert(posts.length>beforeDrag+1,'continuous input must apply speed before pause/change/release');
  await page.waitForFunction(()=>!manualSpeedRequests.az.busy&&manualSpeedRequests.az.pending===null&&window._forkState.axes[0].speedSetpoint===40);
  posts.length=0;
  const input=async(axis,value)=>page.locator('#'+axis+'ManualSpeed').evaluate((e,value)=>{e.value=value;e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));},String(value));
  blockNext=true;await input('az',51);await page.waitForFunction(()=>manualSpeedRequests.az.busy);
  for(let value=52;value<=80;++value)await input('az',value);
  assert.equal(posts.filter(p=>p.path==='/api/speed').length,1,'slow API must not queue every slider event');
  assert.equal(await page.locator('#azManualSpeedValue').textContent(),'80%');
  release();await page.waitForFunction(()=>!manualSpeedRequests.az.busy&&window._forkState.axes[0].speedSetpoint===80);
  assert.equal(posts.filter(p=>p.path==='/api/speed').length,2);assert.equal(maxActive,1);
  assert.deepEqual(posts.at(-1).form,{axis:'az',value:'80',rangeRevision:String(state.azRangeRevision)});
  await input('el',12.75);await page.waitForFunction(()=>window._forkState.axes[1].speedSetpoint===12.75);
  assert.equal(state.axes[0].speedSetpoint,80);assert.equal(state.axes[1].speedSetpoint,12.75);
  assert.equal(state.moving,true);assert.equal(state.axes[0].direction,1);assert.equal(state.axes[1].direction,1);
  assert(posts.every(p=>p.path==='/api/speed'),'sliders must not send settings/STOP/target');
  // A slow full-status refresh must not hold the next speed command hostage.
  await page.evaluate(()=>{window.speedTestUpd=upd;window.speedTestRefreshes=0;upd=async()=>{++window.speedTestRefreshes;await new Promise(resolve=>window.speedTestRelease=resolve);};});
  await input('az',44);await page.waitForFunction(()=>!manualSpeedRequests.az.busy&&window._forkState.axes[0].speedSetpoint===44,null,{timeout:1000});
  await input('az',45);await page.waitForFunction(()=>!manualSpeedRequests.az.busy&&window._forkState.axes[0].speedSetpoint===45,null,{timeout:1000});
  assert.equal(await page.evaluate(()=>window.speedTestRefreshes),0,'speed ACK must not wait for full status');
  await page.evaluate(()=>{upd=window.speedTestUpd;});
  // Ready live connections must carry speed without an HTTP round trip.
  await page.evaluate(()=>{window.speedTestSendLive=sendLive;window.speedTestPackets=[];live.ready=true;live.last=Date.now();sendLive=async(path,body)=>{window.speedTestPackets.push({path,body});return{status:200,json:async()=>({ok:true,result:'ok'})};};});
  const httpPosts=posts.length;await input('el',15);
  await page.waitForFunction(()=>!manualSpeedRequests.el.busy&&window._forkState.axes[1].speedSetpoint===15);
  assert.equal(posts.length,httpPosts);assert.equal(await page.evaluate(()=>window.speedTestPackets.at(-1).path),'/api/speed');
  await page.evaluate(()=>{sendLive=window.speedTestSendLive;live.ready=false;});
  state.autotune.active=true;await page.evaluate(()=>upd(true));await page.waitForFunction(()=>document.getElementById('azManualSpeed').disabled);assert(await page.locator('#azManualSpeed').isDisabled());assert(await page.locator('#elManualSpeed').isDisabled());
  state.autotune.active=false;state.axes[1].speedAdjustable=false;await page.evaluate(()=>upd(true));await page.waitForFunction(()=>document.getElementById('elManualSpeedControl').classList.contains('hide'));assert(!await page.locator('#elManualSpeedControl').isVisible());
  state.model='G800DXA';state.axes[1].speedAdjustable=true;await page.evaluate(()=>upd(true));await page.waitForFunction(()=>window._forkState.model==='G800DXA');assert(!await page.locator('#elManualSpeedControl').isVisible());
  state.axes[0].speedAdjustable=false;await page.evaluate(()=>upd(true));await page.waitForFunction(()=>document.getElementById('manualSpeeds').classList.contains('hide'));assert(!await page.locator('#manualSpeeds').isVisible());
  state.axes[0].speedAdjustable=true;state.deviceRole='hud';await page.evaluate(()=>upd(true));await page.waitForFunction(()=>window._forkState.deviceRole==='hud');assert(!await page.locator('#manualSpeeds').isVisible());
  state.deviceRole='controller';state.model='G5500';await page.evaluate(()=>upd(true));await page.waitForFunction(()=>!document.getElementById('manualSpeeds').classList.contains('hide'));
  for(const width of [1100,390,320]){
   await page.setViewportSize({width,height:1000});await page.locator('#manualSpeeds').scrollIntoViewIfNeeded();
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`overflow at ${width}`);
   assert(await page.locator('#manualSpeeds').evaluate(root=>[...root.querySelectorAll('input')].every(e=>e.getBoundingClientRect().height>=44&&e.getBoundingClientRect().right<=innerWidth)),`touch target at ${width}`);
   assert(await page.locator('#manualSpeeds').evaluate(e=>{const group=e.closest('.manual-drive'),ratio=e.getBoundingClientRect().width/(group.getBoundingClientRect().width-parseFloat(getComputedStyle(group).columnGap));return Math.abs(ratio-(innerWidth>600?1/7.2:1/3))<.01}),`compact slider column at ${width}`);
   assert(await page.locator('#azManualSpeed').evaluate(e=>e.getBoundingClientRect().width/e.parentElement.getBoundingClientRect().width>.95),`track fills compact column at ${width}`);
   await page.locator('.manual-drive').screenshot({path:path.join(out,`speed-${width}.png`)});
  }
  // Enter uses the same validated target path from either field.
  await page.locator('#taz').fill('90');await page.locator('#tel').fill('45');
  for(const id of ['taz','tel']){
   const count=posts.filter(p=>p.path==='/api/target').length;
   await Promise.all([page.waitForResponse(r=>new URL(r.url()).pathname==='/api/target'),page.locator('#'+id).press('Enter')]);
   assert.equal(posts.filter(p=>p.path==='/api/target').length,count+1);
   assert.deepEqual(posts.at(-1).form,{az:'90',el:'45',rangeRevision:String(state.azRangeRevision)});
  }
  let count=posts.filter(p=>p.path==='/api/target').length;
  await page.locator('#taz').fill('451');await page.locator('#taz').press('Enter');
  assert.equal(posts.filter(p=>p.path==='/api/target').length,count,'Enter must validate ranges');
  await page.locator('#taz').fill('');await page.locator('#tel').fill('');await page.locator('#tel').press('Enter');
  assert.equal(posts.filter(p=>p.path==='/api/target').length,count,'Empty Enter must not send a target');
  // Arrow keys leave fields alone, support both axes, and release only their own axis.
  await page.locator('#taz').press('ArrowUp');assert.deepEqual(moves,[]);
  const unfocus=()=>page.evaluate(()=>document.activeElement?.blur());await unfocus();
  for(const [key,dir,stop] of [['ArrowLeft','L','A'],['ArrowRight','R','A'],['ArrowUp','U','E'],['ArrowDown','D','E']]){
   moves.length=0;await page.keyboard.down(key);await page.keyboard.down(key);await page.keyboard.up(key);
   await page.waitForTimeout(50);assert.deepEqual(moves,[dir,stop],key+' must start once and stop on release');
  }
  moves.length=0;await page.keyboard.down('ArrowLeft');await page.keyboard.down('ArrowUp');await page.keyboard.up('ArrowLeft');await page.keyboard.up('ArrowUp');
  await page.waitForTimeout(50);assert.deepEqual(moves,['L','U','A','E']);
  moves.length=0;await page.keyboard.down('ArrowLeft');await page.keyboard.down('ArrowRight');await page.keyboard.up('ArrowLeft');await page.keyboard.up('ArrowRight');
  await page.waitForTimeout(50);assert.deepEqual(moves,['L','A','R','A'],'old opposite key must not stop newer direction');
  moves.length=0;await page.keyboard.down('ArrowRight');await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.keyboard.up('ArrowRight');
  await page.waitForTimeout(50);assert.deepEqual(moves,['R','A'],'blur must release movement');
  moves.length=0;await page.keyboard.down('ArrowDown');await page.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'));delete document.hidden;});await page.keyboard.up('ArrowDown');
  await page.waitForTimeout(50);assert.deepEqual(moves,['D','E'],'hidden page must release movement');
  moves.length=0;await page.keyboard.down('ArrowUp');await page.locator('#taz').focus();await page.keyboard.up('ArrowUp');
  await page.waitForTimeout(50);assert.deepEqual(moves,['U','E'],'editing must release movement');await unfocus();
  moves.length=0;await page.keyboard.down('ArrowLeft');await page.evaluate(()=>selectTab('history'));await page.keyboard.up('ArrowLeft');
  await page.waitForTimeout(50);assert.deepEqual(moves,['L','A'],'leaving control tab must stop');
  moves.length=0;await page.keyboard.press('ArrowRight');await page.waitForTimeout(50);assert.deepEqual(moves,[]);
  await page.evaluate(()=>selectTab('control'));await unfocus();
  state.model='G800DXA';await page.evaluate(()=>upd(true));await page.waitForFunction(()=>window._forkState.model==='G800DXA');
  await page.keyboard.press('ArrowUp');await page.keyboard.press('Control+ArrowLeft');await page.waitForTimeout(50);assert.deepEqual(moves,[],'hidden axes/modifiers must not move');
  await page.locator('#taz').fill('100');await page.locator('#tel').evaluate(e=>e.value='999');
  await Promise.all([page.waitForResponse(r=>new URL(r.url()).pathname==='/api/target'),page.locator('#taz').press('Enter')]);
  assert.deepEqual(posts.at(-1).form,{az:'100',rangeRevision:String(state.azRangeRevision)},'Enter must omit hidden EL on G800');await unfocus();
  state.autotune.active=true;await page.evaluate(()=>upd(true));await page.waitForFunction(()=>window._forkState.autotune.active);await page.keyboard.press('ArrowRight');await page.waitForTimeout(50);assert.deepEqual(moves,[],'autotune must block keyboard starts');
  assert.deepEqual(errors,[]);console.log('PASS: compact live speed sliders/5% minima/storage UI, Enter validation, held arrow axes/release/blur/tab/edit guards, 1100/390/320px');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
