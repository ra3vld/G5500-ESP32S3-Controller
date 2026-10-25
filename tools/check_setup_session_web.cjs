// Browser/production DeviceApi contract: saves, tuning-step transitions and motion permits.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const {spawn}=require('node:child_process'),readline=require('node:readline');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),binary=process.env.DEVICE_API_TEST_BINARY;
if(!binary)throw Error('Set DEVICE_API_TEST_BINARY to the native device_api_tests executable');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const hardware=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
const wait=ms=>new Promise(r=>setTimeout(r,ms));
if(process.env.ARTIFACT_DIR)fs.mkdirSync(process.env.ARTIFACT_DIR,{recursive:true});
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{for(const width of [390,1100]){
  const proc=spawn(binary,['--serve-http'],{cwd:root,stdio:['pipe','pipe','inherit']}),pending=[];
  readline.createInterface({input:proc.stdout}).on('line',line=>{const p=pending.shift();if(!p)throw Error('Unexpected fixture output: '+line);try{p.resolve(JSON.parse(line));}catch(e){p.reject(e);}});
  proc.on('exit',code=>{for(const p of pending.splice(0))p.reject(Error('Native fixture exited '+code));});
  const started=Date.now(),trace=[],errors=[];let queue=Promise.resolve(),saveDelay=0,armDelay=0,renewDelay=0;
  const rpc=(method,route,body='')=>{
   const task=queue.then(()=>new Promise((resolve,reject)=>{pending.push({resolve,reject});proc.stdin.write(`${10001+Date.now()-started}\t${method}\t${route}\t${body}\n`);}));
   queue=task.catch(()=>{});return task;
  };
  const page=await browser.newPage({viewport:{width,height:1000},locale:'ru-RU'});page.on('pageerror',e=>errors.push(e.message));
  try{
   await page.route('http://setup-session.test/**',async route=>{
    const req=route.request(),url=new URL(req.url()),body=req.postData()||url.search.slice(1),form=new URLSearchParams(body);
    if(url.pathname==='/setup'||url.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
    if(url.pathname==='/hardware')return route.fulfill({contentType:'text/html',body:hardware});
    if(url.pathname==='/favicon.ico')return route.fulfill({status:204,body:''});
    if(url.pathname==='/api/settings'&&req.method()==='POST'&&saveDelay){const delay=saveDelay;saveDelay=0;await wait(delay);}
    if(url.pathname==='/api/setup'&&form.get('action')==='arm'&&armDelay){const delay=armDelay;armDelay=0;await wait(delay);}
    if(url.pathname==='/api/setup'&&form.get('action')==='renew'&&renewDelay)await wait(renewDelay);
    const result=await rpc(req.method(),url.pathname,body);trace.push({path:url.pathname,...result,response:result.body,body:Object.fromEntries(form)});
    await route.fulfill({status:result.status,contentType:'application/json',body:JSON.stringify(result.body)});
   });
   await page.goto('http://setup-session.test/setup');
   await page.waitForFunction(()=>wizard.active&&wizard.state?.token&&wizard.state.step===1);
   const session=await page.evaluate(()=>wizard.state.token);
   if(process.env.SETUP_SESSION_SCENARIO==='tuning'){
    await rpc('POST','/__fixture/tuning');await page.evaluate(()=>pollSetup());
    await page.waitForFunction(()=>wizard.state?.step===6&&!wizard.polling);await page.evaluate(async()=>{await upd(true);await loadMotionSettings();});
    assert.equal(await page.locator('#controlHz').inputValue(),'50');
    await page.locator('#controlHz').selectOption('100');
    await page.locator('[name=azMotionVelocityKp]').fill('1.23');await page.locator('[name=elMotionVelocityKp]').fill('1.34');
    if(process.env.ARTIFACT_DIR){await page.locator('#controlFrequencyForm').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`tuning-6-${width}.png`)});}
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===7,{},{timeout:10000});
    const motion=await rpc('GET','/api/motion'),setup=await rpc('GET','/api/setup');
    assert.equal(motion.body.controlPeriodMs,10);assert.equal(motion.body.axes[0].VelocityKp,1.23);assert.equal(motion.body.axes[1].VelocityKp,1.34);
    assert(!setup.moving&&!setup.testToken&&setup.body.required&&setup.body.step===7);
    if(process.env.ARTIFACT_DIR){await page.locator('#setupTitle').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`tuning-7-${width}.png`)});}
    assert.equal(trace.filter(r=>r.body.action==='arm').length,0,'Saving regulators must not acquire motion access');
    const blocked=await rpc('POST','/api/move',new URLSearchParams({d:'R',setupToken:session}).toString());assert.equal(blocked.status,503);
    await page.locator('#setupBack').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===6);
    await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===7);
    assert(!trace.some(r=>r.path==='/api/settings'&&r.status!==200));assert.deepEqual(errors,[]);
    if(process.env.ARTIFACT_DIR)fs.writeFileSync(path.join(process.env.ARTIFACT_DIR,`tuning-${width}.json`),JSON.stringify(trace,null,2));
    console.log(`PASS: ${width}px, BTS7960 setup with production DeviceApi: implicit frequency/PID save, Next to programs, revisit without edits, motion remains blocked`);
    continue;
   }
   await wait(1800);
   assert.equal(trace.filter(r=>r.body.action==='renew').length,0,'Editing must not send motion heartbeats');
   await page.locator('#setupDeviceName').fill('Slow save');saveDelay=2300;
   await page.locator('#setupNext').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===2);
   assert.equal(await page.evaluate(()=>wizard.state.token),session);
   assert.equal((await rpc('GET','/api/status')).body.rotorName,'Slow save');
   assert(!trace.some(r=>r.body.action==='begin_save'&&r.status!==200));
   await page.locator('#setupBack').click();await page.waitForFunction(()=>!wizard.busy&&wizard.state.step===1);
   await page.locator('#setupDeviceName').fill('Draft after hidden tab');
   await page.evaluate(()=>Object.defineProperty(document,'hidden',{configurable:true,get:()=>true}));await wait(1800);
   await page.evaluate(()=>{delete document.hidden;return pollSetup();});
   assert.equal(await page.locator('#setupDeviceName').inputValue(),'Draft after hidden tab');
   assert.equal(await page.evaluate(()=>wizard.state.token),session);
   if(process.env.ARTIFACT_DIR){fs.mkdirSync(process.env.ARTIFACT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`session-name-${width}.png`)});}
   // A hidden hardware iframe must stop both hardware and Modbus HTTP polling.
   await rpc('POST','/__fixture/step','3');await page.evaluate(()=>pollSetup());
   await page.frameLocator('#equipmentFrame').locator('[name="pin.azAdc"]').waitFor({state:'attached'});
   await rpc('POST','/__fixture/step','9');await page.evaluate(()=>pollSetup());
   await wait(500);const count=()=>trace.filter(r=>r.path.startsWith('/api/hardware')).length,before=count();await wait(2300);assert.equal(count(),before,'Hidden hardware polling');
   await page.evaluate(()=>upd(true));
   const button=page.locator('[data-setup-axis="0"][data-direction="1"]');await button.scrollIntoViewIfNeeded();
   const press=async()=>{const r=await button.boundingBox();await page.mouse.move(r.x+r.width/2,r.y+r.height/2);await page.mouse.down();};
   await press();await page.waitForFunction(()=>!!wizard.state.testToken);
   await wait(350);let state=await rpc('GET','/api/setup');assert(state.moving,'Explicit HOLD must move in simulation');
   const old=trace.findLast(r=>r.path==='/api/move'&&r.body.d==='H'&&r.status===200);assert(old);
   await page.mouse.up();await wait(200);state=await rpc('GET','/api/setup');assert(!state.moving,'Release closes the gesture');
   assert(trace.some(r=>r.path==='/api/move'&&r.body.d==='S'&&r.body.client&&r.status===200));
   renewDelay=1900;await press();await wait(2500);state=await rpc('GET','/api/setup');assert(!state.moving&&!state.testToken,'Lost heartbeat cancels movement');
   const arms=trace.filter(r=>r.body.action==='arm').length;renewDelay=0;await wait(1800);
   assert.equal(trace.filter(r=>r.body.action==='arm').length,arms,'Reconnect cannot arm movement');
   assert(!(await rpc('GET','/api/setup')).moving);await page.mouse.up();
   // A new explicit press gets a new permit; an old queued packet remains rejected.
   await press();await wait(350);const current=await rpc('GET','/api/setup');assert(current.moving&&current.testToken!==Number(old.body.setupTestToken));
   const stale=await rpc('POST','/api/move',new URLSearchParams(old.body).toString());assert.equal(stale.status,503);
   await page.mouse.up();await wait(200);assert(!(await rpc('GET','/api/setup')).moving);
   // Releasing while ARM is delayed must not send the delayed initial HOLD.
   armDelay=1900;const holds=()=>trace.filter(r=>r.path==='/api/move'&&r.body.d==='H').length,startHolds=holds();
   await press();await wait(50);await page.mouse.up();await wait(2200);
   assert.equal(holds(),startHolds);assert(!(await rpc('GET','/api/setup')).moving);
   assert.equal(trace.filter(r=>r.body.action==='resume').length,1,'No automatic resume/NVS churn after network stalls');
   assert.deepEqual(errors,[]);
   if(process.env.ARTIFACT_DIR)fs.writeFileSync(path.join(process.env.ARTIFACT_DIR,`session-${width}.json`),JSON.stringify(trace,null,2));
   console.log(`PASS: ${width}px, production DeviceApi: slow save, hidden tab/draft, inactive iframe, STOP/closed HOLD, expired permit, no reconnect replay, delayed ARM release`);
  }catch(e){console.error('STATE',await page.evaluate(()=>({state:wizard.state,error:wizard.error,message:document.getElementById('setupMessage').textContent})));console.error('LAST POSTS',trace.filter(r=>r.body.action||r.path==='/api/settings'||r.path==='/api/move').slice(-12));if(process.env.ARTIFACT_DIR)fs.writeFileSync(path.join(process.env.ARTIFACT_DIR,`failed-${width}.json`),JSON.stringify(trace,null,2));throw e;}finally{await page.close();proc.stdin.end();proc.kill();}
 }}finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
