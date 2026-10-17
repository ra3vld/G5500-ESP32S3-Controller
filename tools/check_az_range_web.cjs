const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const base=process.env.SIM_URL||'http://127.0.0.1:8080';
const output=process.env.ARTIFACT_DIR||path.resolve(__dirname,'../artifacts/az-range');
const request=async(route,body,status=200)=>{const r=await fetch(base+route,body?{method:'POST',body:new URLSearchParams(body)}:{});assert.equal(r.status,status,await r.clone().text());return r.json()};
const state=()=>request('/api/status');
(async()=>{
 fs.mkdirSync(output,{recursive:true});
 await request('/api/move',{d:'S'});await request('/api/settings',{model:'G5500'});await request('/api/azrange',{profile:'p45'});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try {
 const page=await browser.newPage({viewport:{width:1100,height:900}}),errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 await page.goto(base);await page.waitForFunction(()=>window._forkState?.azProfile==='p45');
 await page.getByRole('tab',{name:'Настройки',exact:true}).click();await page.locator('#caltoggle').click();
 const form=page.locator('#azRangeForm');
 await form.locator('[name=start]').fill('-60');await form.locator('[name=span]').fill('450');
 assert.equal(await page.locator('#azRangeEnd').innerText(),'390.0°');
 await form.locator('[type=submit]').click();
 await page.waitForFunction(()=>window._forkState?.azStart===-60);
 assert.equal(await page.locator('#taz').getAttribute('min'),'-60');
 assert.equal(await page.locator('#taz').getAttribute('max'),'390');
 const revision=(await state()).azRangeRevision;
 await request('/api/azrange',{profile:'custom',start:-60,span:450});assert.equal((await state()).azRangeRevision,revision);
 for(const bad of [{start:'x',span:450},{start:'NaN',span:450},{start:-3600.1,span:1},{start:3600.1,span:1},{start:0,span:0},{start:0,span:3600.1},{start:0.01,span:450},{start:0,span:1.01},{start:0.0001,span:450},{start:0,span:1.0001}])await request('/api/azrange',{profile:'custom',...bad},400);
 assert.equal((await state()).azRangeRevision,revision);
 await page.locator('#modem').click();await page.waitForFunction(()=>!panelm.classList.contains('hide'));
 assert.equal(await page.locator('#legacyAzPoints').isVisible(),false);
 await page.locator('#azPointAngle').fill('15.5');await page.locator('#customAzPoints button[type=submit]').click();
 await page.waitForFunction(()=>window._azCal?.points.some(p=>p.angle===15.5));
 const row=page.locator('#azPointList > div').filter({hasText:'15.5°'});
 await row.getByRole('button',{name:'Запомнить положение'}).click();
 await page.waitForFunction(()=>window._azCal?.points.find(p=>p.angle===15.5)?.mv>=0);
 const mv=(await request('/api/azcal')).points.find(p=>p.angle===15.5).mv;
 for(const angle of [-60,390])await request('/api/azcal',{op:'delete',angle},400);
 await request('/api/azcal',{op:'add',angle:15.5},400);
 for(const width of [1100,390,320]) {
  await page.setViewportSize({width,height:1000});
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,`range settings overflow at ${width}`);
  await page.evaluate(()=>window.scrollTo(0,0));
  await page.screenshot({path:path.join(output,`calibration-${width}.png`),fullPage:true});
 }
 await request('/api/azrange',{profile:'custom',start:50,span:450});
 const shifted=await request('/api/azcal');assert.equal(shifted.points.find(p=>p.angle===125.5).mv,mv);
 await page.waitForFunction(()=>window._forkState?.azStart===50);
 assert.equal(await page.locator('#b2_azo').innerText(),'O · AZ 50°');assert.equal(await page.locator('#b2_azf').innerText(),'F · AZ 500°');
 await request('/api/target',{az:55,rangeRevision:revision},409);
 await request('/api/move',{d:'S',rangeRevision:revision});
 await request('/api/azrange',{profile:'custom',start:300,span:90});
 let points=(await request('/api/azcal')).points;assert.deepEqual(points.map(p=>p.angle),[300,390]);assert(points.every(p=>p.mv===-1));
 await request('/api/azrange',{profile:'p36'});assert.equal((await state()).azMax,360);
 await page.waitForFunction(()=>window._forkState?.azProfile==='p36');await page.locator('#azmc').click();
 await page.waitForFunction(()=>window._forkState?.azProfile==='custom');assert.equal((await state()).azStart,300);
 await request('/api/settings',{model:'G800DXA'});await request('/api/azrange',{profile:'custom',start:-700,span:450});
 await request('/api/settings',{model:'G5500'});assert.equal((await state()).azStart,300);
 for(const [start,span] of [[-3600,1],[50,720],[-360,1080],[3600,3600]]) {
  await request('/api/azrange',{profile:'custom',start,span});await page.waitForFunction(s=>window._forkState?.azStart===s,start);
  await page.getByRole('tab',{name:'Управление',exact:true}).click();
  await page.waitForFunction(s=>Number(document.getElementById('taz').min)===s,start);
  assert.equal(await page.locator('#taz').getAttribute('max'),String(start+span));
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  await page.screenshot({path:path.join(output,`control-${start}-${span}.png`),fullPage:true});
 }
 assert.deepEqual(errors,[]);
 console.log('PASS: custom range editor, input rejection, LIVE bounds/revision, stale command/STOP, calibration add/capture/endpoint protection/start shift/span reset, profile/model banks, 320/390/1100px layout');
 } finally {await browser.close();await request('/api/azrange',{profile:'p45'});}
})().catch(e=>{console.error(e);process.exit(1)});
