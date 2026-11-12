# U2 -- no more zero-fill

A DMA with an invalid effective address must write nothing, log the fault
(program, SPU PC, command, LSA, EA, size, list element) and raise the MFC DMA
exception the way the hardware and RPCS3 do.

## Measurements
