const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process'),root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const sample=JSON.parse(execFileSync(path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));await page.clock.install();
  await page.addInitScript(()=>{
   window.G5500_HTTP_ONLY=true;const original=fetch;window.net={hang:true,pending:0,normal:0,stops:0,modes:0,results:0,deferred:false};
   window.fetch=async(url,options={})=>{
    const name=new URL(url,location.href).pathname;
    if(name==='/api/mode'&&net.deferred){++net.modes;return {ok:true,status:202,text:async()=>JSON.stringify({ok:true,result:'deferred',requestId:7})};}
    const result=name==='/api/hud/command'&&net.deferred;
    if(result)++net.results;
    if(net.hang&&['/api/target','/api/move'].includes(name)||result){
     if(name==='/api/target')++net.normal;if(name==='/api/move')++net.stops;++net.pending;
     return {ok:true,status:200,text:()=>new Promise((resolve,reject)=>{
      const abort=()=>{--net.pending;reject(new DOMException('aborted','AbortError'));};
      if(options.signal.aborted)abort();else options.signal.addEventListener('abort',abort,{once:true});
     })};
    }
    return original(url,options);
   };
  });
  await page.route('http://command-memory.test/**',route=>{
   const name=new URL(route.request().url()).pathname;
   if(name==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
   const data=name==='/api/status'?{...sample,livePort:0,deviceRole:'controller',setupRequired:false,configurationLocked:false}:name==='/api/setup'?{required:false,paused:false,step:0,role:'controller',ready:true,storageOk:true,equipment:true}:name==='/api/network'?{choice:0,wifi:{mode:'dhcp'},eth:{mode:'dhcp'}}:{ok:true};
   return route.fulfill({json:data});
  });
  await page.goto('http://command-memory.test/');await page.waitForFunction(()=>window._forkState&&wizard.loaded);
  await page.evaluate(()=>{
   window.normalCommands=Array.from({length:12},()=>controlRequest('/api/target',{method:'POST',body:'az=100'}).then(r=>r.status,e=>'aborted'));
   window.firstStop=controlRequest('/api/move?d=S&selection=1').catch(e=>'aborted');
   window.sameStop=controlRequest('/api/move?d=S&selection=1').catch(e=>'aborted');
  });await page.waitForFunction(()=>net.pending===5);
  assert.equal(await page.evaluate(()=>net.normal),4);assert.equal(await page.evaluate(()=>net.stops),1);
  await page.evaluate(()=>{window.changedStop=controlRequest('/api/move?d=S&selection=2').catch(e=>'aborted');});
  await page.waitForFunction(()=>net.stops===2&&net.pending===5);
  assert.equal(await page.evaluate(()=>controlHttp.stops.size),1,'selection change replaces the old STOP slot');
  await page.clock.runFor(1100);assert.equal(await page.evaluate(()=>net.pending),4);
  assert.equal(await page.evaluate(()=>controlHttp.stops.size),0);
  await page.clock.runFor(4000);await page.waitForFunction(()=>controlHttp.pending===0&&net.pending===0);
  const outcomes=await page.evaluate(()=>Promise.all(normalCommands));assert.equal(outcomes.filter(x=>x===503).length,8);
  await page.evaluate(()=>{net.hang=false;net.deferred=true;window.commandResult=forkPost('/api/mode',{mode:'auto'});});
  await page.clock.runFor(1300);assert.equal(await page.evaluate(()=>commandResult),false);
  assert.equal(await page.evaluate(()=>net.modes),1,'never replay the command after a missing result');
  assert.equal(await page.evaluate(()=>net.results),1);assert.equal(await page.evaluate(()=>net.pending),0);
  assert.deepEqual(errors,[]);
  console.log('PASS: four bounded HTTP commands, independent/coalesced STOP, selection change cancellation, full-body deadlines and no replay after a stalled HUD result');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
