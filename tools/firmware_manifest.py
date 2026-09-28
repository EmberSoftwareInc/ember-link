#!/usr/bin/env python3
"""Verify a release with its PUBLIC signing key and produce upload metadata.
Run in the project's ESP-IDF Python environment. No private key is required.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
import espsecure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--public-key', type=Path, required=True)
    parser.add_argument('--release-id', required=True)
    parser.add_argument('--notes', default='')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--download-url', help='Tagged public GitHub URL for the signed image')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,63}', args.release_id):
        parser.error('Invalid release ID')
    image = args.image.read_bytes()
    if len(image) < 4096 or len(image) > 3*1024*1024 or image[0] != 0xe9 or struct.unpack_from('<H', image, 12)[0] != 9:
        parser.error('Expected a signed ESP32-S3 application image within the 3 MiB slot')
    # ESP image header (24), segment header (8), app descriptor.
    if struct.unpack_from('<I', image, 32)[0] != 0xabcd5432:
        parser.error('Missing app descriptor')
    version = image[48:80].split(b'\0')[0].decode('ascii')
    project = image[80:112].split(b'\0')[0].decode('ascii')
    if project != 'ember-link' or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?', version):
        parser.error('Wrong project or invalid firmware version')
    with args.public_key.open('rb') as key, args.image.open('rb') as binary:
        espsecure.verify_signature_v2(False, None, key, binary)
    with tempfile.TemporaryDirectory() as directory:
        digest_path = Path(directory) / 'digest.bin'
        with args.public_key.open('rb') as key:
            espsecure.digest_sbv2_public_key(key, str(digest_path))
        key_id = digest_path.read_bytes().hex()
    manifest = dict(schema=1, releaseId=args.release_id, targetVersion=version,
                    boardId='lilygo-t-dongle-s3', layoutId='link-v1', signingKeyId=key_id,
                    size=len(image), sha256=hashlib.sha256(image).hexdigest(), notes=args.notes[:4000])
    if args.download_url:
        manifest['url'] = args.download_url
        from release_catalog import catalog
        try:
            catalog([manifest])
        except ValueError as e:
            parser.error(str(e))
    args.output.write_text(json.dumps(manifest, indent=2)+'\n')
    print(f'Verified release manifest: {args.output}')


if __name__ == '__main__':
    main()
