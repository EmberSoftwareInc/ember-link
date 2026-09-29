import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0, str(TOOLS))
import release as r
from release_catalog import catalog


def manifest(version='0.3.7-dev.1'):
    return dict(schema=1, releaseId='link-'+version.replace('.', '-'), targetVersion=version,
                boardId='lilygo-t-dongle-s3', layoutId='link-v1', signingKeyId='a'*64,
                sha256='b'*64, size=4096, notes='Release notes',
                url=f'https://github.com/{r.REPO}/releases/download/v{version}/ember-link.bin')


class LifecycleTests(unittest.TestCase):
    def test_repository_version_declarations_match(self):
        version = r.source_version()
        self.assertTrue(any(r.version_valid(version, c) for c in r.BRANCHES))

    def test_version_channel_rules(self):
        for version, channel in [('0.3.7','stable'),('0.3.7-dev.1','dev'),('0.3.7-dev.12','dev')]:
            self.assertTrue(r.version_valid(version,channel))
        for version, channel in [('0.3.7-dev.1','stable'),('0.3.7','dev'),('0.3.7-dev','dev'),
                                 ('0.3.7-dev.0','dev'),('0.3.7-dev.01','dev'),('01.3.7','stable'),
                                 ('0.3.7-rc1','dev'),('0.3.7','other')]:
            self.assertFalse(r.version_valid(version,channel))

    def test_channels_cannot_mix_and_tag_must_match(self):
        self.assertEqual(catalog([manifest()], 'dev')['channel'],'dev')
        self.assertEqual(catalog([manifest('0.3.7')], 'stable')['channel'],'stable')
        self.assertEqual(catalog([], 'dev'),dict(schema=1,channel='dev',releases=[]))
        for values, channel in [([manifest()],'stable'),([manifest('0.3.7')],'dev'),
                                ([manifest(),manifest('0.3.7')],'dev'),([],'invalid'),
                                ([{**manifest(),'url':manifest('0.3.8-dev.1')['url']}],'dev')]:
            with self.assertRaises(ValueError):catalog(values,channel)

    def test_source_requires_correct_branch_clean_tree_and_matching_versions(self):
        def git(*args):return {'branch':'dev','status':'','rev-parse':'a'*40}[args[0]]
        with patch.object(r,'git',side_effect=git),patch.object(r,'source_version',return_value='0.3.7-dev.1'):
            self.assertEqual(r.source('dev'),('0.3.7-dev.1','a'*40))
            with self.assertRaises(ValueError):r.source('stable')
        with patch.object(r,'git',side_effect=['dev',' M file']):
            with self.assertRaises(ValueError):r.source('dev')
        with patch.object(r,'git',side_effect=git),patch.object(r,'source_version',return_value='0.3.7'):
            with self.assertRaises(ValueError):r.source('dev')

    def package(self, root, channel='dev'):
        version='0.3.7-dev.1' if channel=='dev' else '0.3.7'
        data=b'test-image'*500; (root/'ember-link.bin').write_bytes(data)
        m=manifest(version);m.update(size=len(data),sha256=hashlib.sha256(data).hexdigest())
        p=dict(schema=1,repository=r.REPO,channel=channel,sourceBranch=r.BRANCHES[channel],
               sourceCommit='a'*40,tag='v'+version,version=version,imageSha256=m['sha256'])
        for name,value in [('manifest.json',m),('provenance.json',p),('link-releases.json',catalog([m],channel))]:
            (root/name).write_text(json.dumps(value))
        (root/'release-notes.md').write_text('Notes')
        (root/'SHA256SUMS').write_text(r.checksums(root))
        return p,m

    def test_package_rejects_tampering_even_with_new_checksums(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);p,m=self.package(root)
            self.assertEqual(r.validate_package(root),(p,m))
            (root/'ember-link.bin').write_bytes(b'corrupt')
            with self.assertRaises(ValueError):r.validate_package(root)
            (root/'SHA256SUMS').write_text(r.checksums(root))
            with self.assertRaises(ValueError):r.validate_package(root)

    def test_package_rejects_wrong_channel_catalog_or_source(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);p,m=self.package(root)
            for changed in [dict(p,channel='stable'),dict(p,sourceBranch='main'),dict(p,tag='v0.3.8-dev.1')]:
                (root/'provenance.json').write_text(json.dumps(changed))
                (root/'SHA256SUMS').write_text(r.checksums(root))
                with self.assertRaises(ValueError):r.validate_package(root)
            (root/'provenance.json').write_text(json.dumps(p))
            (root/'link-releases.json').write_text(json.dumps(dict(schema=1,channel='stable',releases=[m])))
            (root/'SHA256SUMS').write_text(r.checksums(root))
            with self.assertRaises(ValueError):r.validate_package(root)

    def test_draft_routes_to_prerelease_without_touching_feed(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);p,_=self.package(root)
            with patch.object(r,'checked_package',return_value=p),patch.object(r,'git',return_value=''),patch.object(r,'gh') as gh,patch.object(r,'update_dev') as feed:
                r.draft(SimpleNamespace(package=root))
                args=gh.call_args.args
                self.assertIn('--draft',args);self.assertIn('--prerelease',args);self.assertIn('--latest=false',args)
                feed.assert_not_called()
            with patch.object(r,'checked_package',return_value=p),patch.object(r,'git',return_value='existing'),patch.object(r,'gh') as gh:
                with self.assertRaises(ValueError):r.draft(SimpleNamespace(package=root))
                gh.assert_not_called()

    def test_publication_requires_qualification_and_matching_draft(self):
        with patch.object(r,'gh') as gh:
            with self.assertRaises(ValueError):r.publish(SimpleNamespace(qualified=False))
            gh.assert_not_called()
        p=dict(tag='v0.3.7-dev.1',channel='dev',sourceCommit='a'*40)
        for info in [dict(tagName=p['tag'],isPrerelease=False,targetCommitish=p['sourceCommit']),
                     dict(tagName=p['tag'],isPrerelease=True,targetCommitish='main')]:
            with self.assertRaises(ValueError):r.validate_release(info,p)

    def test_publish_flags_keep_dev_out_of_latest(self):
        for channel in ('dev','stable'):
            with tempfile.TemporaryDirectory() as temp:
                root=Path(temp);p,_=self.package(root,channel)
                info=dict(tagName=p['tag'],isDraft=True,isPrerelease=channel=='dev',targetCommitish=p['sourceCommit'])
                with patch.object(r,'checked_package',return_value=p),patch.object(r,'release_info',return_value=info),patch.object(r,'check_assets'),patch.object(r,'git',side_effect=['',p['sourceCommit']]),patch.object(r,'run'),patch.object(r,'gh') as gh:
                    r.publish(SimpleNamespace(package=root,qualified=True))
                    args=gh.call_args.args
                    self.assertIn('--latest='+str(channel=='stable').lower(),args)
                    self.assertIn('--prerelease='+str(channel=='dev').lower(),args)

    def test_feed_write_uses_expected_sha_and_only_dev_branch(self):
        current=dict(schema=1,channel='dev',releases=[])
        import base64
        response=json.dumps(dict(sha='original-sha',content=base64.b64encode(json.dumps(current).encode()).decode()))
        with patch.object(r,'gh',side_effect=[response,'{}']) as gh:
            r.update_dev(catalog([manifest()],'dev'),'Test recommendation')
            payload=gh.call_args.kwargs['payload']
            self.assertEqual(payload['branch'],'release-channels');self.assertEqual(payload['sha'],'original-sha')
            self.assertEqual(json.loads(base64.b64decode(payload['content']))['channel'],'dev')
        with patch.object(r,'gh',return_value=response) as gh:
            r.update_dev(current,'No change');self.assertEqual(gh.call_count,1)

    def test_recommend_refuses_stable_or_unpublished_packages(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);p,_=self.package(root,'stable')
            with patch.object(r,'checked_package',return_value=p),patch.object(r,'update_dev') as feed:
                with self.assertRaises(ValueError):r.recommend(SimpleNamespace(package=root))
                feed.assert_not_called()
            p,_=self.package(root,'dev')
            info=dict(tagName=p['tag'],isDraft=True,isPrerelease=True,targetCommitish=p['sourceCommit'])
            with patch.object(r,'checked_package',return_value=p),patch.object(r,'release_info',return_value=info),patch.object(r,'update_dev') as feed:
                with self.assertRaises(ValueError):r.recommend(SimpleNamespace(package=root))
                feed.assert_not_called()


if __name__ == '__main__':unittest.main()
