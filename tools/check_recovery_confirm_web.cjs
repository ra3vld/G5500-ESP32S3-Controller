// Recovery retries against production DeviceApi/Storage, including simulated reboots.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const {spawn}=require('node:child_process'),readline=require('node:readline');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),binary=process.env.DEVICE_API_TEST_BINARY;
if(!binary)throw Error('Set DEVICE_API_TEST_BINARY to the native device_api_tests executable');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{for(const width of [390,1100])for(const action of ['save','rollback','changed']){
  const proc=spawn(binary,['--serve-http'],{cwd:root,stdio:['pipe','pipe','inherit']}),pending=[];
  readline.createInterface({input:proc.stdout}).on('line',line=>{
   const task=pending.shift();assert(task,'Unexpected fixture output');
   try{task.resolve(JSON.parse(line));}catch(e){task.reject(e);}
  });
  proc.on('exit',code=>{for(const task of pending.splice(0))task.reject(Error('Native fixture exited '+code));});
  const started=Date.now(),trace=[],errors=[];let queue=Promise.resolve();
  const rpc=(method,route,body='')=>{
   const task=queue.then(()=>new Promise((resolve,reject)=>{
    pending.push({resolve,reject});proc.stdin.write(`${10001+Date.now()-started}\t${method}\t${route}\t${body}\n`);
   }));queue=task.catch(()=>{});return task;
  };
  const page=await browser.newPage({viewport:{width,height:950},locale:'ru-RU'});
  page.on('pageerror',e=>errors.push(e.message));
  try{
   await page.route('http://recovery-confirm.test/**',async route=>{
    const req=route.request(),url=new URL(req.url());
    if(url.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:html});
    const body=req.method()==='GET'?url.search.slice(1):req.postData()||'';
    const result=await rpc(req.method(),url.pathname,body);
    trace.push({path:url.pathname,method:req.method(),...result});
    await route.fulfill({status:result.status,contentType:'application/json',body:JSON.stringify(result.body)});
   });
   const initial=(await rpc('GET','/api/hardware')).body;
   assert.equal((await rpc('POST','/api/hardware/recovery',new URLSearchParams({token:initial.token,generation:initial.generation}).toString())).status,200);
   await rpc('POST','/__fixture/reboot');
   await page.goto('http://recovery-confirm.test/recovery');
   await page.waitForFunction(()=>state?.recovery&&!!metadata&&!document.getElementById('save').disabled);
   assert(await page.locator('#confirm').isHidden());
   if(action==='changed')await page.locator('[name=forceApSta]').selectOption('1');
   await page.locator(action==='rollback'?'#rollback':'#save').click();
   await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено'));
   const saved=trace.findLast(r=>r.method==='POST'&&['/api/hardware','/api/hardware/rollback'].includes(r.path));
   assert.equal(saved.status,200);assert.equal(saved.body.confirmationRequired,action==='changed');
   const message=await page.locator('#message').textContent();
   if(action==='changed')assert.match(message,/действие на экране, в веб-интерфейсе или начало поворота к цели подтвердит запуск/);
   else assert.match(message,/Дополнительное подтверждение не требуется/);
   await rpc('POST','/__fixture/reboot');await page.reload();
   await page.waitForFunction(()=>!!state&&!!metadata);
   if(action==='changed'){
    await page.waitForFunction(()=>state?.trial&&probe&&!document.getElementById('confirm').disabled);
    assert(await page.locator('#confirm').isVisible());
    await page.locator('#confirm').click();
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('Настройки подтверждены'));
    await page.reload();
   }
   await page.waitForFunction(()=>state&&!state.recovery&&!state.trial);
   assert(await page.locator('#confirm').isHidden());
   assert.match(await page.locator('#state').textContent(),/Работают подтверждённые настройки/);
   assert.equal(trace.filter(r=>r.path==='/api/hardware/confirm').length,action==='changed'?1:0);
   assert.equal(trace.filter(r=>r.method==='POST'&&r.status!==200).length,0);
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   assert.deepEqual(errors,[]);
   if(process.env.ARTIFACT_DIR){
    fs.mkdirSync(process.env.ARTIFACT_DIR,{recursive:true});
    await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`${action}-${width}.png`)});
    fs.writeFileSync(path.join(process.env.ARTIFACT_DIR,`${action}-${width}.json`),JSON.stringify(trace,null,2));
   }
   console.log(`PASS: ${width}px recovery ${action}: ${action==='changed'?'trial and confirmation without restart':'one reboot, no repeated confirmation'}`);
  }finally{await page.close();proc.stdin.end();proc.kill();}
 }}finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
