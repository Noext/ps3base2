
## Red Dead Redemption -- the 190 HLE functions, one ticket per module

Configuration: the proven seven LLE modules plus libnet, libfiber, libssl,
libhttp, librtc. 153 of the 343 imports are covered by lifted firmware; the 190
below are not, and each must be written against RPCS3 with the console's return
codes (AGENTS.md: an implemented function answers what the console answers).

An auditor per module, in parallel, per docs/RECIPE.md. Each ticket is done when
its unit tests pass, the signatures are cited from refs/, and the return codes
match RPCS3's.

| Ticket | Module | Count | Auditor | Status |
|---|---|---|---|---|
| RDR-HLE-01 | sceNp | 39 | np | open |
| RDR-HLE-02 | cellSysutil (audio out) | 34 | sysutil | open |
| RDR-HLE-03 | sceNp2 | 27 | np2 | open |
| RDR-HLE-04 | sceNpCommerce2 | 26 | commerce | open |
| RDR-HLE-05 | sys_io (keyboard/mouse) | 14 | sysio | open |
| RDR-HLE-06 | cellVoice | 13 | voice | open |
| RDR-HLE-07 | sceNpTrophy | 12 | trophy | open |
| RDR-HLE-08 | cellNetCtl | 9 | netctl | open |
| RDR-HLE-09 | cellAudio | 7 | audio | open |
| RDR-HLE-10 | cellGame | 6 | game | merged (14 functions, 159 checks) |
| RDR-HLE-11 | cellGameExec | 1 | game | merged |
| RDR-HLE-12 | cellSaveData | 1 | savedata | merged |
| RDR-HLE-13 | cellSysutilAvconfExt | 1 | avconf | merged |

Standing rule for every ticket: the run must keep `regress.sh` green on
Minecraft and GTA. A module that only this title imports may return the offline
or "service unavailable" answer, and that is what the console does when the
service is absent -- it is not a stub.

