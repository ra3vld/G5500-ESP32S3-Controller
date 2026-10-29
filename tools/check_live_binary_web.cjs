// The production C++ encoder feeds the production browser decoder and handlers.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const vectors=execFileSync(process.env.LIVE_TEST_BIN||path.join(root,'build/live_tests'),['--vectors'],{encoding:'utf8'}).trim().split('\n').map(JSON.parse);
const sample=JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
function equivalent(actual,expected,field='state'){
 if(typeof expected==='number'&&typeof actual==='number')assert(Math.abs(actual-expected)<=Math.max(.00501,Math.abs(expected)*1e-6),`${field}: ${actual} != ${expected}`);
 else if(expected&&typeof expected==='object'){
  assert.deepEqual(Object.keys(actual).sort(),Object.keys(expected).sort(),field);
  for(const key of Object.keys(expected))equivalent(actual[key],expected[key],field+'.'+key);
 }else assert.deepEqual(actual,expected,field);
}
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>{
   window.testSockets=[];
   window.WebSocket=class {static OPEN=1;readyState=1;bufferedAmount=0;sent=[];constructor(){testSockets.push(this)}close(){if(this.readyState===1){this.readyState=3;this.onclose?.()}}send(data){this.sent.push(data)}};
  });
  const state={...sample,livePort:81,liveBinaryVersion:2,liveControlChannel:false,configurationLocked:false};
  await page.route('http://binary.test/**',route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/api/status')return route.fulfill({json:state});
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{items:[],values:[],points:[],peers:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://binary.test/');await page.waitForFunction(()=>live.socket&&httpState);
  assert.equal(await page.evaluate(()=>{live.socket.onopen();return live.socket.sent[0]}),'v=2&telemetry=1');
  for(const vector of vectors){
   const decoded=await page.evaluate(v=>decodeLiveBinary(Uint8Array.from(v.binary.match(/../g),b=>parseInt(b,16)).buffer,v.metadata),vector);
   assert.equal(decoded.revision,15);equivalent(decoded.state,vector.state);
  }
  const vector=vectors[0];
  for(const fault of ['size','version','boot','selection','metadata','missing']){
   assert.equal(await page.evaluate(({v,fault})=>{
    let bytes=Uint8Array.from(v.binary.match(/../g),b=>parseInt(b,16));
    if(fault==='size')bytes=bytes.slice(1);
    if(fault==='version')bytes[2]=3;
    if(fault==='boot')bytes[12]^=1;
    if(fault==='selection')bytes[20]^=1;
    if(fault==='metadata')bytes[8]^=1;
    try{decodeLiveBinary(bytes.buffer,fault==='missing'?null:v.metadata);return false}catch{return true}
   },{v:vector,fault}),true,fault);
  }
  // Metadata alone must not make stale coordinates or a new selection ready.
  await page.evaluate(v=>{live.last=Date.now()-2000;live.socket.onmessage({data:JSON.stringify(v.metadata)});},vector);
  assert.equal(await page.evaluate(()=>live.ready||liveFresh()),false);
  await page.evaluate(v=>live.socket.onmessage({data:Uint8Array.from(v.binary.match(/../g),b=>parseInt(b,16)).buffer}),vector);
  await page.waitForFunction(()=>live.ready&&live.format==='binary');
  assert.equal(await page.evaluate(()=>live.state.az),vector.state.az);
  const old=await page.evaluate(()=>live.revision);
  const oldAt=await page.evaluate(()=>{live.last=Date.now()-2000;return live.last});
  await page.evaluate(v=>{const bytes=Uint8Array.from(v.binary.match(/../g),b=>parseInt(b,16));new DataView(bytes.buffer).setUint32(4,1,true);live.socket.onmessage({data:bytes.buffer});},vector);
  assert.equal(await page.evaluate(()=>live.revision),old);
  assert.equal(await page.evaluate(()=>live.last),oldAt,'old revisions do not renew freshness');
  // A new connection cannot reuse the previous connection's metadata.
  await page.evaluate(()=>{disconnectLive(live.socket,81);clearTimeout(live.retry);live.retry=null;connectLive(81);});
  await page.evaluate(v=>live.socket.onmessage({data:Uint8Array.from(v.binary.match(/../g),b=>parseInt(b,16)).buffer}),vector);
  assert.equal(await page.evaluate(()=>live.ready),false);
  assert.deepEqual(errors,[]);
  console.log('PASS: C++/browser field parity, float precision, binary negotiation, malformed/context guards, stale revisions, metadata freshness and reconnect reset');
  console.log(JSON.stringify(vectors.map(v=>({jsonBytes:Buffer.byteLength(JSON.stringify(v.state)),metadataBytes:Buffer.byteLength(JSON.stringify(v.metadata)),binaryBytes:v.binary.length/2}))));
 }finally{await browser.close()}
})().catch(error=>{console.error(error);process.exit(1)});
