// Platform shell for the upstream eagler-touhou/1 Launcher contract.
// Game construction, input, timing, rendering, text and sound belong to C++.
import createModule from './th10-sdl.mjs';
import {exportReplayName,importReplayName} from './motion-replay.mjs';
const protocol='eagler-touhou/1',game='th10',query=new URLSearchParams(location.search),canvas=document.querySelector('canvas');
let canvasRect=canvas.getBoundingClientRect();new ResizeObserver(()=>{canvasRect=canvas.getBoundingClientRect();}).observe(canvas);
const emit=(event,fields={})=>parent.postMessage({protocol,game,event,...fields},location.origin);
let Module,core,app=0,launched=false,first=false,closing=false,language=query.get('language')==='lang_zh-hans'?'chs':'jp',options={},music=true;
let frames=0,lastHealth=0,lastFrame=0,maxGap=0,lastPresented=0,saveTimer=null;
const error=reason=>{const message=reason?.stack||String(reason);document.querySelector('#error').textContent=message;emit('error',{message,error:message});console.error(reason);};
const u32=(ptr,count)=>new Uint32Array(core.memory.buffer,ptr,count);
const cstring=(text,fn)=>{const bytes=new TextEncoder().encode(text+'\0'),p=core.graphics_allocate(bytes.length);try{new Uint8Array(core.memory.buffer,p,bytes.length).set(bytes);return fn(p);}finally{core.graphics_free(p);}};
const root=()=>'/savesth10/'+language;
let storageSync=Promise.resolve();
const sync=populate=>{const current=storageSync.then(()=>new Promise((resolve,reject)=>Module.FS.syncfs(populate,e=>e?reject(e):resolve())));storageSync=current.catch(()=>{});return current;};
function relativeSave(path){
 if(typeof path!=='string'||path.length>200)throw Error('Invalid save path');
 path=path.replaceAll('\\','/').toLowerCase();
 if(path.startsWith('/savesth10/'))path=path.slice('/savesth10/'.length).replace(/^(?:jp|chs)\//,'');
 else path=path.replace(/^\//,'');
 if(!/^(?:scoreth10c?\.dat|th10\.cfg|replay\/th10_(?:\d{2}|ud[a-z0-9]{4})\.rpyx?)$/.test(path))throw Error('Invalid save path: '+path);
 return path;
}
function fileExists(path){return Module.FS.analyzePath(path).exists;}
async function migrateSaves(){
 if(fileExists('/savesth10/.migration-v3'))return;
 const databases=typeof indexedDB.databases==='function'?await indexedDB.databases():null;
 for(const lang of ['jp','chs']){
  const name='th10-1.00a-'+lang;if(databases&&!databases.some(db=>db.name===name))continue;
  const db=await new Promise((resolve,reject)=>{const request=indexedDB.open(name);request.onerror=()=>reject(request.error);request.onsuccess=()=>resolve(request.result);});
  try{if(!db.objectStoreNames.contains('files'))continue;
   const entries=await new Promise((resolve,reject)=>{const tx=db.transaction('files','readonly'),rows=[];tx.objectStore('files').openCursor().onsuccess=e=>{const c=e.target.result;if(c){rows.push([c.key,c.value]);c.continue();}};tx.oncomplete=()=>resolve(rows);tx.onerror=()=>reject(tx.error);});
   for(const [name,value] of entries){let path;try{path=relativeSave(name);}catch{continue;}const bytes=value instanceof Blob?new Uint8Array(await value.arrayBuffer()):new Uint8Array(value);path=importReplayName(path,bytes,10);const target='/savesth10/'+lang+'/'+path;if(!fileExists(target)){Module.FS.mkdirTree(target.slice(0,target.lastIndexOf('/')));Module.FS.writeFile(target,bytes);}}
  }finally{db.close();}
 }
 Module.FS.writeFile('/savesth10/.migration-v3',new Uint8Array([1]));await sync(false);
}
async function mountData(){
 const index=await (await fetch('./th10.data.json')).json();let buffer;
 if(query.get('managedData')==='1'){
  if(parent===window||typeof parent.__eaglerPrepareManagedRuntimeDataV1!=='function')throw Error('Managed package provider unavailable');
  const result=await parent.__eaglerPrepareManagedRuntimeDataV1({game,generation:query.get('gameGeneration')});buffer=result.buffer;
 }else buffer=await (await fetch('../../packages/th10/th10.data')).arrayBuffer();
 if(buffer.byteLength!==index.remote_package_size)throw Error('Game DATA size mismatch');
 const seen=new Set();for(const f of index.files){
  if(!/^\/(?:game|fonts)\/[a-z0-9_.-]+$/.test(f.filename)||seen.has(f.filename)||!Number.isSafeInteger(f.start)||!Number.isSafeInteger(f.end)||f.start<0||f.end<=f.start||f.end>buffer.byteLength)throw Error('Invalid DATA layout');
  seen.add(f.filename);Module.FS.mkdirTree(f.filename.slice(0,f.filename.lastIndexOf('/')));Module.FS.writeFile(f.filename,new Uint8Array(buffer,f.start,f.end-f.start),{canOwn:true});
 }
 // Standalone runtime is useful for self-host checks; normal Launcher installs
 // package resources directly into FS through its generation lease.
 if(query.get('managedData')!=='1')for(let i=0;i<18;i++){const name=String(i).padStart(2,'0')+'.flac',response=await fetch('../../packages/th10/music/'+name);if(!response.ok)throw Error('Missing '+name);Module.FS.mkdirTree('/music');Module.FS.writeFile('/music/'+name,new Uint8Array(await response.arrayBuffer()),{canOwn:true});}
}
async function installResources(resources=[]){for(const resource of resources){
 if(typeof resource.path!=='string'||!/^\/(?:music|fonts)\/[a-z0-9_.-]+$/.test(resource.path))throw Error('Invalid runtime resource path');
 const url=new URL(resource.url,location.href);if(url.origin!==location.origin)throw Error('Runtime resource must be same-origin');
 const response=await fetch(url);if(!response.ok)throw Error('Resource download failed');const bytes=new Uint8Array(await response.arrayBuffer());Module.FS.mkdirTree(resource.path.slice(0,resource.path.lastIndexOf('/')));Module.FS.writeFile(resource.path,bytes,{canOwn:true});
}}
function applyOptions(){core.sdl_touch_options(!!options.touchEnabled,options.touchMovementMode==='touch-unlimited',Number(options.touchSensitivity||100)/100);core.sdl_touch_gestures?.(options.touchFocusMode==='two-finger',!!options.doubleTapBombEnabled);core.sdl_touch_mode?.(['touch','touch-unlimited','joystick','joystick-free'].indexOf(options.touchMovementMode));if(app)core.application_touch_display?.(app,options.alwaysHitbox?1:0);}
function status(){return Array.from(new Int32Array(core.memory.buffer,core.sdl_game_status(),10));}
function save(){if(app)core.application_save(app);return sync(false);}
async function stop(){if(closing)return;closing=true;try{core.sdl_loop_stop();await save();core.sdl_game_close();await sync(false);app=0;launched=false;emit('exit',{code:0,status:'success'});}finally{closing=false;}}
function launch(){
 if(launched)return;
 core.sdl_music_enabled?.(music);app=core.sdl_game_open(language==='chs',Date.now()&65535);if(!app)throw Error('C++ game initialization failed');
 applyOptions();launched=true;first=false;lastPresented=0;lastHealth=performance.now();lastFrame=0;frames=0;maxGap=0;
 const audio=Module.SDL3?.audioContext;audio?.resume().catch(()=>{});
 canvas.focus({preventScroll:true});core.sdl_loop_pause(document.hidden?1:0);core.sdl_loop_start(app);
 emit('runtime-info',{renderer:'SDL3 / WebGL2 / C++',architecture:'eagler-touhou/1',version:'3.5.1-sdl3'});
}
async function command(message){
 switch(message.command){
 case 'configure':if(launched)throw Error('Cannot configure a running game');language=message.language==='lang_zh-hans'?'chs':'jp';options=message.options||{};music=message.music!=='none';await installResources(message.runtimeResources);await installResources(message.resources);applyOptions();return {};
 case 'resources':await installResources(message.resources);return {};
 case 'keyboard':cstring(String(message.code),p=>core.sdl_key(p,!!message.down));return {};
 case 'keyboard-clear':core.sdl_keys_clear();return {};
 case 'touch-cancel':core.sdl_touch_cancel();return {};
 case 'direct-touch':{const x=((Number(message.x)||0)*innerWidth-canvasRect.left)/canvasRect.width,y=((Number(message.y)||0)*innerHeight-canvasRect.top)/canvasRect.height;core.sdl_touch(({down:0,move:1,up:2,cancel:2})[message.type]??2,Number(message.id)||0,x,y);return {};}
 case 'touch-controls':{const c=message.controls||message,sensitivity=Number(c.touchSensitivity);if(Number.isFinite(sensitivity)&&sensitivity>=50&&sensitivity<=300){options.touchSensitivity=sensitivity;applyOptions();}core.sdl_touch_controls(!!c.fireEnabled,!!c.focusEnabled,c.bombSerial>>>0,c.escapeSerial>>>0,Number(c.joystickX)||0,Number(c.joystickY)||0);return {};}
 case 'launch':launch();return {};
 case 'sync':await save();return {};
 case 'list':{const files=[];for(const dir of ['', '/replay'])for(const name of Module.FS.readdir(root()+dir)){const path=(dir+'/'+name).replace(/^\//,'');try{relativeSave(path);}catch{continue;}const full=root()+'/'+path,s=Module.FS.stat(full);if(Module.FS.isFile(s.mode)){const bytes=Module.FS.readFile(full);files.push({path:exportReplayName(path,bytes,10),size:s.size});}}return {files};}
 case 'read':{let path=relativeSave(message.path);if(path.endsWith('.rpyx'))path=path.slice(0,-1);return {bytes:Array.from(Module.FS.readFile(root()+'/'+path))};}
 case 'write':{if(!Array.isArray(message.bytes)||message.bytes.length>16*1024*1024||message.bytes.some(b=>!Number.isInteger(b)||b<0||b>255))throw Error('Invalid save bytes');const bytes=new Uint8Array(message.bytes),path=importReplayName(relativeSave(message.path),bytes,10);Module.FS.writeFile(root()+'/'+path,bytes);await sync(false);return {};}
 case 'remove':{let path=relativeSave(message.path);if(path.endsWith('.rpyx'))path=path.slice(0,-1);Module.FS.unlink(root()+'/'+path);await sync(false);return {};}
 default:throw Error('Unsupported runtime command: '+message.command);
 }
}
let queue=Promise.resolve();
window.addEventListener('message',event=>{const m=event.data;if(event.source!==parent||event.origin!==location.origin||m?.protocol!==protocol||m.game!==game||typeof m.command!=='string')return;
 queue=queue.then(async()=>{await initialized;try{const result=await command(m);if(typeof m.request==='string')parent.postMessage({protocol,game,request:m.request,ok:true,...result},location.origin);}catch(e){if(typeof m.request==='string')parent.postMessage({protocol,game,request:m.request,ok:false,error:String(e),errno:e.errno},location.origin);else error(e);}}).catch(error);
});
document.addEventListener('visibilitychange',()=>{if(!core||!launched)return;core.sdl_keys_clear();core.sdl_touch_cancel();core.sdl_loop_pause(document.hidden?1:0);if(document.hidden)queue=queue.then(save).catch(error);});
window.addEventListener('pagehide',()=>{if(core&&launched){core.sdl_loop_pause(1);void save().catch(console.error);}});
canvas.addEventListener('webglcontextlost',event=>{event.preventDefault();core?.sdl_loop_pause(1);error('图形环境已失效，请退出后重新开始。');});
for(const name of ['pointerdown','keydown'])window.addEventListener(name,()=>Module?.SDL3?.audioContext?.resume().catch(()=>{}),{capture:true});
const initialized=(async()=>{
 let audioContext;try{audioContext=parent.__touhouAudioContext||parent.__th10AudioContext;}catch{}
 Module=await createModule({canvas,noInitialRun:true,...(audioContext?{SDL3:{audioContext}}:{}),print:console.log,printErr:console.error,
  instantiateWasm(imports,ready){return WebAssembly.instantiateStreaming(fetch('./th10-sdl.wasm'),imports).then(({instance,module})=>{core=instance.exports;ready(instance,module);return core;});}
 });
 window.Module=Module;window.FS=Module.FS;Module.FS.mkdirTree('/savesth10');Module.FS.mount(Module.IDBFS,{},'/savesth10');await sync(true);
 for(const lang of ['jp','chs'])Module.FS.mkdirTree('/savesth10/'+lang+'/replay');await migrateSaves();await mountData();cstring('#screen',core.sdl_canvas);
 Module.runtimePrepare=()=>!document.hidden;
 Module.runtimeFinish=(result,duration)=>{
  const now=performance.now(),p=u32(core.sdl_stats(),6)[5];if(p!==lastPresented){frames++;if(lastFrame)maxGap=Math.max(maxGap,now-lastFrame);lastFrame=now;lastPresented=p;if(!first){first=true;emit('first-frame');}}
  if(result||core.application_error(app)){if(core.application_error(app)){error('Game error '+core.application_error(app));core.sdl_loop_pause(1);}else queueMicrotask(()=>void stop().catch(error));}
  if(now-lastHealth>=1000){emit('frame-health',{fps:frames*1000/(now-lastHealth),maxGapMs:maxGap,frameMs:duration});const a=u32(core.sdl_audio_stats(),12);emit('audio-health',{queuedMs:a[5]*1000/44100,minQueuedMs:a[7]*1000/44100,backend:'script',underruns:0,robust:true});frames=0;maxGap=0;lastHealth=now;}
 };
 Module.runtimeFileChanged=()=>{if(saveTimer!==null)return;saveTimer=setTimeout(()=>{saveTimer=null;queue=queue.then(()=>sync(false)).catch(error);},0);};
 Module.runtimeStopped=()=>{};Module.callMain=launch;
 window.__th10Runtime={core,Module,get app(){return app;},status,launch,stop,command};
 emit('ready');if(query.get('standalone')==='1')launch();
})().catch(e=>{error(e);throw e;});
