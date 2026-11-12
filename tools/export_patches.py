#!/usr/bin/env python3
"""Maintainer tool: export a prepared build tree into this repository's patches/.

Run inside a tree that was synced from manifest/cat-s22-aosp16.xml and then
patched. For every project whose HEAD moved past the pinned revision, the
commits are exported with `git format-patch`. Uncommitted edits to tracked
files are exported as one extra mail-formatted patch per project, so that
`scripts/apply-patches.sh` can replay everything with `git am`.

Untracked files are reported but never exported: device files belong in
device/cat/s22, not in patches.

Usage: export_patches.py <source-root> <pinned-manifest.xml> <output-dir>
"""
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

LOCAL_SUBJECT = 'CAT S22: local build adaptation'


def git(path, *args):
    return subprocess.check_output(['git', *args], cwd=path, text=True)


def main():
    root, manifest, output = (Path(arg).resolve() for arg in sys.argv[1:4])
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    series = []
    for project in ET.parse(manifest).getroot().findall('project'):
        relative = project.get('path') or project.get('name')
        directory = root / relative
        if not directory.is_dir():
            continue
        pinned = project.get('revision')
        head = git(directory, 'rev-parse', 'HEAD').strip()
        destination = output / relative
        count = 0
        if head != pinned:
            if subprocess.run(['git', 'merge-base', '--is-ancestor', pinned, head], cwd=directory).returncode:
                print('SKIP (pinned revision is not an ancestor; pin it in the manifest):', relative)
                continue
            destination.mkdir(parents=True, exist_ok=True)
            subprocess.run(['git', 'format-patch', '--quiet', '--no-signature', '--zero-commit',
                            '--no-stat', '-o', str(destination), f'{pinned}..{head}'],
                           cwd=directory, check=True)
            count = len(list(destination.glob('*.patch')))
        diff = git(directory, 'diff', 'HEAD', '--binary')
        if diff:
            destination.mkdir(parents=True, exist_ok=True)
            count += 1
            name = f'{count:04d}-CAT-S22-local-build-adaptation.patch'
            (destination / name).write_text(
                'From 0000000000000000000000000000000000000000 Mon Sep 17 00:00:00 2001\n'
                'From: CAT S22 build <builder@localhost>\n'
                'Date: Sat, 1 Jan 2000 00:00:00 +0000\n'
                f'Subject: [PATCH] {LOCAL_SUBJECT}\n\n---\n' + diff)
        untracked = git(directory, 'ls-files', '--others', '--exclude-standard').split()
        if untracked:
            print('UNTRACKED (not exported):', relative, ' '.join(untracked))
        if count:
            series.append(f'{relative} {count}')
            print(f'{relative}: {count} patch(es)')
    (output / 'SERIES').write_text('\n'.join(series) + '\n')
    print(len(series), 'projects exported to', output)


if __name__ == '__main__':
    main()
