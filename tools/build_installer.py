#!/usr/bin/env python3
"""Assemble a Pages site from qualified release bytes; never compile firmware here."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import factory_package

ROOT=Path(__file__).resolve().parents[1]
REPO='EmberSoftwareInc/ember-link'


def gh(*args):
    return subprocess.check_output(['gh',*args],text=True)


def require(ok,message):
    if not ok: raise ValueError(message)


def verify_package(directory, expected_channel, verify_signature=True):
    p=json.loads((directory/'provenance.json').read_text())
    m=json.loads((directory/'manifest.json').read_text())
    from release import validate_package, version_valid
    validate_package(directory)
    require(p['channel']==expected_channel and version_valid(p['version'],expected_channel),'Wrong install channel')
    factory=factory_package.validate(directory,p,m)
    if verify_signature:
        with tempfile.TemporaryDirectory() as temp:
            output=Path(temp)/'verified.json'
            subprocess.run([sys.executable,str(ROOT/'tools/firmware_manifest.py'),str(directory/'ember-link.bin'),
                '--public-key',str(ROOT/'docs/signing/ember-link-production.pub'),'--release-id',m['releaseId'],
                '--notes',m['notes'],'--download-url',m['url'],'--output',str(output)],check=True)
            require(json.loads(output.read_text())==m,'Verified image metadata differs')
    return p,m,factory


def copy_package(package, site, channel):
    p,m,factory=verify_package(package,channel)
    destination=site/'firmware'/p['version'];destination.mkdir(parents=True,exist_ok=False)
    for _,name,_,_ in factory_package.PARTS: shutil.copyfile(package/name,destination/name)
    shutil.copyfile(package/'factory.json',destination/'factory.json')
    raw=(destination/'factory.json').read_bytes()
    return dict(path=f'firmware/{p["version"]}/factory.json',sha256=hashlib.sha256(raw).hexdigest(),size=len(raw))


def download_package(tag, directory, channel, expected_image=None):
    require(re.fullmatch(r'v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-dev\.[1-9]\d*)?',tag),'Invalid firmware tag')
    info=json.loads(gh('release','view',tag,'--repo',REPO,'--json','isDraft,isPrerelease,assets'))
    require(not info['isDraft'] and info['isPrerelease']==(channel=='dev'),'Unpublished or wrong-channel release')
    names={a['name'] for a in info['assets']}
    source_tag=tag
    if 'factory.json' not in names:
        # One-time, explicitly pinned factory companion for a legacy app-only release.
        bootstrap=json.loads((ROOT/'installer/bootstrap.json').read_text())
        entry=bootstrap.get(tag)
        if not entry: return None
        source_tag=entry['packageTag']
        require(source_tag=='installer-'+tag, 'Invalid bootstrap tag')
        companion=json.loads(gh('release','view',source_tag,'--repo',REPO,'--json','isDraft,isPrerelease'))
        require(not companion['isDraft'] and companion['isPrerelease'],'Installer companion must not replace stable firmware')
    directory.mkdir()
    from release import ASSETS
    for name in ASSETS+factory_package.EXTRA_ASSETS:
        gh('release','download',source_tag,'--repo',REPO,'--pattern',name,'--dir',str(directory))
    p,m,_=verify_package(directory,channel)
    require(p['tag']==tag and p['version']==tag[1:],'Package version/tag mismatch')
    commit=gh('api',f'repos/{REPO}/commits/{tag}','--jq','.sha').strip()
    require(commit==p['sourceCommit'],'Firmware source/tag mismatch')
    if source_tag!=tag:
        require(hashlib.sha256((directory/'factory.json').read_bytes()).hexdigest()==entry['factorySha256'],'Bootstrap factory changed')
        with tempfile.TemporaryDirectory() as temp:
            gh('release','download',tag,'--repo',REPO,'--pattern','manifest.json','--dir',temp)
            require(json.loads((Path(temp)/'manifest.json').read_text())==m,'Companion changed the published firmware')
    if expected_image: require(m['sha256']==expected_image,'Development recommendation changed')
    return directory


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'installer/dist')
    parser.add_argument('--preview-package',type=Path,help='Local hardware testing only; never used by Pages workflow')
    args=parser.parse_args();site=args.output.resolve();require((site/'index.html').is_file(),'Build the website first')
    shutil.rmtree(site/'firmware',ignore_errors=True)
    result=dict(schema=1,stable=None,dev=None,preview=bool(args.preview_package))
    if args.preview_package:
        p=json.loads((args.preview_package/'provenance.json').read_text())
        result[p['channel']]=copy_package(args.preview_package,site,p['channel'])
    else:
        with tempfile.TemporaryDirectory() as temp:
            stable=json.loads(gh('api',f'repos/{REPO}/releases/latest'))['tag_name']
            package=download_package(stable,Path(temp)/'stable','stable')
            if package:result['stable']=copy_package(package,site,'stable')
            # Follow the operator's recommendation, never simply the newest prerelease.
            import base64
            data=json.loads(gh('api',f'repos/{REPO}/contents/dev.json?ref=release-channels'))
            feed=json.loads(base64.b64decode(data['content']))
            require(feed.get('schema')==1 and feed.get('channel')=='dev','Invalid dev feed')
            releases=feed.get('releases',[]);require(len(releases)<=1,'Review multi-board dev catalog')
            if releases:
                r=releases[0];tag='v'+r['targetVersion']
                package=download_package(tag,Path(temp)/'dev','dev',r['sha256'])
                if package:result['dev']=copy_package(package,site,'dev')
    (site/'catalog.json').write_text(json.dumps(result,indent=2)+'\n')
    # Publish only the deliberately assembled website, never the repo/build tree.
    shutil.copyfile(ROOT/'LICENSE',site/'LICENSE.txt')
    print('Installer catalog:',json.dumps(result))


if __name__=='__main__':main()
