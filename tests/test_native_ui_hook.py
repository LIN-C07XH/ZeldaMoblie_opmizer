import struct
import unittest
from test_camera_hook import environment
from native_ui_hook import apply_native_ui_hook, ANCHORS, ORIGINAL, UI_STATE_OFFSET, MAGIC, ICON_SITE, FRAME_SITE, ICON_RETURN, DRAW_SITE
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *


def ui_environment():
    env = environment()
    for at, code in {**ORIGINAL, **ANCHORS}.items(): env['IMG'][at:at + len(code)] = code
    return env


class NativeUiHookTests(unittest.TestCase):
    def test_original_pane_draw_functions_are_not_patched(self):
        env = ui_environment()
        drawing = {0x41b84c: bytes.fromhex('ff4301d1'),
                   0xac90: bytes.fromhex('08244279')}
        for at, code in drawing.items(): env['IMG'][at:at + len(code)] = code
        result = apply_native_ui_hook(env)
        self.assertEqual({write['offset'] for write in result['writes']}, {ICON_SITE, FRAME_SITE, DRAW_SITE})
        for at, code in drawing.items():
            self.assertEqual(env['IMG'][at:at + len(code)], code)

    def test_version_and_anchors(self):
        env = ui_environment(); env['BUILD_ID'] = 'old'
        with self.assertRaises(ValueError): apply_native_ui_hook(env)
        for at in [*ORIGINAL, *ANCHORS]:
            env = ui_environment(); env['IMG'][at] ^= 1
            with self.subTest(at=hex(at)), self.assertRaises(ValueError): apply_native_ui_hook(env)
            self.assertEqual(env['patch'], b'')

    def test_registers_and_native_paths(self):
        for site in [ICON_SITE, FRAME_SITE]:
            for enabled, handled in [(False, False), (True, False), (True, True)]:
                with self.subTest(site=hex(site), enabled=enabled, handled=handled):
                    env = ui_environment(); apply_native_ui_hook(env)
                    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
                    for at, size in [(0x10000000, 0x4000000), (0x20000000, 0x10000), (0x30000000, 0x10000), (0x40000000, 0x1000)]:
                        uc.mem_map(at, size)
                    uc.mem_write(0x10000000 + site, bytes(env['IMG'][site:site + 4]))
                    uc.mem_write(0x10000000 + env['PATCH_OFF'], bytes(env['patch']))
                    uc.mem_write(0x10000000 + UI_STATE_OFFSET, MAGIC + struct.pack('<IIQ', 4, 0, 0x40000000 if enabled else 0))
                    uc.mem_write(0x40000000, struct.pack('<I', 0xd65f03c0))
                    sp = 0x30008000
                    expected = [0x12340000 + n for n in range(31)]
                    expected[0], expected[1], expected[19] = 0x20000100, 3, 0x20000200
                    for n in range(31): uc.reg_write(globals()[f'UC_ARM64_REG_X{n}'], expected[n])
                    vectors = [(n + 1) | ((0xabcdef00 + n) << 64) for n in range(32)]
                    for n in range(32): uc.reg_write(globals()[f'UC_ARM64_REG_Q{n}'], vectors[n])
                    uc.reg_write(UC_ARM64_REG_SP, sp)
                    uc.reg_write(UC_ARM64_REG_NZCV, 0xa0000000)
                    calls = []
                    def callback(machine, address, size, user):
                        if address != 0x40000000: return
                        args = [machine.reg_read(r) for r in [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2]]
                        self.assertEqual(args, [1, expected[19], 3] if site == ICON_SITE else [2, expected[0], 0])
                        calls.append(args)
                        for n in range(18): machine.reg_write(globals()[f'UC_ARM64_REG_X{n}'], 0)
                        for n in range(32): machine.reg_write(globals()[f'UC_ARM64_REG_Q{n}'], 0)
                        machine.reg_write(UC_ARM64_REG_NZCV, 0)
                        machine.reg_write(UC_ARM64_REG_FPCR, 0x4000000)
                        machine.reg_write(UC_ARM64_REG_FPSR, 1)
                        machine.reg_write(UC_ARM64_REG_X0, int(handled))
                    uc.hook_add(UC_HOOK_CODE, callback)
                    target = ICON_RETURN if site == ICON_SITE and enabled and handled else site + 4
                    uc.emu_start(0x10000000 + site, 0x10000000 + target, count=300)
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_PC), 0x10000000 + target)
                    self.assertEqual(len(calls), int(enabled))
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_SP), sp - 0x50 if site == FRAME_SITE else sp)
                    if site == ICON_SITE:
                        if enabled and handled: expected[0] = 1
                        else: expected[8] = 0x10000000 + 0x3714000
                    for n in range(31): self.assertEqual(uc.reg_read(globals()[f'UC_ARM64_REG_X{n}']), expected[n], f'x{n}')
                    for n in range(32): self.assertEqual(uc.reg_read(globals()[f'UC_ARM64_REG_Q{n}']), vectors[n], f'q{n}')
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_NZCV), 0xa0000000)
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_FPCR), 0)
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_FPSR), 0)

    def test_scoped_draw_prefixes_and_registers(self):
        for name in ('RootPane', 'P_Back_00', 'K_Do_01_00', 'P_KeyA_00', 'P_CaptureKeyA_00',
                     'N_InOut_00', 'P_LStickUp_00', 'P_ZL_00', 'T_A_01', 'T_B_00', 'T_X_00', 'T_Y_00', 'T_Plus_00'):
            for enabled, handled in ((False, False), (True, False), (True, True)):
                with self.subTest(name=name, enabled=enabled, handled=handled):
                    env=ui_environment(); apply_native_ui_hook(env)
                    uc=Uc(UC_ARCH_ARM64, UC_MODE_ARM)
                    for at,size in ((0x10000000,0x4000000),(0x2000000000,0x10000),(0x30000000,0x10000),(0x40000000,0x1000)):
                        uc.mem_map(at,size)
                    obj=0x2000000100
                    uc.mem_write(0x10000000+DRAW_SITE, bytes(env['IMG'][DRAW_SITE:DRAW_SITE+4]))
                    uc.mem_write(0x10000000+env['PATCH_OFF'], bytes(env['patch']))
                    uc.mem_write(0x10000000+UI_STATE_OFFSET, MAGIC+struct.pack('<IIQ',4,0,0x40000000 if enabled else 0))
                    uc.mem_write(0x40000000,struct.pack('<I',0xd65f03c0))
                    uc.mem_write(obj+0xb0,name.encode()+b'\0'); uc.mem_write(obj+0x58,b'\x81')
                    expected=[0x12340000+n for n in range(31)]; expected[0]=obj; expected[30]=0x40000080
                    for n,v in enumerate(expected): uc.reg_write(globals()[f'UC_ARM64_REG_X{n}'],v)
                    vectors=[0x98760000+n+(n<<80) for n in range(32)]
                    for n,v in enumerate(vectors): uc.reg_write(globals()[f'UC_ARM64_REG_Q{n}'],v)
                    uc.reg_write(UC_ARM64_REG_SP,0x30008000); uc.reg_write(UC_ARM64_REG_NZCV,0xa0000000)
                    eligible=name not in ('RootPane','P_Back_00','K_Do_01_00')
                    calls=[]
                    def callback(machine,address,size,user):
                        if address!=0x40000000:return
                        self.assertEqual([machine.reg_read(r) for r in (UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2)], [3,obj,0])
                        calls.append(1)
                        for n in range(18):machine.reg_write(globals()[f'UC_ARM64_REG_X{n}'],0)
                        for n in range(32):machine.reg_write(globals()[f'UC_ARM64_REG_Q{n}'],0)
                        machine.reg_write(UC_ARM64_REG_NZCV,0)
                        machine.reg_write(UC_ARM64_REG_FPCR,0x4000000); machine.reg_write(UC_ARM64_REG_FPSR,1)
                        machine.reg_write(UC_ARM64_REG_X0,int(handled))
                    uc.hook_add(UC_HOOK_CODE,callback)
                    suppress=eligible and enabled and handled
                    target=0x40000080 if suppress else 0x10000000+DRAW_SITE+4
                    uc.emu_start(0x10000000+DRAW_SITE,target,count=400)
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_PC),target)
                    self.assertEqual(len(calls),int(eligible and enabled))
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_SP),0x30008000)
                    if not suppress: expected[8]=0x81
                    for n,v in enumerate(expected):self.assertEqual(uc.reg_read(globals()[f'UC_ARM64_REG_X{n}']),v,f'x{n}')
                    for n,v in enumerate(vectors):self.assertEqual(uc.reg_read(globals()[f'UC_ARM64_REG_Q{n}']),v,f'q{n}')
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_NZCV),0xa0000000)
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_FPCR),0)
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_FPSR),0)



if __name__ == '__main__': unittest.main()
