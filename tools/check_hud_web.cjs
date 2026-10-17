const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const base=process.env.SIM_URL;
const state=async url=>(await fetch(url+'/api/status')).json();
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try {
  const page=await browser.newPage({viewport:{width:1100,height:850}}),errors=[];
  await page.addInitScript(()=>{window.G5500_HTTP_ONLY=true}); // Exercise the retained HTTP fallback.
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base);await page.waitForFunction(()=>window._forkState?.deviceRole==='hud');
  assert.match(await page.locator('#hudLink').textContent(),/Нет связи/);
  assert.equal(await page.locator('#az').textContent(),'—');
  await page.getByRole('tab',{name:'Настройки',exact:true}).click();
  await page.locator('#hudControls summary').click();
  for(const [port,name] of [[process.env.ROTOR_PORT,'R101'],[process.env.ROTOR_TWO_PORT,'R102']]) {
   await page.locator('#hudAddress').fill('127.0.0.1');
   await page.locator('#hudPeerPort').fill(port);
   await page.getByRole('button',{name:'Проверить адрес',exact:true}).click();
   const found=page.locator('#hudDiscovered button').filter({hasText:name});
   await found.waitFor();await found.click();
   await page.locator('#hudPeers button').filter({hasText:name}).waitFor();
  }
  await page.waitForFunction(()=>window._forkState?.hudOnline);
  assert.match(await page.locator('#hudHeading').textContent(),/R101/);
  assert.equal(await page.locator('#caltoggle').isVisible(),false);
  assert.equal(await page.locator('#speedSettings').isVisible(),false);
  await page.getByRole('tab',{name:'Управление',exact:true}).click();
  assert.match(await page.locator('#selectedRotor').textContent(),/R101.*На связи/);
  // Wait for the remote result: HTTP 202 must never render an execution success.
  await page.route('**/api/hud/command?*',async route=>{await new Promise(r=>setTimeout(r,400));await route.continue();});
  const reply=page.waitForResponse(r=>r.url().endsWith('/api/mode')&&r.request().method()==='POST');
  await page.locator('#followMode').selectOption('auto');
  assert.equal((await reply).status(),202);
  assert.match(await page.locator('#forkMessage').textContent(),/Ожидаем подтверждение/);
  await page.waitForFunction(()=>document.getElementById('forkMessage').textContent==='Готово');
  await page.evaluate(()=>forkPost('/api/target',{az:180,el:0}));
  assert.equal((await state(process.env.ROTOR_URL)).targetAz,180);
  const stopReply=page.waitForResponse(r=>r.url().includes('/api/move?d=S'));
  await page.locator('#executeTarget').click();
  assert.equal((await stopReply).status(),202);
  await page.waitForFunction(()=>!window._forkState?.moving);
  assert.equal((await state(process.env.ROTOR_URL)).moving,false);
  const oldSelection=(await state(base)).hudSelectionEpoch;
  await page.locator('#taz').fill('300');
  await page.getByRole('tab',{name:'Настройки',exact:true}).click();
  await page.locator('#hudPeers button').filter({hasText:'R102'}).click();
  await page.getByRole('tab',{name:'Управление',exact:true}).click();
  await page.waitForFunction(()=>window._forkState?.hudName==='R102'&&window._forkState?.hudOnline);
  assert.equal(await page.locator('#taz').inputValue(),'');
  assert.equal((await state(base)).rotorName,'R201');
  await page.evaluate(()=>forkPost('/api/target',{az:220,el:0}));
  assert.equal((await state(process.env.ROTOR_TWO_URL)).targetAz,220);
  const staleRelease=await fetch(base+'/api/move?d=S&selection='+oldSelection);
  assert.equal(staleRelease.status,409);
  assert.equal((await state(process.env.ROTOR_TWO_URL)).moving,true);
  assert.equal((await state(process.env.ROTOR_URL)).targetAz,180);
  fs.mkdirSync('artifacts/screenshots',{recursive:true});
  for(const width of [1100,390,320]) {
   await page.setViewportSize({width,height:850});
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,`overflow ${width}`);
   await page.screenshot({path:`artifacts/screenshots/hud-${width}.png`,fullPage:true});
  }
  // Offline UI must erase stale current/target numbers even if legacy fields are populated.
  await page.route('**/api/status',async route=>{
   const response=await route.fetch();const data=await response.json();data.hudOnline=false;
   await route.fulfill({response,json:data});
  });
  await page.waitForFunction(()=>document.getElementById('az').textContent==='—');
  assert.equal(await page.locator('#azt').textContent(),'—');
  assert.match(await page.locator('#hudLink').textContent(),/Нет связи/);
  assert.deepEqual(errors,[]);
  console.log('PASS: HUD discovery/save/select, remote 202/ACK and fast STOP, remote model controls, offline values, 320/390/1100px');
 } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1});
