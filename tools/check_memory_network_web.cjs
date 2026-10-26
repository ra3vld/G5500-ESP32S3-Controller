// Exercise real embedded pages with headers received but a stalled response body.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'G5500_Fork/diagnostic_pages.hpp'),'utf8');
const pages=[...source.matchAll(/char (\w+)\[\].*?R"HTML\(([\s\S]*?)\)HTML";/g)].map(m=>({name:m[1],html:m[2]}));
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const fixture of pages){
   const target=fixture.name==='ADC_TEST_PAGE'?'/api/adcdiag':fixture.name==='RESET_DIAG_PAGE'?'/api/resetreason':'/api/diaglog';
   const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.clock.install();
   await page.addInitScript(target=>{
    const original=window.fetch.bind(window);window.net={target,hang:true,pending:0,max:0,attempts:0,aborted:0,stops:0};
    window.fetch=async(url,options={})=>{
     const name=new URL(url,location.href).pathname;
     if(name==='/api/move')++net.stops;
     if(name===net.target||name==='/api/hang'){
      ++net.attempts;
      if(net.hang||name==='/api/hang'){
       ++net.pending;net.max=Math.max(net.max,net.pending);
       const body=()=>new Promise((resolve,reject)=>{
        const cancel=()=>{--net.pending;++net.aborted;reject(new DOMException('aborted','AbortError'));};
        if(options.signal.aborted)cancel();else options.signal.addEventListener('abort',cancel,{once:true});
       });
       return {ok:true,status:200,json:body,text:body};
      }
     }
     return original(url,options);
    };
   },target);
   await page.route('http://memory.test/**',route=>{
    const name=new URL(route.request().url()).pathname;
    if(!name.startsWith('/api/'))return route.fulfill({contentType:'text/html; charset=utf-8',body:fixture.html});
    if(name==='/api/diaglog')return route.fulfill({body:'healthy log'});
    const data=name==='/api/adcdiag'?{hasEl:true,azRaw:100,azMv:80,elRaw:200,elMv:160}:name==='/api/status'?{build:{pins:{azAdc:34,elAdc:35}}}:name==='/api/resetreason'?{reason:'POWERON',code:1}:{entries:[],ok:true};
    return route.fulfill({json:data});
   });
   await page.goto('http://memory.test/');await page.waitForFunction(()=>net.pending===1);
   await page.clock.runFor(4900);
   assert.equal(await page.evaluate(()=>net.attempts),1,'polls must share the stalled body');
   await page.clock.runFor(200);await page.waitForFunction(()=>net.aborted===1);
   assert.equal(await page.evaluate(()=>net.pending),0,'body deadline releases its slot');
   await page.clock.runFor(1300);await page.waitForFunction(()=>net.pending===1);
   assert.equal(await page.evaluate(()=>net.attempts),2,'one bounded retry after backoff');
   await page.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'));});
   await page.waitForFunction(()=>net.pending===0);
   const before=await page.evaluate(()=>net.attempts);await page.clock.runFor(20000);
   assert.equal(await page.evaluate(()=>net.attempts),before,'hidden page stops background requests');
   await page.evaluate(()=>{net.hang=false;Object.defineProperty(document,'hidden',{configurable:true,value:false});document.dispatchEvent(new Event('visibilitychange'));});
   await page.waitForFunction(n=>net.attempts>n,before);
   assert.equal(await page.evaluate(()=>net.max),1);
   assert.deepEqual(errors,[]);
   // Two unrelated background sources fill the pool. STOP still bypasses it.
   const pool=await page.evaluate(async()=>{
    const a=background.read('one','/api/hang').catch(()=>{}),b=background.read('two','/api/hang').catch(()=>{});
    const third=await background.read('three','/api/hang').then(()=>false,e=>e.name==='PollingDeferred');
    await fetch('/api/move?d=S');background.cancel('one');background.cancel('two');await Promise.all([a,b]);return {third,stops:net.stops,max:net.max};
   });
   assert(pool.third&&pool.stops===1&&pool.max<=2);
   await page.close();console.log('PASS:',fixture.name,'body deadline, coalescing, retry backoff, hidden cancellation, bounded pool and independent STOP');
  }
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exit(1);});
