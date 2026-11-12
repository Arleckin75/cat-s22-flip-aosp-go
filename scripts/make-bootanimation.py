#!/usr/bin/env python3
"""Rescale a boot animation to the CAT S22's 480x640 screen.

Each frame is placed on its full canvas (honouring trim.txt offsets) and scaled
to 480 px wide. Taller frames (9:16 phone animations) are centre-cropped to 640 px;
the logo sits near the middle, so nothing is lost. Shorter frames (TrebleDroid's
landscape one) are centred on the part's background colour from desc.txt. The
output is an untrimmed, uncompressed (stored) zip as bootanimation requires.

Usage: make-bootanimation.py <bootanimation.zip> <output.zip>
Requires Pillow.
"""
from pathlib import PurePosixPath
import io
import re
import sys
import zipfile

from PIL import Image

WIDTH, HEIGHT = 480, 640


def main(source, output):
    with zipfile.ZipFile(source) as zin:
        desc = zin.read('desc.txt').decode().splitlines()
        src_w, src_h, fps = desc[0].split()[:3]
        src_w, src_h = int(src_w), int(src_h)
        scaled_h = round(src_h * WIDTH / src_w)
        top = (scaled_h - HEIGHT) // 2
        parts, backgrounds = [], {}
        for line in desc[1:]:
            fields = line.split()
            if len(fields) < 4:
                continue
            parts.append(fields[3])
            if len(fields) > 4 and re.fullmatch(r'#[0-9a-fA-F]{6}', fields[4]):
                backgrounds[fields[3]] = tuple(int(fields[4][i:i + 2], 16) for i in (1, 3, 5))
        parts = list(dict.fromkeys(parts))
        with zipfile.ZipFile(output, 'w', zipfile.ZIP_STORED) as zout:
            zout.writestr('desc.txt', '\n'.join([f'{WIDTH} {HEIGHT} {fps}'] + desc[1:]) + '\n')
            for part in parts:
                frames = [n for n in zin.namelist()
                          if PurePosixPath(n).parent.name == part and n.endswith('.png')]
                trims = []
                if f'{part}/trim.txt' in zin.namelist():
                    trims = zin.read(f'{part}/trim.txt').decode().split()
                # trim.txt lines follow the zip's frame order.
                if trims and len(trims) != len(frames):
                    raise ValueError(f'{part}: {len(frames)} frames but {len(trims)} trims')
                zout.writestr(f'{part}/', b'')
                for index, name in enumerate(frames):
                    frame = Image.open(io.BytesIO(zin.read(name))).convert('RGBA')
                    background = backgrounds.get(part, (0, 0, 0)) + (255,)
                    canvas = Image.new('RGBA', (src_w, src_h), background)
                    x = y = 0
                    if trims:
                        _, _, x, y = map(int, re.fullmatch(r'(\d+)x(\d+)\+(\d+)\+(\d+)',
                                                           trims[index]).groups())
                    canvas.paste(frame, (x, y), frame)
                    canvas = canvas.resize((WIDTH, scaled_h), Image.LANCZOS)
                    if scaled_h >= HEIGHT:
                        canvas = canvas.crop((0, top, WIDTH, top + HEIGHT))
                    else:
                        padded = Image.new('RGBA', (WIDTH, HEIGHT), background)
                        padded.paste(canvas, (0, (HEIGHT - scaled_h) // 2))
                        canvas = padded
                    canvas = canvas.convert('RGB')
                    data = io.BytesIO()
                    canvas.save(data, 'PNG', optimize=True)
                    zout.writestr(name, data.getvalue())
                for extra in zin.namelist():
                    if (PurePosixPath(extra).parent.name == part and not extra.endswith('.png')
                            and not extra.endswith('trim.txt') and not extra.endswith('/')):
                        zout.writestr(extra, zin.read(extra))
    print('Wrote', output, f'{WIDTH}x{HEIGHT}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
