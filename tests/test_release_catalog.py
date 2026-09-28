import importlib.util
import unittest
from pathlib import Path
spec = importlib.util.spec_from_file_location('release_catalog', Path(__file__).resolve().parents[1] / 'tools/release_catalog.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.release = dict(schema=1, releaseId='r035', targetVersion='0.3.5', boardId='lilygo-t-dongle-s3', layoutId='link-v1', signingKeyId='a'*64, sha256='b'*64, size=4096, notes='Changes', url='https://github.com/EmberSoftwareInc/ember-link/releases/download/v0.3.5/ember-link.bin')
    def test_shared_metadata_is_preserved(self):
        self.assertEqual(module.catalog([self.release]),dict(schema=1,releases=[self.release]))
        self.assertEqual(module.catalog([]),dict(schema=1,releases=[]))
    def test_bad_channels_artifacts_and_urls_fail(self):
        with self.assertRaises(ValueError): module.catalog([self.release,self.release])
        for patch in [dict(size=4),dict(sha256='bad'),dict(boardId='other'),dict(url='https://example.com/image.bin'),dict(url='https://github.com/EmberSoftwareInc/ember-link/releases/latest/download/image.bin')]:
            with self.assertRaises(ValueError): module.catalog([{**self.release,**patch}])
if __name__ == '__main__': unittest.main()
