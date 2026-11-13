# P2 -- TLS block table: per-thread TLS blocks at the RDR crash

Task: r13 = 0x10007060 points at an all-zero block. Build the table of TLS blocks
per thread (address, size, .tdata copied, .tbss zeroed, reuse). Count threads alive
at the crash. Compare against ps3recomp #253 (null/shared block past N threads) and
#68 (reused block not reinitialised).

Status: running. Proof appended after every measurement.

## Log
