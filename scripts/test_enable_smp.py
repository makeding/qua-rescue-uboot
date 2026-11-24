import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

import install_helpers as h

spec = importlib.util.spec_from_file_location('enable_smp', Path(__file__).with_name('enable-smp.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SMPMigrationTest(unittest.TestCase):
    def test_preserves_other_fields_and_crc(self):
        pairs = [(b'bootcmd', b'run existing_chain'), (b'bootargs', b'console=ttyS0 maxcpus=1 root=/dev/mmcblk0p2'), (b'device-data', b'unchanged')]
        payload = b'\0'.join(key + b'=' + value for key, value in pairs) + b'\0\0'
        payload += bytes(h.ENV_BYTES - 4 - len(payload))
        raw = struct.pack('<I', zlib.crc32(payload)) + payload
        original, end = h.decode_env(raw)
        result = h.encode_env(raw, module.remove_single_cpu_limit(original), end)
        actual = dict(h.decode_env(result)[0])
        self.assertEqual(actual, {b'bootcmd': pairs[0][1], b'bootargs': b'console=ttyS0 root=/dev/mmcblk0p2', b'device-data': b'unchanged'})
        self.assertEqual(len(result), h.ENV_BYTES)

    def test_absent_or_already_clear_arguments(self):
        for pairs in ([(b'bootcmd', b'boot')], [(b'bootargs', b'console=ttyS0 maxcpus=4')]):
            self.assertIsNone(module.remove_single_cpu_limit(pairs))

    def test_rejects_duplicate_limit(self):
        with self.assertRaises(RuntimeError):
            module.remove_single_cpu_limit([(b'bootargs', b'maxcpus=1 maxcpus=1')])


if __name__ == '__main__':
    unittest.main()
