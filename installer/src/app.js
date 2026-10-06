import { ESPLoader, Transport } from 'esptool-js';
import { installPreserving } from './install-plan.mjs';
import { validateManifest,checkHardware,checkedDownload } from './policy.mjs';
import './button-help.js';
import { SetupSerial,validateCardStatus } from './setup-serial.mjs';
const $=id=>document.getElementById(id);
const supported=!!navigator.serial && window.isSecureContext;
let current=null,busy=false,generation=0;
let cardClient=null,cardState=null,cardBusy=false,cardLabels=false;
let firstInstallResolve=null;
function finishFirstInstall(approved){
  const resolve=firstInstallResolve;firstInstallResolve=null;
  $('first-install').hidden=true;
  $('first-install-new').checked=false;$('first-install-erase').checked=false;
  $('first-install-confirm').disabled=true;
  resolve?.(approved);
}
function confirmFirstInstall({serial}){
  $('first-install-device').textContent=`Connected board: ${serial}`;
  $('first-install-new').checked=false;$('first-install-erase').checked=false;
  $('first-install-confirm').disabled=true;$('first-install').hidden=false;
  $('first-install-new').focus();
  return new Promise(resolve=>{firstInstallResolve=resolve;});
}
for(const id of ['first-install-new','first-install-erase'])$(id).addEventListener('change',()=>{
  $('first-install-confirm').disabled=!firstInstallResolve||!$('first-install-new').checked||!$('first-install-erase').checked;
});
$('first-install-confirm').addEventListener('click',()=>{if(!$('first-install-confirm').disabled)finishFirstInstall(true);});
$('first-install-cancel').addEventListener('click',()=>finishFirstInstall(false));
navigator.serial?.addEventListener('disconnect',()=>finishFirstInstall(false));
function status(message,error=false){$('status').textContent=message;$('status').classList.toggle('error',error);}
function ready(){
  $('install').disabled=!supported||busy||cardBusy||!!cardClient||!current||($('channel').value==='dev'&&!$('dev-consent').checked);
  for(const id of ['channel','dev-consent'])$(id).disabled=busy||cardBusy;
  renderCardControls();
}
async function load(){
  const gen=++generation,channel=$('channel').value;current=null;$('dev-consent').checked=false;$('dev-label').hidden=channel!=='dev';ready();
  $('release').textContent='Checking the current release…';
  try{
    const res=await fetch('./catalog.json',{cache:'no-store'});if(!res.ok)throw Error('Release information is unavailable. Please try again later.');
    const catalog=await res.json();if(catalog.schema!==1)throw Error('Unsupported release catalog.');
    if(gen!==generation)return;
    const entry=catalog[channel];
    if(!entry){$('release').textContent='No qualified first-install package is available in this channel yet. See the detailed guide below.';return;}
    if(!/^firmware\/[a-zA-Z0-9.-]+\/factory\.json$/.test(entry.path)||!/^[a-f0-9]{64}$/.test(entry.sha256)||!Number.isInteger(entry.size)||entry.size>16384)throw Error('Invalid release reference.');
    const raw=await checkedDownload(await fetch('./'+entry.path),entry);
    const manifest=validateManifest(JSON.parse(new TextDecoder().decode(raw)),channel);
    if(gen!==generation)return;
    current={manifest,base:new URL('./'+entry.path,location.href)};
    $('release').textContent=`${catalog.preview?'Hardware test preview · ':''}Ember Link ${manifest.version}${channel==='dev'?' · Experimental build':''}`;
  }catch(e){if(gen===generation)$('release').textContent=e.message;}
  finally{if(gen===generation)ready();}
}
$('channel').addEventListener('change',load);
for(const id of ['dev-consent'])$(id).addEventListener('change',ready);
window.addEventListener('beforeunload',e=>{if(busy||cardBusy){e.preventDefault();e.returnValue='';}});
$('support').textContent=supported?'':'Use a desktop browser with Web Serial, such as Chrome or Edge. USB installation is not available in this browser.';
$('install').addEventListener('click',async()=>{
  if($('install').disabled)return;
  const selected=current;let transport=null,writing=false,stage='Choosing the USB device';
  busy=true;ready();$('progress').hidden=false;$('progress').value=0;status('Choose the dongle in the browser’s USB device picker.');
  try{
    const port=await navigator.serial.requestPort({filters:[{usbVendorId:0x303a}]});
    stage='Downloading firmware';
    status('Downloading and checking all installation files. Nothing has been erased.');
    const files=[];
    for(const part of selected.manifest.parts){
      files.push({address:part.offset,data:await checkedDownload(await fetch(new URL(part.path,selected.base)),part)});
    }
    transport=new Transport(port,false);
    const loader=new ESPLoader({transport,baudrate:115200,debugLogging:false,terminal:{clean(){},write(){},writeLine(){}}});
    status('Checking the chip and flash capacity. Keep Link connected.');
    stage='Connecting to the download-mode device';
    await loader.main('no_reset');
    stage='Checking hardware';
    checkHardware(loader.chip.CHIP_NAME,await loader.detectFlashSize());
    const security=await loader.getSecurityInfo();
    if(security.parsedFlags.SECURE_BOOT_EN||security.parsedFlags.SECURE_DOWNLOAD_ENABLE||security.flashCryptCnt!==0||!loader.IS_STUB)throw Error('This board has unsupported security settings or download mode. Nothing was erased.');
    stage='Inspecting the existing installation';
    status('Checking the existing installation and saved settings. Nothing has been written.');
    const total=files.reduce((sum,f)=>sum+f.data.length,0);
    const plan=await installPreserving(loader,files,selected.manifest,{
      confirmFirstInstall,
      onPlan:plan=>status(plan.mode==='first-install'?'Preloaded firmware detected. Review the first-install confirmation below. Nothing has been written.':plan.mode==='blank'?'Blank board detected. Installing Ember Link; you will configure it afterward.':'Compatible Ember Link detected. Reinstalling firmware while preserving saved settings and cloud identity.'),
      onWrite:()=>{writing=true;stage='Writing and verifying firmware';status('Installing and verifying firmware. Keep the board connected and do not close this page.');},
      reportProgress:(i,written)=>{$('progress').value=Math.min(99,100*(files.slice(0,i).reduce((s,f)=>s+f.data.length,0)+written)/total);},
    });
    $('progress').value=100;status(`Ember Link ${selected.manifest.version} was written and verified.${plan.mode==='preserve'?' Saved settings were preserved and verified.':''} Unplug, reconnect normally, then follow step 3 to check the card. A healthy boot still needs to be checked on the device.`);
  }catch(e){
    const cancelled=['NotFoundError','FirstInstallCancelled'].includes(e.name);
    status(e.name==='NotFoundError'?'Device selection cancelled. Nothing was erased.':e.name==='FirstInstallCancelled'?e.message:`${stage}: ${e.message}${writing?' Keep the board in download mode and retry. If its installation can no longer be recognized, contact support; do not erase settings. Do not use the device until installation succeeds.':/Nothing was (written|erased)/.test(e.message)?'':' Nothing was written.'}`,!cancelled);
  }finally{
    finishFirstInstall(false);
    if(transport){try{await transport.disconnect();}catch{status('The USB session could not close. Unplug Link, then reconnect normally if verification succeeded, or hold the small button to retry an incomplete installation.',true);}}
    busy=false;ready();
  }
});
// Card controls use a separate USB setup connection after the firmware boots.
function cardMessage(text,error=false){$('card-status').textContent=text;$('card-status').classList.toggle('error',error);}
function clearCardConsent(){cardState=null;$('card-erase').checked=false;$('card-ejected').checked=false;}
function renderCardControls(){
  const locked=busy||cardBusy;
  $('card-connect').disabled=!supported||locked||!!cardClient;
  $('card-disconnect').disabled=locked||!cardClient;
  $('setup-usb-session').hidden=!cardClient;
  $('setup-disconnect').disabled=locked||!cardClient;
  $('card-check').disabled=locked||!cardClient;
  $('card-maintenance').disabled=locked||!cardClient||!cardState||cardState.maintenance||!$('card-ejected').checked;
  $('card-format').disabled=locked||!cardClient||!cardState?.maintenance||!cardState?.canFormat||!cardState?.present||!cardState?.challenge||!$('card-erase').checked;
  $('card-rename').disabled=locked||!cardClient||!cardLabels||!cardState?.maintenance||!cardState?.fat32||!cardState?.canRename||!cardState?.challenge||cardState?.label==='EMBER LINK';
  $('card-erase').disabled=locked||!cardState?.maintenance||!cardState?.canFormat||!cardState?.present;
  $('card-ejected').disabled=locked||!cardState||cardState.maintenance;
}
async function checkCard(){
  clearCardConsent();
  cardState=validateCardStatus(await cardClient.request('card_status'));
  const capacity=cardState.capacityBytes<1073741824
    ? `${(cardState.capacityBytes/1048576).toFixed(1)} MiB`
    : `${(cardState.capacityBytes/1073741824).toFixed(1)} GiB`;
  $('card-device').textContent=`Link ${cardState.serial}${cardState.present?` · ${capacity} card`:''}`;
  $('card-label').textContent=cardLabels&&cardState.readable?`Card name: ${cardState.label||'No name'}`:'';
  if(!cardState.present)cardMessage('No readable card hardware was found. Unplug Link, insert a card, reconnect, and press the small button twice within one second.');
  else if(cardState.fat32)cardMessage(`Your card is already FAT32. You can keep it as it is.${cardState.maintenance?' Disconnect USB below, then unplug and reconnect normally to leave maintenance.':''}`);
  else cardMessage('This card needs preparation for the FAT32 setup. Existing files will be deleted only after your explicit confirmation.');
  if(cardState.present&&!cardState.canFormat&&!cardState.fat32)cardMessage('Browser formatting supports cards from 64 MiB to 32 GiB with 512-byte sectors. Prepare this card separately or use a supported card.');
  if(cardState.maintenance)$('card-mode').textContent='Card maintenance: wireless transfers and USB storage are unavailable until you unplug and reconnect normally.';
  else $('card-mode').textContent='To prepare or rename the card, first eject its drive in your operating system, then enter card maintenance. This restart does not erase anything.';
}
$('card-connect').addEventListener('click',async()=>{
  if($('card-connect').disabled)return;
  cardBusy=true;clearCardConsent();ready();
  let client;
  try{
    const port=await navigator.serial.requestPort({filters:[{usbVendorId:0x303a,usbProductId:0x4002}]});
    client=new SetupSerial(port,()=>{if(cardClient===client){cardClient=null;clearCardConsent();$('card-device').textContent='';ready();}});
    await client.open();cardClient=client;
    const info=await client.request('info');
    if(info.name!=='Ember Link'||info.usbMode!=='setup')throw Error('Select an Ember Link in USB setup mode.');
    if(info.cardPreparationProtocolVersion!==1)throw Error(`Firmware ${info.version||'on this Link'} does not support browser card preparation. Install a release with this feature, or format the card separately.`);
    cardLabels=info.cardLabelProtocolVersion===1;
    await checkCard();
  }catch(e){await client?.close();cardClient=null;clearCardConsent();cardMessage(e.name==='NotFoundError'?'Device selection cancelled.':e.message,e.name!=='NotFoundError');}
  finally{cardBusy=false;ready();}
});
async function disconnectCard(){
  if(busy||cardBusy||!cardClient)return;
  cardBusy=true;ready();
  try{
    await cardClient.close();cardClient=null;clearCardConsent();
    $('card-device').textContent='';$('card-label').textContent='';$('card-mode').textContent='';
    cardMessage('USB disconnected.');
  }finally{cardBusy=false;ready();}
}
for(const id of ['card-disconnect','setup-disconnect'])$(id).addEventListener('click',disconnectCard);
$('card-check').addEventListener('click',async()=>{
  if($('card-check').disabled)return;
  cardBusy=true;ready();
  try{await checkCard();}catch(e){cardMessage(e.message,true);}
  finally{cardBusy=false;ready();}
});
$('card-maintenance').addEventListener('click',async()=>{
  if($('card-maintenance').disabled)return;
  cardBusy=true;clearCardConsent();ready();
  try{
    await cardClient.request('card_maintenance',{driveEjected:true});
    await cardClient?.close();cardClient=null;
    cardMessage('Link is restarting into card maintenance. Wait a few seconds, then click Connect to check card again. Do not unplug during this restart. No files have been erased.');
  }catch(e){cardMessage(e.message,true);}
  finally{cardBusy=false;ready();}
});
$('card-format').addEventListener('click',async()=>{
  if($('card-format').disabled)return;
  const confirmed=cardState;
  cardBusy=true;clearCardConsent();ready();cardMessage('Preparing and verifying the card. Keep Link connected.');
  try{
    const result=await cardClient.request('card_format',{confirm:'ERASE_MICROSD',serial:confirmed.serial,challenge:confirmed.challenge},180000);
    if(result.verified!==true||result.filesystem!=='FAT32')throw Error('Card verification was not confirmed. Recheck before using it.');
    cardMessage(`FAT32 card prepared and verified${result.label==='EMBER LINK'?' as EMBER LINK':''}. Disconnect USB below, unplug and reconnect normally, then continue to setup.`);
  }catch(e){cardMessage(e.message+' Recheck the card before retrying; this page will not automatically repeat formatting.',true);}
  finally{cardBusy=false;ready();}
});
$('card-rename').addEventListener('click',async()=>{
  if($('card-rename').disabled)return;
  const confirmed=cardState;
  cardBusy=true;clearCardConsent();ready();cardMessage('Renaming the card. Keep Link connected.');
  try{
    const result=await cardClient.request('card_rename',{confirm:'RENAME_MICROSD',serial:confirmed.serial,challenge:confirmed.challenge},30000);
    if(result.verified!==true||result.label!=='EMBER LINK')throw Error('The new card name was not verified.');
    $('card-label').textContent='Card name: EMBER LINK';
    cardMessage('Card renamed to EMBER LINK. Existing files are preserved. Disconnect USB below, then unplug and reconnect normally to see the name on your computer.');
  }catch(e){cardMessage(e.message+' Recheck the card before retrying; this page will not automatically repeat renaming.',true);}
  finally{cardBusy=false;ready();}
});
for(const id of ['card-ejected','card-erase'])$(id).addEventListener('change',ready);
load();
