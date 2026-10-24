"""Execute the generated guest trampoline, including live SIMD/NZCV state."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from camera_hook import apply_camera_hook, ANCHORS, BUILD_ID, CAMERA_STATE_OFFSET, MAGIC, ORIGINAL, SITE, LIFECYCLE_SITE, LIFECYCLE_ORIGINAL, ULTRAHAND_SITE, ULTRAHAND_ORIGINAL, ULTRAHAND_VTABLE, SELECTION_SITE, SELECTION_ORIGINAL, SELECTION_VTABLE, RESOLUTION_SITE, RESOLUTION_ORIGINAL
from game_timing import STATE_OFFSET
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *


def environment():
    patch = bytearray()
    image = bytearray(0x3af0000)
    image[STATE_OFFSET:STATE_OFFSET + 8] = b'ZFPS143\0'
    for offset, data in {SITE: ORIGINAL, LIFECYCLE_SITE: LIFECYCLE_ORIGINAL, ULTRAHAND_SITE: ULTRAHAND_ORIGINAL, SELECTION_SITE: SELECTION_ORIGINAL, RESOLUTION_SITE: RESOLUTION_ORIGINAL, **ANCHORS}.items():
        image[offset:offset + len(data)] = data
    def adrp(reg, pc, target):
        delta = (target >> 12) - (pc >> 12)
        return 0x90000000 | ((delta & 3) << 29) | (((delta >> 2) & 0x7ffff) << 5) | reg
    return dict(BUILD_ID=BUILD_ID, IMG=image, PATCH_OFF=0x3af0000, patch=patch,
                emit=lambda word: patch.extend(struct.pack('<I', word)),
                here=lambda: 0x3af0000 + len(patch), ADRP=adrp,
                B=lambda pc, target: 0x14000000 | (((target - pc) // 4) & 0x3ffffff))


class CameraHookTests(unittest.TestCase):
    def test_native_clamp_does_not_discard_wide_output(self):
        # Execute the real clamp -> patched store -> SetResolution path. A hook
        # in the earlier cap getter alone would be clamped back to 1920 here.
        env = environment(); apply_camera_hook(env)
        uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        base, manager, frame, callback = 0x10000000, 0x20001000, 0x30008000, 0x40000000
        for address, size in [(base, 0x4000000), (0x20000000, 0x10000),
                              (0x30000000, 0x10000), (callback, 0x1000)]:
            uc.mem_map(address, size)
        caller = ANCHORS[0x3dcbc0] + bytes(env['IMG'][RESOLUTION_SITE:RESOLUTION_SITE + 4]) + ANCHORS[0x3dcbe4]
        uc.mem_write(base + 0x3dcbc0, caller)
        # Stop the setter after committing dimensions/dirty notification; the
        # remaining native code only classifies the size and notifies listeners.
        setter = bytes.fromhex('08400091890180524a20299b2c344029492d40293f010c6b60014d7a000300540b0840b9497c40936b010b320b0800b92b0040f94b0100f9c0035fd6')
        uc.mem_write(base + 0x8c66ec, setter)
        uc.mem_write(base + env['PATCH_OFF'], bytes(env['patch']))
        uc.mem_write(base + CAMERA_STATE_OFFSET, MAGIC + struct.pack('<IIQQQQ', 5, 0, 0, 0, 0, callback))
        uc.mem_write(callback, bytes.fromhex('c0035fd6'))
        uc.mem_write(frame + 0x18, struct.pack('<2I', 1920, 1080))
        for reg, value in [(UC_ARM64_REG_X19, manager), (UC_ARM64_REG_X20, 1920),
                           (UC_ARM64_REG_X21, 1080), (UC_ARM64_REG_X29, frame),
                           (UC_ARM64_REG_SP, frame - 0x100)]:
            uc.reg_write(reg, value)
        calls = []
        def adjust(machine, address, size, user):
            if address != callback: return
            output = machine.reg_read(UC_ARM64_REG_X0)
            self.assertEqual(output, frame - 8)
            self.assertEqual(struct.unpack('<2I', machine.mem_read(output, 8)), (1920, 1080))
            machine.mem_write(output, struct.pack('<2I', 2352, 1080))
            calls.append(output)
        uc.hook_add(UC_HOOK_CODE, adjust)
        uc.emu_start(base + 0x3dcbc0, base + 0x3dcbe8, count=300)
        self.assertEqual(len(calls), 1)
        self.assertEqual(struct.unpack('<2I', uc.mem_read(manager + 0x10, 8)), (2352, 1080))
        self.assertTrue(struct.unpack('<I', uc.mem_read(manager + 8, 4))[0] & 0x200000)

    def test_version_and_every_anchor_fail_closed(self):
        env = environment(); env['BUILD_ID'] = 'unsupported'
        with self.assertRaises(ValueError): apply_camera_hook(env)
        for offset in [SITE, LIFECYCLE_SITE, ULTRAHAND_SITE, SELECTION_SITE, RESOLUTION_SITE, *ANCHORS]:
            env = environment(); env['IMG'][offset] ^= 1
            with self.subTest(offset=hex(offset)), self.assertRaises(ValueError): apply_camera_hook(env)
            self.assertEqual(env['patch'], b'')

    def test_live_registers_and_callback_arguments(self):
        for enabled, site, scoped, owner in [(enabled, site, True, True) for site in (SITE, LIFECYCLE_SITE, ULTRAHAND_SITE, SELECTION_SITE, RESOLUTION_SITE) for enabled in (False, True)] + [(True, site, scoped, owner) for site in (ULTRAHAND_SITE, SELECTION_SITE) for scoped, owner in ((False, True), (True, False))]:
            with self.subTest(enabled=enabled, site=hex(site)):
                env = environment(); record = apply_camera_hook(env)
                self.assertEqual(env['IMG'][0x62883c:0x628840], bytes(4))  # old matrix site untouched
                uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
                for base, size in [(0x10000000, 0x4000000), (0x20000000, 0x10000),
                                   (0x30000000, 0x10000), (0x40000000, 0x1000)]:
                    uc.mem_map(base, size)
                uc.mem_write(0x10000000 + site, bytes(env['IMG'][site:site + 4]))
                uc.mem_write(0x10000000 + env['PATCH_OFF'], bytes(env['patch']))
                bridge = MAGIC + struct.pack('<IIQQQQ', 5, 0, *([0x40000000 if enabled else 0] * 4))
                uc.mem_write(0x10000000 + CAMERA_STATE_OFFSET, bridge)
                uc.mem_write(0x40000000, struct.pack('<I', 0xd65f03c0))
                sp = 0x30008000
                uc.mem_write(sp + 0x40, struct.pack('<3f', 4.8, 15, 30))
                uc.mem_write(0x20004630, b'\x04')
                uc.mem_write(0x20002008, struct.pack('<Q', 0xabcdef))
                polar = sp + (0x88 if site == SELECTION_SITE else 0x78 if site == ULTRAHAND_SITE else 0x40)
                uc.mem_write(polar, struct.pack('<3f', 4.8, 15, 30))
                expected = [0x1000 + n for n in range(31)]
                expected[8], expected[19], expected[20], expected[26] = 0x20004000, 0x20000000, 0x20002000, 0x20004000
                if site == RESOLUTION_SITE:
                    expected[1] = 0x20007000; expected[29] = expected[1] + 8
                    expected[8], expected[9] = 1920, 1080
                if site in (ULTRAHAND_SITE, SELECTION_SITE):
                    expected[19], expected[24], expected[29] = 0x20002000, 0x20000490, sp + (0xe0 if site == SELECTION_SITE else 0xd0)
                    uc.mem_write(expected[19], struct.pack('<Q', 0x10000000 + ((ULTRAHAND_VTABLE if site == ULTRAHAND_SITE else SELECTION_VTABLE) if scoped else 0x37909f8)))
                    uc.mem_write(expected[19] + 0x98, struct.pack('<f', 2.5))
                    for at, value in [(expected[19]+8, 0x20003000), (0x20003008, 0x20004000),
                                      (0x200041f8, 0x20005000), (0x20005228, 0x20006000 if owner else 0),
                                      (0x200063b0, 0x20000000)]:
                        uc.mem_write(at, struct.pack('<Q', value))
                for n in range(31): uc.reg_write(globals()[f'UC_ARM64_REG_X{n}'], expected[n])
                vectors = [(n + 1) | ((0xaabb0000 + n) << 64) for n in range(32)]
                for n in range(32): uc.reg_write(globals()[f'UC_ARM64_REG_Q{n}'], vectors[n])
                uc.reg_write(UC_ARM64_REG_SP, sp); uc.reg_write(UC_ARM64_REG_NZCV, 0xa0000000)
                calls = []
                def callback(machine, address, size, user):
                    if address != 0x40000000: return
                    args = [machine.reg_read(r) for r in [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2]]
                    arguments = [polar, 0x20002108, 0x20000000] if site == SITE else [0, 0, 0x20000000]
                    if site in (ULTRAHAND_SITE, SELECTION_SITE): arguments = [polar, expected[19], 0x20000000]
                    if site == RESOLUTION_SITE:
                        arguments = [0x20007000, expected[1], expected[2]]
                        self.assertEqual(struct.unpack('<2I', machine.mem_read(args[0],8)),(1920,1080))
                        machine.mem_write(args[0],struct.pack('<2I',2352,1080))
                    self.assertEqual(args, arguments)
                    calls.append(args)
                    if site in (SITE, ULTRAHAND_SITE, SELECTION_SITE): machine.mem_write(args[0] + 4, struct.pack('<2f', 20, 10))
                    for n in range(18): machine.reg_write(globals()[f'UC_ARM64_REG_X{n}'], 0)
                    for n in range(32): machine.reg_write(globals()[f'UC_ARM64_REG_Q{n}'], 0)
                    machine.reg_write(UC_ARM64_REG_NZCV, 0)
                    machine.reg_write(UC_ARM64_REG_FPCR, 0x4000000)
                    machine.reg_write(UC_ARM64_REG_FPSR, 1)
                uc.hook_add(UC_HOOK_CODE, callback)
                uc.emu_start(0x10000000 + site, 0x10000000 + site + 4, count=300)
                self.assertEqual(len(calls), int(enabled and scoped and owner))
                self.assertEqual(uc.reg_read(UC_ARM64_REG_PC), 0x10000000 + site + 4)
                self.assertEqual(uc.reg_read(UC_ARM64_REG_SP), sp)
                if site == SITE: expected[8] = 4
                elif site == ULTRAHAND_SITE: vectors[0] = struct.unpack('<I', struct.pack('<f', 2.5))[0]
                elif site == SELECTION_SITE: expected[0] = polar
                elif site == RESOLUTION_SITE: pass
                else: expected[9] = 0xabcdef  # displaced native load must still execute
                for n in range(31): self.assertEqual(uc.reg_read(globals()[f'UC_ARM64_REG_X{n}']), expected[n], f'x{n}')
                for n in range(32): self.assertEqual(uc.reg_read(globals()[f'UC_ARM64_REG_Q{n}']), vectors[n], f'q{n}')
                self.assertEqual(uc.reg_read(UC_ARM64_REG_NZCV), 0xa0000000)
                self.assertEqual(uc.reg_read(UC_ARM64_REG_FPCR), 0)
                self.assertEqual(uc.reg_read(UC_ARM64_REG_FPSR), 0)
                radius, pitch, yaw = struct.unpack('<3f', uc.mem_read(polar, 12))
                self.assertAlmostEqual(radius, 4.8, places=5)
                self.assertEqual((pitch, yaw), (20, 10) if enabled and scoped and owner and site in (SITE, ULTRAHAND_SITE, SELECTION_SITE) else (15, 30))
                self.assertEqual(record['abi'], 5)


if __name__ == '__main__': unittest.main()
