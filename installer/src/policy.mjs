export const PARTS = [[0,'bootloader.bin',32768],[32768,'partition-table.bin',4096],[61440,'ota_data_initial.bin',8192],[131072,'ember-link.bin',3145728]];
export function validateManifest(m, channel) {
  const v = channel === 'stable' ? /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/ : /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)-dev\.[1-9]\d*$/;
  if (!['stable','dev'].includes(channel) || m?.schema !== 1 || m.channel !== channel || !v.test(m.version) || m.version.length > 31 || m.boardId !== 'lilygo-t-dongle-s3' || m.layoutId !== 'link-v1' || m.chip !== 'ESP32-S3' || m.flashSize !== 16777216 || !/^[a-f0-9]{40}$/.test(m.sourceCommit) || m.parts?.length !== 4) throw Error('Unsupported installation package.');
  for (let i=0;i<4;i++) {
    const p=m.parts[i], [offset,name,max]=PARTS[i];
    if (p.path !== name || p.offset !== offset || !Number.isInteger(p.size) || p.size<=0 || p.size>max || p.size%4 || !/^[a-f0-9]{64}$/.test(p.sha256)) throw Error('Invalid installation file or address.');
  }
  if (m.applicationSha256 !== m.parts[3].sha256) throw Error('Application identity mismatch.');
  return m;
}
export function checkHardware(chip, size) {
  if(chip !== 'ESP32-S3' || size !== '16MB') throw Error('This installer requires a LILYGO T-Dongle-S3 with an ESP32-S3 and 16 MB flash. Nothing was erased.');
}
export async function checkedDownload(response, part) {
  if(!response.ok) throw Error('Firmware download failed. Nothing was erased.');
  const reader=response.body.getReader();let length=0;const chunks=[];
  try { for(;;){const {done,value}=await reader.read();if(done)break;length+=value.length;if(length>part.size)throw Error('Firmware size mismatch.');chunks.push(value);} }
  finally {await reader.cancel();reader.releaseLock();}
  if(length!==part.size)throw Error('Firmware download is incomplete.');
  const data=new Uint8Array(length);let at=0;for(const c of chunks){data.set(c,at);at+=c.length;}
  const digest=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',data)),b=>b.toString(16).padStart(2,'0')).join('');
  if(digest!==part.sha256)throw Error('Firmware checksum mismatch. Nothing was erased.');
  return data;
}
