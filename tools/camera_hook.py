"""TotK 1.4.3 player chase orbit input, before native pose and collision.

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
MAGIC = b'CAM2\0\0\0\0'
SITE = 0x18f100
LIFECYCLE_SITE = 0x8749e8
LIFECYCLE_ORIGINAL = bytes.fromhex('890640f9')  # ldr x9,[x20,#8]
ORIGINAL = bytes.fromhex('08c15839')  # ldrb w8,[x8,#0x630]
# Independent anchors reject a coincidental byte match or changed call chain.
ANCHORS = {
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
    if CAMERA_STATE_OFFSET + 0x18 > env['PATCH_OFF'] or image[STATE_OFFSET:STATE_OFFSET + 8] != b'ZFPS143\0':
        raise ValueError('Camera hook needs the timing state page')
    for offset, expected in {SITE: ORIGINAL, LIFECYCLE_SITE: LIFECYCLE_ORIGINAL, **ANCHORS}.items():
        if image[offset:offset + len(expected)] != expected:
            raise ValueError(f'Camera hook instruction mismatch at {offset:#x}')
    image[CAMERA_STATE_OFFSET:CAMERA_STATE_OFFSET + 0x18] = MAGIC + struct.pack('<IIQ', 2, 0, 0)
    def pair(base, rt, rt2, offset, scale):
        return base | ((offset // scale & 127) << 15) | (rt2 << 10) | (31 << 5) | rt

    writes, routines = [], {}
    for name, site, original, orbit_update in [("orbit", SITE, ORIGINAL, True),
            ("lifecycle", LIFECYCLE_SITE, LIFECYCLE_ORIGINAL, False)]:
        start = here()
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
        emit(env['ADRP'](16, here(), CAMERA_STATE_OFFSET))
        emit(0x91000210 | ((CAMERA_STATE_OFFSET & 0xfff) << 10))
        emit(0xf9400a10)  # ldr x16,[x16,#0x10] (immutable host callback)
        skip = here()
        emit(0)
        if orbit_update:
            emit(0x910003e0 | (0x340 << 10))  # x0 = original sp+0x40
            emit(0x91042281)                 # x1 = chase pitch accumulator x20+0x108
        else:
            emit(0xaa1f03e0)                 # x0 = NULL: observe player lifecycle only
            emit(0xaa1f03e1)                 # x1 = NULL
        emit(0xaa1303e2)                 # x2 = PlayerCamera component x19
        emit(0xd63f0200)                 # blr x16
        restore = here()
        struct.pack_into('<I', env['patch'], skip - env['PATCH_OFF'],
                         0xb4000010 | (((restore - skip) // 4) << 5))
        for reg in range(0, 32, 2):
            emit(pair(0xad400000, reg, reg + 1, 0xc0 + reg * 16, 16))
        for msr, slot in [(0xd51b4211, 0x98), (0xd51b4411, 0xa0), (0xd51b4431, 0xa8)]:
            emit(0xf94003f1 | ((slot // 8) << 10))
            emit(msr)
        emit(0xf9404bfe)
        for reg in range(0, 18, 2):
            emit(pair(0xa9400000, reg, reg + 1, reg * 8, 8))
        emit(0x910003ff | (0x300 << 10))
        emit(struct.unpack('<I', original)[0])
        emit(env['B'](here(), site + 4))
        replacement = struct.pack('<I', env['B'](site, start))
        image[site:site + 4] = replacement
        code = bytes(env['patch'][start - env['PATCH_OFF']:])
        writes.append(dict(offset=site, original=original.hex(), replacement=replacement.hex(),
                           reason='Native player camera ' + name))
        routines[name] = dict(offset=start, size=len(code), sha256=hashlib.sha256(code).hexdigest())
    return dict(state_offset=CAMERA_STATE_OFFSET, site=SITE, lifecycle_site=LIFECYCLE_SITE,
                abi=2, writes=writes, routines=routines)
