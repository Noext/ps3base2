# RESULTS-P1 -- reading the RDR host SIGSEGV at exe+0x17aa2b6

Branch explore/p1, worktree /root/Dev/wt-P1, base main 01c5e16.

## Task
1. addr2line exe+0x17aa2b6 and every crash frame; name the function (HLE handler
   vs lifted guest code).
2. Guest thread, guest lr, last 30 imports that thread made with returns.
3. Find '*NO NAME*' in the game ELF and in every module -- whoever contains it
   names the subsystem.
4. If the site is one of our handlers (cellSaveData* first suspect): arguments
   received, structure involved, compare against RPCS3's header.

## Log

### M1 -- addr2line of every crash frame (build-rdr/ps3base, main 01c5e16)

```
$ for a in 0x17aa182 0x17aa290 0x17aa2b6 0x17ac22c 0x17ac515 0x17ad1cc 0x17b0d23 \
       0x17b3eb3 0x17b7955 0x17bc0fc 0x1682b9d 0x815f599 0x75b8156 0x75ba3b4 0x75ba6f9; do
    addr2line -f -C -e build-rdr/ps3base $a; done
0x17aa182  f_3600026c_body   out-rdr/ppu/libfiber_000.cpp:206
0x17aa290  f_360002a0_body   out-rdr/ppu/libfiber_000.cpp:230
0x17aa2b6  ppu::ld64         runtime/include/ppu/ops.h:63   <-- FAULT
0x17ac22c  f_36000748_body   runtime/include/ppu/ops.h:104
0x17ac515  f_360008a0_body   out-rdr/ppu/libfiber_000.cpp
0x17ad1cc  f_3600093c_body   out-rdr/ppu/libfiber_000.cpp
0x17b0d23  f_360012a0_body   out-rdr/ppu/libfiber_000.cpp:1565
0x17b3eb3  f_36001b04_body   out-rdr/ppu/libfiber_000.cpp:2251
0x17b7955  f_3600254c_body   out-rdr/ppu/libfiber_000.cpp:3138
0x17bc0fc  f_360033dc_body   out-rdr/ppu/libfiber_000.cpp:4128
0x1682b9d  ppu::call_import_inner+0x85d (ppu_runtime.cpp)
0x815f599  f_00caf26c_body   out-rdr/ppu/rdr_129.cpp:7327
0x75b8156  f_00b57c10_body   out-rdr/ppu/rdr_115.cpp:17907
0x75ba3b4  f_00b58368_body   out-rdr/ppu/rdr_115.cpp:18374
0x75ba6f9  f_00b58430_body   out-rdr/ppu/rdr_115.cpp:18411
```

Plus `nm` (exact symbol bound, addr2line rounds to the whole inline chain):

```
0x17aa2b6 f_360002a0_body+0xe6
0x1682b9d ppu::call_import_inner+0x85d
```

**Conclusion:** every frame 0..22 is LIFTED GUEST CODE, not one of our HLE
handlers. The faulting site is the second instruction of the guest function
360002a0's epilogue, a plain `ld` through the frame pointer chain.

### M2 -- disassembly at the fault (objdump, build-rdr/ps3base)

```
17aa290: mov 0x8(%rbx),%rax          ; r9 = frame->+8   (guest r9)
17aa294: lea 0x28(%rax),%ecx         ; r9 + 0x28
17aa297: mov (%r14,%rcx,1),%rcx      ; ld  r9+0x28
17aa29b: movbe %rcx,0x10(%rbx)
17aa2a1: mov %eax,%eax
17aa2a3: mov (%r14,%rax,1),%rax      ; ld  r9       <- r9 == 0
17aa2ad: movbe %rax,0x48(%rbx)
17aa2b3: add $0x10,%ecx
17aa2b6: mov (%r14,%rcx,1),%rcx      ; ld  r9+0x10  <- FAULT, ea = 0x10
```

r14 is the guest base. `ecx` computed from rax = *(frame+8) = 0, so the load
reads guest 0x10. Matches `fault 0x...010 (guest 0x10)` exactly.

### M3 -- the lifted source of the faulting guest function

`out-rdr/ppu/libfiber_000.cpp`, f_360002a0_body:

```
    ctx.gpr[8] = ctx.gpr[1] | ctx.gpr[1];
    { uint64_t x = ~ctx.gpr[1]; uint64_t y = ctx.gpr[5]; ... ctx.gpr[9] = r; }  // r9 = r5 - sm1
    { uint32_t ea = uint32_t(ctx.gpr[1] + ctx.gpr[9]); ppu::st64(base, ea, uint64_t(ctx.gpr[1])); ctx.gpr[1] = ctx.gpr[1] + ctx.gpr[9]; } // stdu r1,r9(r1)
    ctx.gpr[0] = ctx.lr;
    ppu::st64(base, uint32_t(ctx.gpr[8] + 16), ctx.gpr[0]);   // std r0,16(r8)  save LR in caller frame
    ppu::st64(base, uint32_t(ctx.gpr[1] + 40), ctx.gpr[2]);   // std r2,40(r1)
    ctx.gpr[7] = ctx.gpr[3] & 0xffffffff;                     // r7 = func desc
    ctx.gpr[0] = ld32(base, r7+0); ctx.ctr = r0;              // lwz r0,0(r7)
    ctx.gpr[2] = ld32(base, r7+4);                            // lwz r2,4(r7)   TOC from the OPD
    ctx.gpr[3] = ctx.gpr[4];                                  // r3 = arg
    ctx.lr = 0x360002d0; ppu::call_indirect(ctx, base, uint32_t(ctx.ctr));
    ctx.gpr[2] = ld64(base, r1+40);                           // ld r2,40(r1)
    ctx.gpr[9] = ld64(base, r1+0);                            // ld r9,0(r1)    back-chain
    ctx.gpr[0] = ld64(base, r9+16);                           // ld r0,16(r9)   <-- FAULT
    ctx.lr = ctx.gpr[0];
    ctx.gpr[1] = ctx.gpr[9];
```

This is the standard PPC64 `-mminimal-toc`/ELFv1 **function-pointer thunk**:
save LR into the caller's frame at +16, load TOC from the descriptor,
indirect-call, then walk the back-chain to restore LR.

### M4 -- which module 360002a0 belongs to: libfiber (FULL LLE)

From the crash log (`/tmp/seg.log`, run 42.96 s):

```
I 0.667358 loader: loaded libfiber (out-rdr/gen/reloc/libfiber.elf) text 0x36000000-0x36013000 ...
```
And the faulting lifted file is `out-rdr/ppu/libfiber_000.cpp`. So the whole
crash stack 0..22 is **inside Sony's libfiber**, running as lifted LLE code.

### M5 -- `*NO NAME*`, the string on the faulting frame

Byte search (not text-mode grep, which misses these files) over the game ELFs,
the firmware PRX/SPRX set and the whole **dev_flash** tree:

```
$ python3 - search b'*NO NAME*' in bin/*/*.elf fw/4.91/*.sprx /root/Dev/devflash-out
FULL /root/Dev/ps3base2/fw/4.91/libfiber.prx
FULL /root/Dev/ps3base2/fw/4.91/libspurs_jq.prx
FULL /root/Dev/ps3base2/fw/4.91/libsre.prx
FULL /root/Dev/ps3base2/fw/4.91/reloc-rdr/libsre.elf
FULL /root/Dev/ps3base2/fw/4.91/reloc-rdr/libspurs_jq.elf
FULL /root/Dev/ps3base2/fw/4.91/reloc/libfiber.elf
FULL /root/Dev/ps3base2/fw/4.91/reloc/libsre.elf
0 bin/rdr/EBOOT.elf  bin/gta/EBOOT.elf  bin/mc/mc.elf  bin/01.12/GOWA.elf
0 every fw/4.91/*.sprx (the HLE-side PRX set)
```

**The game ELF does not contain `*NO NAME*` at all.** The string lives only in
libfiber + libsre + libspurs_jq. This is the SPU-task name that cellSpurs uses
for a task created without a name -- i.e. the crash is in the firmware's
fiber/SPURS support library, and the `*NO NAME*` string visible in r6/r7 is a
just-created SPURS task name struct, not a save-data name.

