import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import factory_package as f
import release as r
import build_installer as site
import test_release_lifecycle as lifecycle


class FactoryTests(unittest.TestCase):
    def package(self,root):
        p,m=lifecycle.LifecycleTests().package(root,'stable');p['schema']=2
        app=bytearray(4096);app[0]=0xe9;struct.pack_into('<H',app,12,9)
        (root/'ember-link.bin').write_bytes(app);m['sha256']=p['imageSha256']=f.sha(app);m['size']=len(app)
        (root/'manifest.json').write_text(json.dumps(m));(root/'provenance.json').write_text(json.dumps(p))
        (root/'link-releases.json').write_text(json.dumps(r.catalog([m],'stable')))
        boot=bytearray(app);boot[2:4]=bytes([2,0x4f]);(root/'bootloader.bin').write_bytes(boot)
        partitions=[(1,2,0x9000,0x6000,'nvs'),(1,0,0xf000,0x2000,'otadata'),(1,1,0x11000,0x1000,'phy_init'),(0,16,0x20000,0x300000,'ota_0'),(0,17,0x320000,0x300000,'ota_1'),(1,2,0x620000,0x10000,'link_future')]
        table=b''.join(struct.pack('<HBBII16sI',0x50aa,k,s,o,n,label.encode(),0) for k,s,o,n,label in partitions)
        (root/'partition-table.bin').write_bytes(table+b'\xff'*(4096-len(table)))
        (root/'ota_data_initial.bin').write_bytes(b'\xff'*8192)
        factory=dict(schema=1,boardId='lilygo-t-dongle-s3',layoutId='link-v1',chip='ESP32-S3',flashSize=16777216,version=p['version'],channel=p['channel'],sourceCommit=p['sourceCommit'],applicationSha256=m['sha256'],parts=[])
        for offset,name,_,_ in f.PARTS:
            data=(root/name).read_bytes();factory['parts'].append(dict(offset=offset,path=name,size=len(data),sha256=f.sha(data)))
        (root/'factory.json').write_text(json.dumps(factory));(root/'SHA256SUMS').write_text(r.checksums(root))
        return p,m,factory

    def test_new_package_includes_only_expected_public_parts(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);p,m,factory=self.package(root)
            self.assertEqual(site.verify_package(root,'stable',False),(p,m,factory))
            self.assertEqual(len(r.assets(root)),10)
            with self.assertRaises(ValueError):site.verify_package(root,'dev',False)

    def test_reject_modified_bytes_even_when_top_checksums_recomputed(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.package(root)
            (root/'bootloader.bin').write_bytes(b'wrong');(root/'SHA256SUMS').write_text(r.checksums(root))
            with self.assertRaises(ValueError):r.validate_package(root)

    def test_reject_partition_changes_or_used_device_ota_state(self):
        for name in ['partition-table.bin','ota_data_initial.bin']:
            with tempfile.TemporaryDirectory() as temp:
                root=Path(temp);p,m,factory=self.package(root)
                data=bytearray((root/name).read_bytes());data[4]=17;(root/name).write_bytes(data)
                next(x for x in factory['parts'] if x['path']==name)['sha256']=f.sha(data)
                (root/'factory.json').write_text(json.dumps(factory));(root/'SHA256SUMS').write_text(r.checksums(root))
                with self.assertRaises(ValueError):r.validate_package(root)

    def test_reject_paths_and_extra_flash_ranges(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);p,m,factory=self.package(root)
            factory['parts'][0]['path']='../device-dump.bin'
            (root/'factory.json').write_text(json.dumps(factory))
            with self.assertRaises(ValueError):f.validate(root,p,m)

    def test_first_install_needs_additional_hardware_qualification(self):
        with patch.object(r,'checked_package',return_value={'schema':2}),patch.object(r,'gh') as gh:
            with self.assertRaisesRegex(ValueError,'factory-qualified'):r.publish(SimpleNamespace(package=Path('.'),qualified=True,factory_qualified=False))
            gh.assert_not_called()

    def test_legacy_release_without_factory_remains_unavailable(self):
        with patch.object(site,'gh',return_value=json.dumps(dict(isDraft=False,isPrerelease=False,assets=[]))):
            with tempfile.TemporaryDirectory() as temp:
                self.assertIsNone(site.download_package('v9.9.9',Path(temp)/'package','stable'))
