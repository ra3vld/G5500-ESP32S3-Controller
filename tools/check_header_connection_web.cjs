const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const base=process.env.SIM_URL||'http://127.0.0.1:8080';
const out=process.env.ARTIFACT_DIR||path.resolve(__dirname,'../local-notes/header-connection-web');
(async()=>{
 fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 let heartbeat;
 try{
  const page=await browser.newPage({viewport:{width:1100,height:950}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));page.setDefaultTimeout(10000);
  await page.addInitScript(()=>window.G5500_HTTP_ONLY=true);
  const sample=await (await page.request.get(base+'/api/status')).json();
  let state={...sample,deviceRole:'controller'},mode='ok',release;
  const initial=new Promise(resolve=>release=resolve);
  await page.route('**/api/status',async route=>{
   await initial;
   if(mode==='offline')return route.abort('failed');
   if(mode==='error')return route.fulfill({status:503,json:state});
   if(mode==='invalid')return route.fulfill({json:{ok:false}});
   return route.fulfill({json:state});
  });
  const expect=async(text,kind)=>{
   await page.waitForFunction(([text,kind])=>{const n=document.getElementById('rotatorConnection');return n.textContent===text&&n.dataset.state===kind},[text,kind]);
  };
  const refresh=()=>page.evaluate(()=>upd(true));
  await page.goto(base,{waitUntil:'domcontentloaded'});
  await expect('Проверяем связь…','pending');release();
  await expect('Поворотка на связи','online');
  assert.equal(await page.locator('header #rotatorConnection[role=status]').count(),1);
  assert(!(await page.locator('header').innerText()).includes('оригинал'));
  assert.equal(await page.locator('footer').count(),1);
  assert.match(await page.locator('footer').innerText(),/G5500 Fork \d/);
  assert((await page.locator('#firmwareIdentity').innerText()).includes(sample.build.version.split('+')[0]));
  assert((await page.locator('#firmwareIdentity').innerText()).includes('коммит '+sample.build.commit));
  state={...state,build:{...sample.build,version:'2.3.4-dev.12+g1234567.dirty',commit:'1234567',dirty:true}};await refresh();
  await page.waitForFunction(()=>firmwareIdentity.textContent.includes('2.3.4-dev.12'));
  assert((await page.locator('#firmwareIdentity').innerText()).includes('изменённые исходники'));
  assert((await page.locator('footer').innerText()).includes('UA1CFM'));
  const links=await page.locator('footer a').evaluateAll(nodes=>nodes.map(n=>({href:n.href,rel:n.rel,target:n.target})));
  assert.deepEqual(links.map(l=>l.href),['https://github.com/ra3vld/G5500-ESP32S3-Controller','https://github.com/ua1cfm/G5500-ESP32S3-Controller']);
  assert(links.every(l=>l.target==='_blank'&&l.rel.includes('noopener')));
  state={...state,configurationLocked:true,setupRequired:false};await refresh();
  await page.waitForFunction(()=>window._forkState?.configurationLocked===true&&!document.getElementById('hardwareTrialNotice').classList.contains('hide'));
  for(const width of [390,1100]){
   await page.setViewportSize({width,height:950});
   await page.evaluate(()=>selectTab('control'));
   const notice=page.locator('#hardwareTrialNotice');
   assert(await notice.isVisible());
   assert.match(await notice.textContent(),/Подтвердить и перезапустить/);
   assert.equal(await notice.locator('a').getAttribute('href'),'/hardware');
   await page.evaluate(()=>document.querySelector('.wrap').scrollTop=0);
   assert(await notice.evaluate(el=>el.getBoundingClientRect().bottom<=innerHeight));
   assert(await page.evaluate(()=>hardwareTrialNotice.getBoundingClientRect().bottom<=controlPanel.getBoundingClientRect().top));
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   await page.screenshot({path:path.join(out,`hardware-confirmation-${width}.png`)});
   await page.evaluate(()=>selectTab('settings'));assert(await notice.isVisible());
  }
  state={...state,setupRequired:true};await refresh();
  await page.waitForFunction(()=>window._forkState?.setupRequired===true&&document.getElementById('hardwareTrialNotice').classList.contains('hide'));
  assert(await page.locator('#hardwareTrialNotice').isHidden());
  state={...state,configurationLocked:false,setupRequired:false};await refresh();
  await page.waitForFunction(()=>window._forkState?.configurationLocked===false&&window._forkState?.setupRequired===false&&document.getElementById('hardwareTrialNotice').classList.contains('hide'));
  assert(await page.locator('#hardwareTrialNotice').isHidden());
  await page.evaluate(()=>selectTab('control'));
  for(const failure of ['offline','error','invalid']){
   mode=failure;await refresh();await expect('Нет связи с устройством','offline');
   mode='ok';await refresh();await expect('Поворотка на связи','online');
  }
  state={...state,deviceRole:'hud',hudSelected:'000000000000',hudOnline:false};
  await refresh();await expect('Поворотка не выбрана','unselected');
  state={...state,hudSelected:'123456abcdef',hudName:'Антенна 1',hudOnline:true,hudTransport:'wifi'};
  await refresh();await expect('Поворотка на связи','online');
  assert((await page.locator('#rotatorConnection').getAttribute('title')).includes('Антенна 1 · Wi-Fi'));
  state={...state,hudOnline:false};await refresh();await expect('Нет связи с повороткой','offline');
  await page.getByRole('tab',{name:'Настройки',exact:true}).click();
  assert(await page.locator('#rotatorConnection').isVisible());
  state={...state,hudSelected:'abcdef123456',hudName:'Антенна 2',hudOnline:true,hudTransport:'uart'};
  await refresh();await expect('Поворотка на связи','online');
  assert((await page.locator('#rotatorConnection').getAttribute('title')).includes('Антенна 2 · UART'));
  // A stalled request and cached render must not keep the connection green.
  await page.route('**/api/status',route=>new Promise(resolve=>setTimeout(()=>{route.abort('timedout').catch(()=>{}).finally(resolve)},6000)));
  await refresh();await expect('Нет связи с устройством','offline');
  await page.evaluate(()=>queueLiveRender());await expect('Нет связи с устройством','offline');
  await page.unrouteAll({behavior:'wait'});
  state={...sample,deviceRole:'controller'};
  await page.route('**/api/status',route=>route.fulfill({json:state}));
  await refresh();await expect('Поворотка на связи','online');
  for(const width of [320,390,760,1100]){
   await page.setViewportSize({width,height:950});
   for(const tab of ['control','settings']){
    await page.evaluate(tab=>selectTab(tab),tab);
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth&&document.querySelector('.wrap').scrollWidth<=document.querySelector('.wrap').clientWidth));
    const overlaps=await page.evaluate(()=>{
     const a=document.getElementById('modelTitle').getBoundingClientRect(),b=document.getElementById('rotatorConnection').getBoundingClientRect();
     return Math.min(a.right,b.right)>Math.max(a.left,b.left)&&Math.min(a.bottom,b.bottom)>Math.max(a.top,b.top);
    });assert(!overlaps,'Header title and status must not overlap');
    await page.screenshot({path:path.join(out,`header-${tab}-${width}.png`)});
    await page.locator('footer').scrollIntoViewIfNeeded();
    assert(await page.locator('footer a').evaluateAll(nodes=>nodes.every(n=>n.getBoundingClientRect().height>=44)));
    assert(await page.evaluate(tab=>document.querySelector('footer').getBoundingClientRect().top>=document.getElementById(tab+'Panel').getBoundingClientRect().bottom,tab));
    await page.screenshot({path:path.join(out,`footer-${tab}-${width}.png`)});
   }
  }
  // Exercise real browser WebSocket events; HTTP failure alone is not disconnection.
  const livePage=await browser.newPage();livePage.on('pageerror',e=>errors.push(e.message));
  let socket,revision=0,liveState={...sample,deviceRole:'hud',hudSelected:'123456abcdef',hudName:'Антенна',hudOnline:true},httpFails=false;
  await livePage.route('**/api/status',route=>httpFails?route.abort('failed'):route.fulfill({json:liveState}));
  await livePage.routeWebSocket('**/api/live',ws=>socket=ws);
  await livePage.goto(base);await livePage.waitForFunction(()=>window._forkState);
  const send=()=>socket?.send(JSON.stringify({v:1,boot:1,type:'state',revision:++revision,state:liveState}));
  heartbeat=setInterval(send,200);
  await livePage.waitForFunction(()=>live.ready);
  httpFails=true;await livePage.evaluate(()=>upd(true));
  assert.equal(await livePage.locator('#rotatorConnection').innerText(),'Поворотка на связи');
  liveState={...liveState,hudOnline:false};send();
  await livePage.waitForFunction(()=>rotatorConnection.textContent==='Нет связи с повороткой');
  liveState={...liveState,hudSelected:'000000000000'};send();
  await livePage.waitForFunction(()=>rotatorConnection.textContent==='Поворотка не выбрана');
  liveState={...liveState,hudSelected:'123456abcdef',hudOnline:true};send();
  await livePage.waitForFunction(()=>rotatorConnection.textContent==='Поворотка на связи');
  clearInterval(heartbeat);heartbeat=null;
  await livePage.waitForFunction(()=>!live.ready&&rotatorConnection.textContent==='Нет связи с устройством');
  httpFails=false;await livePage.evaluate(()=>upd(true));
  await livePage.waitForFunction(()=>rotatorConnection.textContent==='Поворотка на связи');
  assert.deepEqual(errors,[]);
  console.log('PASS: HTTP/LIVE freshness, failure and recovery, HUD selection and remote availability, footer credits/links, responsive header/footer in both tabs');
 }finally{clearInterval(heartbeat);await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
