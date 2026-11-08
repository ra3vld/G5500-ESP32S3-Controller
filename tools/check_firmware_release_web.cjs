const {chromium}=require('playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..'),out=path.join(root,'artifacts/firmware-release');
const source=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8');
const box=source.match(/<section class="settings-group" id="firmwareReleaseBox">[\s\S]*?<\/section>/)[0];
const helper=fs.readFileSync(path.join(root,'tools/web_release.js'),'utf8');
const repo='https://github.com/ra3vld/G5500-ESP32S3-Controller';
const html=`<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><style>body{font:16px system-ui;max-width:860px;margin:16px}button,a{display:inline-block;margin:8px 8px 8px 0;padding:8px}p{overflow-wrap:anywhere}[hidden]{display:none!important}</style><a id="firmwareUpdateBadge" href="#settings" hidden></a>${box}<script>${helper}</script>`;
const cases=[
 ['older','1.0.9','v1.1.0',true],['equal','1.1.0+gabcdef','v1.1.0',false],
 ['ahead','1.2.0-dev.5+gabc','v1.1.0',false],['numeric','1.9.0','v1.10.0',true],
 ['prerelease','1.1.0-dev.9','v1.1.0',true],['unknown','unknown','v1.1.0',false],
 ['blocked','1.0.0','v1.1.0',false],['rate-limit','1.0.0','v1.1.0',false],
 ['none','1.0.0','v1.1.0',false],['unsafe','1.0.0','v1.1.0',false],
 ['draft','1.0.0','v1.1.0',false],['timeout','1.0.0','v1.1.0',false]
];
(async()=>{
 fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 const passed=[];
 try{
  for(const [name,installed,tag,available] of cases){
   const page=await browser.newPage({viewport:{width:1100,height:900}}),errors=[];
   page.on('pageerror',e=>errors.push(e.message));let requests=0;
   await page.route('**/*',async route=>{
    const request=route.request();assert.equal(request.method(),'GET','Version checks must not install firmware');
    if(request.url()==='http://release.test/')return route.fulfill({contentType:'text/html',body:html});
    assert.equal(request.url(),'https://api.github.com/repos/ra3vld/G5500-ESP32S3-Controller/releases/latest');requests++;
    assert.equal(request.headers().cookie,undefined);
    if(requests===1){
     if(name==='blocked')return route.abort('blockedbyclient');
     if(name==='rate-limit')return route.fulfill({status:403,json:{message:'API rate limit'}});
     if(name==='none')return route.fulfill({status:404,json:{message:'Not Found'}});
     if(name==='timeout'){await new Promise(r=>setTimeout(r,200));return route.abort('timedout');}
    }
    return route.fulfill({json:{tag_name:tag,html_url:name==='unsafe'&&requests===1?'https://example.invalid/':repo+'/releases/tag/'+tag,
     draft:name==='draft'&&requests===1,prerelease:false}});
   });
   await page.goto('http://release.test/');
   if(name==='timeout')await page.evaluate(()=>{const original=window.setTimeout;window.setTimeout=(fn,ms,...args)=>original(fn,ms===8000?40:ms,...args);});
   await page.evaluate(version=>{firmwareReleases.update(version);for(let i=0;i<20;i++)firmwareReleases.update(version);},installed);
   await page.waitForFunction(()=>!document.getElementById('firmwareReleaseCheck').disabled);
   assert.equal(requests,1,'Repeated controller status must not repeat GitHub requests');
   assert.equal(await page.locator('#firmwareUpdateBadge').isVisible(),available,name);
   const text=await page.locator('#firmwareReleaseStatus').innerText();
   if(['blocked','rate-limit','none','unsafe','draft','timeout'].includes(name)){
    assert.match(text,/не удалось|ограничил|не найдены|неподходящие|не ответил/i);
    await page.getByRole('button',{name:'Проверить сейчас'}).click();
    await page.waitForFunction(()=>!document.getElementById('firmwareReleaseCheck').disabled);
    assert.equal(requests,2);assert.match(await page.locator('#firmwareReleaseStatus').innerText(),/Доступна новая прошивка/);
   }else if(available)assert.match(text,/Доступна новая/);
   else if(name==='unknown')assert.match(text,/не определена/);
   else assert.match(text,/Новой стабильной прошивки нет/);
   assert.equal(await page.locator('#firmwareReleaseLink').getAttribute('href'),repo+'/releases/tag/'+tag);
   if(name==='older')for(const width of [1100,390]){
    await page.setViewportSize({width,height:900});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    await page.screenshot({path:path.join(out,`release-${width}.png`),fullPage:true});
   }
   assert.deepEqual(errors,[]);passed.push(name);await page.close();
  }
  // The compact page has its own status DTO and must start the shared check
  // automatically when the version arrives over its actual polling path.
  const compact=fs.readFileSync(path.join(root,'G5500_Fork/web_page_spp.hpp'),'utf8').match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
  for(const width of [1100,390]){
   const page=await browser.newPage({viewport:{width,height:950}}),errors=[];let requests=0;
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('**/*',route=>{
    const url=new URL(route.request().url());assert.equal(route.request().method(),'GET');
    if(url.hostname==='api.github.com'){requests++;return route.fulfill({json:{tag_name:'v2.0.0',html_url:repo+'/releases/tag/v2.0.0',draft:false,prerelease:false}});}
    if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:compact});
    assert.equal(url.pathname,'/api/status');
    return route.fulfill({json:{version:'1.1.0',commit:'fixture',ip:'192.168.1.2',controlMode:'manual',hasEl:true,
     heapFree:80000,heapMinFree:70000,allocFailures:0,rssi:-50,axes:[0,1].map(()=>({calibrated:true,current:0,output:0,direction:0,min:0,max:180,limit:100,speed:30}))}});
   });
   await page.goto('http://spp-release.test/');
   await page.waitForFunction(()=>document.getElementById('firmwareReleaseStatus').textContent.includes('Доступна новая'));
   await page.waitForTimeout(1100);assert.equal(requests,1);
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   await page.screenshot({path:path.join(out,`spp-release-${width}.png`),fullPage:true});
   assert.deepEqual(errors,[]);await page.close();passed.push('compact-'+width);
  }
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({passed,live_network:false,firmware_writes:false},null,2)+'\n');
  console.log('PASS: firmware release SemVer, stable/dev/equal/ahead, deduplication, blocked/rate-limit/404/timeout/unsafe/draft retry, desktop/mobile; no firmware writes');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1});
