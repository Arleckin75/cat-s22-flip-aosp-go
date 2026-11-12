#!/usr/bin/env python3
"""Install device/cat/s22 into the source tree and generate stock-derived files.

Inputs (see scripts/env.sh):
  CAT_S22_SOURCE    patched Android tree
  CAT_S22_STOCK     directory holding v30/{system,vendor}.img (prepare-stock.sh)
  CAT_S22_UPSTREAM  checkout of ajimsjames/cat-s22-flip-android15-fixes
  CAT_S22_ADB_KEY   optional: an adb public key to trust in the image. Only for
                    private debugging builds; never set it for builds you share.

Generated from the user's own stock V30 (not stored in this repository):
  compatibility_matrix.device.xml  optional declarations of V30's vendor HALs
  compatibility_matrix.4.xml       FCM level 4 matrix required by V30 vendor
  TED2Camera.apk                   stock CAT camera app
"""
from collections import defaultdict
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

env = os.environ
repo = Path(env['CAT_S22_REPO'])
root = Path(env['CAT_S22_SOURCE'])
stock = Path(env['CAT_S22_STOCK']) / 'v30'
upstream = Path(env['CAT_S22_UPSTREAM'])
upstream_rev = env['CAT_S22_UPSTREAM_REV']
dest = root / 'device/cat/s22'


def debugfs_cat(image, path):
    return subprocess.check_output(['debugfs', '-R', f'cat {path}', str(image)],
                                   stderr=subprocess.DEVNULL)


# --- Upstream hardware helpers and IMS, fetched at a fixed commit. -----------
if not (upstream / '.git').is_dir():
    subprocess.run(['git', 'clone', env['CAT_S22_UPSTREAM_URL'], str(upstream)], check=True)
if subprocess.run(['git', '-C', str(upstream), 'cat-file', '-e', upstream_rev + '^{commit}'],
                  stderr=subprocess.DEVNULL).returncode:
    subprocess.run(['git', '-C', str(upstream), 'fetch', 'origin'], check=True)
subprocess.run(['git', '-C', str(upstream), 'checkout', '--quiet', '--detach', upstream_rev], check=True)

# --- Device directory from this repository. ---------------------------------
if dest.exists():
    shutil.rmtree(dest)
shutil.copytree(repo / 'device/cat/s22', dest)
for source, target in (
    ('ims_volte/ims/ims.apk', 'ims.apk'),
    ('ims_volte/compatibility_matrix.device.xml', 'compatibility_matrix.device.xml'),
):
    shutil.copyfile(upstream / source, dest / target)
(dest / 'UPSTREAM.txt').write_text(env['CAT_S22_UPSTREAM_URL'] + '\n' + upstream_rev + '\n')


def replace_once(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise RuntimeError(f'Unexpected source context in {path}: {old!r}')
    path.write_text(text.replace(old, new, 1))


# --- VINTF: accept the real V30 vendor HAL inventory. -------------------------
# Optional declarations of interfaces V30 actually ships; kernel, VNDK and
# SELinux checks are not relaxed.
matrix = dest / 'compatibility_matrix.device.xml'
matrix_tree = ET.parse(matrix)
for element in list(matrix_tree.getroot()):
    matrix_tree.getroot().remove(element)
matrix_tree.getroot().set('version', '1.0')
vendor_root = ET.fromstring(debugfs_cat(stock / 'vendor.img', '/etc/vintf/manifest.xml'))
listing = subprocess.check_output(['debugfs', '-R', 'ls -p /etc/vintf/manifest', str(stock / 'vendor.img')],
                                  stderr=subprocess.DEVNULL).decode()
for line in listing.splitlines():
    fields = line.split('/')
    if len(fields) > 5 and fields[5].endswith('.xml'):
        fragment = debugfs_cat(stock / 'vendor.img', '/etc/vintf/manifest/' + fields[5])
        for hal in ET.fromstring(fragment).findall('hal'):
            vendor_root.append(hal)
for hal in vendor_root.findall('hal'):
    fmt = hal.get('format', 'hidl')
    versions = {node.text for node in hal.findall('version')}
    interfaces = defaultdict(set)
    for interface in hal.findall('interface'):
        interfaces[interface.findtext('name')].update(node.text for node in interface.findall('instance'))
    for node in hal.findall('fqname'):
        match = re.fullmatch(r'@([^:]+)::([^/]+)/(.+)', node.text or '')
        if match:
            versions.add(match[1])
            interfaces[match[2]].add(match[3])
    if not interfaces:
        continue
    entry = ET.SubElement(matrix_tree.getroot(), 'hal', format=fmt, optional='true')
    ET.SubElement(entry, 'name').text = hal.findtext('name')
    for version in sorted(versions or ({'1'} if fmt == 'aidl' else set())):
        ET.SubElement(entry, 'version').text = version
    for interface_name, instances in sorted(interfaces.items()):
        interface = ET.SubElement(entry, 'interface')
        ET.SubElement(interface, 'name').text = interface_name
        for instance in sorted(instances):
            ET.SubElement(interface, 'instance').text = instance
matrix_tree.write(matrix, encoding='UTF-8', xml_declaration=True)

fcm4 = debugfs_cat(stock / 'system.img', '/system/etc/vintf/compatibility_matrix.4.xml')
if ET.fromstring(fcm4).get('level') != '4':
    raise RuntimeError('Expected the stock FCM level 4 compatibility matrix')
(dest / 'compatibility_matrix.4.xml').write_bytes(fcm4)

# --- Keypad debounce daemon, adapted from the upstream bare-metal source. -----
# Built against bionic; finds the keypad by name instead of a fixed event node.
source = (upstream / 'keypad/keypad_debounce_smart.c').read_text()
body = source[source.index('#define INPUT_DEV'):]
body = re.sub(r'^#define (INPUT_DEV|O_RDONLY|O_WRONLY|O_NONBLOCK)\s+.*\n', '', body, flags=re.M)
for old, new in (
    ('void _start(void)', 'int main(void)'),
    ('int src = (int)_open(INPUT_DEV, O_RDONLY);', 'int src = open_matrix();'),
    ('    _ioctl(src, EVIOCGRAB, 1);\n', ''),
    ('    _ioctl(ui, UI_DEV_CREATE, 0);',
     '    if (_ioctl(ui, UI_DEV_CREATE, 0) < 0) return 3;\n    if (_ioctl(src, EVIOCGRAB, 1) < 0) return 4;'),
    ('struct _timespec slp', 'struct timespec slp'),
    ('        /* Special handling for Speaker button */',
     '        if (handle_programmable(ui, code, val, now)) continue;\n\n'
     '        /* Special handling for Speaker button */'),
    ('    struct uinput_user_dev uid;',
     '    _ioctl(ui, UI_SET_KEYBIT, KEY_MACRO1);\n    _ioctl(ui, UI_SET_KEYBIT, KEY_MACRO2);\n\n'
     '    struct uinput_user_dev uid;'),
    ('sc2(__NR_nanosleep, (long)&slp, 0);', 'nanosleep(&slp, NULL);'),
    ('if (n < (long)sizeof(ev)) continue;',
     'if (n < (long)sizeof(ev)) {\n            if (n < 0 && errno == EINTR) continue;\n'
     '            return 5; /* exit releases EVIOCGRAB; init can restart us */\n        }'),
):
    if old not in body:
        raise RuntimeError('Unexpected keypad source: ' + old)
    body = body.replace(old, new)
prefix = (repo / 'device/cat/s22-templates/keypad_prefix.c').read_text()
(dest / 'keypad.c').write_text(prefix + body)

# --- IMS: platform signing does not replace the privileged allowlist. --------
aapt2 = root / 'prebuilts/sdk/tools/linux/bin/aapt2'
badging = subprocess.check_output([str(aapt2), 'dump', 'badging', str(dest / 'ims.apk')], text=True)
permissions = ET.Element('permissions')
app = ET.SubElement(permissions, 'privapp-permissions', package='org.codeaurora.ims')
for name in sorted(set(re.findall(r"uses-permission: name='([^']+)'", badging))):
    ET.SubElement(app, 'permission', name=name)
ET.ElementTree(permissions).write(dest / 'privapp-permissions-cat-ims.xml', encoding='UTF-8',
                                  xml_declaration=True)

# --- Stock CAT camera (TED2Camera, com.android.camera) from V30 product. ------
camera = debugfs_cat(stock / 'product.img', '/app/TED2Camera/TED2Camera.apk')
if camera[:2] != b'PK' or len(camera) < 1_000_000:
    raise RuntimeError('Stock camera APK not found in V30 product.img')
(dest / 'TED2Camera.apk').write_bytes(camera)

# --- Product: device/cat/s22/AndroidProducts.mk registers aosp_cat_s22. -------
product = dest / 'aosp_cat_s22.mk'
adb_key = env.get('CAT_S22_ADB_KEY')
if adb_key:
    shutil.copyfile(adb_key, dest / 'host-adbkey.pub')
    with product.open('a') as stream:
        stream.write('\n# Private debugging build: trust one workstation without a prompt.\n'
                     'PRODUCT_ADB_KEYS += device/cat/s22/host-adbkey.pub\n')
    print('WARNING: image trusts the adb key', adb_key, '- do not share this build.')
print('CAT S22 device files installed in', dest)
