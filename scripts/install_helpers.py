"""Shared verified factory-environment helpers for Qua rescue installation."""
import io
import os
import struct
import tarfile
import zlib

FACTORY_OFFSET = 0x220000
FACTORY_BYTES = 0x400000
ENV_BYTES = 0x20000


def read_env(fd):
    data = os.pread(fd, FACTORY_BYTES, FACTORY_OFFSET)
    if len(data) != FACTORY_BYTES:
        raise RuntimeError('short factory read')
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        entries = [member for member in archive.getmembers()
                   if member.name == 'tmp/factory/env.txt']
        if len(entries) != 1 or entries[0].size != ENV_BYTES or not entries[0].isfile():
            raise RuntimeError('unexpected factory environment member')
        offset = FACTORY_OFFSET + entries[0].offset_data
        raw = archive.extractfile(entries[0]).read()
    if len(raw) != ENV_BYTES or struct.unpack_from('<I', raw)[0] != zlib.crc32(raw[4:]):
        raise RuntimeError('environment CRC mismatch')
    end = raw[4:].find(b'\0\0')
    if end < 0:
        raise RuntimeError('environment terminator missing')
    pairs = [part.split(b'=', 1) for part in raw[4:4 + end].split(b'\0')]
    if any(len(pair) != 2 for pair in pairs) or len({pair[0] for pair in pairs}) != len(pairs):
        raise RuntimeError('malformed or duplicate environment key')
    return offset, raw, pairs, end


def encode_env(raw, pairs, old_end):
    prefix = b'\0'.join(key + b'=' + value for key, value in pairs) + b'\0\0'
    tail = raw[4 + old_end + 2:]
    growth = len(prefix) - old_end - 2
    if growth >= 0:
        if len(tail) < growth or any(tail[:growth]):
            raise RuntimeError('environment has insufficient zero padding')
        payload = prefix + tail[growth:]
    else:
        payload = prefix + bytes(-growth) + tail
    if len(payload) != ENV_BYTES - 4:
        raise RuntimeError('environment length changed')
    return struct.pack('<I', zlib.crc32(payload)) + payload


def pwrite_verified(fd, offset, data):
    if os.pwrite(fd, data, offset) != len(data):
        raise RuntimeError('short device write')
    os.fsync(fd)
    if os.pread(fd, len(data), offset) != data:
        raise RuntimeError('device write readback mismatch')


def save_file(path, data):
    with path.open('xb') as stream:
        os.fchmod(stream.fileno(), 0o600)
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
