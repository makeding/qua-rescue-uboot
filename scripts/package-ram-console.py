#!/usr/bin/env python3
"""Package a bounded ARM32 console image and its RAM-only trampoline."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def elf_layout(data):
    offset = struct.unpack_from('<I', data, 32)[0]
    stride, count = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<10I', data, offset + i * stride) for i in range(count)]
    symbols = {}
    end = 0
    for section in sections:
        if section[2] & 2:
            end = max(end, section[3] + section[5])
        if section[1] == 2:
            strings = sections[section[6]]
            table = data[strings[4]:strings[4] + strings[5]]
            for at in range(section[4], section[4] + section[5], section[9]):
                name, value, _, _, _, _ = struct.unpack_from('<IIIBBH', data, at)
                label = table[name:].split(b'\0', 1)[0].decode(errors='replace')
                symbols[label] = value
    wanted = ['__image_copy_start', '__image_copy_end', '__bss_start', '__bss_end__', '__stack_start']
    if any(name not in symbols for name in wanted):
        raise RuntimeError('candidate linker layout symbols missing')
    layout = {name: symbols[name] for name in wanted}
    end = max(end, layout['__stack_start'] + 0x1000)
    if not (layout['__image_copy_start'] == 0x21000 and
            layout['__bss_start'] <= layout['__bss_end__'] <= layout['__stack_start'] and
            end < 0x01000000):
        raise RuntimeError('candidate runtime layout exceeds the low-RAM console allocation')
    layout['runtime_end_with_stack_gd_reserve'] = end
    commands = sorted(name.removeprefix('__u_boot_cmd_') for name in symbols
                      if name.startswith('__u_boot_cmd_') and name not in
                      {'__u_boot_cmd_start', '__u_boot_cmd_end'})
    forbidden_commands = {'factory', 'factory_ro', 'rtkemmc', 'mmc', 'sf', 'erase',
                          'saveenv', 'uart_write', 'bootr', 'goru', 'bcldr', 'b2ndbc'}
    forbidden_writers = {'factory_save', 'saveenv', 'mmc_write', 'do_uart_write'}
    if forbidden_commands.intersection(commands) or forbidden_writers.intersection(symbols):
        raise RuntimeError('persistent storage command/writer remains in RAM console')
    if not {'go', 'reset', 'help', 'version'}.issubset(commands):
        raise RuntimeError('required console command table is missing')
    return layout, commands


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('image', type=Path)
    parser.add_argument('loader', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--staging-address', type=lambda x: int(x, 0), default=0x26000000)
    parser.add_argument('--chain-plan', type=Path)
    args = parser.parse_args()
    elf = args.elf.read_bytes()
    if elf[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', elf, 18)[0] != 40:
        raise RuntimeError('candidate must be an ELF32 little-endian ARM executable')
    if struct.unpack_from('<I', elf, 24)[0] != 0x21000:
        raise RuntimeError('unexpected candidate entry address')
    layout, commands = elf_layout(elf)
    image = args.image.read_bytes()
    if not 0 < len(image) <= 1024 * 1024:
        raise RuntimeError('candidate outside trampoline size limit')
    loader = args.loader.read_bytes()
    if len(loader) > 4096:
        raise RuntimeError('trampoline exceeds reserved 4-KiB budget')
    placeholder = struct.pack('<5I', 0x26000000, 0, 0, 0x21000, 0x51554254)
    if loader.count(placeholder) != 1:
        raise RuntimeError('trampoline parameter block is not unique')
    crc = zlib.crc32(image)
    parameters = struct.pack('<5I', args.staging_address, len(image), crc, 0x21000, 0x51554254)
    loader = loader.replace(placeholder, parameters)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'u-boot-ram-console.bin').write_bytes(image)
    (args.output / 'chain-copy-arm32.bin').write_bytes(loader)
    manifest = {
        'status': 'offline-only; not booted; console candidate, not complete recovery firmware',
        'source_commit': '7145bdb850c3527af9bff67be7a98f0e7daacbff plus local RAM-target edits',
        'architecture': 'AArch32', 'candidate_entry': '0x21000',
        'staging_address': hex(args.staging_address), 'trampoline_address': '0x01f00000',
        'candidate_bytes': len(image), 'candidate_crc32': f'{crc:08x}',
        'candidate_sha256': hashlib.sha256(image).hexdigest(),
        'trampoline_bytes': len(loader), 'trampoline_sha256': hashlib.sha256(loader).hexdigest(),
        'trampoline_crc32': f'{zlib.crc32(loader):08x}',
        'runtime_layout': {name: hex(value) for name, value in layout.items()},
        'console_commands': commands,
        'inherited_board_heap': ['0x07880000', '0x07900000'],
        'inherited_noncache_pool': ['0x07900000', '0x07a00000'],
        'preconditions': ['original first-stage AArch32 U-Boot console',
            'bdinfo confirms RAM ranges and absence of overlap',
            'UART capture armed', 'clean and disable D-cache before trampoline'],
        'persistent_writes': False,
    }
    if args.chain_plan:
        plan = json.loads(args.chain_plan.read_text())
        marker = 'qua_ram_chain=' + plan['nonce']
        if marker.encode() not in image:
            raise RuntimeError('chain trial marker missing from candidate')
        manifest['status'] = 'offline-built; not executed; one-shot chain-to-Linux trial'
        manifest['linux_acceptance_marker'] = marker
        manifest['persistent_writes'] = 'external installer only: trial slot and one-shot environment'
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (args.output / 'uart-steps.txt').write_text(
        'UNEXECUTED RAM TRIAL. Confirm bdinfo/RAM ranges and arm UART capture first.\n'
        'In original FIRST-STAGE AArch32 U-Boot, not AArch64 second stage:\n'
        f'loady {args.staging_address:#x}\n'
        '  Send u-boot-ram-console.bin by YMODEM.\n'
        'loady 0x01f00000\n'
        '  Send chain-copy-arm32.bin by YMODEM.\n'
        'dcache off\nicache off\n'
        f'crc32 {args.staging_address:#x} 0x{len(image):x}\n'
        f'  Require {crc:08x}; stop on mismatch.\n'
        f'crc32 0x01f00000 0x{len(loader):x}\n'
        f'  Require {zlib.crc32(loader):08x}; stop on mismatch.\n'
        'go 0x01f00000\n'
        'Expected: Qua RAM rescue console banner and interactive prompt.\n'
        'A physical reset/power cycle reloads the unchanged stored bootcode.\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
