const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const base=process.env.SIM_URL||'http://127.0.0.1:8080';
const output=process.env.ARTIFACT_DIR||path.resolve(__dirname,'../local-notes/el-range-web');
const request=async(route,body,status=200)=>{const r=await fetch(base+route,body?{method:'POST',body:new URLSearchParams(body)}:{});assert.equal(r.status,status,await r.clone().text());return r.json()};
(async()=>{
 fs.mkdirSync(output,{recursive:true});
 await request('/api/move',{d:'S'});await request('/api/settings',{model:'G5500'});
 await request('/api/azrange',{profile:'p45'});await request('/api/elrange',{profile:'standard'});
 await request('/api/calmode',{m:'2'});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try {
  const page=await browser.newPage({viewport:{width:1100,height:900}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/#settings');await page.waitForFunction(()=>window._forkState?.elProfile==='standard'&&live.ready);
  assert(await page.locator('#settingsPanel').isVisible());
  assert.deepEqual(await page.locator('#settingsPanel > [id]').evaluateAll(nodes=>nodes.map(n=>n.id)),['rotorSettings','calibrationSettings','presetSettings','hudPanel','networkSettings','otaSettings','signalSettings']);
  await page.locator('#caltoggle').click();
  for(const [button,panel] of [['modem','panelm'],['mode2','panel2'],['modem','panelm']]) {
   await page.locator('#'+button).click();
   await page.waitForFunction(id=>!document.getElementById(id).classList.contains('hide'),panel,{timeout:2000});
   assert(await page.evaluate(()=>live.ready),'Calibration switch must work with an active LIVE connection');
  }
  const form=page.locator('#elRangeForm');
  await form.locator('[name=start]').fill('-30');await form.locator('[name=span]').fill('240');
  assert.equal(await page.locator('#elRangeEnd').innerText(),'210.0°');
  await form.locator('[type=submit]').click();await page.waitForFunction(()=>window._forkState?.elStart===-30);
  assert.equal(await page.locator('#tel').getAttribute('min'),'-30');assert.equal(await page.locator('#tel').getAttribute('max'),'210');
  assert.equal(await page.locator('#presetEditor [name=el]').getAttribute('min'),'-30');
  assert.equal(await page.locator('#elPointList > div').count(),2);assert(await page.locator('#customElPoints').isVisible());
  const rev=(await request('/api/status')).elRangeRevision;
  await request('/api/elrange',{profile:'custom',start:-30,span:240});assert.equal((await request('/api/status')).elRangeRevision,rev);
  for(const bad of [{start:'NaN',span:180},{start:-3600.1,span:180},{start:0,span:0},{start:0,span:3600.1},{start:.01,span:180},{start:0,span:1.0001}])await request('/api/elrange',{profile:'custom',...bad},400);
  await page.locator('#elPointAngle').fill('15.5');await page.locator('#customElPoints button[type=submit]').click();
  await page.waitForFunction(()=>window._elCal?.points.some(p=>p.angle===15.5));
  const row=page.locator('#elPointList > div').filter({hasText:'15.5°'});
  await row.getByRole('button',{name:'Запомнить положение'}).click();
  await page.waitForFunction(()=>window._elCal?.points.find(p=>p.angle===15.5)?.mv>=0);
  const mv=(await request('/api/elcal')).points.find(p=>p.angle===15.5).mv;
  for(const angle of [-30,210])await request('/api/elcal',{op:'delete',angle},400);
  await request('/api/elcal',{op:'add',angle:15.5},400);
  for(const width of [1100,390,320]) {
   await page.setViewportSize({width,height:950});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   await page.evaluate(()=>scrollTo(0,0));await page.screenshot({path:path.join(output,`calibration-${width}.png`),fullPage:true});
   await page.getByRole('tab',{name:'Управление',exact:true}).click();
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   await page.screenshot({path:path.join(output,`control-${width}.png`),fullPage:true});
   await page.getByRole('tab',{name:'Настройки',exact:true}).click();
  }
  await request('/api/elrange',{profile:'custom',start:20,span:240});
  assert.equal((await request('/api/elcal')).points.find(p=>p.angle===65.5).mv,mv);
  await page.waitForFunction(()=>window._forkState?.elStart===20);
  assert((await page.locator('#b2_elo').locator('../..').innerText()).includes('Начало · 20°'));assert((await page.locator('#b2_elf').locator('../..').innerText()).includes('Конец · 260°'));
  await request('/api/target',{el:100,rangeRevision:rev},409);await request('/api/move',{d:'S',rangeRevision:rev});
  await request('/api/elrange',{profile:'custom',start:-10,span:90});
  const points=(await request('/api/elcal')).points;assert.deepEqual(points.map(p=>p.angle),[-10,80]);assert(points.every(p=>p.mv===-1));
  await page.locator('#elmstd').click();await page.waitForFunction(()=>window._forkState?.elProfile==='standard');
  await page.locator('#elmc').click();await page.waitForFunction(()=>window._forkState?.elProfile==='custom');assert.equal((await request('/api/status')).elStart,-10);
  await request('/api/settings',{model:'G800DXA'});await request('/api/elrange',{profile:'custom',start:0,span:180},400);
  await page.waitForFunction(()=>window._forkState?.model==='G800DXA');assert(!await form.isVisible());
  await request('/api/settings',{model:'G5500'});assert.equal((await request('/api/status')).elStart,-10);
  const diagnostics=fs.readFileSync(path.resolve(__dirname,'../G5500_Fork/diagnostic_pages.hpp'),'utf8');
  for(const [route,symbol] of [['/adc','ADC_TEST_PAGE'],['/resetdiag','RESET_DIAG_PAGE'],['/diaglog','DIAG_PAGE']]) {
   const html=diagnostics.split('static constexpr char '+symbol)[1].split('R"HTML(')[1].split(')HTML";')[0];
   await page.route(base+route,r=>r.fulfill({contentType:'text/html',body:html}));
  }
  await page.route(base+'/api/adcdiag',r=>r.fulfill({json:{hasEl:true,azRaw:1000,azMv:750,elRaw:900,elMv:680}}));
  await page.route(base+'/api/resetreason',r=>r.fulfill({json:{reason:'POWERON',code:1}}));
  await page.route(base+'/api/resethistory',r=>r.fulfill({json:{entries:[]}}));
  for(const route of ['/adc','/resetdiag','/diaglog']) {
   await page.goto(base+route);const link=page.getByRole('link',{name:'Настройки',exact:true});assert(await link.isVisible());
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   await page.screenshot({path:path.join(output,route.slice(1)+'-320.png'),fullPage:true});
   await link.click();await page.waitForFunction(()=>!document.getElementById('settingsPanel').hidden);
  }
  assert.deepEqual(errors,[]);
  console.log('PASS: diagnostic settings links, layout at 320/390/1100px, immediate calibration switch with LIVE, EL range/points/profile/editor/limits/revision/reset');
 } finally {await browser.close();await request('/api/settings',{model:'G5500'});await request('/api/elrange',{profile:'standard'});}
})().catch(e=>{console.error(e);process.exit(1)});
