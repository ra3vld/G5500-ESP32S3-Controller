// Recalibration must display the new sample even while background reads are busy.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(process.env.CALIBRATION_HTML||path.join(root,'G5500_Fork/web_page.hpp'),'utf8');
const main=html.includes('R"HTML(')?html.split('R"HTML(')[1].split(')HTML";')[0]:html;
const sample=JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const axis of ['az','el'])for(const profile of ['custom','standard'])for(const mode of ['multi','2'])for(const scenario of profile==='custom'&&mode==='multi'?['busy','old-table','old-status','read-failure']:['busy']){
   const status={...sample,deviceRole:'controller',model:'G5500',calMode:mode,livePort:81,liveControlChannel:false,configurationLocked:false,setupRequired:false};
   const tables={};for(const a of ['az','el']){
    const custom=profile==='custom',start=custom?-50:0,end=custom?190:a==='az'?450:180;
    Object.assign(status,{[a+'Profile']:custom?'custom':a==='az'?'p45':'standard',[a+'Start']:start,[a+'Max']:end,[a+'RangeRevision']:1,[a+'2p0']:142,[a+'2pf']:3160,[a+'p0']:142});
    tables[a]={profile:status[a+'Profile'],start,end,span:end-start,customStart:start,customSpan:end-start,storageFault:false,points:[{angle:start,mv:142,endpoint:true},{angle:end,mv:3160,endpoint:true}]};
   }
   status.azMode=profile==='custom'?0:45;
   const prefix=axis+(mode==='2'?'2':'');
   const page=await browser.newPage(),errors=[];let held=0,captures=0,release,pauseNext=null;
   const blocked=new Promise(resolve=>release=resolve);
   page.on('pageerror',e=>errors.push(e.message));page.setDefaultTimeout(5000);
   await page.addInitScript(()=>{
    window.WebSocket=class {static OPEN=1;readyState=1;bufferedAmount=0;close(){this.readyState=3}send(){}};
   });
   await page.route('http://calibration.test/**',async route=>{
    const req=route.request(),url=new URL(req.url()),name=url.pathname;
    if(name==='/')return route.fulfill({contentType:'text/html',body:main});
    if(name==='/api/status'){
     const snapshot=structuredClone(status);
     if(pauseNext===name){pauseNext=null;++held;await blocked;}
     return route.fulfill({json:snapshot});
    }
    if(name==='/api/cal'||name==='/api/cal2'){
     const p=url.searchParams.get('p');assert.equal(p,mode==='2'?'o'+(axis==='el'?'2':''):axis+'0');
     status[axis+(mode==='2'?'2p0':'p0')]=543;tables[axis].points[0].mv=543;++captures;
     return route.fulfill({contentType:'text/plain',body:'SAVED'});
    }
    if(name.startsWith('/api/hold/')){++held;await blocked;return route.fulfill({json:{ok:true}});}
    for(const a of ['az','el'])if(name==='/api/'+a+'cal'){
     if(req.method()==='POST'){
      const body=new URLSearchParams(req.postData());assert.equal(a,axis);assert.equal(body.get('op'),'capture');
      tables[a].points.find(p=>p.angle===Number(body.get('angle'))).mv=543;++captures;
      return route.fulfill({json:{ok:true}});
     }
     const snapshot=structuredClone(tables[a]);
     if(pauseNext===name){pauseNext=null;++held;await blocked;}
     if(scenario==='read-failure'&&captures&&a===axis)return route.fulfill({status:503,json:{error:'read_failed'}});
     return route.fulfill({json:snapshot});
    }
    if(name==='/api/setup')return route.fulfill({json:{required:false,paused:false,step:0,role:'controller',ready:true,storageOk:true,equipment:true,token:1,generation:1}});
    return route.fulfill({json:{items:[],values:[],points:[],peers:[]}});
   });
   try{
    await page.goto('http://calibration.test/#settings');
    await page.waitForFunction(()=>window._forkState&&wizard.loaded);
    await page.locator('#calibrationSettings details').evaluate(e=>e.open=true);
    const row=page.locator('#'+prefix+'PointList .point-row').first();
    await page.waitForFunction(prefix=>document.getElementById(prefix+'PointList').textContent.includes('142 мВ'),prefix);
    await wait(100);
    // Healthy live telemetry suppresses periodic full HTTP status refreshes.
    await page.evaluate(()=>{live.ready=true;live.last=Date.now();live.state={};});
    if(scenario==='busy'){
     await page.evaluate(()=>{for(const n of [1,2])void background.read('hold:'+n,'/api/hold/'+n).catch(()=>{});});
    }else if(scenario==='old-table'){
     pauseNext='/api/'+axis+'cal';await page.evaluate(axis=>void loadAxisCalibration(axis),axis);
    }else if(scenario==='old-status'){
     pauseNext='/api/status';await page.evaluate(()=>void upd(true));
    }
    const expectedHeld=scenario==='busy'?2:scenario==='read-failure'?0:1;
    for(let i=0;i<50&&held<expectedHeld;++i)await wait(10);assert.equal(held,expectedHeld);
    await row.getByRole('button',{name:'Запомнить положение'}).click();
    for(let i=0;i<100&&!captures;++i)await wait(10);assert.equal(captures,1);
    if(scenario!=='busy')release();
    await page.waitForFunction(()=>settingsRequests.size===0);
    assert.equal(captures,1,'one capture request');
    assert.equal(tables[axis].points[0].mv,543,'the controller stored a new sample');
    if(scenario==='read-failure'){
     assert.equal(await page.locator('#settingsFeedback').getAttribute('data-state'),'error');
     assert.match(await page.locator('#settingsFeedbackText').innerText(),/сохранены.*не удалось обновить/);
    }else{
     await page.waitForFunction(prefix=>document.getElementById(prefix+'PointList').textContent.includes('543 мВ'),prefix,{timeout:2000});
     assert.equal(await page.locator('#settingsFeedback').getAttribute('data-state'),'success');
    }
    assert.deepEqual(errors,[]);
    console.log('PASS: '+axis.toUpperCase()+' '+profile+' '+mode+' '+scenario);
   }finally{release();await page.close();}
  }
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exit(1)});
