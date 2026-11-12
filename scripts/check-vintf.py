#!/usr/bin/env python3
"""Offline VINTF compatibility check of the package against stock V30.

Uses the built system/system_ext/product manifests, the packaged vendor image
and the real stock kernel configuration (config/stock-v30-kernel.config).
Manifests from APEXes mounted at boot are not covered.
"""
from pathlib import Path
import os
import shutil
import subprocess

env = os.environ
repo = Path(env['CAT_S22_REPO'])
root = Path(env['CAT_S22_SOURCE'])
out = Path(env['CAT_S22_OUT'])
built = Path(env['CAT_S22_PRODUCT_OUT']) / 'system'
stage = out / 'work/vintf'
shutil.rmtree(stage, ignore_errors=True)
for name, source in (('system', built), ('system_ext', built / 'system_ext'), ('product', built / 'product')):
    if (source / 'etc/vintf').exists():
        shutil.copytree(source / 'etc/vintf', stage / name / 'etc/vintf')
(stage / 'apex').mkdir(parents=True)
(stage / 'apex/apex-info-list.xml').write_text('<apex-info-list/>\n')

# Extract the vendor image's /etc tree (VINTF manifests) without mounting it.
vendor_tree = stage / 'vendor'
vendor_tree.mkdir()
subprocess.run(['debugfs', '-R', f'rdump /etc {vendor_tree}', str(out / 'images/vendor.img')],
               check=True, capture_output=True)
(vendor_tree / 'odm').mkdir(exist_ok=True)

args = [str(root / 'out/host/linux-x86/bin/checkvintf'), '--check-compat']
for name in ('system', 'system_ext', 'product', 'apex'):
    args += ['--dirmap', f'/{name}:{stage / name}']
args += ['--dirmap', f'/vendor:{vendor_tree}', '--dirmap', f'/odm:{vendor_tree / "odm"}',
         '--property', 'ro.product.first_api_level=29',
         '--kernel', f'4.9.227:{repo / "config/stock-v30-kernel.config"}']
result = subprocess.run(args, capture_output=True, text=True)
(out / 'reports').mkdir(exist_ok=True)
(out / 'reports/vintf-check.log').write_text(result.stdout + result.stderr)
print((result.stdout + result.stderr)[-4000:])
print('VINTF', 'COMPATIBLE' if result.returncode == 0 else 'FAILED (exit %d)' % result.returncode)
raise SystemExit(result.returncode)
