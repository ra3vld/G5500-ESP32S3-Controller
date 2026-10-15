// Wi-Fi form regression: saved SSID, encoding, and visible save failures.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8');
const html=source.split('R"HTML(')[1].split(')HTML";')[0];
const status=JSON.parse(execFileSync(path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
status.ssid='Saved network';

(async()=>{
 const browser=await chromium.launch({...(process.env.CHROMIUM_PATH?{executablePath:process.env.CHROMIUM_PATH}:{}),headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:390,height:844}}),errors=[],requests=[];
  page.on('pageerror',error=>errors.push(error.message));
  page.on('dialog',dialog=>dialog.accept());
  let saveSucceeds=false,hangSave=false,hangStatus=false,statusRequests=0;
  await page.route('**/*',async route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/api/status'){
    ++statusRequests;
    if(hangStatus)return;
    return route.fulfill({json:status});
   }
   if(url.pathname==='/api/rssi')return route.fulfill({json:{values:[],min:null,max:null,avg:null,count:0}});
   if(url.pathname==='/api/wifi'){
    requests.push({method:route.request().method(),url,form:new URLSearchParams(route.request().postData())});
    if(hangSave)return;
    return route.fulfill({status:saveSucceeds?200:500,contentType:'text/plain; charset=utf-8',body:saveSucceeds?'Настройки Wi-Fi сохранены. Контроллер перезагружается.':'Не удалось сохранить настройки Wi-Fi. Перезагрузка отменена.'});
   }
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://controller.test/');
  await page.waitForFunction(()=>document.getElementById('ssid').value==='Saved network');
  const ssid='Тестовая сеть & +',password='dummy=&+?% password';
  await page.locator('#ssid').fill(ssid);
  await page.locator('#pass').fill(password);
  await page.waitForTimeout(600);
  assert.equal(await page.locator('#ssid').inputValue(),ssid,'polling must preserve typed SSID');
  const save=page.getByRole('button',{name:'Save Wi‑Fi + reboot',exact:true});
  await save.click();
  await page.waitForFunction(()=>document.getElementById('wifiMessage').textContent.includes('Перезагрузка отменена'));
  assert.equal(requests[0].method,'POST');
  assert.equal(requests[0].url.search,'');
  assert.equal(requests[0].form.get('ssid'),ssid);
  assert.equal(requests[0].form.get('pass'),password);
  assert.equal(await page.locator('#pass').inputValue(),password,'failed save must allow retry');
  saveSucceeds=true;
  await save.click();
  await page.waitForFunction(()=>document.getElementById('wifiMessage').textContent.includes('сохранены'));
  assert.equal(await page.locator('#pass').inputValue(),'');
  await page.locator('#ssid').fill('');
  await save.click();
  assert.match(await page.locator('#wifiMessage').textContent(),/Укажите SSID/);
  assert.equal(requests.length,2,'empty SSID must not request a reboot');
  await page.clock.install();
  await page.locator('#ssid').fill(ssid);
  hangSave=true;
  await save.click();
  await page.waitForFunction(()=>document.getElementById('wifiSaveButton').disabled);
  await page.waitForTimeout(50);
  const beforeDuplicate=requests.length;
  await page.evaluate(()=>wifi());
  assert.equal(requests.length,beforeDuplicate,'pending save must not be submitted twice');
  await page.clock.runFor(12500);
  await page.waitForFunction(()=>document.getElementById('wifiMessage').textContent.includes('Ответ не получен'));
  assert.equal(await save.isDisabled(),false);
  assert.equal(requests.length,3,'timeout must not retry the save automatically');
  hangStatus=true;
  await page.evaluate(()=>{void upd();});
  // Leave the pending promise running while several polling intervals elapse.
  const initialStatusRequests=statusRequests;
  await page.clock.runFor(3000);
  assert.ok(statusRequests<=initialStatusRequests+1,'status polling must not overlap requests');
  assert.deepEqual(errors,[]);
  console.log('PASS: Wi-Fi POST, encoded credentials, failure/success, stalled save timeout, duplicate-save guard and single status request');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
