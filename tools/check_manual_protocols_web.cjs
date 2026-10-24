const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const dgram=require('node:dgram'),net=require('node:net');
const base=process.env.SIM_URL||'http://127.0.0.1:28880';
const output=process.env.ARTIFACT_DIR||path.resolve(__dirname,'../local-notes/manual-protocols-web');
const keys=['n1mmManualFollow','pstManualFollow','gs232ManualFollow','rotctldManualFollow'];
const post=async(url,data)=>{const r=await fetch(base+url,{method:'POST',body:new URLSearchParams(data)});assert(r.ok,await r.text());};
const state=async()=>await(await fetch(base+'/api/status')).json();
const udp=(text,port)=>new Promise((resolve,reject)=>{const socket=dgram.createSocket('udp4');socket.send(text,port,'127.0.0.1',e=>{socket.close();e?reject(e):resolve();});});
const tcp=text=>new Promise((resolve,reject)=>{const socket=net.createConnection({host:'127.0.0.1',port:Number(process.env.TCP_PORT||28533)},()=>socket.end(text+'\n'));socket.resume();socket.setTimeout(3000,()=>socket.destroy(new Error('TCP timeout')));socket.on('error',reject);socket.on('close',resolve);});
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 fs.mkdirSync(output,{recursive:true});
 await post('/api/move',{d:'S'});await post('/api/settings',{model:'G5500'});await post('/api/mode',{mode:'manual'});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:900}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/#settings');await page.waitForFunction(()=>window._forkState&&live.ready);
  await page.locator('#programSettings details').evaluate(e=>e.open=true);
  const checkControls=async values=>{
   for(let i=0;i<4;i++)assert.equal(await page.locator('#'+keys[i]).inputValue(),String(values[i]));
  };
  for(const [cycle,values] of [[true,false,true,false],[false,true,false,true]].entries()){
   for(let i=0;i<4;i++)await page.locator('#'+keys[i]).selectOption(String(values[i]));
   await delay(650);await checkControls(values); // Polling must preserve unsaved selection.
   const request=page.waitForRequest(r=>r.url().endsWith('/api/settings')&&r.method()==='POST');
   await page.locator('#programSettings [type=submit]').click();
   const body=new URLSearchParams((await request).postData());
   for(let i=0;i<4;i++)assert.equal(body.get(keys[i]),String(values[i]));
   assert([...body].length<=32,'Settings must fit the firmware form-argument budget');
   await page.waitForFunction(()=>!document.getElementById('forkSettings').dataset.dirty);
   const saved=await state();for(let i=0;i<4;i++)assert.equal(saved[keys[i]],values[i]);
   await page.reload();await page.waitForFunction(()=>window._forkState&&live.ready);
   await page.locator('#programSettings details').evaluate(e=>e.open=true);await checkControls(values);
   for(let i=0;i<4;i++){
    await post('/api/move',{d:'S'});
    const target=90+i*50+cycle*15; // Distinct goals preserve the repeated-target filter.
    if(i===0)await udp(`<N1MMRotor><rotor>*</rotor><goazi>${target}</goazi></N1MMRotor>`,saved.n1mmPort);
    else if(i===1)await udp(`<PST><AZIMUTH>${target}</AZIMUTH></PST>`,saved.pstPort);
    else await tcp(i===2?`M${target}`:`P ${target} 45`);
    let result;
    for(let retry=0;retry<40;retry++){result=await state();if(values[i]?result.moving:result.pendingValid)break;await delay(25);}
    assert.equal(result.controlMode,'manual');
    const context=keys[i]+'='+values[i]+' '+JSON.stringify({pending:result.pendingValid,moving:result.moving,target:result.targetAz,fault:result.fault});
    assert.equal(result.pendingValid,!values[i],context);assert.equal(result.moving,values[i],context);
    assert.equal(values[i]?result.targetAz:result.pending.az,target);
   }
   await post('/api/move',{d:'S'});
  }
  // Re-create the requested mixed policy for the visual review.
  for(let i=0;i<4;i++)await page.locator('#'+keys[i]).selectOption(String(i===0||i===2));
  await page.locator('#programSettings [type=submit]').click();
  await page.waitForFunction(()=>!document.getElementById('forkSettings').dataset.dirty);
  const notice=page.locator('#settingsFeedback');
  if(await notice.isVisible())await notice.getByRole('button',{name:'Закрыть уведомление'}).click();
  for(const width of [1100,390,320]){
   await page.setViewportSize({width,height:width<600?1700:900});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`Overflow at ${width}px`);
   const group=page.locator('#manualProtocolSettings');
   for(const key of keys)assert(await page.locator('#'+key).isVisible());
   await group.screenshot({path:path.join(output,`protocols-${width}.png`)});
  }
  assert.deepEqual(errors,[]);
  console.log('PASS: saved/reloaded independent policies, dirty-form preservation, live N1MM/PST UDP and GS-232/rotctld TCP in manual mode, 1100/390/320px layout');
 }finally{await browser.close();await post('/api/move',{d:'S'});}
})().catch(e=>{console.error(e);process.exit(1)});
