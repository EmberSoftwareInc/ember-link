"""Public first-install assets only. Never package a flash dump or NVS data."""
import hashlib
import json
from pathlib import Path
import struct

PARTS = ((0, 'bootloader.bin', 'bootloader/bootloader.bin', 0x8000),
         (0x8000, 'partition-table.bin', 'partition_table/partition-table.bin', 0x1000),
         (0xf000, 'ota_data_initial.bin', 'ota_data_initial.bin', 0x2000),
         (0x20000, 'ember-link.bin', 'ember-link.bin', 3*1024*1024))
EXTRA_ASSETS = ('bootloader.bin', 'partition-table.bin', 'ota_data_initial.bin', 'factory.json')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate(directory, provenance, application):
    m = json.loads((directory/'factory.json').read_text())
    require(m.get('schema') == 1 and m.get('boardId') == 'lilygo-t-dongle-s3'
            and m.get('chip') == 'ESP32-S3' and m.get('flashSize') == 16*1024*1024
            and m.get('layoutId') == 'link-v1', 'Unsupported factory hardware/layout')
    require(m.get('version') == provenance['version'] and m.get('channel') == provenance['channel']
            and m.get('sourceCommit') == provenance['sourceCommit']
            and m.get('applicationSha256') == application['sha256'], 'Factory provenance mismatch')
    require(len(m.get('parts', [])) == len(PARTS), 'Incomplete factory package')
    for part, (offset, name, _, maximum) in zip(m['parts'], PARTS):
        require(part.get('path') == name and part.get('offset') == offset, 'Unexpected flash part/offset')
        data = (directory/name).read_bytes()
        require(0 < len(data) <= maximum and len(data) % 4 == 0
                and part.get('size') == len(data) and part.get('sha256') == sha(data), 'Factory size/hash mismatch: '+name)
        if name in ('bootloader.bin', 'ember-link.bin'):
            require(len(data) >= 24 and data[0] == 0xe9 and struct.unpack_from('<H', data, 12)[0] == 9, 'Expected ESP32-S3 image')
        if name == 'bootloader.bin':
            require(data[2] == 2 and data[3] == 0x4f, 'Expected DIO, 80 MHz, 16 MB bootloader header')
        if name == 'ota_data_initial.bin':
            require(len(data) == 8192 and data == b'\xff'*8192, 'Initial OTA data must be blank, never device data')
        if name == 'partition-table.bin':
            entries=[]
            for i in range(0,len(data),32):
                if data[i:i+2] != b'\xaa\x50': break
                _, kind, sub, start, size, label, flags = struct.unpack('<HBBII16sI',data[i:i+32])
                entries.append((kind,sub,start,size,label.rstrip(b'\0').decode(),flags))
            require(entries == [(1,2,0x9000,0x6000,'nvs',0),(1,0,0xf000,0x2000,'otadata',0),
                (1,1,0x11000,0x1000,'phy_init',0),(0,16,0x20000,0x300000,'ota_0',0),
                (0,17,0x320000,0x300000,'ota_1',0),(1,2,0x620000,0x10000,'link_future',0)], 'Unexpected partition layout')
    require(m['parts'][-1]['sha256'] == application['sha256'], 'Wrong signed application')
    return m


def prepare(build, directory, provenance, application):
    args = json.loads((build/'flasher_args.json').read_text())
    require(args['extra_esptool_args']['chip'] == 'esp32s3', 'Wrong build chip')
    require({int(k,0):v for k,v in args['flash_files'].items()} == {p[0]:p[2] for p in PARTS}, 'Review changed flash offsets')
    require((build/'ember-link.bin').read_bytes() == (directory/'ember-link.bin').read_bytes(), 'Factory build and signed release differ')
    parts=[]
    for offset,name,source,_ in PARTS:
        data=(build/source).read_bytes()
        if name != 'ember-link.bin': (directory/name).write_bytes(data)
        parts.append(dict(path=name,offset=offset,size=len(data),sha256=sha(data)))
    m=dict(schema=1,boardId='lilygo-t-dongle-s3',layoutId='link-v1',chip='ESP32-S3',flashSize=16*1024*1024,
           version=provenance['version'],channel=provenance['channel'],sourceCommit=provenance['sourceCommit'],
           applicationSha256=application['sha256'],parts=parts)
    (directory/'factory.json').write_text(json.dumps(m,indent=2)+'\n')
    return validate(directory,provenance,application)
