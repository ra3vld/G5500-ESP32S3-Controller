// Offline UI/API-contract checks; no firmware or devices.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=fs.readFileSync(path.resolve(__dirname,'../G5500_Fork/web_page.hpp'),'utf8'),script=source.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script);
const nodes={},posts=[],feedback=[];
function node(id){return nodes[id]||=( {textContent:'',value:'',disabled:false,style:{},dataset:{},elements:{},reportValidity:()=>true} )}
for(const axis of ['az','el']){
 node(axis+'TuneTravel').value='60';node(axis+'TuneLimit').value='50';
 const f=node(axis+'MotionForm');
 f.elements[axis+'RegulatorMode']={value:'cascade'};
 f.elements[axis+'MotionOutputLimitPercent']={value:'50'};
 f.elements[axis+'MotionMovementDegrees']={value:'1'};
 f.elements[axis==='az'?'coastDegrees':'elCoastDegrees']={value:'2'};
}
const state={deviceRole:'controller',controlMode:'manual',moving:false,azStart:0,azMax:450,elStart:0,elMax:180,azRangeRevision:1,
 axes:[{current:90,calibrated:true,regulatorMode:'cascade',motionPhase:'idle',measuredVelocity:0,requestedVelocity:0,speedOutputPercent:0},{current:90,calibrated:true,regulatorMode:'simple',motionPhase:'idle',measuredVelocity:0,speedOutputPercent:0}],autotune:{phase:'idle',active:false,axis:-1,id:0}};
const context=vm.createContext({window:{_forkState:state},document:{getElementById:node,querySelector:node},Number,Math,
 setText:(n,t)=>n.textContent=t,settingsFeedback:t=>feedback.push(t),motionFaultText:t=>t,
 forkPost:async(path,body)=>{posts.push({path,body:{...body}});return true},loadMotionSettings:async()=>{},upd:async()=>{},
 fetch:async()=>({ok:true,json:async()=>context.details})});
vm.runInContext(script.slice(script.indexOf('let autoTuneRequest='),script.indexOf('function renderMotionStatus(')),context);
vm.runInContext(script.slice(script.indexOf('function motionFaultText('),script.indexOf('function renderDriveStatus(')),context);
(async()=>{
 vm.runInContext('renderAutoTune(window._forkState)',context);
 assert.equal(node('azTunePosition').textContent,'90.00°');assert.equal(node('azTuneVelocity').textContent,'0.00 °/с');
 assert.equal(node('azTuneCenter').textContent,'В середину (225°)');assert(!node('azTuneCenter').disabled);assert(node('azTuneReset').disabled);
 assert.equal(node('azTuneRoom').textContent,'90.00° / 360.00°');
 await vm.runInContext("autoTunePrepare('center','az')",context);
 assert.equal(posts.at(-1).path,'/api/motion/autotune');assert.equal(posts.at(-1).body.action,'center');assert.equal(posts.at(-1).body.axis,'az');assert.equal(posts.at(-1).body.rangeRevision,1);posts.length=0;
 state.elStart=-45;state.elMax=190;vm.runInContext('renderAutoTune(window._forkState)',context);assert.equal(node('elTuneCenter').textContent,'В середину (72.5°)');
 state.axes[0].motionFault='stalled';state.fault='az_stalled';vm.runInContext('renderAutoTune(window._forkState)',context);
 assert.match(node('azTuneDriveState').textContent,/Ошибка: AZ: движение прекратилось/);assert(node('azTuneCenter').disabled);assert(!node('azTuneReset').disabled);
 await vm.runInContext("autoTunePrepare('reset','az')",context);assert.equal(posts.at(-1).path,'/api/motion/reset');assert.equal(posts.at(-1).body.axis,'all');posts.length=0;
 state.axes[0].motionFault='feedback_stale';state.fault='az_feedback_stale';vm.runInContext('renderAutoTune(window._forkState)',context);assert.equal(node('azTuneVelocity').textContent,'—');
 state.axes[0].motionFault='';state.fault='';
 await vm.runInContext("autoTuneAction('start','az')",context);
 assert.equal(posts.length,1);assert.equal(posts[0].path,'/api/motion/autotune');
 assert.equal(posts[0].body.id,0);assert.equal(posts[0].body.rangeRevision,1);assert.equal(posts[0].body.mode,'cascade');
 assert.equal(posts[0].body.travel,'60');assert.equal(posts[0].body.outputLimit,'50');
 const result={speedPercent:20,approachSpeedPercent:20,coastDegrees:2,slowdownDegrees:10,accelerationMs:500,decelerationMs:500,decelerationFactor:1,
 PositionKp:.2,PositionKi:0,PositionKd:0,VelocityKp:1,VelocityKi:2,VelocityKd:0,Velocity:3,Acceleration:2,Deceleration:2,Jerk:4,Feedforward:8,
 OutputLimitPercent:50,KickPercent:35,KickMs:1000,KickReleaseMs:100,MovementDegrees:1,StoppedVelocity:.2};
 state.autotune={phase:'high',active:true,axis:0,id:1,mode:'cascade',output:25,direction:-1};
 vm.runInContext('renderAutoTune(window._forkState)',context);
 assert.match(node('azTuneStatus').textContent,/Измерение отклика.*назад/);assert(node('azTuneStart').disabled);assert(node('elTuneStart').disabled);
 assert(!node('azTuneCancel').disabled);assert(node('elTuneCancel').disabled);assert(node('azTuneApply').disabled);
 assert(node('azTuneCenter').disabled);assert(node('azTuneReset').disabled);assert(!node('azTuneStop').disabled);
 const blocked=posts.length;await vm.runInContext("autoTunePrepare('center','az')",context);await vm.runInContext("autoTunePrepare('reset','az')",context);assert.equal(posts.length,blocked);
 await vm.runInContext("autoTunePrepare('stop','az')",context);assert.equal(posts.at(-1).path,'/api/move');assert.equal(posts.at(-1).body.d,'S');
 state.autotune.phase='minimum';vm.runInContext('renderAutoTune(window._forkState)',context);assert.match(node('azTuneStatus').textContent,/минимальной устойчивой скорости/);assert(node('azTuneApply').disabled);
 result.minimumVelocity=1.5;result.minimumOutput=18;
 state.autotune={phase:'complete',active:false,axis:0,id:1,mode:'cascade',validationError:.12};context.details={...state.autotune,result};
 vm.runInContext('renderAutoTune(window._forkState)',context);await new Promise(resolve=>setImmediate(resolve));
 assert(!node('azTuneApply').disabled);assert.match(node('azTuneResult').textContent,/Проверенная минимальная скорость: 1.500/);assert(node('elTuneApply').disabled);assert.match(node('azTuneResult').textContent,/Положение P \/ I \/ D/);assert.match(node('azTuneResult').textContent,/Удержание выключено/);
 await vm.runInContext("autoTuneAction('apply','az')",context);assert.equal(posts.at(-1).body.id,1);
 state.autotune={phase:'cancelled',active:false,axis:0,id:1,reason:'stopped'};
 vm.runInContext('renderAutoTune(window._forkState)',context);assert.match(node('azTuneStatus').textContent,/STOP/);assert.equal(node('azTuneResult').textContent,'');assert(node('azTuneApply').disabled);
 const count=posts.length;state.axes[0].current=10;await vm.runInContext("autoTuneAction('start','az')",context);
 assert.equal(posts.length,count);assert.match(node('azTuneStatus').textContent,/Недостаточно места/);
 state.axes[0].current=90;state.controlMode='auto';await vm.runInContext("autoTuneAction('start','az')",context);assert.match(node('azTuneStatus').textContent,/ручной режим/);
 state.controlMode='manual';node('azMotionForm').dataset.dirty='1';await vm.runInContext("autoTuneAction('start','az')",context);assert.match(feedback.at(-1),/Сначала сохраните/);
 assert.match(source,/type="button" id="\$\{axis\}TuneStart"/);
 state.controlMode='manual';delete node('azMotionForm').dataset.dirty;delete state.axes[0].measuredVelocity;vm.runInContext('renderAutoTune(window._forkState)',context);assert.equal(node('azTuneVelocity').textContent,'—');
 state.moving=true;vm.runInContext('renderAutoTune(window._forkState)',context);assert(node('azTuneCenter').disabled);assert(node('azTuneStart').disabled);assert(!node('azTuneStop').disabled);
 const countMoving=posts.length;await vm.runInContext("autoTunePrepare('center','az')",context);assert.equal(posts.length,countMoving);
 console.log('PASS: autotune telemetry, mechanical midpoint, reset, independent STOP, motion/tuning guards, bounded start, run ID, preview and Apply');
})().catch(e=>{console.error(e);process.exit(1)});
