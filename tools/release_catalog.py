"""Build the public recommendation catalog from signature-verified release manifests.
No publication or signing occurs here. Use firmware_manifest.py for each input first.
"""
import argparse
import json
import re
from pathlib import Path
from urllib.parse import urlsplit


def catalog(manifests):
    if len(manifests) > 16:
        raise ValueError('At most 16 board/layout/key recommendations are supported')
    channels = set()
    releases = []
    for manifest in manifests:
        m = dict(manifest)
        u = urlsplit(m.get('url', ''))
        parts = u.path.split('/')
        if not (u.scheme == 'https' and u.netloc == 'github.com' and not u.query and not u.fragment
                and len(parts) == 7 and parts[1:5] == ['EmberSoftwareInc', 'ember-link', 'releases', 'download']
                and parts[5] not in ['', 'latest'] and parts[6].endswith('.bin')):
            raise ValueError('Each manifest needs a tagged EmberSoftwareInc/ember-link GitHub .bin URL')
        if (m.get('schema') != 1 or not re.fullmatch(r'[A-Za-z0-9_-]{1,63}', m.get('releaseId', ''))
                or not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', m.get('targetVersion', ''))
                or len(m['targetVersion']) > 31
                or m.get('boardId') != 'lilygo-t-dongle-s3' or m.get('layoutId') != 'link-v1'
                or not re.fullmatch(r'[a-f0-9]{64}', m.get('signingKeyId', ''))
                or not re.fullmatch(r'[a-f0-9]{64}', m.get('sha256', ''))
                or type(m.get('size')) is not int or not 4096 <= m['size'] <= 3*1024*1024
                or not isinstance(m.get('notes'), str) or len(m['notes']) > 4000):
            raise ValueError('Invalid verified release manifest')
        channel = (m['boardId'], m['layoutId'], m['signingKeyId'])
        if channel in channels:
            raise ValueError('Only one recommended release per board/layout/key is allowed')
        channels.add(channel)
        releases.append({k: m[k] for k in ['schema', 'releaseId', 'targetVersion', 'boardId', 'layoutId', 'signingKeyId', 'size', 'sha256', 'notes', 'url']})
    return dict(schema=1, releases=releases)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, action='append', default=[])
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    try:
        result = catalog([json.loads(path.read_text()) for path in a.manifest])
    except (ValueError, KeyError, TypeError) as e:
        p.error(str(e))
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'Wrote {len(result["releases"])} recommendations to {a.output}')
