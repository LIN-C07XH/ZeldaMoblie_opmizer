import struct
import unittest
from test_camera_hook import environment
from native_ui_hook import apply_native_ui_hook, ANCHORS, ORIGINAL, UI_STATE_OFFSET, MAGIC, ICON_SITE, FRAME_SITE, ICON_RETURN, DRAW_SITE, LAYOUT_DRAW_SITE
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
        self.assertEqual({write['offset'] for write in result['writes']}, {ICON_SITE, FRAME_SITE, DRAW_SITE, LAYOUT_DRAW_SITE})
        for at, code in drawing.items():
            self.assertEqual(env['IMG'][at:at + len(code)], code)

    def test_version_and_anchors(self):
        env = ui_environment(); env['BUILD_ID'] = 'old'
        with self.assertRaises(ValueError): apply_native_ui_hook(env)
        for at in [*ORIGINAL, *ANCHORS]:
            env = ui_environment(); env['IMG'][at] ^= 1
            with self.subTest(at=hex(at)), self.assertRaises(ValueError): apply_native_ui_hook(env)
            self.assertEqual(env['patch'], b'')

    def test_real_layout_draw_sees_fitted_projection_after_state_reset(self):
        import os
        from pathlib import Path
        native=Path(os.environ.get('ZELDA_TEST_NATIVE_MAIN','/nonexistent'))
        if not native.is_file(): self.skipTest('local original native image required')
        image=native.read_bytes(); env=ui_environment(); apply_native_ui_hook(env)
        uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
        base,layout,context,pane,table,callback,draw=0x10000000,0x20000100,0x20000300,0x20000500,0x20000600,0x40000000,0x40001000
        for address,size in ((base,0x4000000),(0x20000000,0x10000),(0x30000000,0x10000),(callback,0x2000)):uc.mem_map(address,size)
        uc.mem_write(base+LAYOUT_DRAW_SITE,image[LAYOUT_DRAW_SITE:0x10c71e4])
        uc.mem_write(base+LAYOUT_DRAW_SITE,bytes(env['IMG'][LAYOUT_DRAW_SITE:LAYOUT_DRAW_SITE+4]))
        uc.mem_write(base+0x10cc850,image[0x10cc850:0x10cc870])
        uc.mem_write(base+env['PATCH_OFF'],bytes(env['patch']))
        uc.mem_write(base+UI_STATE_OFFSET,MAGIC+struct.pack('<IIQ',4,0,callback))
        uc.mem_write(callback,bytes.fromhex('c0035fd6'));uc.mem_write(draw,bytes.fromhex('c0035fd6'))
        uc.mem_write(layout+0x18,struct.pack('<Q',pane));uc.mem_write(pane,struct.pack('<Q',table))
        uc.mem_write(table+0xd8,struct.pack('<Q',draw))
        matrix=[1/960,0,0,0,0,1/540,0,0,0,0,-1/150,-1,0,0,0,1]
        uc.mem_write(context+0x10,struct.pack('<16f',*matrix))
        for reg,value in ((UC_ARM64_REG_X0,layout),(UC_ARM64_REG_X1,context),(UC_ARM64_REG_X2,0x1234),(UC_ARM64_REG_SP,0x30008000),(UC_ARM64_REG_X30,base+0x1000)):uc.reg_write(reg,value)
        calls=[]
        def receive(machine,address,size,user):
            if address==callback:
                self.assertEqual([machine.reg_read(r) for r in (UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2)],[4,context,0])
                machine.mem_write(context+0x10,struct.pack('<f',(1/540)/(1912/880)))
                machine.reg_write(UC_ARM64_REG_X0,1) # adjustment never skips the native draw
            if address==draw:
                self.assertEqual([machine.reg_read(r) for r in (UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2)],[pane,context,0x1234])
                self.assertEqual(struct.unpack('<Q',machine.mem_read(context+0xc0,8))[0],layout)
                self.assertEqual(machine.mem_read(context+0x18c,1),b'\x01')
                m=struct.unpack('<16f',machine.mem_read(context+0x10,64))
                self.assertAlmostEqual(m[0]*1912/(m[5]*880),1,places=5)
                calls.append(address)
        uc.hook_add(UC_HOOK_CODE,receive);uc.emu_start(base+LAYOUT_DRAW_SITE,base+0x1000,count=350)
        self.assertEqual(calls,[draw])
        self.assertEqual(struct.unpack('<Q',uc.mem_read(context+0xc0,8))[0],0)

    def test_registers_and_native_paths(self):
        for site in [ICON_SITE, FRAME_SITE, LAYOUT_DRAW_SITE]:
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
                    expected[0], expected[1], expected[19] = 0x20000100, 0x20000300 if site == LAYOUT_DRAW_SITE else 3, 0x20000200
                    for n in range(31): uc.reg_write(globals()[f'UC_ARM64_REG_X{n}'], expected[n])
                    vectors = [(n + 1) | ((0xabcdef00 + n) << 64) for n in range(32)]
                    for n in range(32): uc.reg_write(globals()[f'UC_ARM64_REG_Q{n}'], vectors[n])
                    uc.reg_write(UC_ARM64_REG_SP, sp)
                    uc.reg_write(UC_ARM64_REG_NZCV, 0xa0000000)
                    calls = []
                    def callback(machine, address, size, user):
                        if address != 0x40000000: return
                        args = [machine.reg_read(r) for r in [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2]]
                        self.assertEqual(args, [1, expected[19], 3] if site == ICON_SITE else [4, expected[1], 0] if site == LAYOUT_DRAW_SITE else [2, expected[0], 0])
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
                    self.assertEqual(uc.reg_read(UC_ARM64_REG_SP), sp - (0x50 if site == FRAME_SITE else 0x30 if site == LAYOUT_DRAW_SITE else 0))
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
