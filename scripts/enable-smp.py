#!/usr/bin/env python3
"""Remove maxcpus=1 from Qua factory environments and the imported rescue copy."""
import argparse
import os
from pathlib import Path

import install_helpers as h

def remove_single_cpu_limit(pairs):
    old = dict(pairs).get(b'bootargs')
    if old is None:
        return None
    tokens = old.split(b' ')
    if tokens.count(b'maxcpus=1') > 1:
        raise RuntimeError('duplicate diagnostic maxcpus=1 arguments')
    if b'maxcpus=1' not in tokens:
        return None
    new = b' '.join(token for token in tokens if token != b'maxcpus=1')
    return [(key, new if key == b'bootargs' else value) for key, value in pairs]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup_directory', type=Path)
    args = parser.parse_args()
    args.backup_directory.mkdir(mode=0o700)
    fd = os.open('/dev/mmcblk0', os.O_RDWR)
    try:
        updates = []
        environments = []
        for bank in (0x220000, 0x420000):
            h.FACTORY_OFFSET = bank
            offset, raw, pairs, end = h.read_env(fd)
            environments.append((f'factory-{bank:x}', offset, raw, pairs, end))
        offset = 211184 * 512
        raw = os.pread(fd, h.ENV_BYTES, offset)
        pairs, end = h.decode_env(raw)
        environments.append(('rescue-import', offset, raw, pairs, end))
        for name, offset, raw, pairs, end in environments:
            desired = remove_single_cpu_limit(pairs)
            if desired is None:
                print(f'SMP_BOOTARGS_ALREADY_CLEAR environment={name}', flush=True)
                continue
            data = h.encode_env(raw, desired, end)
            h.save_file(args.backup_directory / f'environment-{name}.bin', raw)
            updates.append((name, offset, data, desired))
        for name, offset, data, desired in updates:
            h.pwrite_verified(fd, offset, data)
            if h.decode_env(os.pread(fd, h.ENV_BYTES, offset))[0] != [list(pair) for pair in desired]:
                raise RuntimeError('factory environment readback differs')
            print(f'SMP_BOOTARGS_VERIFIED environment={name}', flush=True)
    finally:
        os.close(fd)


if __name__ == '__main__':
    main()
