#!/usr/bin/env python3
"""Drive the title by *looking at the screen*, and count what it sees.

Why this exists: the run book's sequence was a fixed list of presses, and a
fixed list desynchronises the moment the title inserts a screen it did not
expect (a dialog that swallows a press). This driver closes the loop instead:
it screenshots, classifies the screen, and only then decides the next press.
Every decision is written to a journal, so a run can be re-read later without
the picture.

Screens are classified from the pixels, not from timing:
  DIALOG   the grey message box with its blue OK bar
  MENU     the main menu (Play Game row visible)
  LIST     Start Game / Create New World panel
  FORM     the new-world form (World Name field)
  WORLD    a rendered world (hotbar band at the bottom)
  LOADING  "Initializing server"/"Generating spawn area"
  UNKNOWN  anything else (counted, never guessed at)
"""
import json, subprocess, sys, time, os

CAP = None
DISPLAY = None

def shot(path):
    w = subprocess.run(["xdotool", "search", "--name", "Minecraft"],
                       capture_output=True, text=True,
                       env={**os.environ, "DISPLAY": DISPLAY})
    ids = [l for l in w.stdout.split() if l.strip()]
    if not ids:
        return False
    subprocess.run(["import", "-window", ids[-1], path],
                   env={**os.environ, "DISPLAY": DISPLAY},
                   capture_output=True)
    return os.path.exists(path) and os.path.getsize(path) > 0

def _thumb(path):
    from PIL import Image
    import numpy as np
    im = Image.open(path).convert("L").resize((64, 36), Image.BILINEAR)
    a = np.asarray(im).astype(float)
    return (a - a.mean()) / (a.std() + 1e-6)


def _refs():
    global _REFS
    try:
        return _REFS
    except NameError:
        pass
    _REFS = []
    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "screens")
    if os.path.isdir(base):
        for name in sorted(os.listdir(base)):
            if not name.endswith(".png"):
                continue
            label = name.rsplit(".", 1)[0].split("-")[0].upper()
            _REFS.append((label, _thumb(os.path.join(base, name))))
    return _REFS


def classify(path):
    """Classify a frame by nearest reference frame.

    Raises if the file is not a readable image; callers treat that as "no state
    this frame" rather than as a screen.

    Colour heuristics were tried first and mis-fired -- a loading banner scored
    as a menu, the new-world form as a world. Every reference under tools/screens
    is a frame this campaign looked at and named by eye, so the labels are
    grounded in the picture instead of in a threshold. Downscaled greyscale
    makes it cheap and insensitive to the pixel-level noise between two captures
    of the same screen.
    """
    from PIL import Image
    import numpy as np
    refs = _refs()
    if not refs:
        return "OTHER"
    t = _thumb(path)
    best, bestd = "OTHER", None
    for label, ref in refs:
        d = float(np.abs(t - ref).mean())
        if bestd is None or d < bestd:
            best, bestd = label, d
    return best


def press(key, hold=0.25):
    env = {**os.environ, "DISPLAY": DISPLAY}
    subprocess.run(["xdotool", "keydown", key], env=env)
    time.sleep(hold)
    subprocess.run(["xdotool", "keyup", key], env=env)

def main():
    global DISPLAY
    DISPLAY = sys.argv[1]
    journal = sys.argv[2]
    max_s = float(sys.argv[3]) if len(sys.argv) > 3 else 1200
    shots = sys.argv[4] if len(sys.argv) > 4 else "/tmp/drive-shots"
    os.makedirs(shots, exist_ok=True)
    t0 = time.time()
    events = []
    n = 0
    while time.time() - t0 < max_s:
        n += 1
        p = f"{shots}/s-{n:04d}.png"
        if not shot(p):
            time.sleep(1)
            continue
        state = classify(p)
        events.append({"t": round(time.time() - t0, 2), "shot": p, "state": state})
        sys.stderr.write(f"{events[-1]['t']:7.1f}s {state}\n")
        if state == "DIALOG":
            # confirm button
            press("z"); time.sleep(2.5)
        elif state == "MENU":
            press("z"); time.sleep(3.0)
        elif state == "LIST":
            # cursor starts on "Create New World"
            press("z"); time.sleep(3.0)
        elif state == "FORM":
            for _ in range(6):
                press("Down", 0.2); time.sleep(0.5)
            press("z"); time.sleep(3.0)
        elif state == "LOADING":
            time.sleep(4)
        elif state == "WORLD":
            # nothing to do; the run has succeeded
            time.sleep(5)
        else:
            press("z"); time.sleep(2.0)
    with open(journal, "w") as f:
        json.dump(events, f, indent=1)
    counts = {}
    for e in events:
        counts[e["state"]] = counts.get(e["state"], 0) + 1
    print(json.dumps({"counts": counts, "events": len(events)}, indent=1))

if __name__ == "__main__":
    main()


def form_focus(path):
    """Box of the focused form widget, as (top, bottom), or None.

    Counting Down presses is not reliable at this title's frame rate: measured,
    six Downs entered the form, a press was dropped, the cursor ended on
    "More Options", and the confirm press opened that panel instead of creating
    the world. The focus ring is drawn on the ground, so read it instead of
    counting. A ring is two horizontal yellow edges about 50 px apart; pairing
    them yields the widget's box.
    """
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(path).convert("RGB")).astype(int)
    h, w, _ = a.shape
    sx, sy = w / 1208.0, h / 680.0
    m = (a[:, :, 0] > 190) & (a[:, :, 1] > 180) & (a[:, :, 2] < 140)
    sub = m[:, int(300 * sx):int(915 * sx)]
    cnt = sub.sum(axis=1)
    rows = np.where(cnt > max(150, 0.25 * sub.shape[1]))[0]
    if not len(rows):
        return None
    bands = []
    s = prev = rows[0]
    for r in rows[1:]:
        if r - prev > 3:
            bands.append((s, prev))
            s = r
        prev = r
    bands.append((s, prev))
    bands = [(int(x / sy), int(y / sy)) for x, y in bands]
    # Pair the band tops: a ring's top edge and its bottom edge are 40-60 px
    # apart. Anything unpaired is a highlighted texture tile, not a row.
    for i in range(len(bands) - 1):
        gap = bands[i + 1][0] - bands[i][0]
        if 40 <= gap <= 60:
            return bands[i]
    return None


# Row geometry of the form, in 1208x680 coordinates, measured from the
# screenshots: each entry is the focus ring's top edge.
FORM_ROWS = {
    "world_name": 65,
    "seed": 136,
    "difficulty": 393,
    "more_options": 445,
    "create": 504,
}


def form_row(path):
    """Name the focused form row, or None if the ring is not where we know."""
    band = form_focus(path)
    if band is None:
        return None
    top = band[0]
    best, bestd = None, None
    for name, y in FORM_ROWS.items():
        d = abs(top - y)
        if bestd is None or d < bestd:
            best, bestd = name, d
    return best if bestd is not None and bestd <= 12 else None


# The form's widgets, by the y of their highlight, measured with short taps:
#   65 World Name (yellow ring)   136 Seed (yellow ring)
#   208 Game Mode (lavender bar)  393 Difficulty (yellow ring)
#   449 More Options (lavender)   507 Create New World (lavender)
FORM_CREATE_Y = 507
FORM_HIGHLIGHT_TOL = 6
# The form's text rows. When one of these holds the focus ring, the field has
# taken keyboard focus and consumes Down/Up as text editing, so navigation does
# not move the selection at all -- the cursor sits there and the next confirm
# does nothing. Measured: this is what made three agents' fixed press counts
# stop working mid-run.
FORM_TEXT_ROWS = (110, 154)   # World Name, Seed
FORM_TEXT_TOL = 14


def form_highlight(path):
    """The y-top of the focused form widget's highlight, or None.

    Two kinds of highlight exist on this form and both must be read: text
    fields and tick rows get a yellow focus ring, and the plain buttons get a
    lavender selection bar. Looking for only one of them made the driver blind
    to exactly the widget it needed ("Create New World" is a button).

    Pressing works because of the *short* tap: the menus act on key repeat, and
    a 0.35 s hold at this title's ~6 fps frame rate is sampled twice, moving the
    cursor two rows for one press. That is the desynchronisation this driver
    removes -- a 30 ms tap plus the host input edge latch delivers exactly one
    step per press.
    """
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(path).convert("RGB")).astype(int)
    h, w, _ = a.shape
    sy = h / 680.0
    R, G, B = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    yellow = (R > 190) & (G > 180) & (B < 140)
    lavender = (B > R + 25) & (B > 150) & (G > 110) & (R > 90) & (R < 200)
    left, right = int(300 * w / 1208.0), int(915 * w / 1208.0)
    found = []
    for mask in (yellow, lavender):
        sub = mask[:, left:right]
        cnt = sub.sum(axis=1)
        rows = np.where(cnt > 200)[0]
        if not len(rows):
            continue
        s = prev = rows[0]
        bands = []
        for r in rows[1:]:
            if r - prev > 4:
                bands.append((s, prev))
                s = r
            prev = r
        bands.append((s, prev))
        for top, _bot in bands:
            top = int(top / sy)
            if top > 50:      # skip the title/status band at the very top
                found.append(top)
    if not found:
        return None
    return found[0]


def form_text_focused(path):
    """True if the focus ring sits on a text field (Name or Seed).

    A text field swallows the arrow keys, so the driver must leave it with
    Escape before it can navigate. Without this the walk-Down loop below spins
    forever on a ring that never moves.
    """
    y = form_highlight(path)
    if y is None:
        return False
    return any(abs(y - r) <= FORM_TEXT_TOL for r in FORM_TEXT_ROWS)
