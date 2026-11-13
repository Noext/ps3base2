# N3b -- SPU channel audit: every rdch/wrch of the MultiStream program

Branch: explore/n3b, from main HEAD 202e9ef.
Scope: extend PS3BASE_SPU_REFUSAL_DUMP to record ALL channels (rdch and wrch), produce
the per-channel table for the MultiStream program, and diff it against RPCS3's
SPUThread.cpp semantics (decrementer, event status, notifications especially).

Status: running. Nothing measured yet. This file is appended after every measurement.
