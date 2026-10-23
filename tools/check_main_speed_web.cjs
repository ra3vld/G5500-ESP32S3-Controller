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
   axes:sample.axes.map((a,i)=>({...a,speedEnabled:true,speedAdjustable:true,regulatorMode:i?'cascade':'simple',speedSetpoint:i?24.5:50,speedLimit:i?37.26:90,measuredVelocity:i?-12.75:8.25,calibrated:true,direction:1,tracking:true,motionReady:true,motionPhase:'run'}))};
  let blockStatus=false,releaseStatus=null,statusRequests=0,holdAckDelay=0,release=null,blockNext=false,active=0,maxActive=0;const moves=[],moveBodies=[];let dropStops=false,dropHolds=false,delayHold=false,releaseHold=null;
  await page.route('http://speed.test/**',async route=>{
   const request=route.request(),url=new URL(request.url());
   if(url.pathname==='/api/move'){
    const body=Object.fromEntries(request.method()==='POST'?new URLSearchParams(request.postData()):url.searchParams);moves.push(body.d);moveBodies.push(body);
    if(body.d==='H'&&holdAckDelay)await new Promise(resolve=>setTimeout(resolve,holdAckDelay));
    if(body.d==='H'&&delayHold){delayHold=false;await new Promise(resolve=>releaseHold=resolve);}
    if((body.d==='H'&&dropHolds)||(body.d!=='H'&&dropStops))return route.abort();
   }
   if(request.method()==='POST'){
    const form=Object.fromEntries(new URLSearchParams(request.postData()));posts.push({path:url.pathname,form});
    if(url.pathname==='/api/speed'){
     ++active;maxActive=Math.max(maxActive,active);if(blockNext){blockNext=false;await new Promise(resolve=>release=resolve);}
     state.axes[form.axis==='az'?0:1].speedSetpoint=Number(form.value);--active;
    }
    return route.fulfill({json:{ok:true,result:'ok'}});
   }
   if(url.pathname==='/api/status'){++statusRequests;if(blockStatus)await new Promise(resolve=>releaseStatus=resolve);return route.fulfill({json:state});}
   if(url.pathname==='/api/motion')return route.fulfill({json:{axes:[]}});
   if(url.pathname==='/api/rssi')return route.fulfill({json:{values:[],count:0,min:null,max:null,avg:null}});
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{ok:true,items:[],points:[],peers:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  let telemetry=null,liveAckDelay=0;const livePackets=[];
  await page.routeWebSocket('**/api/live',ws=>{
   telemetry=ws;ws.onMessage(packet=>{livePackets.push(packet);const form=Object.fromEntries(new URLSearchParams(packet));if(form.path==='/api/move'){moves.push(form.d);moveBodies.push(form);ws.send(JSON.stringify({v:1,type:'result',boot:form.boot,id:Number(form.cid),result:{ok:true,result:'ok'}}));return;}if(liveAckDelay){const p=Object.fromEntries(new URLSearchParams(packet));setTimeout(()=>ws.send(JSON.stringify({v:1,type:'result',boot:p.boot,id:Number(p.cid),result:{ok:true,result:'ok'}})),liveAckDelay);}});
   ws.send(JSON.stringify({v:1,type:'state',boot:'abcdef',revision:1,state}));
  });
  await page.goto('http://speed.test/');await page.locator('#azManualSpeed').waitFor({state:'visible'});
  assert.equal(await page.locator('#azManualSpeedValue').textContent(),'50%');
  assert.equal(await page.locator('#elManualSpeedValue').textContent(),'24.5 °/с');
  assert.equal(await page.locator('#azManualSpeedActual').textContent(),'8.3 °/с');
  assert.equal(await page.locator('#elManualSpeedActual').textContent(),'12.8 °/с');
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
  assert.equal(await page.locator('#elManualSpeedValue').textContent(),'12.8 °/с');
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
   assert(await page.locator('.manual-drive').evaluate(e=>[...e.querySelectorAll('.btn,output')].every(n=>n.scrollWidth<=n.clientWidth)),`control text must fit at ${width}`);
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
  // Keyboard uses a sequenced lease, never an unbounded L/R/U/D command.
  await page.locator('#taz').press('ArrowUp');assert.deepEqual(moves,[]);
  const unfocus=()=>page.evaluate(()=>document.activeElement?.blur());await unfocus();
  const resetMoves=()=>{moves.length=0;moveBodies.length=0;};
  for(const [key,az,el] of [['ArrowLeft','-1','0'],['ArrowRight','1','0'],['ArrowUp','0','1'],['ArrowDown','0','-1']]){
   resetMoves();await page.keyboard.down(key);await page.keyboard.down(key);await page.keyboard.up(key);
   await page.waitForTimeout(80);assert.deepEqual(moves,['H','S'],key+' must open and close one gesture');
   assert.equal(moveBodies[0].az,az);assert.equal(moveBodies[0].el,el);assert.equal(moveBodies[0].gesture,moveBodies[1].gesture);
   assert(Number(moveBodies[1].serial)>Number(moveBodies[0].serial));
  }
  resetMoves();await page.keyboard.down('ArrowLeft');await page.waitForTimeout(30);await page.keyboard.down('ArrowUp');await page.waitForTimeout(30);await page.keyboard.up('ArrowLeft');await page.waitForTimeout(250);await page.keyboard.up('ArrowUp');
  await page.waitForTimeout(80);assert(moves.includes('A'));assert.equal(moves.at(-1),'S');
  assert(moveBodies.some(b=>b.d==='H'&&b.az==='0'&&b.el==='1'),'releasing AZ must preserve held EL');
  resetMoves();await page.keyboard.down('ArrowLeft');await page.waitForTimeout(30);await page.keyboard.down('ArrowRight');await page.waitForTimeout(30);await page.keyboard.up('ArrowLeft');
  assert.equal(await page.evaluate(()=>keyboardAxes.az?.key),'ArrowRight');await page.keyboard.up('ArrowRight');
  await page.waitForTimeout(80);assert.deepEqual(moves,['H','H','S']);
  for(const [key,release] of [['ArrowRight',()=>page.evaluate(()=>window.dispatchEvent(new Event('blur')))],['ArrowDown',()=>page.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'));delete document.hidden;})],['ArrowUp',()=>page.locator('#taz').focus()]]){
   resetMoves();await page.keyboard.down(key);await release();await page.keyboard.up(key);await page.waitForTimeout(80);assert.deepEqual(moves,['H','S']);await unfocus();
  }
  resetMoves();await page.keyboard.down('ArrowLeft');await page.evaluate(()=>selectTab('history'));await page.keyboard.up('ArrowLeft');
  await page.waitForTimeout(80);assert.deepEqual(moves,['H','S'],'leaving control tab must close the gesture');
  resetMoves();await page.keyboard.press('ArrowRight');await page.waitForTimeout(50);assert.deepEqual(moves,[]);
  await page.evaluate(()=>selectTab('control'));await unfocus();
  // Starting manual input aborts a slow full-status read and prevents new
  // polling requests from starving HOLD/STOP on the serial HTTP server.
  resetMoves();blockStatus=true;const polling=page.waitForRequest('**/api/status');
  await page.evaluate(()=>{window.blockedManualStatus=upd(true);});await polling;
  await page.keyboard.down('ArrowRight');
  await page.waitForFunction(()=>!statusPending&&keyboardHeld());const statusAtHold=statusRequests;
  await page.waitForTimeout(1100);assert.equal(statusRequests,statusAtHold);
  assert.equal(await page.evaluate(()=>keyboardHeld()),true);
  blockStatus=false;releaseStatus();await page.keyboard.up('ArrowRight');await page.waitForTimeout(100);
  // A 600ms ACK is healthy; renew immediately after it rather than adding
  // another timer interval to an already delayed request.
  resetMoves();holdAckDelay=600;await page.keyboard.down('ArrowRight');
  await page.waitForTimeout(1600);assert.equal(await page.evaluate(()=>keyboardHeld()),true);
  assert(moves.filter(d=>d==='H').length>=3);await page.keyboard.up('ArrowRight');
  holdAckDelay=0;await page.waitForTimeout(700);
  // Missing ACKs still stop within the 900ms command deadline.
  resetMoves();holdAckDelay=1600;await page.keyboard.down('ArrowRight');
  await page.waitForTimeout(1050);assert.equal(await page.evaluate(()=>keyboardHeld()),false);
  assert.equal(moves.at(-1),'S');await page.keyboard.up('ArrowRight');
  holdAckDelay=0;await page.waitForTimeout(700);
  // Long holds use a fixed renewal rate, independent of OS repeat speed/flags.
  resetMoves();await page.keyboard.down('ArrowRight');
  await page.evaluate(()=>{for(let i=0;i<30;++i)document.body.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true}));});
  await page.waitForTimeout(1600);await page.keyboard.up('ArrowRight');await page.waitForTimeout(80);
  assert(moves.filter(d=>d==='H').length>=6&&moves.filter(d=>d==='H').length<=10);
  assert(moveBodies.every(b=>b.gesture===moveBodies[0].gesture));assert.equal(moves.at(-1),'S');
  let total=moves.length;await page.waitForTimeout(600);assert.equal(moves.length,total,'released keys must not renew');
  // Losing STOP retries only the same closed gesture; the device lease is tested natively.
  resetMoves();await page.keyboard.down('ArrowUp');await page.waitForTimeout(40);dropStops=true;await page.keyboard.up('ArrowUp');
  await page.waitForTimeout(350);dropStops=false;assert.equal(moves.filter(d=>d==='S').length,3);
  assert(new Set(moveBodies.filter(b=>b.d==='S').map(b=>b.serial)).size===1);
  total=moves.length;await page.waitForTimeout(400);assert.equal(moves.length,total);assert.equal(await page.evaluate(()=>keyboardHeld()),false);
  // A failed renewal cancels the hold rather than falling back to a raw jog.
  resetMoves();await page.keyboard.down('ArrowRight');await page.waitForTimeout(30);dropHolds=true;
  await page.waitForTimeout(350);dropHolds=false;await page.keyboard.up('ArrowRight');
  assert.equal(await page.evaluate(()=>keyboardHeld()),false);assert.equal(moves.at(-1),'S');
  assert(!moves.some(d=>['L','R','U','D'].includes(d)));
  // Capture-phase keyup survives handlers elsewhere stopping event propagation.
  resetMoves();await page.keyboard.down('ArrowDown');
  await page.evaluate(()=>document.addEventListener('keyup',event=>event.stopImmediatePropagation(),{once:true}));
  await page.keyboard.up('ArrowDown');await page.waitForTimeout(80);assert.deepEqual(moves,['H','S']);
  // WebSocket commands bypass busy HTTP telemetry; disconnect falls back to
  // HTTP within the same bounded gesture, without replay after reconnect.
  await page.evaluate(()=>{clearTimeout(live.retry);live.retry=null;window.G5500_HTTP_ONLY=false;connectLive(81);});
  await page.waitForFunction(()=>live.ready,null,{timeout:3000}).catch(async e=>{console.error('live setup',await page.evaluate(()=>({socket:!!live.socket,retry:!!live.retry,boot:live.boot,ready:live.ready,last:live.last})),!!telemetry,errors);throw e;});resetMoves();
  liveAckDelay=2000;
  await page.evaluate(()=>{window.slowLiveReply=sendLive('/api/speed',{axis:'az',value:40});});
  await page.waitForTimeout(1700);
  assert.equal(await page.evaluate(()=>live.ready&&live.pending.size===1),true,'brief TCP stall must preserve socket and pending ACK');
  assert.equal(await page.evaluate(async()=>{const r=await window.slowLiveReply;return r.status;}),200);
  liveAckDelay=0;livePackets.length=0;
  await page.keyboard.down('ArrowRight');await page.waitForTimeout(250);
  telemetry.close();await page.waitForTimeout(500);
  assert.equal(await page.evaluate(()=>keyboardHeld()),true,'disconnect must preserve the hold through HTTP fallback');
  assert(moves.filter(d=>d==='H').length>=3);assert(!moves.includes('S'));
  await page.evaluate(()=>{clearTimeout(live.retry);live.retry=null;connectLive(81);});
  await page.waitForFunction(()=>live.ready);
  assert.equal(await page.evaluate(()=>keyboardHeld()),true,'same-boot reconnect must preserve the active hold');
  telemetry.send(JSON.stringify({v:1,type:'state',boot:'fedcba',revision:1,state}));
  await page.waitForFunction(()=>!keyboardHeld());
  await page.keyboard.up('ArrowRight');await page.waitForTimeout(80);assert.equal(moves.at(-1),'S');
  assert(livePackets.some(p=>new URLSearchParams(p).get('path')==='/api/move'),'ready WebSocket must carry manual commands');
  await page.evaluate(()=>{window.G5500_HTTP_ONLY=true;live.socket?.close();clearTimeout(live.retry);live.retry=null;});
  // Delayed HTTP HOLD ACK must not delay release or restart after its late reply.
  resetMoves();delayHold=true;await page.keyboard.down('ArrowRight');
  await page.waitForFunction(()=>keyboardStream.busy!==null);
  await page.keyboard.up('ArrowRight');await page.waitForTimeout(40);
  assert.deepEqual(moves,['H','S']);releaseHold();await page.waitForTimeout(250);
  assert.deepEqual(moves,['H','S']);
  // Screen buttons use the same finite lease, ACK checks and independent axes.
  const pointer=async(direction,type,id)=>page.locator('.movebtn[data-dir="'+direction+'"]').dispatchEvent(type,{pointerId:id,pointerType:'touch',button:0});
  resetMoves();await pointer('L','pointerdown',11);await page.waitForTimeout(650);
  assert(moves.filter(d=>d==='H').length>=3);assert(!moves.includes('L'));
  await pointer('U','pointerdown',12);await page.waitForTimeout(30);await pointer('L','pointerup',11);
  await page.waitForTimeout(250);assert(moves.includes('A'));
  assert(moveBodies.some(b=>b.d==='H'&&b.az==='0'&&b.el==='1'));
  await pointer('U','pointercancel',12);await page.waitForTimeout(60);assert.equal(moves.at(-1),'S');
  resetMoves();await pointer('R','pointerdown',13);await page.waitForTimeout(30);dropHolds=true;
  await page.waitForTimeout(350);dropHolds=false;assert.equal(await page.evaluate(()=>keyboardHeld()),false);assert.equal(moves.at(-1),'S');
  await pointer('R','pointerup',13);total=moves.length;await page.waitForTimeout(250);assert.equal(moves.length,total);
  assert(!await page.locator('#forkMessage').textContent().then(t=>t.includes('Проверьте связь и состояние устройства перед повтором')));
  resetMoves();
  state.model='G800DXA';await page.evaluate(()=>upd(true));await page.waitForFunction(()=>window._forkState.model==='G800DXA');
  await page.keyboard.press('ArrowUp');await page.keyboard.press('Control+ArrowLeft');await page.waitForTimeout(50);assert.deepEqual(moves,[],'hidden axes/modifiers must not move');
  await page.locator('#taz').fill('100');await page.locator('#tel').evaluate(e=>e.value='999');
  await Promise.all([page.waitForResponse(r=>new URL(r.url()).pathname==='/api/target'),page.locator('#taz').press('Enter')]);
  assert.deepEqual(posts.at(-1).form,{az:'100',rangeRevision:String(state.azRangeRevision)},'Enter must omit hidden EL on G800');await unfocus();
  state.autotune.active=true;await page.evaluate(()=>upd(true));await page.waitForFunction(()=>window._forkState.autotune.active);await page.keyboard.press('ArrowRight');await page.waitForTimeout(50);assert.deepEqual(moves,[],'autotune must block keyboard starts');
  // A received goal visibly asks for permission; only its explicit click executes it.
  await page.evaluate(()=>{window.speedTestUpdateFork=updateFork;updateFork=()=>{};});
  state.autotune.active=false;state.model='G5500';state.controlMode='manual';state.moving=false;
  state.axes.forEach(a=>{a.tracking=false;a.direction=0;a.holdArmed=false;});
  state.pendingValid=true;state.pending={hasAz:true,az:100,hasEl:true,el:100};
  const render=()=>page.evaluate(j=>window.speedTestUpdateFork(j),state),action=page.locator('#executeTarget');
  await render();
  await page.locator('#taz').fill('100');await page.locator('#tel').fill('100');
  assert.equal(await action.textContent(),'Ехать 100/100');
  assert.equal(await action.evaluate(e=>getComputedStyle(e).backgroundColor),'rgb(242, 201, 76)');
  assert.equal(await page.locator('#waitingTarget').textContent(),'Ожидает разрешения: AZ 100° / EL 100°');
  for(const width of [1100,390,320]){
   await page.setViewportSize({width,height:1000});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`pending overflow at ${width}`);
   assert(await page.locator('#waitingTarget').evaluate(e=>parseFloat(getComputedStyle(e).fontSize)>=22&&e.scrollWidth<=e.clientWidth),'pending goal must be large and fully visible');
   assert(await action.evaluate(e=>e.scrollWidth<=e.clientWidth),'execute label must fit the button');
   assert(await action.evaluate(e=>{const r=document.createRange();r.setStart(e.firstChild,e.textContent.indexOf(' ')+1);r.setEnd(e.firstChild,e.textContent.length);return r.getClientRects().length===1;}),'pending coordinates must stay on one line');
   assert(await page.locator('.manual-drive').evaluate(e=>[...e.querySelectorAll('.btn,output')].every(n=>n.scrollWidth<=n.clientWidth)),`pending control text must fit at ${width}`);
   assert(await page.locator('.movebtn[data-dir="R"] span').evaluate(e=>{const r=document.createRange();r.setStart(e.firstChild,0);r.setEnd(e.firstChild,5);return r.getClientRects().length===1;}),'RIGHT must stay on one line');
   await page.locator('.command-card').screenshot({path:path.join(out,`pending-${width}.png`)});
  }
  const countExecute=()=>posts.filter(p=>p.path==='/api/execute').length;
  const beforeExecute=countExecute();await page.waitForTimeout(150);assert.equal(countExecute(),beforeExecute);
  await action.click();assert.equal(countExecute(),beforeExecute+1);assert.equal(posts.at(-1).path,'/api/execute');
  state.moving=true;await render();assert.equal(await action.textContent(),'stop');
  assert.equal(await action.evaluate(e=>e.classList.contains('pending-target')),false,'pending must never hide STOP');
  const stopCount=moves.length;await action.click();assert.equal(moves[stopCount],'S');
  state.moving=false;state.pending={hasAz:false,az:0,hasEl:true,el:45};await render();assert.equal(await action.textContent(),'Ехать EL 45°');
  state.model='G800DXA';state.pending={hasAz:true,az:0,hasEl:true,el:100};await render();assert.equal(await action.textContent(),'Ехать 0°');
  assert.equal(await page.locator('#waitingTarget').textContent(),'Ожидает разрешения: 0°');
  state.pendingValid=false;state.lastTarget={hasAz:true,az:170,hasEl:false,el:0};await render();
  assert.equal(await action.textContent(),'170°');assert.equal(await action.evaluate(e=>e.classList.contains('good')),true);
  assert.equal(await page.locator('#waitingTarget').textContent(),'Нет ожидающей цели');
  state.axes[0].motionFault='feedback_stale';await render();assert.equal(await page.locator('#azManualSpeedActual').textContent(),'—');
  state.axes[0].motionFault='';state.axes[0].calibrated=false;await render();assert.equal(await page.locator('#azManualSpeedActual').textContent(),'—');
  state.axes[0].calibrated=true;state.axes[0].measuredVelocity=0;await render();assert.equal(await page.locator('#azManualSpeedActual').textContent(),'0.0 °/с');
  state.model='G5500';state.pendingValid=true;state.pending={hasAz:true,az:450,hasEl:true,el:180};
  for(const pending of [true,false]){
   state.pendingValid=pending;state.lastTarget={hasAz:true,az:450,hasEl:true,el:180};await render();
   for(const width of [390,320]){
    await page.setViewportSize({width,height:1000});
    assert(await page.locator('.manual-drive').evaluate(e=>[...e.querySelectorAll('.btn,output')].every(n=>n.scrollWidth<=n.clientWidth)),`maximum target labels must fit at ${width}, pending=${pending}`);
    assert(await page.locator('#movementButtons').evaluate(e=>[...e.querySelectorAll('.btn:not(.blank)')].every(n=>{const box=n.getBoundingClientRect(),r=document.createRange();r.selectNodeContents(n);return [...r.getClientRects()].every(b=>b.left>=box.left&&b.right<=box.right+1&&b.top>=box.top&&b.bottom<=box.bottom+1);})),`button text must stay inside at ${width}, pending=${pending}`);
   }
  }
  assert.deepEqual(errors,[]);console.log('PASS: pending goal/explicit execution/STOP priority, rounded setpoint and actual speed, compact live sliders, Enter, leased keyboard/pointer guards, 1100/390/320px');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
