// Actual browser input against production DeviceApi/Storage and simulated reboots.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const {spawn}=require('node:child_process'),readline=require('node:readline');
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),binary=process.env.DEVICE_API_TEST_BINARY;
if(!binary)throw Error('Set DEVICE_API_TEST_BINARY to the native device_api_tests executable');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
const helper=fs.readFileSync(path.join(root,'tools/web_activity.js'),'utf8').trim();
for(const name of ['web_page.hpp','hardware_page.hpp','diagnostic_pages.hpp'])
 assert(fs.readFileSync(path.join(root,'G5500_Fork',name),'utf8').includes(helper),name+' must report input');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{for(const width of [390,1100])for(const input of ['pointer','keyboard','explicit']){
  const proc=spawn(binary,['--serve-http'],{cwd:root,stdio:['pipe','pipe','inherit']}),pending=[];
  readline.createInterface({input:proc.stdout}).on('line',line=>{
   const task=pending.shift();assert(task,'Unexpected fixture output');
   try{task.resolve(JSON.parse(line));}catch(e){task.reject(e);}
  });
  proc.on('exit',code=>{for(const task of pending.splice(0))task.reject(Error('Native fixture exited '+code));});
  const started=Date.now(),requests=[],errors=[];let queue=Promise.resolve();
  const rpc=(method,route,body='')=>{
   const task=queue.then(()=>new Promise((resolve,reject)=>{
    pending.push({resolve,reject});proc.stdin.write(`${10001+Date.now()-started}\t${method}\t${route}\t${body}\n`);
   }));queue=task.catch(()=>{});return task;
  };
  const page=await browser.newPage({viewport:{width,height:950},locale:'ru-RU'});
  page.on('pageerror',e=>errors.push(e.message));
  try{
   await page.route('http://boot-activity.test/**',async route=>{
    const req=route.request(),url=new URL(req.url());
    if(url.pathname==='/hardware')return route.fulfill({contentType:'text/html',body:html});
    const result=await rpc(req.method(),url.pathname,req.method()==='GET'?url.search.slice(1):req.postData()||'');
    requests.push({path:url.pathname,status:result.status});
    await route.fulfill({status:result.status,contentType:'application/json',body:JSON.stringify(result.body)});
   });
   const initial=(await rpc('GET','/api/hardware')).body;
   assert.equal((await rpc('POST','/api/hardware',new URLSearchParams({token:initial.token,generation:initial.generation,forceApSta:1}).toString())).status,200);
   await rpc('POST','/__fixture/reboot');
   await page.goto('http://boot-activity.test/hardware');
   await page.waitForFunction(()=>state?.trial&&probe&&!document.getElementById('confirm').disabled);
   assert.equal(requests.filter(r=>r.path==='/api/hardware/activity').length,0);
   // Programmatic updates and automatic polling cannot confirm the trial.
   await page.evaluate(()=>{document.querySelector('h1').click();document.dispatchEvent(new Event('input'));return refresh();});
   assert((await rpc('GET','/api/hardware')).body.trial);
   if(input==='pointer')await page.locator('h1').click();
   else if(input==='keyboard')await page.keyboard.press('Tab');
   else await page.locator('#confirm').click();
   await page.waitForFunction(()=>state&&!state.trial);
   assert.equal((await rpc('GET','/api/hardware')).body.trial,false);
   assert.equal(requests.filter(r=>r.path==='/api/hardware/activity').length,input==='explicit'?0:1);
   assert.equal(requests.filter(r=>r.path==='/api/hardware/confirm').length,input==='explicit'?1:0);
   assert.deepEqual(requests.filter(r=>r.status!==200&&!(r.path==='/api/hardware/display'&&r.status===501)),[]);
   assert.deepEqual(errors,[]);
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   if(process.env.ARTIFACT_DIR){fs.mkdirSync(process.env.ARTIFACT_DIR,{recursive:true});await page.screenshot({path:path.join(process.env.ARTIFACT_DIR,`activity-${input}-${width}.png`)});}
   console.log(`PASS: ${width}px ${input}: passive polling ignored, trial confirmed without restart`);
  }finally{await page.close();proc.stdin.end();proc.kill();}
 }}finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
