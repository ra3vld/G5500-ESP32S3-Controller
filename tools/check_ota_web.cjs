const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const output=spawnSync(path.join(root,'build/simulator'),[],{input:'step 0\n',encoding:'utf8',timeout:10000});
assert.equal(output.status,0,output.stderr);
const status=JSON.parse(output.stdout.trim()).state;
const bytes=Buffer.alloc(5000);bytes[0]=0xe9;
const initial=()=>({available:true,reason:'available',flashBytes:4194304,imageBytes:1500000,slotBytes:1966080,minimumFlashBytes:3145728,recommendedFlashBytes:4194304,chunkBytes:2048,chipId:0,token:'test-session',locked:false,writing:false,complete:false,receivedBytes:0,totalBytes:0,error:'',arduinoEnabled:false,hostname:'g5500-test',password:''});
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const scenario of ['success','cancel','failure']){
   let state=initial(),starts=0,finishes=0,cancels=0;const chunks=[],errors=[];
   const page=await browser.newPage({viewport:{width:1100,height:900},locale:'ru-RU'});
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://ota.test/**',async route=>{
    const req=route.request(),u=new URL(req.url());let body={},code=200;
    if(u.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
    if(u.pathname==='/api/status')body=status;
    else if(u.pathname==='/api/ota')body=state;
    else if(u.pathname.startsWith('/api/ota/')){
     assert.equal(req.headers()['x-ota-token'],'test-session');
     if(u.pathname.endsWith('/start')){starts++;state.locked=true;state.writing=true;state.totalBytes=Number(u.searchParams.get('size'));}
     if(u.pathname.endsWith('/write')){
      const data=req.postDataBuffer();assert.equal(Number(u.searchParams.get('offset')),state.receivedBytes);
      if(scenario==='failure'){body={error:'write_failed'};code=409;state.error='write_failed';state.writing=false;}
      else {chunks.push(data);state.receivedBytes+=data.length;}
      if(scenario==='cancel')await new Promise(r=>setTimeout(r,150));
     }
     if(u.pathname.endsWith('/finish')){finishes++;state.complete=true;state.writing=false;}
     if(u.pathname.endsWith('/cancel')){cancels++;state.writing=false;state.error='cancelled';}
     if(u.pathname.endsWith('/arduino')){state.arduinoEnabled=true;state.password='temporary-password';}
     if(code===200)body={ok:true};
    }else if(u.pathname==='/api/rssi')body={items:[]};
    return route.fulfill({status:code,contentType:'application/json',body:JSON.stringify(body)});
   });
   await page.goto('http://ota.test/#settings');
   await page.locator('#otaSettings summary').click();
   await page.waitForFunction(()=>document.getElementById('otaCapacity').textContent.startsWith('OTA доступно'));
   assert(await page.locator('#otaUpload').isDisabled());
   if(scenario==='success'){
    state={...initial(),available:false,reason:'flash_too_small',minimumFlashBytes:5111808,recommendedFlashBytes:8388608,imageBytes:2500000};
    await page.evaluate(()=>loadOta());assert.match(await page.locator('#otaCapacity').textContent(),/другой модуль.*8 МиБ/);assert(await page.locator('#otaFile').isDisabled());
    state={...initial(),available:false,reason:'partition_layout'};await page.evaluate(()=>loadOta());assert.match(await page.locator('#otaCapacity').textContent(),/через USB/);assert.doesNotMatch(await page.locator('#otaCapacity').textContent(),/другой модуль/);
    state=initial();await page.evaluate(()=>loadOta());
    await page.locator('#otaArduino').click();await page.waitForFunction(()=>document.getElementById('otaArduinoInfo').textContent.includes('temporary-password'));
    await page.locator('#otaFile').setInputFiles({name:'too-big.bin',mimeType:'application/octet-stream',buffer:Buffer.alloc(2000000)});
    assert(await page.locator('#otaUpload').isDisabled());assert.match(await page.locator('#otaFileHelp').textContent(),/слишком большой/);
    await page.locator('#otaFile').setInputFiles({name:'needs-eight.bin',mimeType:'application/octet-stream',buffer:Buffer.alloc(2500000)});
    assert.match(await page.locator('#otaFileHelp').textContent(),/другой модуль.*8 МиБ/);
    const wrong=Buffer.from(bytes);wrong[12]=9;
    await page.locator('#otaFile').setInputFiles({name:'wrong.bin',mimeType:'application/octet-stream',buffer:wrong});
    await page.locator('#otaUpload').click();await page.waitForFunction(()=>document.getElementById('otaMessage').textContent.includes('другого чипа'));assert.equal(starts,0);
    await page.evaluate(()=>loadOta());
    await page.locator('#otaFile').setInputFiles([]);await page.evaluate(()=>{otaMessage('');document.getElementById('otaProgress').hidden=true;});
    for(const width of [1100,390,320]){
     await page.setViewportSize({width,height:900});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'OTA overflow at '+width);
     fs.mkdirSync(path.join(root,'artifacts/screenshots'),{recursive:true});
     await page.locator('#otaSettings').screenshot({path:path.join(root,`artifacts/screenshots/ota-${width}.png`)});
     await page.locator('#otaArduinoInfo').scrollIntoViewIfNeeded();
     await page.screenshot({path:path.join(root,`artifacts/screenshots/ota-bottom-${width}.png`)});
    }
   }
   await page.locator('#otaFile').setInputFiles({name:'firmware.bin',mimeType:'application/octet-stream',buffer:bytes});
   await page.locator('#otaUpload').click();
   if(scenario==='cancel'){await page.waitForFunction(()=>document.getElementById('otaMessage').textContent.includes('Остановка')===false);await page.locator('#otaCancel').click();}
   await page.waitForFunction(()=>!otaBusy);
   assert.equal(starts,1);
   if(scenario==='success'){assert.equal(finishes,1);assert.deepEqual(Buffer.concat(chunks),bytes);assert.match(await page.locator('#otaMessage').textContent(),/перезагружается/);}
   else{assert.equal(finishes,0);assert.equal(cancels,1);assert(await page.locator('#otaReboot').isVisible());assert.match(await page.locator('#otaMessage').textContent(),/остановлено до перезагрузки/);}
   assert.deepEqual(errors,[]);await page.close();
  }
  console.log('OTA browser: capability, insufficient flash/layout, file limits/chip, complete chunks, cancellation/error, ArduinoOTA and 320/390/1100px passed');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
