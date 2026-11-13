#!/usr/bin/env python3
"""crash_stack.py -- does stack_from() keep the HOST STACK when rip is 0?

Why this test exists: RDR has a rare crash at rip = 0, a NULL HOST CODE POINTER.
The old stack_from() searched backtrace() for a frame equal to rip; with rip = 0
that search can never succeed, so it left start at 2 and printed the single frame
"#0 ?" -- the host stack was thrown away exactly when it was needed, which is why
that crash could not be diagnosed.

The subject is stack_from() in runtime/src/crash_posix.cpp, compiled HERE as its
own translation unit with a stub backtrace_port, so the test drives the real
logic without linking the whole runtime (which needs the generated image table).
That is the same discipline as the other tests in this directory: the code under
test is included, not re-implemented.

Run: python3 tests/crash_stack.py   (exit 0 = pass)
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A program that calls stack_from() with rip = 0 and with a real rip, and prints
# how many frames each produced.
PROG = r"""
#include <cstdio>
#include <cstring>
#include <ucontext.h>

/* The two functions the crash handler uses, declared here because crash_posix.cpp
 * keeps them internal. The linker checks the signatures. */
namespace backtrace_port {
int capture(void** pcs, int depth);
}
int stack_from(const ucontext_t* uc, void** pcs, int depth);

namespace backtrace_port {
/* A fake stack: three "host frames". The test is about what stack_from does with
 * them, not about unwinding a real one. */
int capture(void** pcs, int depth) {
    (void)depth;
    pcs[0] = reinterpret_cast<void*>(0x1111);
    pcs[1] = reinterpret_cast<void*>(0x2222);
    pcs[2] = reinterpret_cast<void*>(0x3333);
    return 3;
}
}

int main() {
    ucontext_t uc;
    std::memset(&uc, 0, sizeof uc);
    void* pcs[24];

    /* Case 1: rip = 0, the NULL host code pointer RDR hits. */
    uc.uc_mcontext.gregs[REG_RIP] = 0;
    int n0 = stack_from(&uc, pcs, 24);
    std::printf("rip=0 frames=%d\n", n0);
    for (int i = 0; i < n0; i++) std::printf("  #%d %p\n", i, pcs[i]);

    /* Case 2: a rip that IS in the captured stack (the ordinary case). */
    uc.uc_mcontext.gregs[REG_RIP] = 0x2222;
    int n1 = stack_from(&uc, pcs, 24);
    std::printf("rip=0x2222 frames=%d\n", n1);
    for (int i = 0; i < n1; i++) std::printf("  #%d %p\n", i, pcs[i]);
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "t.cpp")
        exe = os.path.join(tmp, "t")
        with open(src, "w") as f:
            f.write(PROG)
        # crash_posix.cpp pulls in the rest of the runtime through its includes,
        # so the test extracts just the function under test into its own file.
        crash = os.path.join(ROOT, "runtime/src/crash_posix.cpp")
        text = open(crash, encoding="utf-8", errors="replace").read()
        m = re.search(r"int stack_from\(const ucontext_t\* uc.*?\n\}", text, re.S)
        if not m:
            print("SKIP: stack_from() not found in crash_posix.cpp")
            return 2
        func = os.path.join(tmp, "stack_from.cpp")
        with open(func, "w") as f:
            f.write('#include <algorithm>\n#include <ucontext.h>\n#include <cstdint>\n'
                    'namespace backtrace_port { int capture(void** pcs, int depth); }\n'
                    + m.group(0) + "\n")
        cmd = ["clang++", "-O0", "-g", "-std=gnu++20", "-march=x86-64-v3",
               "-I", os.path.join(ROOT, "runtime/include"),
               src, func, "-o", exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print("SKIP: the test program did not build:")
            print((r.stderr or "")[-1200:])
            return 2
        p = subprocess.run([exe], capture_output=True, text=True, timeout=60)
        out = p.stdout or ""
        print(out.strip())
        n0 = re.search(r"rip=0 frames=(\d+)", out)
        if not n0:
            print("FAIL: no output")
            return 1
        frames0 = int(n0.group(1))
        # The bug: rip=0 produced ONE frame (#0 ?) and dropped the host stack.
        if frames0 < 4:
            print("FAIL: rip=0 produced %d frame(s); the host stack was discarded "
                  "(expected the faulting pc plus the 3 captured frames)" % frames0)
            return 1
        print("ok   rip=0 keeps the host stack: %d frames (pc + 3 captured)" % frames0)
        return 0


if __name__ == "__main__":
    sys.exit(main())
