// Camera display with mocked image transport; no device connection.
const {chromium}=require('playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const sample=JSON.parse(execFileSync(path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:390,height:850}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>window.G5500_HTTP_ONLY=true);
  let state={...sample,cameraSupported:true,cameraReady:true,cameraPort:82,cameraError:''};
  let frames=0,fail=false,hold=false,release;
  const image=Buffer.from('<svg xmlns="http://www.w3.org/2000/svg" width="640" height="480"><rect width="640" height="480" fill="#446b59"/><text x="100" y="240" fill="white" font-size="40">Camera preview</text></svg>');
  await page.route('http://camera.test:82/**',async route=>{
   ++frames;assert.equal(new URL(route.request().url()).pathname,'/camera.jpg');
   if(hold)await new Promise(resolve=>release=resolve);
   return route.fulfill({status:fail?503:200,contentType:fail?'text/plain':'image/svg+xml',body:fail?'unavailable':image});
  });
  await page.route('http://camera.test/**',route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/api/status')return route.fulfill({json:state});
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{ok:true,axes:[],points:[],items:[],peers:[],values:[]}});
   return route.fulfill({contentType:'text/html',body:html});
  });
  await page.goto('http://camera.test/');
  await page.waitForFunction(()=>!document.getElementById('cameraImage').hidden);
  await page.waitForFunction(()=>document.getElementById('cameraImage').naturalWidth===640);
  await page.waitForTimeout(1100);assert(frames>=8,'camera should refresh at about eight frames per second');
  for(const width of [320,390,1100]){
   await page.setViewportSize({width,height:850});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  }
  fs.mkdirSync(path.join(root,'local-notes/camera-web'),{recursive:true});
  await page.locator('#cameraCard').screenshot({path:path.join(root,'local-notes/camera-web/camera.png')});
  await page.getByRole('tab',{name:'Настройки',exact:true}).click();
  const paused=frames;await page.waitForTimeout(1100);assert.equal(frames,paused,'settings must pause capture');
  await page.getByRole('tab',{name:'Управление',exact:true}).click();
  await page.waitForFunction(()=>!document.getElementById('cameraImage').hidden);
  fail=true;await page.waitForFunction(()=>document.getElementById('cameraMessage').textContent.includes('Нет изображения'));
  fail=false;await page.waitForFunction(()=>!document.getElementById('cameraImage').hidden,{},{timeout:7000});
  hold=true;await page.waitForTimeout(700);
  assert(release,'one pending frame');const pending=frames;await page.waitForTimeout(1000);assert.equal(frames,pending,'slow camera must not pile up requests');
  release();hold=false;
  await page.evaluate(()=>{cameraView.state={cameraSupported:true,cameraReady:false,cameraError:'camera_init_failed'};updateCamera(cameraView.state);});
  assert.match(await page.locator('#cameraMessage').textContent(),/Камера не найдена/);
  state={...state,cameraSupported:false,cameraReady:false,cameraPort:0};
  await page.evaluate(s=>updateCamera(s),state);assert(await page.locator('#cameraCard').isHidden());
  assert.deepEqual(errors,[]);
  console.log('PASS: camera refresh, retry, bounded requests, settings pause, errors and mobile layout');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exit(1)});
