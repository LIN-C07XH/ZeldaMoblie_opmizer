"""TotK 1.4.3 native control guide events and keyboard labels.

GuideKeyIcon's normal and option-swapped keys converge at 0xba2a18.
The output is a borrowed UTF-16 span {pointer, int32 length, int64 metadata}.
UIScreen's per-frame update receives a live screen in x0. The host filters
the two verified vtables before inspecting CtrlGuide/DoCommand fields.
"""
import hashlib
import struct
from game_timing import BUILD_ID, STATE_OFFSET

UI_STATE_OFFSET = STATE_OFFSET + 0x80
MAGIC = b'ZUI143\0\0'
ICON_SITE = 0xba2a18
FRAME_SITE = 0x217b838
DRAW_SITE = 0x1485f0
ICON_RETURN = 0xba2a58
ORIGINAL = {
    ICON_SITE: bytes.fromhex('885b01d0'),  # adrp x8,0x3714000 (relocated on replay)
    FRAME_SITE: bytes.fromhex('ff4301d1'), # sub sp,sp,#0x50
    DRAW_SITE: bytes.fromhex('08604139'),  # ldrb w8,[x0,#0x58]
}
ANCHORS = {
    0x357fbe0: struct.pack('<Q', DRAW_SITE), # Picture::Draw inherited from Pane
    0x357f8e0: struct.pack('<Q', DRAW_SITE), # TextBox::Draw inherited from Pane
    0x1485f4: bytes.fromhex('291080523f01286a'), # visible + calculated-matrix gate
    0x10d76e8: bytes.fromhex('021c803d'), # global matrix at Pane+0x70
    0x5930e0: bytes.fromhex('6a6a0139'), # calculated global alpha at Pane+0x5a
    0xba42c0: bytes.fromhex('ff0301d1'),         # localized message lookup
    0xba2a1c: bytes.fromhex('08212c91084d218b089140f9'), # key-name lookup
    ICON_RETURN: bytes.fromhex('f44f44a9fd7b43a9ff430191c0035fd6'),
    0xc28b8c: bytes.fromhex('280040f9080000f9280840b9080800b9'), # span copy
    0x37267e0: struct.pack('<Q', FRAME_SITE),  # CtrlGuide update vtable slot
    0x3726e68: struct.pack('<Q', FRAME_SITE),  # DoCommand update vtable slot
    0x3726830: struct.pack('<Q', 0x2c81d96),   # LayoutMsg/CtrlGuide_00
    0x3726eb8: struct.pack('<Q', 0x2c924e1),   # LayoutMsg/DoCommand_00
    0x2147df0: bytes.fromhex('08e040f9680200b4'), # native guide rebuild
    0x2147eb0: bytes.fromhex('080040b91f01146b'), # compare GuideType with requested type
    0x2147d2c: bytes.fromhex('60de00f9'),         # active guide types, screen+0x1b8
    0x4cb73c: bytes.fromhex('00dd00f9'),          # native removal of the same bits
    0x37172d0: struct.pack('<Q', 0x3716fb0),      # GuideType enumeration
    0x3717310: struct.pack('<Q', 0x3717001),      # Climb, index 8
    0x3717328: struct.pack('<Q', 0x3717020),      # Swim, index 11
    0x21487c8: bytes.fromhex('a00240f9'),         # native blackboard receiver
    0x2148850: bytes.fromhex('8899a297'),         # native bool getter
    0x21488ac: bytes.fromhex('77dea097'),         # native enum getter
    0x2148b54: bytes.fromhex('988b2594'),         # native string getter
    0x395dff0: struct.pack('<Q', 0x3ad66b0),      # blackboard singleton GOT
    0x3cffe8: bytes.fromhex('62020034081046f9280200b4092c4cb9'),
    0x980288: bytes.fromhex('fd7bbca9f70b00f9f65702a9f44f03a9'),
    0x2aab9b4: bytes.fromhex('fd7bbda9f65701a9f44f02a9fd030091'),
}


def apply_native_ui_hook(env):
    if env['BUILD_ID'] != BUILD_ID:
        raise ValueError('Native UI requires the verified v1.4.3 main Build ID')
    image, emit, here = env['IMG'], env['emit'], env['here']
    if UI_STATE_OFFSET + 24 > env['PATCH_OFF'] or image[STATE_OFFSET:STATE_OFFSET + 8] != b'ZFPS143\0':
        raise ValueError('Native UI needs the timing state page')
    for offset, expected in {**ORIGINAL, **ANCHORS}.items():
        if image[offset:offset + len(expected)] != expected:
            raise ValueError(f'Native UI instruction mismatch at {offset:#x}')
    image[UI_STATE_OFFSET:UI_STATE_OFFSET + 24] = MAGIC + struct.pack('<IIQ', 4, 0, 0)
    writes, routines = [], {}

    def pair(base, rt, rt2, offset, scale):
        return base | ((offset // scale & 127) << 15) | (rt2 << 10) | (31 << 5) | rt

    def restore_gpr():
        emit(0xf9404bfe)
        for reg in range(0, 18, 2): emit(pair(0xa9400000, reg, reg + 1, reg * 8, 8))
        emit(0x910003ff | (0x300 << 10))

    for kind, site in [(1, ICON_SITE), (2, FRAME_SITE), (3, DRAW_SITE)]:
        start = here()
        if kind == 3:
            # Read only a live Pane::Draw argument. Preserve flags and scratch
            # registers; ordinary panes never enter the host callback.
            emit(0xa9bf47f0) # stp x16,x17,[sp,#-16]!
            emit(0xb940b010) # first four bytes of the pane name, +0xb0
            matches = []
            for prefix in (b'N_In', b'P_Ke', b'P_Ca', b'P_LS', b'P_ZL', b'T_A_', b'T_B_', b'T_X_', b'T_Y_', b'T_Pl'):
                word = int.from_bytes(prefix, 'little')
                emit(0x52800011 | ((word & 0xffff) << 5))
                emit(0x72a00011 | ((word >> 16) << 5))
                emit(0x4a110211) # eor w17,w16,w17, NZCV unchanged
                matches.append(here()); emit(0)
            emit(0xa8c147f0)
            emit(struct.unpack('<I', ORIGINAL[site])[0]); emit(env['B'](here(), site + 4))
            accept = here(); emit(0xa8c147f0)
            for branch in matches:
                struct.pack_into('<I', env['patch'], branch - env['PATCH_OFF'],
                                 0x34000011 | (((accept - branch) // 4) << 5))
        emit(0xd10003ff | (0x300 << 10))
        for reg in range(0, 18, 2): emit(pair(0xa9000000, reg, reg + 1, reg * 8, 8))
        emit(0xf9004bfe)
        for mrs, slot in [(0xd53b4211, 0x98), (0xd53b4411, 0xa0), (0xd53b4431, 0xa8)]:
            emit(mrs); emit(0xf90003f1 | ((slot // 8) << 10))
        for reg in range(0, 32, 2): emit(pair(0xad000000, reg, reg + 1, 0xc0 + reg * 16, 16))
        emit(0xf9005bff) # callback result defaults to zero, sp+0xb0
        emit(env['ADRP'](16, here(), UI_STATE_OFFSET))
        emit(0x91000210 | ((UI_STATE_OFFSET & 0xfff) << 10))
        emit(0xf9400a10)
        skip = here(); emit(0)
        if kind == 1:
            emit(0xaa0103e2) # x2=effective key (option swap already resolved)
            emit(0xaa1303e1) # x1=native output span, x19
        else:
            emit(0xaa0003e1) # x1=live UIScreen
            emit(0xaa1f03e2)
        emit(0x52800000 | (kind << 5)) # w0=callback kind
        emit(0xd63f0200)
        emit(0xf9005be0) # result at sp+0xb0
        restore = here()
        struct.pack_into('<I', env['patch'], skip - env['PATCH_OFF'],
                         0xb4000010 | (((restore - skip) // 4) << 5))
        for reg in range(0, 32, 2): emit(pair(0xad400000, reg, reg + 1, 0xc0 + reg * 16, 16))
        for msr, slot in [(0xd51b4211, 0x98), (0xd51b4411, 0xa0), (0xd51b4431, 0xa8)]:
            emit(0xf94003f1 | ((slot // 8) << 10)); emit(msr)
        if kind != 2:
            emit(0xf9405bf0) # handled?
            handled = here(); emit(0)
        restore_gpr()
        if kind == 1: emit(env['ADRP'](8, here(), 0x3714000))
        else: emit(struct.unpack('<I', ORIGINAL[site])[0])
        emit(env['B'](here(), site + 4))
        if kind != 2:
            success = here()
            struct.pack_into('<I', env['patch'], handled - env['PATCH_OFF'],
                             0xb5000010 | (((success - handled) // 4) << 5))
            if kind == 1:
                emit(0x52800020) # native bool success, w0=1
                emit(0xf90003e0) # override the saved x0 only
            restore_gpr()
            emit(env['B'](here(), ICON_RETURN) if kind == 1 else 0xd65f03c0)
        replacement = struct.pack('<I', env['B'](site, start))
        image[site:site + 4] = replacement
        code = bytes(env['patch'][start - env['PATCH_OFF']:])
        writes.append(dict(offset=site, original=ORIGINAL[site].hex(), replacement=replacement.hex()))
        routines[str(kind)] = dict(offset=start, size=len(code), sha256=hashlib.sha256(code).hexdigest())
    return dict(abi=4, state_offset=UI_STATE_OFFSET, writes=writes, routines=routines)
