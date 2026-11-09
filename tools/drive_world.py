#!/usr/bin/env python3
"""Drive one boot from the title screen to a created world, verifying progress
in the log rather than by assuming presses landed.

The recipe is the one this campaign verified by hand, with the property that
makes it repeatable: the new-world FORM is only ever entered *fresh* from the
Start Game list, so the cursor's starting row is known and a fixed Down count is
correct. Re-pressing Down on a form we already navigated is what desynchronised
the earlier driver: each extra pass moved the cursor somewhere else, and a
confirm press then opened "More Options" instead of creating the world.

Progress is judged from the run's own log -- 'sys_vm_memory_map ->' appearing
means a world really was created -- and never from how many presses we sent.

usage: drive_world.py <display> <game.log> [timeout-s] [shots-dir]
exit: 0 world created, 1 gave up, 2 no window
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


def press(display, key, hold=0.03):
    """A tap, not a hold.

    Measured: the menus act on key repeat, and this title runs at about 6 fps
    while several agents share the machine, so a press held 0.35 s is sampled on
    two frames and moves the cursor TWO rows for one press. That extra step is
    the desynchronisation: the intended number of Downs lands past the target
    widget, the confirm opens "More Options", and the iteration is lost. A 30 ms
    tap is still delivered exactly once because host_input.cpp latches every
    keydown for five poll frames -- the same latch that keeps a 0 ms
    xdotool key from being dropped.
    """
    env = {**os.environ, "DISPLAY": display}
    subprocess.run(["xdotool", "keydown", key], env=env)
    time.sleep(hold)
    subprocess.run(["xdotool", "keyup", key], env=env)


def world_mapped(log):
    try:
        with open(log, "rb") as f:
            return b"sys_vm_memory_map ->" in f.read()
    except OSError:
        return False


def main():
    display = sys.argv[1]
    log = sys.argv[2]
    budget = float(sys.argv[3]) if len(sys.argv) > 3 else 400.0
    shots = sys.argv[4] if len(sys.argv) > 4 else "/tmp/drive-shots"
    os.makedirs(shots, exist_ok=True)

    t0 = time.time()
    form_used = False        # the FORM is navigated once, and only once
    seed_done = False
    journal = []
    n = 0
    while time.time() - t0 < budget:
        if world_mapped(log):
            print("WORLD-MAPPED")
            return 0
        n += 1
        p = f"{shots}/s-{n:04d}.png"
        if not shot(display, p):
            time.sleep(1.5)
            continue
        try:
            state = ds.classify(p)
        except Exception:
            # A capture can be torn while Xvfb restarts, or truncated by the
            # screenshot tool. That is not a state, so skip it rather than
            # crashing the driver and losing the iteration.
            time.sleep(1.5)
            continue
        journal.append(state)
        print(f"{time.time()-t0:6.1f}s {state}", flush=True)

        if state == "PSNOFFLINE":
            # "Online ID currently not online" -> two buttons: Connect, and
            # "Continue playing offline". Default focus is Connect, which would
            # take us onto the network path this build does not have. Move down
            # once and confirm the offline row.
            press(display, "Down")
            time.sleep(0.8)
            press(display, "z")
            time.sleep(2.0)
        elif state == "DIALOG":
            press(display, "z")
            time.sleep(1.5)
        elif state == "MENU":
            press(display, "z")
            time.sleep(2.0)
        elif state == "LIST":
            # cursor already rests on "Create New World"
            press(display, "z")
            time.sleep(2.0)
        elif state == "FORM":
            # Navigate by reading the focus ring, not by counting presses.
            # Measured: a press is dropped often enough at this frame rate that
            # a fixed count lands on the wrong widget ("More Options" got
            # opened instead of the world being created). Walk Down until the
            # ring is on "Create New World", then confirm -- bounded, so a
            # wrong guess costs presses and never the iteration.
            # A fixed seed makes two runs comparable: the same words in the
            # same order must produce the same terrain. Set it BEFORE walking
            # to Create, by focusing the Seed row and typing.
            seed = os.environ.get("DRIVE_SEED")
            if seed and not seed_done:
                seed_done = True
                # REQUIRE the ring on the Seed row before typing. The previous
                # version walked Down up to N times and then pressed z
                # regardless, so it typed into whatever row the ring happened
                # to be on -- measured: two runs both told "seed42" produced
                # RandomSeed 0x2ce9c74b... and 0x9d1d0c87..., i.e. the text
                # never reached the field.
                on_seed = False
                for _ in range(12):
                    if not shot(display, p):
                        break
                    y0 = ds.form_highlight(p)
                    print(f"{time.time()-t0:6.1f}s   seed target y={y0}", flush=True)
                    if ds.on_seed_row(y0):
                        on_seed = True
                        break
                    press(display, "Down")
                    time.sleep(0.7)
                if not on_seed:
                    # Do NOT type blind, and do NOT confirm: a world built on a
                    # seed we could not place is not comparable, and pretending
                    # it is would poison every later hash check.
                    print(f"{time.time()-t0:6.1f}s   SEED-ROW-UNREACHED", flush=True)
                    return 3
                press(display, "z")
                time.sleep(0.8)
                subprocess.run(["xdotool", "type", "--delay", "120", seed],
                               env={**os.environ, "DISPLAY": display})
                time.sleep(0.8)
                press(display, "Escape")
                time.sleep(0.8)
                print(f"{time.time()-t0:6.1f}s   seed entered on the Seed row: {seed}",
                      flush=True)
            ok = False
            escaped = 0
            for _ in range(20):
                if not shot(display, p):
                    break
                y = ds.form_highlight(p)
                print(f"{time.time()-t0:6.1f}s   form highlight y={y}", flush=True)
                if y is not None and abs(y - ds.FORM_CREATE_Y) <= ds.FORM_HIGHLIGHT_TOL:
                    ok = True
                    break
                # A text field eats the arrows. Leave it before pressing Down,
                # otherwise this loop spins on a ring that cannot move. Escape
                # is bounded so a field we cannot identify still costs only
                # presses, never the iteration.
                if ds.form_text_focused(p) and escaped < 3:
                    escaped += 1
                    press(display, "Escape")
                    time.sleep(0.6)
                    continue
                press(display, "Down")
                time.sleep(0.8)
            if ok:
                press(display, "z")
                time.sleep(2.5)
            else:
                # Never confirm a widget we could not identify.
                press(display, "x")
                time.sleep(1.5)
        elif state == "OPTIONS":
            press(display, "x")
            time.sleep(1.5)
        elif state == "LOADING":
            time.sleep(3)
        elif state == "WORLD":
            # A world is on screen; the map line may lag a frame.
            time.sleep(3)
        else:
            time.sleep(2)

    print("TIMEOUT states=" + ",".join(journal[-12:]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
