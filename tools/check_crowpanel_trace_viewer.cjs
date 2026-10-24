// Exercise the local native-LVGL recording player and its video download.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const output=path.join(root,'build/crowpanel-trace/browser-check');
fs.mkdirSync(output,{recursive:true});
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1000,height:1000}}),errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  await page.goto(process.env.CROWPANEL_TRACE_VIEWER_URL||'http://127.0.0.1:8766/');
  await page.waitForFunction(()=>document.getElementById('screen').naturalWidth===480);
  assert.equal(await page.locator('#screen').evaluate(element=>element.naturalHeight),480);
  const initialClock=await page.locator('#clock').textContent();
  await page.locator('#play').click();
  await page.waitForFunction(initial=>document.getElementById('clock').textContent!==initial,initialClock);
  await page.locator('#play').click();
  const pausedClock=await page.locator('#clock').textContent();
  await page.waitForTimeout(200);
  assert.equal(await page.locator('#clock').textContent(),pausedClock);
  await page.locator('#speed').selectOption('2');
  await page.locator('#seek').evaluate(element=>{element.value=5000;element.dispatchEvent(new Event('input',{bubbles:true}));});
  await page.waitForFunction(()=>document.getElementById('clock').textContent.startsWith('0:05.000'));
  await page.waitForFunction(()=>document.getElementById('screen').complete);
  await page.screenshot({path:path.join(output,'desktop.png')});
  await page.setViewportSize({width:390,height:900});
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  const screen=await page.locator('#screen').boundingBox();
  assert(Math.abs(screen.width-screen.height)<1,'The round display must retain a square image on mobile');
  await page.screenshot({path:path.join(output,'mobile.png')});
  const response=page.waitForResponse(response=>response.url().endsWith('/export')&&response.request().method()==='POST',{timeout:120000});
  const download=page.waitForEvent('download',{timeout:120000});
  await page.locator('#export').click();
  assert.equal((await response).status(),200);
  const video=await download;
  assert(video.suggestedFilename().endsWith('.mp4'));
  await video.saveAs(path.join(output,'export.mp4'));
  await page.waitForFunction(()=>!document.getElementById('export').disabled);
  await page.locator('#home').click();
  await page.waitForFunction(()=>document.getElementById('clock').textContent.startsWith('0:00.000'));
  assert.equal(await page.locator('#error').textContent(),'');
  assert.deepEqual(errors,[]);
  assert(fs.statSync(path.join(output,'export.mp4')).size>1000);
  console.log('PASS: native frame, play/pause, seek/speed, mobile layout, MP4 browser download, no page errors');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
