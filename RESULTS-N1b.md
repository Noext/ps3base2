# RESULTS N1b -- the SPU branch that selects the zero effective address

Agent: N1b. Task: with PS3BASE_SPU_REFUSAL_DUMP=1, find the branch that leads
to the zero in the effective address, the value that decides it, and where that
value comes from. Disassemble SPU code around 0x6600-0x6960 and 0x63e0-0x6440.

Status: running.

## Starting facts given (not re-derived)

- Block at LS 0x36200, +0x58 = 0x12190000 (valid audio port), +0x60 = 0.
- 0x6b24 lqr r53,0x36260 reads +0x60=0; rotqbyi r4,r53,4 -> r4=0; 0x6408 wrch ch18,r4 -> EA 0 -> GET refused.
- Same (kind of) instruction at 0x68e8 writes 0x70c95a00 (valid).
- Last branch record: 0x666c cmp 0x017bfe80 vs 0 -> TAKEN.
- Dump: PS3BASE_SPU_REFUSAL_DUMP=1 on main 202e9ef.

## Measurements

(append raw proof after every measurement)
