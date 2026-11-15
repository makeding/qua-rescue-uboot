#!/usr/bin/env python3
"""Assemble a device-private install bundle from an offline rescue build."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=Path)
    parser.add_argument('package', type=Path,
                        help='package directory made by build-rescue.sh')
    parser.add_argument('environment_baseline', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    plan = json.loads(args.profile.read_text())
    package = args.package
    manifest = json.loads((package / 'manifest.json').read_text())
    slot = plan['trial_slot']
    required = {'candidate.bin': package / 'u-boot-ram-console.bin',
                'u-boot.elf': package.parent / 'build' / 'u-boot',
                'chain-copy-arm32.bin': package / 'chain-copy-arm32.bin',
                'chain-to-arm64.bin': package.parent / 'chain-to-arm64.bin',
                'environment-baseline.bin': args.environment_baseline}
    for name, path in required.items():
        if not path.is_file():
            raise RuntimeError('missing input ' + name + ': ' + str(path))
    output = args.output
    output.mkdir(parents=True, exist_ok=False)
    for name, path in required.items():
        shutil.copyfile(path, output / name)
    script_dir = Path(__file__).resolve().parent
    for name in ['install-usb-chain.py', 'install_helpers.py']:
        shutil.copyfile(script_dir / name, output / name)
    shutil.copyfile(args.profile, output / 'plan.json')
    payload = bytearray(slot['sectors'] * 512)
    items = [('candidate.bin', slot['start_sector'], slot['candidate_max_sectors'] * 512),
             ('environment-baseline.bin', slot['base_env_sector'], 0x20000),
             ('chain-copy-arm32.bin', slot['first_loader_sector'], 512),
             ('chain-to-arm64.bin', slot['second_loader_sector'], 512)]
    for name, sector, limit in items:
        data = (output / name).read_bytes()
        if len(data) > limit:
            raise RuntimeError(name + ' exceeds allocated slot')
        offset = (sector - slot['start_sector']) * 512
        if offset < 0 or offset + len(data) > len(payload):
            raise RuntimeError(name + ' lies outside trial slot')
        payload[offset:offset + len(data)] = data
    (output / 'trial-slot.bin').write_bytes(payload)
    manifest.update(status='offline bundle; install only after device-specific review',
                    trial_slot_sha256=sha256(output / 'trial-slot.bin'),
                    original_delay_seconds=2, new_delay_seconds=3)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'bundle': str(output), 'trial_slot_sha256': manifest['trial_slot_sha256']}, indent=2))


if __name__ == '__main__':
    main()
