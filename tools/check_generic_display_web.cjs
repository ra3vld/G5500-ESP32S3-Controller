// Real firmware GPIO metadata and browser setup for every generic display family.
const {chromium}=require('playwright');
const {execFileSync}=require('node:child_process');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..'),out=path.join(root,'build/generic-display-web');
fs.mkdirSync(out,{recursive:true});
const source=path.join(out,'metadata.cpp'),binary=path.join(out,'metadata');
fs.writeFileSync(source,'#include "pin_json.hpp"\n#include <iostream>\nint main(){char b[12288];forkrot::JsonWriter j(b,sizeof b);forkrot::writePinCapabilities(j,false);if(!j.good())return 1;std::cout<<j.data();}\n');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
const required={1:['tftMosi','tftMiso','tftSck','tftCs','tftDc','touchCs'],2:['lcdRs','lcdE','lcdD4','lcdD5','lcdD6','lcdD7'],4:['tftMosi','tftSck','tftCs','tftDc','touchSda','touchScl'],5:['tftMosi','tftSck','tftCs','tftDc','touchSda','touchScl']};
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined),headless:true,args:['--no-sandbox']});
 let checked=0;
 try {
  for(const board of [1,2,3,4,9])for(const kind of [1,2,4,5]){
   if(process.env.GENERIC_PROFILES&&!process.env.GENERIC_PROFILES.split(',').includes(`${board}-${kind}`))continue;
   execFileSync('g++',['-std=c++17','-I',path.join(root,'G5500_Fork'),`-DBOARD_PROFILE=${board}`,`-DDISPLAY_KIND=${kind}`,source,'-o',binary]);
   const metadata=JSON.parse(execFileSync(binary,{encoding:'utf8'}));
   for(const width of [1100,390]){
    const page=await browser.newPage({viewport:{width,height:950},locale:'ru-RU'}),errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    const pins=Object.fromEntries(metadata.roles.map(d=>[d.name,-1]));
    const configured={driveInterface:0,simulation:1,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:-1,buttonActiveLow:1,buttonPullup:1,pwmInverted:0,pins};
    const state={token:1,generation:1,role:'controller',recovery:true,trial:false,ready:false,reason:'initial_setup',heapFree:80000,heapMaxBlock:40000,backend:'NONE',classicEsp32:board===4,sppSupported:board===4,separateDrive:true,btsDrive:true,configured};
    await page.route('http://hardware.test/**',route=>{
     const url=new URL(route.request().url());
     return route.fulfill(url.pathname==='/hardware'?{contentType:'text/html',body:html}:{json:url.pathname==='/api/hardware/pins'?metadata:url.pathname==='/api/hardware/display'?{supported:kind!==2,displayKind:kind,storageOk:true,width:kind===5?480:kind===4?240:320,height:kind===5?320:240}:url.pathname==='/api/hardware/modbus'?{pc:0,bus:0,address:1,node:201,first:201,count:1}:state});
    });
    await page.goto('http://hardware.test/hardware');
    await page.locator('[name="pin.'+required[kind][0]+'"]').waitFor({state:'attached'});
    assert(await page.locator('#save').isDisabled(),'Unassigned display cannot start');
    await page.locator('#recovery-manual-open').click();
    const bank=page.locator(`[data-pin-group="${kind===2?'lcd':'tft'}"]`);await bank.evaluate(e=>{for(let at=e;at;at=at.parentElement)if(at.tagName==='DETAILS')at.open=true;});
    const outputs=metadata.gpios.filter(g=>(g.caps&2)&&!g.owner);
    for(const [i,name] of required[kind].entries())await page.locator(`[name="pin.${name}"]`).selectOption(String(outputs[i].gpio));
    assert.equal(await page.locator('#pin-validation').innerText(),'');
    assert.equal(await page.locator('#save').isDisabled(),false,`Valid ${board}/${kind}`);
    if([4,5].includes(kind)){
     assert.match(await bank.locator(':scope > summary').innerText(),/SPI и тачскрин I²C/);
     for(const name of ['tftRst','tftBacklight','touchRst','touchIrq'])assert.equal(await page.locator(`[name="pin.${name}"]`).count(),1);
     assert.equal(await page.locator('[name="pin.touchCs"]').count(),0);
     assert.equal(await page.locator('[name="pin.tftMiso"]').count(),0);
     // A touch wire cannot share a display output or another peripheral.
     await page.evaluate(()=>{form.elements['pin.touchSda'].value=form.elements['pin.tftCs'].value;updatePins();});
     assert(await page.locator('#save').isDisabled());
     await page.locator('[name="pin.touchSda"]').selectOption(String(outputs[4].gpio));
     assert.equal(await page.locator('#save').isDisabled(),false);
    }
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);
    if(board===9&&[4,5].includes(kind)){
     await page.locator('[name="driveMode"]').selectOption('0');
     for(const [name,index] of [['azNegative',6],['azPositive',7]])await page.locator(`[name="pin.${name}"]`).selectOption(String(outputs[index].gpio));
     const sensor=page.locator('[name="azSensor"]');
     await sensor.evaluate(e=>{for(let at=e;at;at=at.parentElement)if(at.tagName==='DETAILS')at.open=true;});
     await sensor.selectOption('1');
     for(const [name,index] of [['azSensorSda',4],['azSensorScl',5]]){
      const field=page.locator(`[name="pin.${name}"]`);
      await field.evaluate(e=>{for(let at=e;at;at=at.parentElement)if(at.tagName==='DETAILS')at.open=true;});
      await field.selectOption(String(outputs[index].gpio));
     }
     assert.equal(await page.locator('#save').isDisabled(),false,'Encoder shares touch I2C');
     await page.locator('[name="azSensorAddress"]').fill(kind===4?'21':'93');
     assert(await page.locator('#save').isDisabled(),'Touch address conflict');
     await page.locator('[name="azSensorAddress"]').fill('6');
     assert.equal(await page.locator('#save').isDisabled(),false);
    }
    if(board===9&&kind===5)await page.screenshot({path:path.join(out,`c3-st7796-${width}.png`),fullPage:true});
    assert.deepEqual(errors,[]);await page.close();
    ++checked;
   }
  }
  console.log(`PASS: ${checked/2} generic board/display combinations, real GPIO metadata, missing and conflicting wires, valid setup, desktop/mobile`);
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
