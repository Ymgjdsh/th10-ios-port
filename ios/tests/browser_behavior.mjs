// Capture an independent upstream WASM trace. This is an instrumented
// regression scenario, not an original-executable or historical-2.3 golden.
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve,dirname} from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {WASI} from 'node:wasi';

const args=process.argv.slice(2),value=(key,fallback)=>args.includes(key)?args[args.indexOf(key)+1]:fallback;
assert(value('--baseline-root'),'Supply --baseline-root pointing to the separate upstream Git checkout');
const root=resolve(value('--baseline-root')),base=value('--url','http://127.0.0.1:8093');
const output=resolve(value('--output',resolve(dirname(fileURLToPath(import.meta.url)),'../../.local/reports/baseline-behavior.json')));
const expectedCommit='0074e589667ecff17dcce34a5f88736b75091d54';
const git=(...a)=>execFileSync('git',a,{cwd:root,encoding:'utf8',windowsHide:true}).trim();
assert.equal(git('rev-parse','HEAD'),expectedCommit);
assert.equal(git('status','--porcelain','--untracked-files=no'),'','Tracked upstream reference must remain clean');
const build=JSON.parse(readFileSync(resolve(root,'th10_web/artifacts/sdl3/build.json')));
const manifest=await (await fetch(base+'/manifest.json')).json();
assert.equal(manifest.execution.sha256,build.sha256);
assert.equal(manifest.execution.loaderSha256,build.loaderSha256);
mkdirSync(dirname(output),{recursive:true});
writeFileSync(output,JSON.stringify({status:'running',baselineCommit:expectedCommit})+'\n');

// Ask the baseline's compiler for WASM ABI offsets. No native pointer-width
// assumptions or guessed object offsets are used for economy/RNG reads.
const fields={
 applicationState:['th10::browser::Application','state'],
 applicationEngine:['th10::browser::Application','engine'],
 scriptRandom:['th10::browser::AnimationEngine','script_random'],
 visualRandom:['th10::browser::AnimationEngine','visual_random'],
 invulnerability:['th10::Player','invulnerability'],
 ...Object.fromEntries(['high_score','score','power','item_value','enemy_activity','character','shot_type','lives','difficulty','stage','section','stage_frames','section_frames','score_units','high_score_units','rank','extend_index','flags'].map(n=>['economy_'+n,['th10::GameEconomy',n]])),
};
const helper=resolve(dirname(output),'baseline-behavior-layout.cpp'),wasm=helper.replace(/\.cpp$/,'.wasm');
writeFileSync(helper,'#include "th10_web/cpp/platform/Application.hpp"\n#include <cstdio>\n#include <cstddef>\nint main(){std::printf("{'+Object.keys(fields).map(n=>'\\"'+n+'\\":%zu').join(',')+'}",'+Object.values(fields).map(([type,field])=>`offsetof(${type},${field})`).join(',')+');}\n');
execFileSync(value('--python','python'),[resolve(root,'tools/emsdk/install/emscripten/emcc.py'),'-O2','-std=c++17','-DTH_NATIVE_PLATFORM=1','-DTH_SDL3=1','-Wno-invalid-offsetof','--use-port=sdl3','--use-port=sdl3_ttf','-I'+root,'-I'+resolve(root,'portable/sdl'),'-sDEFAULT_TO_CXX=1','-sSTANDALONE_WASM=1',helper,'-o',wasm],{cwd:root,env:{...process.env,EM_CONFIG:resolve(root,'tools/emsdk/.emscripten')},windowsHide:true,stdio:'inherit'});
const layoutRunner=resolve(dirname(output),'baseline-behavior-layout.mjs');
writeFileSync(layoutRunner,"import {readFileSync} from 'node:fs';import {WASI} from 'node:wasi';const w=new WASI({version:'preview1',args:[],env:{},returnOnExit:true});const {instance}=await WebAssembly.instantiate(readFileSync(process.argv[2]),{wasi_snapshot_preview1:w.wasiImport});process.exitCode=w.start(instance);\n");
const layout=JSON.parse(execFileSync(process.execPath,[layoutRunner,wasm],{encoding:'utf8',windowsHide:true}));
if(value('--browser'))process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE=value('--browser');
const {launchBrowser}=await import(pathToFileURL(resolve(root,'th10_web/scripts/native/browser-launch.mjs')));
const browser=await launchBrowser(),context=await browser.newContext({viewport:{width:844,height:634}}),page=await context.newPage(),errors=[];
page.on('pageerror',e=>errors.push(e.stack));
try{
 await page.addInitScript(()=>{Date.now=()=>1788840000000;});
 await page.goto(base+'/runtime/th10/th10.html?manual=1&language=lang_zh-hans');
 await page.waitForFunction(()=>window.__th10Runtime,null,{timeout:120000});
 const trace=await page.evaluate(async layout=>{
  const {core:c,Module}=window.__th10Runtime;Module.runtimePrepare=()=>0;
  const app=c.sdl_game_open(1,12345);if(!app)throw Error('Baseline application did not open');
  const name=c.graphics_allocate(100),active=new Set(),rows=[];
  let totalTicks=0,invulnerabilityWrites=0;
  const view=()=>new DataView(c.memory.buffer);
  const tick=(keys=[])=>{const wanted=new Set(keys);for(const key of new Set([...active,...wanted]))if(active.has(key)!==wanted.has(key)){new Uint8Array(c.memory.buffer,name,100).set(new TextEncoder().encode(key+'\0'));c.sdl_key(name,wanted.has(key));}active.clear();for(const key of wanted)active.add(key);const error=c.sdl_loop_tick(app,1/60,17);++totalTicks;if(error)throw Error('Tick failed '+error);};
  const wait=(count,keys=[])=>{for(let i=0;i<count;i++)tick(keys);};
  const sample=gameplayTicks=>{
   const v=view(),state=v.getUint32(app+layout.applicationState,true),economy=c.game_state_economy(state),engine=v.getUint32(app+layout.applicationEngine,true);
   const rng=offset=>{const p=v.getUint32(engine+offset,true);return {seed:v.getUint16(p,true),calls:v.getUint32(p+4,true)};};
   const values=Object.fromEntries(Object.entries(layout).filter(([n])=>n.startsWith('economy_')).map(([n,o])=>[n.slice(8),n==='economy_power'?v.getInt16(economy+o,true):v.getInt32(economy+o,true)]));
   const status=Array.from(new Int32Array(c.memory.buffer,c.sdl_game_status(),5));
   if(values.stage!==status[1]||values.lives!==status[3]||values.power!==status[4])throw Error('Economy/ABI read validation failed');
   rows.push({gameplayTicks,totalTicks,status,economy:values,rng:{script:rng(layout.scriptRandom),visual:rng(layout.visualRandom)}});
  };
  wait(330);for(let i=0;i<5;i++){wait(3,['KeyZ']);wait(42);}wait(100);
  if(view().getInt32(c.application_state(app)+0x38c,true)!==7)throw Error('Gameplay did not start');
  sample(0);
  for(let i=0;i<14000;i++){
   const player=c.world_actor(c.application_world(app),1),v=view();
   if(player){v.setInt32(player+layout.invulnerability,120,true);v.setInt32(player+layout.invulnerability+4,121,true);v.setFloat32(player+layout.invulnerability+8,121,true);++invulnerabilityWrites;}
   tick(['KeyZ','ShiftLeft','ControlLeft']);
   if((i+1)%600===0||i+1===14000)sample(i+1);
   if(i%120===119)await new Promise(resolve=>setTimeout(resolve,0));
  }
  const fontErrors=c.sdl_fonts_errors();c.sdl_keys_clear();c.graphics_free(name);c.sdl_game_close();
  return {rows,totalTicks,invulnerabilityWrites,fontErrors};
 },layout);
 assert.equal(trace.fontErrors,0);assert.deepEqual(errors,[]);
 const result={schema:'th10-independent-baseline-behavior/1',status:'captured',baselineCommit:expectedCommit,wasmSha256:build.sha256,loaderSha256:build.loaderSha256,scriptSha256:createHash('sha256').update(readFileSync(fileURLToPath(import.meta.url))).digest('hex'),browser:browser.version(),seed:12345,language:'chs',fixedDateNow:1788840000000,layout,input:{warmup:[{ticks:330,keys:[]},{repeat:5,steps:[{ticks:3,keys:['KeyZ']},{ticks:42,keys:[]}]},{ticks:100,keys:[]}],gameplay:{ticks:14000,keys:['KeyZ','ShiftLeft','ControlLeft'],beforeEachTick:{playerInvulnerability:{previous:120,current:121,fractional:121},when:'if player exists'}},tickArguments:{seconds:1/60,milliseconds:17},sampling:'After 655 warmup ticks, then after every 600 gameplay ticks and after gameplay tick 14000'},scope:['Independent upstream WASM capture; no candidate outputs used.','Mirrors upstream behavior script warmup/held keys/invulnerability writes, but fixes seed to 12345 and records checkpoints instead of comparing missing historical 2.3 fixture.','Forced invulnerability changes ordinary gameplay and prevents this from establishing unmodified original-executable behavior.','Desktop Chromium/SwiftShader, not iOS device acceptance.'],...trace,errors};
 writeFileSync(output,JSON.stringify(result,null,2)+'\n');
 console.log(JSON.stringify({seed:result.seed,totalTicks:trace.totalTicks,invulnerabilityWrites:trace.invulnerabilityWrites,rows:trace.rows.map(({gameplayTicks,status,rng})=>({gameplayTicks,status,rng}))},null,2));
}catch(error){writeFileSync(output,JSON.stringify({status:'failed',baselineCommit:expectedCommit,error:String(error),errors},null,2)+'\n');throw error;}
finally{await browser.close();}
