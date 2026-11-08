#!/usr/bin/env python3
"""One step of a screen-driven sequence: look, then press -- never press blind.

This is the piece the run book was missing. A fixed list of presses assumes the
title shows exactly one screen per press; the moment it inserts a screen the
list did not anticipate (the autosave message box does exactly that), the list
and the title desynchronise and every later press lands on the wrong screen.
Looking first makes the sequence self-correcting: if the title is on a screen we
did not expect, we act on what is actually there.

States come from tools/screens via nearest-reference matching, so a label means
"this looks like a frame we verified by eye", not "some threshold fired".

usage: drive_once.py <display> <screenshot-out> [max-clicks]
exit:  0 = acted, 3 = already in a world, 4 = no window
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import drive_screens as ds  # noqa: E402


def shot(display, path):
    ids = subprocess.run(["xdotool", "search", "--name", "Minecraft"],
                         capture_output=True, text=True,
                         env={**os.environ, "DISPLAY": display}).stdout.split()
    if not ids:
        return False
    subprocess.run(["import", "-window", ids[-1], path],
                   env={**os.environ, "DISPLAY": display}, capture_output=True)
    return os.path.exists(path) and os.path.getsize(path) > 0


def press(display, key, hold=0.25):
    env = {**os.environ, "DISPLAY": display}
    subprocess.run(["xdotool", "keydown", key], env=env)
    time.sleep(hold)
    subprocess.run(["xdotool", "keyup", key], env=env)


def main():
    display = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/drive-once.png"
    if not shot(display, out):
        return 4
    state = ds.classify(out)
    print(state)
    if state == "WORLD":
        return 3
    if state == "DIALOG":
        press(display, "z")
        time.sleep(1.5)
    elif state == "MENU":
        press(display, "z")
        time.sleep(2.0)
    elif state == "LIST":
        # The cursor already rests on "Create New World" in this build.
        press(display, "z")
        time.sleep(2.0)
    elif state == "FORM":
        # How many Downs reach "Create New World" is exactly the kind of thing
        # two runs disagreed about (this campaign needed six, piste 42 reports
        # two). Rather than encode either number, try one and check the screen:
        # if we are still on the form, the cursor had further to travel, so
        # press the rest. The loop is bounded, so a wrong guess costs two
        # presses and never a lost run.
        for attempt, downs in enumerate((2, 4, 2)):
            for _ in range(downs):
                press(display, "Down", 0.2)
                time.sleep(0.35)
            press(display, "z")
            time.sleep(2.0)
            if not shot(display, out):
                return 4
            if ds.classify(out) != "FORM":
                break
    elif state == "LOADING":
        time.sleep(2)
    else:
        press(display, "z")
        time.sleep(1.5)
    return 0


if __name__ == "__main__":
    sys.exit(main())
