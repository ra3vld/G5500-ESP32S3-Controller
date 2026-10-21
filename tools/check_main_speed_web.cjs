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
  let release=null,blockNext=false,active=0,maxActive=0;
  await page.route('http://speed.test/**',async route=>{
   const request=route.request(),url=new URL(request.url());
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
   await page.locator('.manual-drive').screenshot({path:path.join(out,`speed-${width}.png`)});
  }
  assert.deepEqual(errors,[]);console.log('PASS: main speed sliders, independent units/limits, coalesced in-flight commands, mode/capability guards, 1100/390/320px');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
