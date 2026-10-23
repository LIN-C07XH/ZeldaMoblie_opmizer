import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'script'))
import prepare_ui_fonts as fonts


def composites():
    # Format-only fixtures; no game font outlines or resource binaries.
    icon = bytearray(0x70)
    icon[:12] = b'FCPX\xff\xfe\x14\0\0\0\x03\x09'
    struct.pack_into('<IIIIfIIIIII', icon, 12, len(icon), 1, 4, 1, 75, 0, 1, 32, 63, 0, 0)
    struct.pack_into('<fffI', icon, 0x38, 0, 1, 0, 72)
    struct.pack_into('<Hhf4HII', icon, 0x48, 0, -6, 1, 0, 0, 0, 0, 0, 72)
    icon[0x60:] = b'External.bfotf\0\0'
    text = bytearray(0xd0)
    text[:0x38] = icon[:0x38]
    struct.pack_into('<I', text, 12, len(text))
    struct.pack_into('<I', text, 0x24, 2)
    text[0x60:0x88] = icon[0x38:0x60]
    struct.pack_into('<I', text, 0x6c, 0x90)
    text[0xa8:] = b'ninP_RodinNTLG-M_DH_003_subset.bfotf\0\0\0\0\0\0\0'
    return bytes(icon), bytes(text)


def layout():
    names = b'External_00.fcpx\0NormalS_00.fcpx\0'
    fnl = bytearray(struct.pack('<4sIHHII', b'fnl1', 20 + len(names), 2, 0, 8, 25) + names)
    blocks = [fnl]
    for font in (0, 1):
        box = bytearray(0xb0)
        struct.pack_into('<4sI', box, 0, b'txt1', len(box))
        struct.pack_into('<4H', box, 84, 4, 4, 0, font)
        blocks.append(box)
    result = bytearray(struct.pack('<4sHHIIHH', b'FLYT', 0xfeff, 20, 0x9000000, 20 + sum(map(len, blocks)), 3, 0))
    return bytes(result + b''.join(blocks))


class UiFontTests(unittest.TestCase):
    def test_layout_pack_preserves_do_command_and_animation_resources(self):
        # DoCommand's native animation setup must use its original layout.
        # Only the existing External font text boxes are expanded.
        icons = layout()
        action = bytearray(icons)
        action[20 + 20:20 + 28] = b'NormalS_'
        files = {'blyt/DoCommand_00.bflyt': bytes(action),
                 'blyt/PaGuideKeySimple_00.bflyt': icons,
                 'anim/DoCommand_00_In.bflan': b'original animation'}
        with patch.object(fonts, 'decompress', return_value=b'archive'), \
             patch.object(fonts, 'sarc', return_value=files), \
             patch.object(fonts, 'replace_sarc', return_value=b'patched') as replace, \
             patch.object(fonts.zstd, 'compress', return_value=b'compressed'):
            packed, layouts, boxes = fonts.patch_layout_pack(b'packed', {})
        self.assertEqual((packed, layouts, boxes), (b'compressed', 1, 1))
        changes = replace.call_args.args[1]
        self.assertEqual(set(changes), {'blyt/PaGuideKeySimple_00.bflyt'})
        self.assertEqual(fonts.expand_icon_buffers(files['blyt/DoCommand_00.bflyt']),
                         (files['blyt/DoCommand_00.bflyt'], 0))

    def test_ascii_face_and_native_icon_metrics(self):
        icon, text = composites()
        with patch.object(fonts, 'EXTERNAL_SHA', hashlib.sha256(icon).hexdigest()):
            result = fonts.keyboard_composite(icon, text)
        self.assertEqual(struct.unpack_from('<I', result, 12)[0], len(result))
        self.assertEqual(result[0x18:0x24], icon[0x18:0x24])  # keep 75-point native root
        self.assertEqual(struct.unpack_from('<I', result, 0x24)[0], 2)
        first, last = result[0x38:0x60], result[0x60:0x88]
        self.assertAlmostEqual(struct.unpack_from('<f', first, 4)[0], .4)
        self.assertAlmostEqual(struct.unpack_from('<f', first, 20)[0], .4)
        count, pointer = struct.unpack_from('<II', first, 32)
        self.assertEqual(count, 1)
        self.assertEqual(struct.unpack_from('<II', result, 0x18 + pointer), (32, 126))
        for record, name in [(first, b'ninP_RodinNTLG-M_DH_003_subset.bfotf'), (last, b'External.bfotf')]:
            start = 0x18 + struct.unpack_from('<I', record, 12)[0]
            self.assertEqual(result[start:result.index(0, start)], name)
        native = bytearray(last)
        native[12:16] = icon[0x44:0x48]
        native[36:40] = icon[0x5c:0x60]
        self.assertEqual(native, icon[0x38:0x60])
        with self.assertRaises(ValueError):
            fonts.keyboard_composite(icon[:-1] + b'X', text)

    def test_long_labels_only_expand_icon_capacity(self):
        original = layout()
        result, count = fonts.expand_icon_buffers(original)
        self.assertEqual(count, 1)
        first = 20 + struct.unpack_from('<I', original, 24)[0]
        self.assertEqual(struct.unpack_from('<H', result, first + 84)[0], 64)
        reverted = bytearray(result)
        struct.pack_into('<H', reverted, first + 84, 4)
        self.assertEqual(reverted, original)  # position, font, initial text and other pane unchanged
        self.assertEqual(fonts.expand_icon_buffers(result), (result, 0))


if __name__ == '__main__':
    unittest.main()
