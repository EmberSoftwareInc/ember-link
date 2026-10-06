// Read-only, conservative ESP-IDF NVS v2 inspection. Never log or persist flash bytes.
// Format reference: ESP-IDF components/nvs_flash/{src,private_include}.
const fail = () => { throw Error('Saved settings need review. Reconnect normally, let Link finish pending work, or contact support. Nothing was written.'); };
const u32 = (b, n=0) => new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(n,true);
export function crc32(bytes, seed=0xffffffff) {
  let crc=(seed ^ 0xffffffff)>>>0;
  for(const b of bytes){crc^=b;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}
  return (crc^0xffffffff)>>>0;
}
export const erased = bytes => bytes.every(b=>b===255);
function key(bytes){const end=bytes.indexOf(0);if(end<1)fail();return new TextDecoder('utf-8',{fatal:true}).decode(bytes.subarray(0,end));}
export function inspectNvs(bytes) {
  const copies=[];
  try {
  if(bytes.length!==0x6000)fail();
  const entries=[], sequences=new Set();let pages=0;
  for(let base=0;base<bytes.length;base+=4096){
    const page=bytes.subarray(base,base+4096);if(erased(page))continue;
    // Reject incomplete initialization/garbage collection instead of guessing which value won.
    if(![0xfffffffe,0xfffffffc].includes(u32(page)) || page[8]!==0xfe || crc32(page.subarray(4,28))!==u32(page,28))fail();
    const seq=u32(page,4);if(sequences.has(seq))fail();sequences.add(seq);pages++;
    const state=i=>(page[32+(i>>2)]>>((i%4)*2))&3;
    for(let i=0;i<126;){
      const s=state(i),off=64+i*32;
      if(s===0){i++;continue;}
      if(s===3){if(!erased(page.subarray(off,off+32)))fail();i++;continue;}
      if(s!==2)fail();
      const h=page.subarray(off,off+32),span=h[2],type=h[1];
      if(span<1 || i+span>126 || crc32(h.subarray(8),crc32(h.subarray(0,4)))!==u32(h,4))fail();
      for(let n=i;n<i+span;n++)if(state(n)!==2)fail();
      const e={ns:h[0],type,chunk:h[3],key:key(h.subarray(8,24)),value:h.subarray(24)};
      if([0x21,0x41,0x42].includes(type)){
        const size=h[24]|h[25]<<8;
        if(span!==1+Math.ceil(size/32))fail();
        e.value=page.subarray(off+32,off+32+size);
        if(crc32(e.value)!==u32(h,28))fail();
      }else if(![1,2,4,8,0x11,0x12,0x14,0x18,0x48].includes(type)||span!==1)fail();
      entries.push(e);i+=span;
    }
  }
  if(pages>5)fail(); // NVS needs an empty page for continued writes.
  const namespaces=new Map(), names=new Set();
  for(const e of entries.filter(e=>e.ns===0)){
    if(e.type!==1||!e.value[0]||e.value[0]===255||namespaces.has(e.value[0])||names.has(e.key))fail();
    namespaces.set(e.value[0],e.key);names.add(e.key);
  }
  const records=new Map(),used=new Set();
  for(const e of entries.filter(e=>e.ns!==0&&e.type!==0x42)){
    if(!namespaces.has(e.ns))fail();
    const id=namespaces.get(e.ns)+'/'+e.key;if(records.has(id))fail();
    let value=e.value;
    if(e.type===0x48){
      const size=u32(value),count=value[4],start=value[5];
      if(e.chunk!==255 || ![0,128].includes(start) || count>127 || size>0x6000)fail();
      const chunks=[];let total=0;
      for(let n=0;n<count;n++){
        const matches=entries.filter(c=>c.ns===e.ns&&c.key===e.key&&c.type===0x42&&c.chunk===start+n);
        if(matches.length!==1)fail();const c=matches[0];used.add(c);chunks.push(c.value);total+=c.value.length;
      }
      if(total!==size)fail();value=new Uint8Array(size);copies.push(value);let at=0;for(const c of chunks){value.set(c,at);at+=c.length;}
    }
    records.set(id,{type:e.type,value});
  }
  if(entries.some(e=>e.type===0x42&&!used.has(e)))fail();
  // Receipt records start with schema:u32, acknowledged:bool, ID:string.
  // Firmware treats an unacknowledged record as unresolved, even after reboot.
  for(const id of ['link_cloud/update_v1','link_cloud/receipt']){
    const r=records.get(id);if(!r)continue;const b=r.value;
    if(![0x41,0x48].includes(r.type)||b.length<69||u32(b)!==1||b[4]!==1||!b.subarray(5,69).includes(0))fail();
  }
  const display=records.get('linkdisplay/state_v1');
  if(display){const b=display.value;
    if(![0x41,0x48].includes(display.type)||b.length!==112||new TextDecoder().decode(b.subarray(0,4))!=='LDS1'||b[16]>1||(b[24]!==0&&b[16]!==1))fail();
  }
  const settings=records.get('link_cloud/settings_v2');
  if(settings){try{
    if(![0x41,0x48].includes(settings.type)||settings.value.at(-1)!==0)fail();
    const s=JSON.parse(new TextDecoder().decode(settings.value.subarray(0,-1)));if(s.schema!==2||s.transport!=='poll')fail();
  }catch{fail();}}
  // Return no credentials, preferences, or raw flash bytes to the caller.
  } finally { for(const value of copies)value.fill(0); }
}
