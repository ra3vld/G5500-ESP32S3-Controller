from pathlib import Path
import sys
import tempfile
import struct
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_firmware_size import check


class FirmwareSizeTests(unittest.TestCase):
    def test_compiled_partition_table(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);image=root/'firmware.bin';table=root/'partitions.bin'
            def entry(kind,size):return struct.pack('<HBBII16sI',0x50aa,kind,0,0x10000,size,b'app',0)
            table.write_bytes(entry(1,32)+entry(0,1024)+entry(0,768)+b'\xff'*32)
            image.write_bytes(b'\0'*768)
            self.assertEqual(check(image,table),(768,768))
            image.write_bytes(b'\0'*769)
            with self.assertRaisesRegex(ValueError,'exceeds'):check(image,table)
            table.write_bytes(b'\xff'*32)
            with self.assertRaisesRegex(ValueError,'No application'):check(image,table)

    def test_actual_bin_and_smallest_app_slot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image, table = root/'firmware.bin', root/'partitions.csv'
            table.write_text('# name,type,subtype,offset,size\n'
                             'nvs,data,nvs,0x9000,0x5000\n'
                             'app0,app,ota_0,0x10000,3M\n'
                             'app1,app,ota_1,,2048K\n')
            with image.open('wb') as stream:
                stream.truncate(2*1024*1024)
            self.assertEqual(check(image, table), (2*1024*1024, 2*1024*1024))
            with image.open('ab') as stream:
                stream.write(b'\0')
            with self.assertRaisesRegex(ValueError, 'exceeds'):
                check(image, table)
            table.write_text('factory,app,factory,0x10000,0x300000\n')
            self.assertEqual(check(image, table)[1], 3*1024*1024)


if __name__ == '__main__':
    unittest.main()
