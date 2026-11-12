"""Extract full BLOCK OTA images without executing the OTA installer (Linux)."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile


def ranges(value):
    values = [int(x) for x in value.split(',')]
    if values[0] != len(values) - 1 or values[0] % 2:
        raise ValueError('Invalid transfer ranges')
    return list(zip(values[1::2], values[2::2]))


def extract(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as ota:
        sizes = {}
        for line in ota.read('dynamic_partitions_op_list').decode().splitlines():
            fields = line.split()
            if fields and fields[0] == 'resize':
                sizes[fields[1]] = int(fields[2])
        for name in ota.namelist():
            if name.endswith('.img'):
                target = destination / Path(name).name
                if target.exists():
                    raise FileExistsError(target)
                target.write_bytes(ota.read(name))
        for partition, size in sizes.items():
            lines = ota.read(partition + '.transfer.list').decode().splitlines()
            if lines[0] != '4':
                raise ValueError('Unsupported transfer-list version')
            commands = [line.split() for line in lines[4:] if line.strip()]
            if any(cmd[0] not in ('new', 'zero', 'erase') for cmd in commands):
                raise ValueError('Only full OTA input is supported')
            target = destination / (partition + '.img')
            if target.exists():
                raise FileExistsError(target)
            print('Extracting', partition, size, flush=True)
            compressed = destination / (partition + '.new.dat.br')
            compressed.write_bytes(ota.read(partition + '.new.dat.br'))
            try:
                with target.open('xb') as image:
                    image.truncate(size)
                    with subprocess.Popen(['brotli', '-d', '-c', str(compressed)], stdout=subprocess.PIPE) as decoder:
                        for cmd in commands:
                            for start, end in ranges(cmd[1]):
                                if not 0 <= start <= end <= size // 4096:
                                    raise ValueError('Range outside image')
                                if cmd[0] != 'new':
                                    continue  # Newly created sparse file is already zeroed.
                                image.seek(start * 4096)
                                remaining = (end - start) * 4096
                                while remaining:
                                    chunk = decoder.stdout.read(min(remaining, 1024 * 1024))
                                    if not chunk:
                                        raise ValueError('Truncated new-data stream')
                                    image.write(chunk)
                                    remaining -= len(chunk)
                        if decoder.stdout.read(1):
                            raise ValueError('Unused new-data bytes')
                        if decoder.wait() != 0:
                            raise RuntimeError('Brotli decompression failed')
            finally:
                compressed.unlink(missing_ok=True)
        (destination / 'metadata.txt').write_bytes(ota.read('META-INF/com/android/metadata'))
    report = []
    for path in sorted(destination.glob('*.img')):
        digest = hashlib.sha256()
        with path.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(chunk)
        report.append({'file': path.name, 'bytes': path.stat().st_size, 'sha256': digest.hexdigest()})
    (destination / 'images.json').write_text(json.dumps(report, indent=2))
    print('Extraction complete:', destination, flush=True)


if __name__ == '__main__':
    extract(Path(sys.argv[1]), Path(sys.argv[2]))
