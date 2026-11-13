# RESULTS-P4 -- driving RDR past the ALERT dialogue

Task: closed-loop driving. Read the screen, choose "Continue Without Saving", then
"Create A New Save Game", capture every screen. Deliverable: the capture of the
screen that follows the ALERT dialogue.

State at start: main HEAD 01c5e16. RDR reaches its own ALERT screen
("A Red Dead Redemption single player save could not be found" with Continue
Without Saving / Create A New Save Game / Retry), then dies on a host SIGSEGV
at guest 0x10.

## Log

(format: timestamp UTC, what, raw proof)

