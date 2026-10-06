import {md5} from '@noble/hashes/legacy';
import {bytesToHex} from '@noble/hashes/utils';
import {erased} from './nvs-inspection.mjs';
import {readFlashVerified} from './flash-read.mjs';
const u32=(b,n)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(n,true);
const text=b=>{const end=b.indexOf(0);return end<0?null:new TextDecoder().decode(b.subarray(0,end));};
// Narrow compatibility recognition of the preloaded Arduino layout inspected on
// a new T-Dongle-S3. This is not firmware authenticity or proof of board ownership.
export function matchesFactoryTable(table){
  if(table.length!==4096)return false;
  const entries=[[1,2,0x9000,0x5000,'nvs'],[1,0,0xe000,0x2000,'otadata'],
    [0,16,0x10000,0x640000,'app0'],[0,17,0x650000,0x640000,'app1'],
    [1,130,0xc90000,0x360000,'spiffs'],[1,3,0xff0000,0x10000,'coredump']];
  if(!entries.every(([type,sub,offset,size,label],i)=>{
    const e=table.subarray(i*32,i*32+32);
    return e[0]===0xaa&&e[1]===0x50&&e[2]===type&&e[3]===sub&&u32(e,4)===offset&&u32(e,8)===size&&text(e.subarray(12,28))===label&&u32(e,28)===0;
  }))return false;
  return table[192]===0xeb&&table[193]===0xeb&&erased(table.subarray(194,208))&&
    bytesToHex(table.subarray(208,224))===bytesToHex(md5(table.subarray(0,192)))&&erased(table.subarray(224));
}
function factoryApp(app){
  return app[0]===0xe9&&app[12]===9&&app[13]===0&&u32(app,32)===0xabcd5432&&
    text(app.subarray(80,112))==='arduino-lib-builder'&&text(app.subarray(48,80))==='45c1b25';
}
export async function recognizeFactory(loader,table){
  if(!matchesFactoryTable(table))return false;
  for(const address of [0x10000,0x650000]){
    const app=await readFlashVerified(loader,address,256);
    if(address===0x650000&&erased(app))continue;
    if(!factoryApp(app))return false;
  }
  // Refuse residual Link data even when another program replaced its table.
  // Search bytes conservatively, including deleted NVS entries; false positives
  // stop installation rather than risk removing a prior Link identity.
  const markers=['link_cloud','linkdisplay','emberconn','emberid','ember-link','auth\0'].map(s=>new TextEncoder().encode(s));
  for(const [address,size] of [[0x9000,0x7000],[0x620000,0x10000],[0x20000,256],[0x320000,256]]){
    const bytes=await readFlashVerified(loader,address,size);
    try{
      if(markers.some(marker=>bytes.some((_,i)=>i+marker.length<=bytes.length&&marker.every((v,j)=>bytes[i+j]===v))))return false;
    }finally{bytes.fill(0);}
  }
  return true;
}
