#!/usr/bin/env python3
"""Explicit operator-only Link releases. CI never calls publication commands."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from release_catalog import catalog
import factory_package

ROOT = Path(__file__).resolve().parents[1]
REPO = 'EmberSoftwareInc/ember-link'
BRANCHES = {'stable': 'main', 'dev': 'dev'}
FEED_BRANCH = 'release-channels'
PUBLIC_KEY = ROOT / 'docs/signing/ember-link-production.pub'
ASSETS = ('ember-link.bin', 'manifest.json', 'link-releases.json', 'provenance.json', 'release-notes.md', 'SHA256SUMS')


def run(*args, cwd=ROOT, capture=False, data=None):
    return subprocess.run([str(a) for a in args], cwd=cwd, check=True, input=data,
                          text=True, stdout=subprocess.PIPE if capture else None).stdout


def git(*args):
    return run('git', *args, capture=True).strip()


def gh(*args, payload=None):
    return run('gh', *args, capture=True, data=json.dumps(payload) if payload is not None else None)


def require(ok, message):
    if not ok:
        raise ValueError(message)


def version_valid(version, channel):
    pattern = r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)'
    if channel == 'dev':
        pattern += r'-dev\.[1-9][0-9]*'
    return channel in BRANCHES and len(version) <= 31 and re.fullmatch(pattern, version) is not None


def source_version():
    a = re.search(r'set\(PROJECT_VER "([^"]+)"\)', (ROOT/'firmware/CMakeLists.txt').read_text())
    b = re.search(r'#define EMBER_LINK_VERSION "([^"]+)"', (ROOT/'firmware/main/app_version.h').read_text())
    require(a and b and a[1] == b[1], 'Firmware version declarations disagree')
    return a[1]


def source(channel, clean=True):
    require(git('branch', '--show-current') == BRANCHES[channel], f'Switch to {BRANCHES[channel]} first')
    if clean:
        require(not git('status', '--porcelain'), 'Commit or remove pending source changes first')
    version = source_version()
    require(version_valid(version, channel), f'Invalid {channel} version: {version}')
    return version, git('rev-parse', 'HEAD')


def set_version(args):
    require(git('branch', '--show-current') == BRANCHES[args.channel], f'Switch to {BRANCHES[args.channel]} first')
    require(not git('status', '--porcelain'), 'Commit pending changes before setting a version')
    require(version_valid(args.version, args.channel), 'Use X.Y.Z for stable or X.Y.Z-dev.N for dev (N >= 1)')
    previous = source_version()
    for name in ('firmware/CMakeLists.txt', 'firmware/main/app_version.h'):
        path = ROOT/name
        path.write_text(path.read_text().replace('"'+previous+'"', '"'+args.version+'"'))
    print('Version set. Review, commit and push these changes before prepare.')


def verify_image(image, version, notes):
    with tempfile.TemporaryDirectory() as temp:
        manifest = Path(temp)/'manifest.json'
        run(sys.executable, ROOT/'tools/firmware_manifest.py', image, '--public-key', PUBLIC_KEY,
            '--release-id', 'link-'+version.replace('.', '-'), '--notes', notes,
            '--download-url', f'https://github.com/{REPO}/releases/download/v{version}/ember-link.bin', '--output', manifest)
        value = json.loads(manifest.read_text())
    require(value['targetVersion'] == version, 'Built image version does not match the source')
    return value


def assets(directory):
    p = json.loads((directory/'provenance.json').read_text())
    return ASSETS[:-1] + (factory_package.EXTRA_ASSETS if p.get('schema') == 2 else ()) + ('SHA256SUMS',)


def checksums(directory):
    return ''.join(hashlib.sha256((directory/name).read_bytes()).hexdigest()+'  '+name+'\n' for name in assets(directory)[:-1])


def prepare(args):
    version, commit = source(args.channel)
    out, key = args.out.resolve(), args.key.resolve()
    require(not out.exists(), 'Output directory already exists; use a fresh directory')
    require(not out.is_relative_to(ROOT), 'Build outside the source repository')
    require(key.is_file() and not key.is_relative_to(ROOT), 'Use the established production private key outside the repo')
    notes = args.notes_file.read_text()
    require(0 < len(notes) <= 4000, 'Provide 1–4000 characters of release notes')
    out.mkdir(parents=True, mode=0o700)
    defaults = (ROOT/'firmware/sdkconfig.defaults').read_text()
    require('CONFIG_SECURE_BOOT_SIGNING_KEY="keys/ota_signing_key.pem"' in defaults, 'Review changed signing defaults')
    config = out/'sdkconfig'
    config.write_text(defaults.replace('"keys/ota_signing_key.pem"', json.dumps(str(key))))
    # Fresh build from this clean checkout; do not package a previously built image.
    run('idf.py', '-C', ROOT/'firmware', '-B', out/'build', '-D', f'SDKCONFIG={config}', 'build')
    require(source(args.channel) == (version, commit), 'Source changed during build; discard this package')
    shutil.copyfile(out/'build/ember-link.bin', out/'ember-link.bin')
    manifest = verify_image(out/'ember-link.bin', version, notes)
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (out/'link-releases.json').write_text(json.dumps(catalog([manifest], args.channel), indent=2)+'\n')
    (out/'provenance.json').write_text(json.dumps(dict(schema=2, repository=REPO, channel=args.channel,
        sourceBranch=BRANCHES[args.channel], sourceCommit=commit, tag='v'+version, version=version,
        imageSha256=manifest['sha256']), indent=2)+'\n')
    factory_package.prepare(out/'build', out, json.loads((out/'provenance.json').read_text()), manifest)
    (out/'release-notes.md').write_text(notes)
    (out/'SHA256SUMS').write_text(checksums(out))
    print(f'Prepared {version} at {out}. Qualify this exact image before publication.')


def validate_package(directory):
    require((directory/'SHA256SUMS').read_text() == checksums(directory), 'Release package checksum mismatch')
    p = json.loads((directory/'provenance.json').read_text())
    channel, version = p['channel'], p['version']
    require(p['repository'] == REPO and p['schema'] in (1, 2) and version_valid(version, channel), 'Invalid release provenance')
    require(p['sourceBranch'] == BRANCHES[channel] and p['tag'] == 'v'+version
            and re.fullmatch('[a-f0-9]{40}', p['sourceCommit']), 'Invalid source or tag')
    manifest = json.loads((directory/'manifest.json').read_text())
    require(manifest['targetVersion'] == version and manifest['sha256'] == p['imageSha256']
            and hashlib.sha256((directory/'ember-link.bin').read_bytes()).hexdigest() == p['imageSha256'], 'Image metadata mismatch')
    require(json.loads((directory/'link-releases.json').read_text()) == catalog([manifest], channel), 'Wrong channel catalog')
    if p['schema'] == 2:
        factory_package.validate(directory, p, manifest)
    return p, manifest


def checked_package(directory):
    p, m = validate_package(directory)
    require(verify_image(directory/'ember-link.bin', p['version'], m['notes']) == m, 'Signature/manifest mismatch')
    require(git('remote', 'get-url', 'origin') in (f'https://github.com/{REPO}.git', f'git@github.com:{REPO}.git'), 'Unexpected origin repository')
    run('git', 'fetch', 'origin', p['sourceBranch'])
    run('git', 'merge-base', '--is-ancestor', p['sourceCommit'], 'origin/'+p['sourceBranch'])
    require(git('show', p['sourceCommit']+':firmware/main/app_version.h').find('"'+p['version']+'"') >= 0, 'Tagged source version mismatch')
    return p


def draft(args):
    p = checked_package(args.package)
    # Existing releases/tags are never silently replaced by this command.
    existing = git('ls-remote', '--tags', 'origin', 'refs/tags/'+p['tag'])
    require(not existing, 'Release tag already exists; never reuse a published version')
    gh('release', 'create', p['tag'], *[args.package/name for name in assets(args.package)], '--repo', REPO,
       '--target', p['sourceCommit'], '--title', 'Ember Link '+p['version'], '--notes-file', args.package/'release-notes.md',
       '--draft', '--latest=false', *(['--prerelease'] if p['channel'] == 'dev' else []))
    print('Draft created. It does not change either update feed.')


def release_info(tag):
    return json.loads(gh('release', 'view', tag, '--repo', REPO, '--json', 'tagName,isDraft,isPrerelease,targetCommitish'))


def validate_release(info, p):
    require(info['tagName'] == p['tag'] and info['isPrerelease'] == (p['channel'] == 'dev'), 'Release channel/tag mismatch')
    require(info['targetCommitish'] == p['sourceCommit'], 'Draft target changed; inspect it before publication')


def check_assets(directory, tag):
    with tempfile.TemporaryDirectory() as temp:
        gh('release', 'download', tag, '--repo', REPO, '--dir', temp)
        for name in assets(directory):
            require((Path(temp)/name).read_bytes() == (directory/name).read_bytes(), 'Uploaded asset differs: '+name)


def publish(args):
    require(args.qualified, 'Pass --qualified only after recording hardware qualification')
    p = checked_package(args.package)
    require(p['schema'] == 1 or getattr(args, 'factory_qualified', False),
            'Record fresh-board installation and pass --factory-qualified before publishing a factory package')
    info = release_info(p['tag']); validate_release(info, p)
    require(info['isDraft'], 'Already published; do not mutate immutable release assets')
    check_assets(args.package, p['tag'])
    require(not git('ls-remote', '--tags', 'origin', 'refs/tags/'+p['tag']), 'Unexpected existing tag; inspect before publishing')
    gh('release', 'edit', p['tag'], '--repo', REPO, '--draft=false',
       '--prerelease='+str(p['channel'] == 'dev').lower(), '--latest='+str(p['channel'] == 'stable').lower())
    run('git', 'fetch', 'origin', 'refs/tags/'+p['tag']+':refs/tags/'+p['tag'])
    require(git('rev-parse', p['tag']+'^{commit}') == p['sourceCommit'], 'Published tag points to unexpected source')
    print('Published. Stable publication updates the existing stable feed.' if p['channel'] == 'stable'
          else 'Published prerelease. Run recommend-dev separately to offer it to opted-in testers.')
    sync_installer()


def sync_installer():
    try:
        gh('workflow', 'run', 'installer.yml', '--repo', REPO, '--ref', 'main')
    except subprocess.CalledProcessError:
        print('The release or channel change is saved, but installer deployment did not start. Retry the Browser installer workflow on main.', file=sys.stderr)


def update_dev(value, message):
    body = json.loads(gh('api', f'repos/{REPO}/contents/dev.json?ref={FEED_BRANCH}'))
    current = json.loads(base64.b64decode(body['content']))
    require(current.get('channel') == 'dev', 'Unexpected dev feed; stop and inspect')
    if current == value:
        print('Dev recommendation already matches.');
        sync_installer()
        return
    gh('api', '--method', 'PUT', f'repos/{REPO}/contents/dev.json', '--input', '-', payload=dict(
        message=message, branch=FEED_BRANCH, sha=body['sha'],
        content=base64.b64encode((json.dumps(value, indent=2)+'\n').encode()).decode()))
    print('Development feed updated. Raw GitHub downloads may be cached for several minutes.')
    sync_installer()


def recommend(args):
    p = checked_package(args.package)
    require(p['channel'] == 'dev', 'Only dev packages belong in the development feed')
    info = release_info(p['tag']); validate_release(info, p)
    require(not info['isDraft'], 'Publish the qualified prerelease first')
    run('git', 'fetch', 'origin', 'refs/tags/'+p['tag']+':refs/tags/'+p['tag'])
    require(git('rev-parse', p['tag']+'^{commit}') == p['sourceCommit'], 'Release tag/source mismatch')
    check_assets(args.package, p['tag'])
    update_dev(json.loads((args.package/'link-releases.json').read_text()), 'Recommend '+p['tag']+' for development testers')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    v = sub.add_parser('version'); v.add_argument('--channel', choices=BRANCHES, required=True); v.add_argument('version')
    p = sub.add_parser('prepare'); p.add_argument('--channel', choices=BRANCHES, required=True)
    p.add_argument('--key', type=Path, required=True); p.add_argument('--out', type=Path, required=True); p.add_argument('--notes-file', type=Path, required=True)
    for name in ('draft', 'publish', 'recommend-dev'):
        p = sub.add_parser(name); p.add_argument('package', type=Path)
        if name == 'publish':
            p.add_argument('--qualified', action='store_true')
            p.add_argument('--factory-qualified', action='store_true')
    sub.add_parser('withdraw-dev')
    a = parser.parse_args()
    try:
        {'version':set_version, 'prepare':prepare, 'draft':draft, 'publish':publish,
         'recommend-dev':recommend, 'withdraw-dev':lambda _:update_dev(catalog([], 'dev'), 'Withdraw development firmware recommendation')}[a.command](a)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as e:
        parser.exit(1, f'Release stopped: {e}\n')


if __name__ == '__main__':
    main()
