"""Reconstruct stock V30 images from V29 plus the V30 incremental OTA.

Usage: reconstruct_v30.py <stock-dir>  (expects v29/, v30.zip, imgpatchtools/)
Every V29 source hash and every V30 target write is verified.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile

stock = Path(sys.argv[1]).resolve()
source = stock / 'v29'
target = stock / 'v30'
patches = stock / 'v30-patches'
target.mkdir(exist_ok=True)
patches.mkdir(exist_ok=True)
tools = stock / 'imgpatchtools/bin'


def digest(path, algorithm='sha1'):
    h = hashlib.new(algorithm)
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def run_tool(arguments):
    result = subprocess.run([str(x) for x in arguments], cwd=stock, capture_output=True, text=True)
    print(result.stdout, flush=True)
    if result.stderr:
        print(result.stderr, flush=True)
    # Upstream main() returns zero even when a patch operation fails.
    if result.returncode != 0 or 'Done with error code:' in result.stdout or not result.stdout.rstrip().endswith('Done'):
        raise RuntimeError('Patch tool did not report success')


def read_ranges(stream, encoded):
    values = list(map(int, encoded.split(',')))
    if values[0] != len(values) - 1 or values[0] % 2:
        raise ValueError('Invalid block ranges')
    for start, end in zip(values[1::2], values[2::2]):
        stream.seek(start * 4096)
        remaining = (end - start) * 4096
        while remaining:
            chunk = stream.read(min(remaining, 1024 * 1024))
            if not chunk:
                raise ValueError('Truncated image')
            yield chunk
            remaining -= len(chunk)


with zipfile.ZipFile(stock / 'v30.zip') as ota:
    script = ota.read('META-INF/com/google/android/updater-script').decode()
    for name in ota.namelist():
        if name.endswith(('.dat', '.list', '.p')):
            (patches / Path(name).name).write_bytes(ota.read(name))
    for partition in ('system', 'vendor', 'product'):
        path = target / (partition + '.img')
        match = re.search(r'range_sha1\(map_partition\("' + partition + r'"\), "([0-9,]+)"\) == "([0-9a-f]{40})"', script)
        if not match:
            raise ValueError('Missing source block hash')
        h = hashlib.sha1()
        with (source / path.name).open('rb') as stream:
            for chunk in read_ranges(stream, match[1]):
                h.update(chunk)
        if h.hexdigest() != match[2]:
            raise ValueError('V29 source block hash mismatch: ' + partition)
        if not path.exists():
            shutil.copyfile(source / path.name, path)
            print('Patching', partition, flush=True)
            run_tool([tools / 'BlockImageUpdate', path, patches / (partition + '.transfer.list'),
                      patches / (partition + '.new.dat'), patches / (partition + '.patch.dat')])
        # Independently validate every target write against the transfer list.
        commands = (patches / (partition + '.transfer.list')).read_text().splitlines()[4:]
        with path.open('rb') as stream, (patches / (partition + '.new.dat')).open('rb') as new:
            for line in commands:
                fields = line.split()
                if not fields:
                    continue
                op = fields[0]
                if op in ('bsdiff', 'imgdiff', 'move'):
                    expected, encoded = (fields[4], fields[5]) if op != 'move' else (fields[1], fields[2])
                    h = hashlib.sha1()
                    for chunk in read_ranges(stream, encoded):
                        h.update(chunk)
                    if h.hexdigest() != expected:
                        raise ValueError('V30 target command hash mismatch: ' + partition)
                elif op in ('new', 'zero'):
                    for chunk in read_ranges(stream, fields[1]):
                        expected_bytes = new.read(len(chunk)) if op == 'new' else bytes(len(chunk))
                        if chunk != expected_bytes:
                            raise ValueError('V30 target data mismatch: ' + partition)
                elif op not in ('stash', 'free', 'erase'):
                    raise ValueError('Unknown OTA operation: ' + op)
            if new.read(1):
                raise ValueError('Unused new-data bytes')
        print('Source hash and all target writes verified:', partition, flush=True)
    # Normalize only concat() calls consisting entirely of string literals.
    normalized = re.sub(r'concat\(((?:\s*"[^"]*"\s*,?)+)\)',
                        lambda m: '"' + ''.join(re.findall(r'"([^"]*)"', m[1])) + '"', script)
    pattern = r'patch_partition\("EMMC:[^":]+/([^/":]+):(\d+):([0-9a-f]{40})",\s*"EMMC:[^":]+/[^/":]+:(\d+):([0-9a-f]{40})",\s*package_extract_file\("([^"]+)"\)\)'
    seen = set()
    for match in re.finditer(pattern, normalized):
        # Modern recovery patch_partition() takes TARGET first, SOURCE second.
        partition, new_size, new_sha, old_size, old_sha, patch_name = match.groups()
        if partition.endswith('bak'):
            continue
        path = target / (partition + '.img')
        original = source / path.name
        if original.stat().st_size != int(old_size) or digest(original) != old_sha:
            raise ValueError('Unexpected V29 source: ' + partition)
        if path.exists():
            raise FileExistsError(path)
        run_tool([tools / 'ApplyPatch', original, path, new_sha, new_size, old_sha, patches / Path(patch_name).name])
        if path.stat().st_size != int(new_size) or digest(path) != new_sha:
            raise ValueError('V30 target image hash mismatch: ' + partition)
        seen.add(partition)
    if seen != {'boot', 'vbmeta', 'vbmeta_system', 'modem', 'dtbo'}:
        raise ValueError('Unexpected patched firmware set: ' + repr(seen))
    shutil.copyfile(source / 'adspso.img', target / 'adspso.img')
    (target / 'metadata.txt').write_bytes(ota.read('META-INF/com/android/metadata'))

report = [{'file': p.name, 'bytes': p.stat().st_size, 'sha256': digest(p, 'sha256')}
          for p in sorted(target.glob('*.img'))]
(target / 'images.json').write_text(json.dumps(report, indent=2))
print('V30_RECONSTRUCTION_VERIFIED', flush=True)
