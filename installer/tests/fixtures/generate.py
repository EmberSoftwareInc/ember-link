from pathlib import Path
import csv,subprocess,sys
out=Path(__file__).parent;out.mkdir(exist_ok=True)
rows=[['key','type','encoding','value'],['link_cloud','namespace','','']]
for key in ['update_v1','receipt']:
 data=bytearray(400);data[0]=1;data[4]=1;data[5:10]=b'test1';rows.append([key,'data','hex2bin',data.hex()])
rows.append(['settings_v2','data','hex2bin',(b'{"schema":2,"transport":"poll"}\0').hex()])
# Force a blob to span pages. Entirely synthetic bytes, not a real device dump.
rows.append(['synthetic','data','hex2bin',('42'*5000)])
rows.append(['linkdisplay','namespace','',''])
display=bytearray(112);display[:4]=b'LDS1';display[4]=1;display[6]=1;display[16]=1;display[24:29]=b'test2';rows.append(['state_v1','data','hex2bin',display.hex()])
with (out/'synthetic-settings.csv').open('w',newline='') as f:csv.writer(f).writerows(rows)
subprocess.run([sys.executable,'-m','esp_idf_nvs_partition_gen','generate',str(out/'synthetic-settings.csv'),str(out/'synthetic-settings.bin'),'0x6000'],check=True)
# Keep generator input small: fixture regeneration script documents full data.
(out/'synthetic-settings.csv').unlink()
