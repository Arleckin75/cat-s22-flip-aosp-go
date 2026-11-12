#!/usr/bin/env python3
"""Assemble the flashable set: super.img (system + vendor), boot.img, vbmeta.

Everything except system.img comes from the builder's own stock V30:
  boot.img    stock V30 kernel and DTB; ramdisk fstab loses its one `product`
              line (the TrebleDroid system image carries product inside system)
  vendor.img  stock V30 with the same single line removed from /etc/fstab.qcom
  vbmeta      stock V30 header with flags=3 (verification and hashtree off);
              only valid on an unlocked bootloader
  super.img   sparse; same 2.5 GiB size and qti_dynamic_partitions group as V30

Every step is verified: kernel/DTB bytes unchanged, fstab edits are exactly one
line, and the sparse super round-trips to byte-identical partitions.
"""
from pathlib import Path
import gzip
import hashlib
import json
import os
import shlex
import shutil
import struct
import subprocess
import sys

env = os.environ
root = Path(env['CAT_S22_SOURCE'])
stock = Path(env['CAT_S22_STOCK']) / 'v30'
out = Path(env['CAT_S22_OUT'])
product_out = Path(env['CAT_S22_PRODUCT_OUT'])
host_bin = root / 'out/host/linux-x86/bin'
mkbootimg = root / 'system/tools/mkbootimg'
work, images, reports = out / 'work', out / 'images', out / 'reports'
SUPER_SIZE, GROUP_SIZE = 2684354560, 2680160256


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def drop_product_line(text):
    lines = text.splitlines(keepends=True)
    removed = [line for line in lines if line.split()[:1] == ['product']]
    if len(removed) != 1:
        raise RuntimeError('Expected exactly one product mount line')
    return ''.join(line for line in lines if line not in removed)


def debugfs(image, command, write=False):
    args = ['debugfs'] + (['-w'] if write else []) + ['-R', command, str(image)]
    return subprocess.run(args, capture_output=True, check=True).stdout


def build_boot():
    unpacked = work / 'boot-unpacked'
    shutil.rmtree(unpacked, ignore_errors=True)
    arguments = shlex.split(subprocess.check_output([
        sys.executable, str(mkbootimg / 'unpack_bootimg.py'), '--boot_img', str(stock / 'boot.img'),
        '--out', str(unpacked), '--format=mkbootimg'], text=True))
    archive = gzip.decompress((unpacked / 'ramdisk').read_bytes())
    rebuilt, offset, changed = bytearray(), 0, 0
    # Rewrite the newc CPIO in place so every other entry stays byte-identical.
    while offset + 110 <= len(archive):
        magic = archive[offset:offset + 6]
        if magic not in (b'070701', b'070702'):
            raise ValueError('Unsupported ramdisk CPIO header')
        fields = [int(archive[offset + 6 + i * 8:offset + 14 + i * 8], 16) for i in range(13)]
        namesize, size = fields[11], fields[6]
        name_bytes = archive[offset + 110:offset + 110 + namesize]
        name = name_bytes.rstrip(b'\0').decode()
        data_offset = (offset + 110 + namesize + 3) & ~3
        data = archive[data_offset:data_offset + size]
        offset = (data_offset + size + 3) & ~3
        if name.lstrip('./') == 'fstab.qcom':
            data = drop_product_line(data.decode()).encode()
            fields[6] = len(data)
            if magic == b'070702':
                fields[12] = sum(data) & 0xffffffff
            changed += 1
        rebuilt += magic + ''.join(f'{value:08x}' for value in fields).encode() + name_bytes
        rebuilt += bytes((-len(rebuilt)) % 4) + data
        rebuilt += bytes((-len(rebuilt)) % 4)
        if name == 'TRAILER!!!':
            break
    if changed != 1:
        raise ValueError('Ramdisk fstab was not patched exactly once')
    rebuilt += bytes((-len(rebuilt)) % 512)
    ramdisk = work / 'ramdisk.gz'
    ramdisk.write_bytes(gzip.compress(bytes(rebuilt), mtime=0))
    arguments[arguments.index('--ramdisk') + 1] = str(ramdisk)
    boot = out / 'boot.img'
    subprocess.run([sys.executable, str(mkbootimg / 'mkbootimg.py'), *arguments, '--output', str(boot)],
                   check=True)
    check = work / 'boot-verify'
    shutil.rmtree(check, ignore_errors=True)
    subprocess.run([sys.executable, str(mkbootimg / 'unpack_bootimg.py'), '--boot_img', str(boot),
                    '--out', str(check)], check=True, stdout=subprocess.DEVNULL)
    for part in ('kernel', 'dtb'):
        if (unpacked / part).read_bytes() != (check / part).read_bytes():
            raise ValueError('Stock ' + part + ' changed')
    if gzip.decompress((check / 'ramdisk').read_bytes()) != bytes(rebuilt):
        raise ValueError('Repacked ramdisk mismatch')


def build_vendor():
    vendor = images / 'vendor.img'
    shutil.copyfile(stock / 'vendor.img', vendor)
    original = debugfs(vendor, 'cat /etc/fstab.qcom').decode()
    patched = drop_product_line(original).encode()
    fstab = reports / 'fstab.vendor.patched'
    fstab.write_bytes(patched)
    debugfs(vendor, 'rm /etc/fstab.qcom', write=True)
    debugfs(vendor, f'write {fstab} /etc/fstab.qcom', write=True)
    debugfs(vendor, 'set_inode_field /etc/fstab.qcom mode 0100644', write=True)
    debugfs(vendor, 'ea_set /etc/fstab.qcom security.selinux "u:object_r:vendor_configs_file:s0\\000"',
            write=True)
    if debugfs(vendor, 'cat /etc/fstab.qcom') != patched:
        raise RuntimeError('Vendor fstab write verification failed')
    subprocess.run(['e2fsck', '-fn', str(vendor)], check=True)


def build_vbmeta():
    vbmeta = bytearray((stock / 'vbmeta.img').read_bytes())
    if vbmeta[:4] != b'AVB0' or struct.unpack_from('>I', vbmeta, 120)[0] != 0:
        raise RuntimeError('Unexpected stock vbmeta flags')
    struct.pack_into('>I', vbmeta, 120, 3)
    (out / 'vbmeta-disabled.img').write_bytes(vbmeta)


def build_super():
    args = [str(host_bin / 'lpmake'), '--metadata-size', '65536', '--super-name', 'super',
            '--metadata-slots', '2', '--device', f'super:{SUPER_SIZE}',
            '--group', f'qti_dynamic_partitions:{GROUP_SIZE}']
    for name in ('system', 'vendor'):
        image = images / (name + '.img')
        args += ['--partition', f'{name}:readonly:{image.stat().st_size}:qti_dynamic_partitions',
                 '--image', f'{name}={image}']
    args += ['--sparse', '--output', str(out / 'super.img')]
    subprocess.run(args, check=True)
    raw = work / 'super.raw.img'
    subprocess.run([str(host_bin / 'simg2img'), str(out / 'super.img'), str(raw)], check=True)
    if raw.stat().st_size != SUPER_SIZE:
        raise RuntimeError('Incorrect expanded super size')
    (reports / 'lpdump.txt').write_bytes(subprocess.check_output([str(host_bin / 'lpdump'), str(raw)]))
    roundtrip = work / 'roundtrip'
    shutil.rmtree(roundtrip, ignore_errors=True)
    roundtrip.mkdir()
    subprocess.run([str(host_bin / 'lpunpack'), str(raw), str(roundtrip)], check=True)
    for name in ('system.img', 'vendor.img'):
        if sha(images / name) != sha(roundtrip / name):
            raise RuntimeError('Sparse super round-trip mismatch: ' + name)
    raw.unlink()


for directory in (work, images, reports):
    directory.mkdir(parents=True, exist_ok=True)
shutil.copyfile(product_out / 'system.img', images / 'system.img')
subprocess.run(['e2fsck', '-fn', str(images / 'system.img')], check=True)
build_boot()
build_vendor()
build_vbmeta()
build_super()
files = [out / 'boot.img', out / 'vbmeta-disabled.img', out / 'super.img',
         images / 'system.img', images / 'vendor.img']
manifest = {str(p.relative_to(out)): {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in files}
manifest['physical_super_bytes'] = SUPER_SIZE
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Verified package:', out)
