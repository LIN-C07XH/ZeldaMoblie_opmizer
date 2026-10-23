"""Extend the game's icon composite with its existing text face; no font binaries are distributed.

FCPX 9.3: header + relative root pointer, 32-byte composite root, 40-byte
face records. main+0x10e4000 consumes this format in the verified v1.4.3.
Offsets in face records are relative to the root, not the file header.
"""
import hashlib
import struct

from extract_shaders import decompress, sarc, zstd

FONT_PACKS = {
    '/Font/Font.Nin_NX_NVN.bfarc.zs': '4244acc7b9ff893c2c8aa0256d11e0a06351efd9975f0ef576ee29fc894d2703',
    '/Font/Font_CNzh.Nin_NX_NVN.bfarc.zs': '4538c90a64f53371f49fcff140d9ec4ad5b11dffad04a2416186c224380f0f12',
    '/Font/Font_KRko.Nin_NX_NVN.bfarc.zs': '636c871ee766c9a73e2081e644b37fc218b18ca251ec79c4189e5c78a94a50a6',
    '/Font/Font_TWzh.Nin_NX_NVN.bfarc.zs': 'c562cc4375c83c60a3a2523846fc369a579bac2e99b3584cb9201f8f6b1b908e',
}
LAYOUT_PACK = '/UI/LayoutArchive/Common.Product.140.Nin_NX_NVN.blarc.zs'
LAYOUT_SHA = '613af5ec5547e27a025d0b603ef91fa48912c5109c1fffb7917276d1fd98d424'
EXTERNAL_SHA = '8bf9108090bc9e20967fab9f48049d9681037f08863c215cd773b2be9bc01cae'
FONT_MEMBER = 'fcpx/External_00.bfcpx'
TEXT_SCALE = 0.4  # nominal 75 -> 30, before the original pane's scale
BUFFER_BYTES = 64  # UTF-16 including NUL; original icon buffers can be only four bytes

def replace_sarc(data, replacements):
    """Preserve names, hash ordering and each member's original alignment."""
    files = sarc(data)  # validate bounds and unique names before writing
    if not replacements.keys() <= files.keys():
        raise ValueError('replacement SARC member missing')
    count = struct.unpack_from('<H', data, 26)[0]
    base = struct.unpack_from('<I', data, 12)[0]
    result = bytearray(data[:base])
    for i, (name, value) in enumerate(files.items()):
        value = replacements.get(name, value)
        original = base + struct.unpack_from('<I', data, 32 + i * 16 + 8)[0]
        alignment = max(4, min(0x1000, original & -original))
        position = (len(result) + alignment - 1) & -alignment
        result.extend(bytes(position - len(result)))
        start = len(result) - base
        result.extend(value)
        struct.pack_into('<II', result, 32 + i * 16 + 8, start, len(result) - base)
    assert len(files) == count
    struct.pack_into('<I', result, 8, len(result))
    expected = files | replacements
    if sarc(result) != expected:
        raise ValueError('derived SARC failed round-trip')
    return bytes(result)


def keyboard_composite(external, text):
    if hashlib.sha256(external).hexdigest() != EXTERNAL_SHA:
        raise ValueError('unsupported External font composite')
    # CaptionRuby's default face is Rodin M, present in each of the four font packs.
    # Reuse its face metrics, with an explicit ASCII range. External remains the
    # unrestricted final face, with every original icon metric untouched.
    if (text[:12] != external[:12] or struct.unpack_from('<II', text, 0x24) != (2, 32)
            or struct.unpack_from('<I', text, 0x80)[0] != 0):
        raise ValueError('unsupported text font composite')
    text_face = bytearray(text[0x60:0x88])
    name_start = 0x18 + struct.unpack_from('<I', text_face, 12)[0]
    name_end = text.find(b'\0', name_start)
    text_name = text[name_start:name_end + 1]
    if text_name != b'ninP_RodinNTLG-M_DH_003_subset.bfotf\0':
        raise ValueError('unexpected keyboard text face')
    struct.pack_into('<f', text_face, 4, TEXT_SCALE)
    struct.pack_into('<f', text_face, 20, TEXT_SCALE)
    # Two records followed by one [first,last] range and the two font names.
    root = 0x18
    ranges = 0x88
    text_name_start = ranges + 8
    icon_name_start = (text_name_start + len(text_name) + 3) & ~3
    struct.pack_into('<I', text_face, 12, text_name_start - root)
    struct.pack_into('<II', text_face, 32, 1, ranges - root)
    icon_face = bytearray(external[0x38:0x60])
    struct.pack_into('<I', icon_face, 12, icon_name_start - root)
    struct.pack_into('<I', icon_face, 36, ranges - root)  # no ranges; pointer not read
    result = bytearray(external[:0x38])
    struct.pack_into('<I', result, 0x24, 2)
    result.extend(text_face + icon_face + struct.pack('<II', 0x20, 0x7e))
    result.extend(text_name)
    result.extend(bytes(icon_name_start - len(result)))
    result.extend(b'External.bfotf\0')
    result.extend(bytes(-len(result) % 4))
    struct.pack_into('<I', result, 12, len(result))
    return bytes(result)


def expand_icon_buffers(data):
    """Reserve longer strings only in TextBoxes that use External_00.fcpx."""
    if data[:6] != b'FLYT\xff\xfe' or struct.unpack_from('<I', data, 12)[0] != len(data):
        raise ValueError('unsupported UI layout')
    position = struct.unpack_from('<H', data, 6)[0]
    blocks, fonts = [], []
    while position < len(data):
        magic, length = struct.unpack_from('<4sI', data, position)
        if length < 8 or position + length > len(data):
            raise ValueError('invalid layout block')
        blocks.append((position, magic, length))
        if magic == b'fnl1':
            count = struct.unpack_from('<H', data, position + 8)[0]
            table = position + 12
            for i in range(count):
                start = table + struct.unpack_from('<I', data, table + 4 * i)[0]
                end = data.find(b'\0', start, position + length)
                if start < table + count * 4 or end < start:
                    raise ValueError('invalid layout font name')
                fonts.append(data[start:end].decode())
        position += length
    if len(blocks) != struct.unpack_from('<H', data, 16)[0]:
        raise ValueError('invalid layout block count')
    result, changed = bytearray(data), 0
    for start, magic, length in blocks:
        if magic != b'txt1':
            continue
        if length < 0xa8:
            raise ValueError('truncated TextBox')
        buffer, initial, _, font = struct.unpack_from('<4H', data, start + 84)
        if font >= len(fonts):
            raise ValueError('invalid TextBox font index')
        if fonts[font] == 'External_00.fcpx' and buffer < BUFFER_BYTES:
            if initial > buffer or buffer % 2:
                raise ValueError('invalid original icon text buffer')
            struct.pack_into('<H', result, start + 84, BUFFER_BYTES)
            changed += 1
    return bytes(result), changed


def patch_font_pack(packed, dictionaries):
    data = decompress(packed, dictionaries)
    files = sarc(data)
    replacement = keyboard_composite(files[FONT_MEMBER], files['fcpx/CaptionRuby_00.bfcpx'])
    text_name = 'scft/ninP_RodinNTLG-M_DH_003_subset.bfotf'
    if text_name not in files:
        raise ValueError('keyboard text face absent from font pack')
    return zstd.compress(replace_sarc(data, {FONT_MEMBER: replacement}), level=19)


def patch_layout_pack(packed, dictionaries):
    data = decompress(packed, dictionaries)
    changes, boxes = {}, 0
    for name, value in sarc(data).items():
        if name.endswith('.bflyt'):
            replacement, count = expand_icon_buffers(value)
            if count:
                changes[name] = replacement
                boxes += count
    if not changes or not boxes:
        raise ValueError('no icon TextBoxes found in supported UI pack')
    return zstd.compress(replace_sarc(data, changes), level=19), len(changes), boxes


def prepare_ui_assets(rom, dictionaries):
    result = {}
    for path, expected in FONT_PACKS.items():
        packed = rom.file(path)
        if hashlib.sha256(packed).hexdigest() != expected:
            raise ValueError(f'unsupported v1.4.3 font pack: {path}')
        result[path] = patch_font_pack(packed, dictionaries)
    source = rom.file(LAYOUT_PACK)
    if hashlib.sha256(source).hexdigest() != LAYOUT_SHA:
        raise ValueError('unsupported v1.4.3 common UI pack')
    packed, layouts, boxes = patch_layout_pack(source, dictionaries)
    result[LAYOUT_PACK] = packed
    print(f'Keyboard UI: four font composites; {boxes} icon text buffers in {layouts} layouts')
    return result
