import {md5} from '@noble/hashes/legacy';
import {bytesToHex} from '@noble/hashes/utils';
import {crc32,erased,inspectNvs} from './nvs-inspection.mjs';
import {readFlashVerified} from './flash-read.mjs';
import {recognizeFactory} from './factory-install.mjs';
const SECTOR=4096,FLASH=0x1000000;
export const PRESERVED=[[0x9000,0x6000],[0x11000,0xf000],[0x320000,0xce0000]];
// Reviewed configuration/journal compatibility, NOT a semver guess. Extend only after review.
const COMPATIBLE={
  '0.3.8':['0.3.6','0.3.7','0.3.8-dev.1','0.3.8-dev.2','0.3.8'],
  '0.3.8-dev.2':['0.3.6','0.3.7','0.3.8-dev.1','0.3.8-dev.2'],
};
const unsupported=()=>{throw Error('This board has an unrecognized or incompatible installation. Nothing was written. Use the normal Link updater or contact support; do not erase its settings.');};
const u32=(b,n=0)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(n,true);
function text(b){const end=b.indexOf(0);if(end<0)unsupported();return new TextDecoder().decode(b.subarray(0,end));}
export function checkPartitionTable(bytes){
  if(bytes.length!==3072)unsupported();
  const expected=[[1,2,0x9000,0x6000,'nvs'],[1,0,0xf000,0x2000,'otadata'],
    [1,1,0x11000,0x1000,'phy_init'],[0,16,0x20000,0x300000,'ota_0'],
    [0,17,0x320000,0x300000,'ota_1'],[1,2,0x620000,0x10000,'link_future']];
  expected.forEach(([type,sub,offset,size,label],i)=>{const e=bytes.subarray(i*32,(i+1)*32);
    if(e[0]!==0xaa||e[1]!==0x50||e[2]!==type||e[3]!==sub||u32(e,4)!==offset||u32(e,8)!==size||text(e.subarray(12,28))!==label||u32(e,28)!==0)unsupported();
  });
  const checksum=bytes.subarray(192,224);
  if(checksum[0]!==0xeb||checksum[1]!==0xeb||!erased(checksum.subarray(2,16))||
    bytesToHex(checksum.subarray(16))!==bytesToHex(md5(bytes.subarray(0,192)))||!erased(bytes.subarray(224)))unsupported();
}
export function checkWriteRanges(files){
  const addresses=[0,0x8000,0xf000,0x20000],maxima=[0x8000,0x1000,0x2000,0x300000];
  if(files.length!==4)unsupported();
  files.forEach((f,i)=>{
    if(!(f.data instanceof Uint8Array)||f.address!==addresses[i]||!f.data.length||f.data.length>maxima[i]||f.data.length%4)unsupported();
    const start=Math.floor(f.address/SECTOR)*SECTOR,end=Math.ceil((f.address+f.data.length)/SECTOR)*SECTOR;
    if(PRESERVED.some(([p,size])=>start<p+size&&end>p))unsupported();
  });
}
export function checkOtaState(bytes){
  if(bytes.length!==8192)unsupported();
  for(const offset of [0,4096]){
    const e=bytes.subarray(offset,offset+32);if(erased(e))continue;
    if(u32(e)===0xffffffff||u32(e)===0||crc32(e.subarray(0,4))!==u32(e,28)||![2,3,4].includes(u32(e,24)))
      throw Error('Link has an unfinished or unreadable boot/update state. Reconnect normally and finish its update before reinstalling. Nothing was written.');
  }
}
export async function planInstallation(loader,files,manifest){
  checkWriteRanges(files);
  checkPartitionTable(files[1].data);
  const table=await readFlashVerified(loader,0x8000,4096);
  if(erased(table)){
    // A missing partition table is not proof of an empty device: check the entire flash.
    const empty=md5.create();const block=new Uint8Array(65536).fill(255);
    for(let n=0;n<FLASH;n+=block.length)empty.update(block);
    if((await loader.flashMd5sum(0,FLASH)).toLowerCase()!==bytesToHex(empty.digest()))unsupported();
    return {mode:'blank'};
  }
  const expected=new Uint8Array(4096).fill(255);expected.set(files[1].data);
  if(!table.every((b,i)=>b===expected[i])){
    if(await recognizeFactory(loader,table))return {mode:'first-install'};
    unsupported();
  }
  if(!COMPATIBLE[manifest.version])unsupported();
  const settings=await readFlashVerified(loader,0x9000,0x6000);let settingsBlank;
  try{settingsBlank=erased(settings);inspectNvs(settings);}finally{settings.fill(0);}
  checkOtaState(await readFlashVerified(loader,0xf000,8192));
  const versions=[];
  for(const address of [0x20000,0x320000]){
    const app=await readFlashVerified(loader,address,256);if(erased(app))continue;
    if(app[0]!==0xe9||app[12]!==9||app[13]!==0||u32(app,32)!==0xabcd5432||text(app.subarray(80,112))!=='ember-link')unsupported();
    const version=text(app.subarray(48,80));if(!COMPATIBLE[manifest.version].includes(version))unsupported();versions.push(version);
  }
  // A partially initialized first install may have its layout but no app/settings yet.
  if(!versions.length&&!settingsBlank)unsupported();
  if(!versions.length){
    for(const [address,size] of [[0x20000,0x600000],[0x620000,0x10000]]){
      const empty=md5.create(),block=new Uint8Array(4096).fill(255);for(let n=0;n<size;n+=4096)empty.update(block);
      if((await loader.flashMd5sum(address,size)).toLowerCase()!==bytesToHex(empty.digest()))unsupported();
    }
  }
  return {mode:'preserve'};
}
export async function preservedDigests(loader){
  const results=[];for(const [address,size] of PRESERVED){
    const digest=(await loader.flashMd5sum(address,size)).toLowerCase();if(!/^[a-f0-9]{32}$/.test(digest))unsupported();results.push(digest);
  }return results;
}
export async function installPreserving(loader,files,manifest,{onPlan=()=>{},onWrite=()=>{},reportProgress=()=>{},confirmFirstInstall=async()=>false}={}){
  const plan=await planInstallation(loader,files,manifest);
  onPlan(plan);
  if(plan.mode==='first-install'){
    const mac=await loader.chip.readMac(loader);
    if(!/^(?:[a-f0-9]{2}:){5}[a-f0-9]{2}$/i.test(mac))throw Error('Could not identify this board. Nothing was written.');
    const serial=mac.replaceAll(':','').toUpperCase();
    if(await confirmFirstInstall({serial})!==true){const error=Error('First installation cancelled. Nothing was written.');error.name='FirstInstallCancelled';throw error;}
    // The approval belongs to this connected board and this attempt only.
    // Reinspect after approval: never turn a read error or Link-policy failure
    // into permission to erase, even if a different board was reconnected.
    if((await loader.chip.readMac(loader)).replaceAll(':','').toUpperCase()!==serial||
       (await planInstallation(loader,files,manifest)).mode!=='first-install')throw Error('Board changed. Nothing was written. Reconnect and inspect it again.');
    onWrite();
    await loader.eraseFlash();
    const empty=md5.create(),block=new Uint8Array(65536).fill(255);
    for(let n=0;n<FLASH;n+=block.length)empty.update(block);
    if((await loader.flashMd5sum(0,FLASH)).toLowerCase()!==bytesToHex(empty.digest()))throw Error('Internal flash erase verification failed. Stop and contact support.');
  }
  const before=await preservedDigests(loader);if(plan.mode!=='first-install')onWrite();
  await loader.writeFlash({fileArray:files,flashMode:'keep',flashFreq:'keep',flashSize:'keep',eraseAll:false,compress:true,
    calculateMD5Hash:data=>bytesToHex(md5(data)),reportProgress});
  for(const f of files)if((await loader.flashMd5sum(f.address,f.data.length)).toLowerCase()!==bytesToHex(md5(f.data)))throw Error('Written firmware verification failed.');
  const after=await preservedDigests(loader);
  if(before.some((hash,i)=>hash!==after[i]))throw Error('Settings preservation verification failed. Stop and contact support before further installation.');
  return plan;
}
