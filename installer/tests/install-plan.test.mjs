import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {readFlashVerified} from '../src/flash-read.mjs';
import {inspectNvs,crc32} from '../src/nvs-inspection.mjs';
import {planInstallation,installPreserving,checkWriteRanges,checkOtaState,checkPartitionTable,PRESERVED} from '../src/install-plan.mjs';
const hash=b=>createHash('md5').update(b).digest('hex');
const fixture=()=>new Uint8Array(readFileSync(new URL('./fixtures/synthetic-settings.bin',import.meta.url)));
const u32=(b,n,v)=>new DataView(b.buffer,b.byteOffset,b.byteLength).setUint32(n,v,true);
const putText=(b,n,s)=>{b.fill(0,n,n+32);b.set(new TextEncoder().encode(s),n);};
const app=(version='0.3.8')=>{const b=new Uint8Array(4096);b[0]=0xe9;b[12]=9;u32(b,32,0xabcd5432);putText(b,48,version);putText(b,80,'ember-link');return b;};
function partitionTable(){
 const b=new Uint8Array(3072).fill(255);
 [[1,2,0x9000,0x6000,'nvs'],[1,0,0xf000,0x2000,'otadata'],[1,1,0x11000,0x1000,'phy_init'],
 [0,16,0x20000,0x300000,'ota_0'],[0,17,0x320000,0x300000,'ota_1'],[1,2,0x620000,0x10000,'link_future']].forEach(([type,sub,offset,size,label],i)=>{
  const n=i*32;b[n]=0xaa;b[n+1]=0x50;b[n+2]=type;b[n+3]=sub;u32(b,n+4,offset);u32(b,n+8,size);
  b.fill(0,n+12,n+28);b.set(new TextEncoder().encode(label),n+12);u32(b,n+28,0);
 });
 b[192]=0xeb;b[193]=0xeb;b.set(createHash('md5').update(b.subarray(0,192)).digest(),208);return b;
}
function files(){return [{address:0,data:new Uint8Array(4096).fill(11)},{address:0x8000,data:partitionTable()},
 {address:0xf000,data:new Uint8Array(8192).fill(255)},{address:0x20000,data:app()}];}
class Loader{
 constructor(blank=false){this.flash=new Uint8Array(0x1000000).fill(255);this.writes=0;this.blank=blank;this.transport={read:async()=>{const digest=this.readDigest;this.readDigest=null;return digest;}};
  if(!blank){this.flash.set(files()[1].data,0x8000);this.flash.set(app(),0x20000);this.flash.set(fixture(),0x9000);this.flash.fill(91,0x620000,0x630000);}}
 async readFlash(a,s){assert.equal(this.readDigest??null,null,'Unread digest before next command');const data=this.flash.slice(a,a+s);this.readDigest=new Uint8Array(createHash('md5').update(data).digest());return data;}
 async flashMd5sum(a,s){return hash(this.flash.subarray(a,a+s));}
 async writeFlash(o){this.writes++;assert.equal(o.eraseAll,false);this.options=o;
  for(const f of o.fileArray){this.flash.fill(255,f.address,Math.ceil((f.address+f.data.length)/4096)*4096);this.flash.set(f.data,f.address);}}
}
const manifest={version:'0.3.8'};
test('Espressif-generated multi-page NVS fixture is accepted; inspection does not mutate input',()=>{
 const data=fixture(),before=data.slice();inspectNvs(data);assert.deepEqual(data,before);
 inspectNvs(new Uint8Array(0x6000).fill(255));
});
test('corrupt, unsupported, and unfinished NVS pages fail closed',()=>{
 for(const edit of [b=>b[0]=0xf8,b=>b[8]=0xfd,b=>b[28]^=1,b=>b[100]^=1,b=>b[32]=(b[32]&~3)|1]){
  const data=fixture();edit(data);assert.throws(()=>inspectNvs(data));
 }
 assert.throws(()=>inspectNvs(new Uint8Array(16)));
});
// Locate an official-generator blob chunk and change its value with valid CRCs.
function changeBlob(data,key,edit){
 for(let base=0;base<data.length;base+=4096)for(let i=0;i<126;i++){
  const off=base+64+32*i,h=data.subarray(off,off+32),name=new TextDecoder().decode(h.subarray(8,24)).split('\0')[0];
  if(h[1]!==0x42||name!==key)continue;
  const size=h[24]|h[25]<<8,value=data.subarray(off+32,off+32+size);edit(value);
  u32(h,28,crc32(value));u32(h,4,crc32(h.subarray(8),crc32(h.subarray(0,4))));return;
 }throw Error('Missing synthetic blob');
}
test('pending cloud firmware/design/display receipts block writes even with valid NVS CRCs',async()=>{
 for(const key of ['update_v1','receipt','state_v1']){
  const l=new Loader();const data=fixture();changeBlob(data,key,b=>b[key==='state_v1'?16:4]=0);l.flash.set(data,0x9000);
  await assert.rejects(installPreserving(l,files(),manifest));assert.equal(l.writes,0);
 }
});
test('future transport settings schema is refused',()=>{
 const b=fixture();changeBlob(b,'settings_v2',v=>{v[10]='3'.charCodeAt(0);});assert.throws(()=>inspectNvs(b));
});
test('known installation keeps all preserved ranges and uses sector-only erasure',async()=>{
 const l=new Loader(),before=PRESERVED.map(([a,s])=>hash(l.flash.subarray(a,a+s)));
 const plan=await installPreserving(l,files(),manifest);assert.equal(plan.mode,'preserve');assert.equal(l.writes,1);
 for(let i=0;i<PRESERVED.length;i++){const [a,s]=PRESERVED[i];assert.equal(hash(l.flash.subarray(a,a+s)),before[i]);}
});
test('only a completely erased board is classified blank',async()=>{
 const l=new Loader(true);assert.equal((await installPreserving(l,files(),manifest)).mode,'blank');
 for(const a of [0,0x9000,0x20000,0x620000,0xffffff]){
  const dirty=new Loader(true);dirty.flash[a]=0;await assert.rejects(installPreserving(dirty,files(),manifest));assert.equal(dirty.writes,0);
 }
});
test('unknown layout, versions, image identity, targets and read failures never write',async()=>{
 for(const mutate of [l=>l.flash[0x8000]^=1,l=>l.flash[0x8000+3072]=0,l=>l.flash.set(app('0.3.9'),0x20000),
  l=>putText(l.flash,0x20000+80,'other-project'),l=>l.flash[0x20000+32]=0,l=>l.flash.set(app('0.3.9'),0x320000),
  l=>l.readFlash=async()=>new Uint8Array(3),l=>l.readFlash=async()=>{throw Error('Disconnected');}]){
  const l=new Loader();mutate(l);await assert.rejects(installPreserving(l,files(),manifest));assert.equal(l.writes,0);
 }
 const l=new Loader();await assert.rejects(installPreserving(l,files(),{version:'0.3.9'}));assert.equal(l.writes,0);
 await assert.rejects(installPreserving(l,files(),{version:'0.3.8-dev.2'}));assert.equal(l.writes,0);
});
test('readback mismatch blocks installation',async()=>{
 const l=new Loader();l.readFlash=async(a,s)=>new Uint8Array(s);await assert.rejects(installPreserving(l,files(),manifest));assert.equal(l.writes,0);
});
test('pending OTA verification, invalid sequences, and bad CRCs block installation',()=>{
 for(const state of [0,1,5]){const b=new Uint8Array(8192).fill(255);u32(b,0,1);u32(b,24,state);u32(b,28,crc32(b.subarray(0,4)));assert.throws(()=>checkOtaState(b));}
 const b=new Uint8Array(8192).fill(255);u32(b,0,1);u32(b,24,2);u32(b,28,crc32(b.subarray(0,4)));checkOtaState(b);
 b[28]^=1;assert.throws(()=>checkOtaState(b));
});
test('write bounds include sector rounding and reject unexpected destinations or extra parts',()=>{
 for(const edit of [f=>f[0].address=0x9000,f=>f[0].data=new Uint8Array(0x8004),f=>f[1].data=new Uint8Array(4100),f=>f[2].data=new Uint8Array(8196),f=>f[3].data=new Uint8Array(0x300004),f=>f.push({address:0x620000,data:new Uint8Array(4)})]){
  const f=files();edit(f);assert.throws(()=>checkWriteRanges(f));
 }
});
test('firmware and preservation verification must both pass before success',async()=>{
 for(const address of [0x20000,0x9000,0x620000]){
  const l=new Loader(),write=l.writeFlash.bind(l);l.writeFlash=async o=>{await write(o);l.flash[address]^=1;};
  await assert.rejects(installPreserving(l,files(),manifest));
 }
});
test('failed write leaves settings intact; recognized interrupted install can retry',async()=>{
 const l=new Loader(),before=hash(l.flash.subarray(0x9000,0xf000)),write=l.writeFlash.bind(l);
 l.writeFlash=async o=>{l.flash.set(o.fileArray[0].data,0);throw Error('Power cut');};
 await assert.rejects(installPreserving(l,files(),manifest));assert.equal(hash(l.flash.subarray(0x9000,0xf000)),before);
 l.writeFlash=write;await installPreserving(l,files(),manifest);
});
test('partially initialized blank install needs blank settings and application regions',async()=>{
 const l=new Loader(true);l.flash.set(files()[1].data,0x8000);assert.equal((await planInstallation(l,files(),manifest)).mode,'preserve');
 l.flash[0x25000]=0;await assert.rejects(planInstallation(l,files(),manifest));
});

test('partition layout validates offsets, flags, labels, table checksum and padding',()=>{
 checkPartitionTable(partitionTable());
 for(const offset of [0,4,12,28,192,208,224,3071]){const b=partitionTable();b[offset]^=1;assert.throws(()=>checkPartitionTable(b));}
});

test('flash reads consume the trailing stub digest before the next command',async()=>{
 const l=new Loader();await readFlashVerified(l,0x8000,4096);assert.equal(l.readDigest,null);
 await readFlashVerified(l,0x9000,0x6000);assert.equal(l.readDigest,null);
});
test('missing, malformed or mismatched read digests stop before writing',async()=>{
 for(const digest of [undefined,new Uint8Array(4),new Uint8Array(16)]){
  const l=new Loader();l.transport.read=async()=>digest;
  await assert.rejects(installPreserving(l,files(),manifest),/Could not read flash at 0x8000/);assert.equal(l.writes,0);
 }
});

// Synthetic representation of the inspected factory layout; no device dump.
function factoryLoader(){
 const l=new Loader(true),table=new Uint8Array(4096).fill(255);
 [[1,2,0x9000,0x5000,'nvs'],[1,0,0xe000,0x2000,'otadata'],
 [0,16,0x10000,0x640000,'app0'],[0,17,0x650000,0x640000,'app1'],
 [1,130,0xc90000,0x360000,'spiffs'],[1,3,0xff0000,0x10000,'coredump']].forEach(([type,sub,offset,size,label],i)=>{
  const n=i*32;table[n]=0xaa;table[n+1]=0x50;table[n+2]=type;table[n+3]=sub;u32(table,n+4,offset);u32(table,n+8,size);
  table.fill(0,n+12,n+28);table.set(new TextEncoder().encode(label),n+12);u32(table,n+28,0);
 });
 table[192]=0xeb;table[193]=0xeb;table.set(createHash('md5').update(table.subarray(0,192)).digest(),208);
 l.flash.set(table,0x8000);const image=app('45c1b25');putText(image,80,'arduino-lib-builder');l.flash.set(image,0x10000);
 l.erases=0;l.chip={readMac:async()=> '50:78:7d:2b:90:6c'};
 l.eraseFlash=async()=>{l.erases++;l.flash.fill(255);};return l;
}
test('recognized preloaded firmware requires explicit approval and identifies the connected board',async()=>{
 for(const approval of [undefined,async()=>false,async()=> 'true']){
  const l=factoryLoader();await assert.rejects(installPreserving(l,files(),manifest,{confirmFirstInstall:approval}),/cancelled/);
  assert.equal(l.erases,0);assert.equal(l.writes,0);
 }
 const l=factoryLoader();let shown;
 const plan=await installPreserving(l,files(),manifest,{confirmFirstInstall:async board=>{shown=board;assert.equal(l.erases,0);assert.equal(l.writes,0);return true;}});
 assert.deepEqual(shown,{serial:'50787D2B906C'});assert.equal(plan.mode,'first-install');assert.equal(l.erases,1);assert.equal(l.writes,1);
 for(const [a,size] of PRESERVED)assert.ok(l.flash.subarray(a,a+size).every(v=>v===255));
});
test('Link installs never offer first-install erasure, including invalid and pending states',async()=>{
 for(const mutate of [()=>{},l=>l.flash.set(app('0.3.9'),0x20000),l=>l.flash[0x9000+28]^=1,
 l=>{const b=fixture();changeBlob(b,'receipt',v=>v[4]=0);l.flash.set(b,0x9000);},l=>l.flash[0x8000]^=1]){
  const l=new Loader();let prompts=0;l.eraseFlash=async()=>assert.fail('Link must never be erased');
  try{await installPreserving(l,files(),manifest,{confirmFirstInstall:async()=>{prompts++;return true;}});}catch{}
  assert.equal(prompts,0);
 }
});
test('unknown factory versions, changed tables, Link markers and corrupt reads cannot request erase',async()=>{
 for(const mutate of [l=>putText(l.flash,0x10000+48,'unknown'),l=>putText(l.flash,0x10000+80,'ember-link'),
 l=>l.flash[0x8000+208]^=1,l=>l.flash[0x8000+224]=0,l=>l.flash.set(app(),0x650000),
 l=>l.readFlash=async()=>{throw Error('Read failed');},
 ...[0x9000,0xe800,0x620000,0x20000,0x320000].map(a=>l=>l.flash.set(new TextEncoder().encode('link_cloud'),a))]){
  const l=factoryLoader();mutate(l);let prompts=0;
  await assert.rejects(installPreserving(l,files(),manifest,{confirmFirstInstall:async()=>{prompts++;return true;}}));
  assert.equal(prompts,0);assert.equal(l.erases,0);assert.equal(l.writes,0);
 }
});
test('first-install approval is rechecked against board identity and installation state',async()=>{
 for(const change of [l=>l.chip.readMac=async()=> '00:00:00:00:00:00',l=>l.flash.set(files()[1].data,0x8000),
 l=>l.readFlash=async()=>{throw Error('Unplugged');}]){
  const l=factoryLoader();await assert.rejects(installPreserving(l,files(),manifest,{confirmFirstInstall:async()=>{change(l);return true;}}));
  assert.equal(l.erases,0);assert.equal(l.writes,0);
 }
});
test('failed erase verification stops before firmware writes and no automatic erase retry occurs',async()=>{
 const l=factoryLoader();l.eraseFlash=async()=>{l.erases++;l.flash.fill(255);l.flash[0xffffff]=0;};
 await assert.rejects(installPreserving(l,files(),manifest,{confirmFirstInstall:async()=>true}),/erase verification failed/);
 assert.equal(l.erases,1);assert.equal(l.writes,0);
 await assert.rejects(installPreserving(l,files(),manifest,{confirmFirstInstall:async()=>true}));assert.equal(l.erases,1);
});
test('power loss after verified first-install erase retries as a blank board without another erase',async()=>{
 const l=factoryLoader(),write=l.writeFlash.bind(l);l.writeFlash=async()=>{throw Error('Power cut');};
 await assert.rejects(installPreserving(l,files(),manifest,{confirmFirstInstall:async()=>true}),/Power cut/);
 assert.equal(l.erases,1);l.writeFlash=write;
 assert.equal((await installPreserving(l,files(),manifest)).mode,'blank');assert.equal(l.erases,1);
});
