"""TotK 1.4.3 Chase, ability selection and scoped UltraHand orbit input before native pose/collision.

UltraCam's CameraControl signature identifies main+0x62883c, a late matrix
builder shared by scene cameras. It is intentionally left untouched here.
ExecutePlayerCameraChase's vtable (main+0x3790f58, slot 0x120) instead leads
through 0x18e414 to the orbit-to-pose calculation at 0x18f100. Its stack holds
{distance, elevation_degrees, azimuth_degrees} at +0x40. Native code subsequently
builds the requested pose (+0x338/+0x344); the native camera collision pipeline remains intact. No game addresses or writable camera pointers survive the callback.
"""
import hashlib
import struct
from game_timing import BUILD_ID, STATE_OFFSET

CAMERA_STATE_OFFSET = STATE_OFFSET + 0x40
MAGIC = b'CAM5\0\0\0\0'
RESOLUTION_SITE = 0x3dcbe0
RESOLUTION_ORIGINAL = bytes.fromhex('a8273f29')  # stp w8,w9,[x29,#-8]: final size after native clamps
ULTRAHAND_SITE = 0x23d7b70
ULTRAHAND_ORIGINAL = bytes.fromhex('609a40bd')  # ldr s0,[x19,#0x98]
ULTRAHAND_VTABLE = 0x3793ec0
SELECTION_SITE = 0x23d380c
SELECTION_ORIGINAL = bytes.fromhex('a06301d1')  # sub x0,x29,#0x58
SELECTION_VTABLE = 0x3793868  # ExecutePlayerCameraSpecialPower; before grabbing
SITE = 0x18f100
LIFECYCLE_SITE = 0x8749e8
LIFECYCLE_ORIGINAL = bytes.fromhex('890640f9')  # ldr x9,[x20,#8]
ORIGINAL = bytes.fromhex('08c15839')  # ldrb w8,[x8,#0x630]
# Independent anchors reject a coincidental byte match or changed call chain.
ANCHORS = {
    0x3de5f8: bytes.fromhex('091cd5393f0500710828d539'),
    0x3dcbc0: bytes.fromhex('a8274329a12300d1e00313aae2031f2a9f02086b88b2881abf02096ba9b2891a'),
    0x3dcbe4: bytes.fromhex('c2a61394'),
    0x8c66ec: bytes.fromhex('08400091890180524a20299b2c344029'),
    0x3de614: bytes.fromhex('894b01f029210d91287968f8'),
    0x2d51358: struct.pack('<6I', 1280, 720, 1600, 900, 1920, 1080),
    0x872a44: bytes.fromhex('081542f90a81049108c105912aa10ca9'),
    0xc9300: bytes.fromhex('00b040bdc0035fd6'),  # PerspectiveProjection::getAspect
    0x3902438: struct.pack('<Q', 0x27d5438),
    0x3793988: struct.pack('<Q', 0x23d2d40),  # SpecialPower vtable+0x120
    0x23d2d40: bytes.fromhex('ff0305d1'),  # sub sp,sp,#0x140
    0x23d2d6c: bytes.fromhex('fd830391'),  # add x29,sp,#0xe0
    0x23d299c: bytes.fromhex('61ae01bd60b201bd'),  # native latitude limits +1ac/+1b0
    0x23d3810: bytes.fromhex('b776d397'),  # native polar -> vector
    0x23d3824: bytes.fromhex('23baff97'),  # requested Eye = native target + vector
    0x23d43fc: bytes.fromhex('204500d000ec2191c0035fd6'),  # getName: SpecialPower
    0xc6f6e0: bytes.fromhex('080440f9080540f900fd40f9c0035fd6'),
    0x1951320: bytes.fromhex('081441f9680000b400d941f9c0035fd6e0031faac0035fd6'),

    0x3793fe0: bytes.fromhex('748e3d0200000000'),
    0x23d8e74: bytes.fromhex('b3f8ff17'),
    0x23d7b6c: bytes.fromhex('00013fd6'),
    0x23d7c94: bytes.fromhex('9665d397'),
    0x23d7de8: bytes.fromhex('4165d397'),
    0x18a4c0c: bytes.fromhex('fd7bbfa9fd030091c80501d0085d44f9'),
    0x18a9544: bytes.fromhex('fd7bbea9f44f01a9fd03009108544239'),
    0x18a9598: bytes.fromhex('fd7bbea9f44f01a9fd03009108584239'),

    # PlayerCamera native attention state = component+0x490; +0x4dc
    # clears each frame and only sets bit 0 after validating a lock-on client.
    0x87532c: bytes.fromhex('68421291'),
    0x8759f8: bytes.fromhex('1e030094'),
    0x87669c: bytes.fromhex('1fdc04b9'),
    0x876d70: bytes.fromhex('68de44b90901003269de04b9'),
    0x18e414: bytes.fromhex('ef3bb66d'),  # stp d15,d14,[sp,#-0xa0]!
    0x18f108: bytes.fromhex('ec4740bd'),          # ldr s12,[sp,#0x44]
    0x18f170: bytes.fromhex('003903bd'),          # str s0,[x8,#0x338]
    0x3791078: struct.pack('<Q', 0x18e414),       # Chase vtable +0x120
}


def apply_camera_hook(env):
    if env['BUILD_ID'] != BUILD_ID:
        raise ValueError('Camera hook requires the verified v1.4.3 main Build ID')
    image, emit, here = env['IMG'], env['emit'], env['here']
    if CAMERA_STATE_OFFSET + 0x30 > env['PATCH_OFF'] or image[STATE_OFFSET:STATE_OFFSET + 8] != b'ZFPS143\0':
        raise ValueError('Camera hook needs the timing state page')
    for offset, expected in {SITE: ORIGINAL, LIFECYCLE_SITE: LIFECYCLE_ORIGINAL, ULTRAHAND_SITE: ULTRAHAND_ORIGINAL, SELECTION_SITE: SELECTION_ORIGINAL, RESOLUTION_SITE: RESOLUTION_ORIGINAL, **ANCHORS}.items():
        if image[offset:offset + len(expected)] != expected:
            raise ValueError(f'Camera hook instruction mismatch at {offset:#x}')
    image[CAMERA_STATE_OFFSET:CAMERA_STATE_OFFSET + 0x30] = MAGIC + struct.pack('<IIQQQQ', 5, 0, 0, 0, 0, 0)
    def pair(base, rt, rt2, offset, scale):
        return base | ((offset // scale & 127) << 15) | (rt2 << 10) | (31 << 5) | rt

    writes, routines = [], {}
    for name, site, original, orbit_update in [("orbit", SITE, ORIGINAL, True),
            ("lifecycle", LIFECYCLE_SITE, LIFECYCLE_ORIGINAL, False),
            ("ultrahand", ULTRAHAND_SITE, ULTRAHAND_ORIGINAL, False),
            ("selection", SELECTION_SITE, SELECTION_ORIGINAL, False),
            ("resolution", RESOLUTION_SITE, RESOLUTION_ORIGINAL, False)]:
        start = here()
        if name == "resolution":
            emit(struct.unpack('<I', original)[0])  # native store precedes host adjustment
        # A mid-function call has more live state than an ABI function entry. Save
        # all vector registers, caller GPRs, LR, NZCV, FPCR and FPSR. Never use x18.
        emit(0xd10003ff | (0x300 << 10))  # sub sp,sp,#0x300
        for reg in range(0, 18, 2):
            emit(pair(0xa9000000, reg, reg + 1, reg * 8, 8))
        emit(0xf9004bfe)  # str x30,[sp,#0x90]
        for mrs, slot in [(0xd53b4211, 0x98), (0xd53b4411, 0xa0), (0xd53b4431, 0xa8)]:
            emit(mrs)
            emit(0xf90003f1 | ((slot // 8) << 10))
        for reg in range(0, 32, 2):
            emit(pair(0xad000000, reg, reg + 1, 0xc0 + reg * 16, 16))
        scope_skip = None
        null_skips = []
        ability_update = name in ("ultrahand", "selection")
        if ability_update:
            # Shared Target update also serves lock-on and event cameras.
            # Each ability bridge accepts only its exact execution class;
            # ordinary Aiming, lock-on and event cameras retain native control.
            emit(0xf9400271)  # ldr x17,[x19]
            vtable = ULTRAHAND_VTABLE if name == "ultrahand" else SELECTION_VTABLE
            emit(env['ADRP'](15, here(), vtable))
            emit(0x910001ef | ((vtable & 0xfff) << 10))
            emit(0xeb0f023f)  # cmp x17,x15
            scope_skip = here(); emit(0)
        emit(env['ADRP'](16, here(), CAMERA_STATE_OFFSET))
        emit(0x91000210 | ((CAMERA_STATE_OFFSET & 0xfff) << 10))
        emit(0xf9401610 if name == "resolution" else 0xf9401210 if name == "selection" else 0xf9400e10 if ability_update else 0xf9400a10)
        skip = here()
        emit(0)
        if name == "resolution":
            emit(0xaa0103e0)  # x0 = native output size, written by the displaced store
        elif ability_update:
            emit(0xd10163a0)  # sub x0,x29,#0x58: native {radius,latitude,longitude}
            emit(0xaa1303e1)  # x1 = current ability camera execution
            # Same live owner -> PlayerCamera chain as UltraHand's native
            # c6f6e0 / 1951320 helpers; no retained component pointer.
            for rn, offset in [(19, 8), (2, 8), (2, 0x1f8), (2, 0x228), (2, 0x3b0)]:
                emit(0xf9400002 | ((offset // 8) << 10) | (rn << 5))
                null_skips.append(here()); emit(0)
        elif orbit_update:
            emit(0x910003e0 | (0x340 << 10))  # x0 = original sp+0x40
            emit(0x91042281)                 # x1 = chase pitch accumulator x20+0x108
        else:
            emit(0xaa1f03e0)                 # x0 = NULL: observe player lifecycle only
            emit(0xaa1f03e1)                 # x1 = NULL
        if not ability_update and name != "resolution":
            emit(0xaa1303e2)             # x2 = PlayerCamera component x19
        emit(0xd63f0200)                 # blr x16
        restore = here()
        struct.pack_into('<I', env['patch'], skip - env['PATCH_OFF'],
                         0xb4000010 | (((restore - skip) // 4) << 5))
        for branch in null_skips:
            struct.pack_into('<I', env['patch'], branch - env['PATCH_OFF'],
                             0xb4000002 | (((restore - branch) // 4) << 5))
        if scope_skip is not None:
            struct.pack_into('<I', env['patch'], scope_skip - env['PATCH_OFF'],
                             0x54000001 | (((restore - scope_skip) // 4) << 5))
        for reg in range(0, 32, 2):
            emit(pair(0xad400000, reg, reg + 1, 0xc0 + reg * 16, 16))
        for msr, slot in [(0xd51b4211, 0x98), (0xd51b4411, 0xa0), (0xd51b4431, 0xa8)]:
            emit(0xf94003f1 | ((slot // 8) << 10))
            emit(msr)
        emit(0xf9404bfe)
        for reg in range(0, 18, 2):
            emit(pair(0xa9400000, reg, reg + 1, reg * 8, 8))
        emit(0x910003ff | (0x300 << 10))
        if name != "resolution": emit(struct.unpack('<I', original)[0])
        emit(env['B'](here(), site + 4))
        replacement = struct.pack('<I', env['B'](site, start))
        image[site:site + 4] = replacement
        code = bytes(env['patch'][start - env['PATCH_OFF']:])
        writes.append(dict(offset=site, original=original.hex(), replacement=replacement.hex(),
                           reason='Native player camera ' + name))
        routines[name] = dict(offset=start, size=len(code), sha256=hashlib.sha256(code).hexdigest())
    return dict(state_offset=CAMERA_STATE_OFFSET, site=SITE, lifecycle_site=LIFECYCLE_SITE,
                ultrahand_site=ULTRAHAND_SITE, selection_site=SELECTION_SITE, resolution_site=RESOLUTION_SITE,
                abi=5, writes=writes, routines=routines)
