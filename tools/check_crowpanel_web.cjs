// Exercise the actual settings form with a stubbed status/settings transport.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const status=JSON.parse(execFileSync(path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
status.build.display='CROWPANEL_21';status.build.drive='SIMULATED';status.encoderInverted=true;
(async()=>{
 const browser=await chromium.launch({...(process.env.CHROMIUM_PATH?{executablePath:process.env.CHROMIUM_PATH}:{}),headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage(),errors=[],requests=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('**/*',async route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/api/status')return route.fulfill({json:status});
   if(url.pathname==='/api/rssi')return route.fulfill({json:{values:[],min:null,max:null,avg:null,count:0}});
   if(url.pathname==='/api/settings'){
    const form=new URLSearchParams(route.request().postData());requests.push(form);
    if(form.has('encoderInverted'))status.encoderInverted=form.get('encoderInverted')==='true';
    return route.fulfill({json:{ok:true,result:'ok'}});
   }
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  async function open(){
   await page.goto('http://controller.test/');
   await page.waitForFunction(()=>!!window._forkState);
   await page.getByRole('tab',{name:'Настройки',exact:true}).click();
   await page.locator('#forkSettings').evaluate(e=>e.closest('details').open=true);
  }
  await open();
  const box=page.locator('[name=encoderInverted]');
  assert(await box.isVisible());assert(await box.isChecked());
  await box.uncheck();
  await page.getByRole('button',{name:'Сохранить настройки',exact:true}).click();
  await page.waitForFunction(()=>window._forkState.encoderInverted===false);
  assert.equal(requests.at(-1).get('encoderInverted'),'false');
  await open();assert.equal(await box.isChecked(),false);
  await box.check();
  await page.getByRole('button',{name:'Сохранить настройки',exact:true}).click();
  await page.waitForFunction(()=>window._forkState.encoderInverted===true);
  assert.equal(requests.at(-1).get('encoderInverted'),'true');
  status.build.display='NONE';status.build.drive='YAESU';delete status.encoderInverted;
  await open();assert.equal(await box.isVisible(),false);
  const count=requests.length;
  await page.getByRole('button',{name:'Сохранить настройки',exact:true}).click();
  await page.waitForResponse(r=>r.url().endsWith('/api/status'));
  assert.equal(requests.length,count+1);assert.equal(requests.at(-1).has('encoderInverted'),false);
  assert.deepEqual(errors,[]);
  console.log('PASS: encoder inversion visible/default-on, explicit false/true POST, reload and hidden on other boards');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
