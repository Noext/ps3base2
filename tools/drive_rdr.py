#!/usr/bin/env python3
"""Drive Red Dead Redemption by looking at the screen, and capture what it shows.

Why this exists
---------------
A fixed press schedule desynchronises: RDR's start-up moves by 15 s between two
runs of the same binary (measured 27.7 s and 42.6 s to the title on the same
machine, load 12 and 30). A schedule that presses START at t=40 s therefore hits
the title on one run and the intro scene on the next -- which is exactly what
happened here before this driver existed: three runs were spent pressing into a
black frame while the title had not appeared yet.

So the driver closes the loop. It grabs the screen, runs tesseract on two bands,
classifies what it sees, and only then decides the next press. Every decision is
journaled with its timestamp and the text that produced it, so a run can be
re-read afterwards without the pictures.

One press per SCREEN, not one press per SAMPLE
----------------------------------------------
This is the bug this version fixes, and it is worth stating because it produced
a false pass. The previous version consumed one entry of its --plan list per
ALERT *sample*. The ALERT is sampled several times per second, so a plan of
[square, cross] spent BOTH entries on the one ALERT screen: square dismissed the
ALERT, and the cross -- meant for the ALERT -- landed on the WARNING that
followed it. The run "got past the WARNING" only because the ALERT happened to
be sampled twice; with a single sample the driver sat on the WARNING with an
empty plan and never pressed anything.

A press is now attached to the SCREEN it acts on, and a screen's press is
re-armed only when the driver has evidence the screen is still up some seconds
later (the pad queue drains in ~4 s, so a retry inside that window would
double-press). The evidence that a screen was actually left is the appearance of
the screen that follows it.

The two bands, and why they are separate
----------------------------------------
RDR draws its dialogues over a transparent red frame, so the MAIN MENU's three
rows stay legible underneath the ALERT. A single wide OCR band therefore reads
"Single Player" on the ALERT screen and classifies it as the menu -- measured,
and it cost a run. The centre band holds the dialogue's heading and body, and the
bottom band holds the title prompt and the menu rows; the states are tested in
that order.

  CENTRE (y 150..450)  "A Red Dead Redemption single player save could not be
                        found. What would you like to do?"   -> ALERT
                       "Autosave has been disabled..."       -> WARNING (square branch)
                       "This game saves data automatically..." -> WARNING (cross branch)
  BOTTOM (y 480..680)  "Press START"                          -> TITLE
                       "Scanning for installed downloadable..." -> SCANNING
                       "Online Multiplayer ... Single Player"   -> MENU
                       "Loading..."                             -> LOADING
                       "EXODUS IN AMERICA"                      -> CHAPTER

ALERT and WARNING are separate states on purpose: WARNING is the screen that
FOLLOWS the ALERT (both of the ALERT's continue branches lead to one), so a
driver that folds them together cannot report having seen what it was asked to
capture. They are distinguished by their text, not by their frame -- both are
the same red RDR panel.

Start-up is not a fixed cost: on a loaded machine it is 42 s to the title and on
an idle one 27 s. The driver waits for the screen instead of for a clock.

The two ALERT branches, and the WARNING each one leads to
---------------------------------------------------------
The ALERT's three rows carry the PS3 pad glyphs, drawn in the pixels, and they
are NOT in the usual confirm/cancel order:

    Continue Without Saving  [SQUARE]
    Create A New Save Game   [CROSS]
    Retry                    [CIRCLE]

Read from the capture rather than assumed: pressing cross first picks
"Create A New Save Game" and skips the branch the caller asked for.

Both continue branches lead to a WARNING in the same red frame, with DIFFERENT
body text, and both WARNINGs are dismissed by the same button, Cross = Continue:

  --branch square   ALERT --[square]--> WARNING "Autosave has been disabled.
                    You may enable autosave in the options menu."      [Continue X]
  --branch cross    ALERT --[cross]-->  WARNING "This game saves data
                    automatically at certain points. Do not switch off the power
                    when the HDD access indicator is flashing. Autosave may be
                    turned off at any time in the options menu."
                    (the three ALERT rows are still drawn under it)    [Continue X]

Both then reach the RDR key-art "Loading..." screen and then the chapter card.

Required input, and the reason a run reports "0 savedata"
---------------------------------------------------------
RDR only prompts when it finds no save. With the default vfs/ the tree carries
three RDR save files, ListAutoLoad matches 3, the title loads them and drives on
into the world. Give it an EMPTY savedata/ directory and it draws the ALERT
instead and waits indefinitely:

    PS3BASE_VFS_DIR=<tree with vfs/dev_hdd0/home/00000001/savedata/ empty>

That single variable is the difference between "the ALERT is unreachable" and
"the ALERT is a stable screen". A VFS with the game data copied in and savedata/
left empty is what this driver expects, and it refuses to run otherwise.

Input path
----------
Presses go through PS3BASE_INPUT_CMD, the runtime's own pad queue, applied in
fill() after poll_pad. Synthetic X key events do NOT reach cellPadGetData:
SDL_GetKeyboardFocus() does not return our window under Xvfb, which is why
xdotool-driven menus drift. The queue has no such problem. The queue is drained
by the guest at its own pace, so a press written to the file may not reach the
title for a few seconds -- which is why the retry window below is 10 s and not 1.

usage: drive_rdr.py <bin> <display> <vfs-dir> <out-dir> [max-seconds]
                     [--branch square|cross] [--hold MS] [--gap MS]
                     [--retry S]

  --branch  which ALERT row to press: "square" = Continue Without Saving,
            "cross" = Create A New Save Game. Default "square".
  --hold    how long each queued press is held, milliseconds (default 200).
  --gap     milliseconds of neutral pad after the press (default 120).
  --retry   seconds a screen must persist after its press before the press is
            repeated (default 10.0).

Writes <out-dir>/validation.json (the journal), <out-dir>/run.log (the guest's
log) and <out-dir>/caps/*.png (a frame at every state change, plus a periodic
one, so a run can be checked afterwards).

exit: 0 the run ended by itself, 1 the driver hit max-seconds, 2 usage/environment.
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time

# Bands in the capture's own pixels (RDR runs at 1280x720 with an 8 px offset).
BAND_CENTRE = (1208, 300, 36, 150)      # w, h, x, y -- dialogue heading + body
BAND_BOTTOM = (1208, 200, 36, 480)      # w, h, x, y -- title prompt / menu rows

# The screen each press is expected to produce. Seeing one of these is the
# evidence that the press worked and the screen it acted on was left; until one
# appears the press stays armed and is retried after --retry seconds.
AFTER_ALERT = ('WARNING', 'LOADING', 'CHAPTER')
AFTER_WARNING = ('LOADING', 'CHAPTER')

# Which ALERT row each branch presses, and the body text its WARNING carries.
BRANCHES = {
    'square': ('Continue Without Saving', 'autosave has been disabled'),
    'cross': ('Create A New Save Game', 'saves data automatically'),
}


def band(text):
    return ' '.join(text.split()).strip()


class Driver:
    def __init__(self, args):
        self.args = args
        self.out = args.out
        self.caps = os.path.join(self.out, 'caps')
        self.raw = os.path.join(self.out, 'raw.png')
        self.crop = os.path.join(self.out, 'crop.png')
        self.icmd = os.path.join(self.out, 'icmd')
        self.env = dict(os.environ)
        self.env.update({
            'DISPLAY': args.display,
            'GOWA_FLASH_DIR': os.environ.get(
                'GOWA_FLASH_DIR', '/root/Dev/devflash-out/dev_flash'),
            'PS3BASE_DISC_DIR': os.environ.get(
                'PS3BASE_DISC_DIR', '/root/Dev/rdr-port/vfs/dev_bdvd'),
            'PS3BASE_VFS_DIR': args.vfs,
            'PS3BASE_LOG': 'info',
            'PS3BASE_PAD_LOG': '1',
            'PS3BASE_INPUT_CMD': self.icmd,
            'XDG_RUNTIME_DIR': os.environ.get('XDG_RUNTIME_DIR', '/run/user/0'),
            'MAGICK_TEMPORARY_PATH': self.out,
        })
        self.journal = []
        self.stats = {}
        self.done = set()          # one-shot states: TITLE, MENU
        self.pressed = {}          # state -> elapsed of its last press
        self.satisfied = set()     # states whose following screen was seen
        self.last = None
        self.last_keep = None

    def prepare(self):
        os.makedirs(self.caps, exist_ok=True)
        open(self.icmd, 'w').close()

    def send(self, button):
        with open(self.icmd, 'w') as f:
            f.write('%s %d %d\n' % (button, self.args.hold, self.args.gap))

    def grab(self):
        r = subprocess.run(['import', '-window', 'root', self.raw],
                           env=self.env, capture_output=True)
        return r.returncode == 0 and os.path.exists(self.raw)

    def ocr(self, box):
        w, h, x, y = box
        subprocess.run(['convert', self.raw,
                        '-crop', '%dx%d+%d+%d' % (w, h, x, y), '+repage',
                        '-resize', '150%', '-colorspace', 'gray',
                        '-auto-level', self.crop],
                       env=self.env, capture_output=True)
        r = subprocess.run(['tesseract', self.crop, '-', '--psm', '6'],
                           capture_output=True, text=True)
        return band(r.stdout)

    def classify(self):
        """(state, text). The centre band is tested first, for the reason in
        the module docstring: the menu rows show through the dialogue."""
        centre = self.ocr(BAND_CENTRE)
        low = centre.lower()
        # The WARNING is tested first: it is the screen AFTER the ALERT and it
        # carries the same red panel, so the two are told apart by their body
        # text and nothing else. Both branches' WARNING bodies are matched, so a
        # run on either branch can name the WARNING it actually saw.
        if 'autosave has been disabled' in low or 'saves data automatically' in low:
            return 'WARNING', centre
        if ('save could not be found' in low or 'would you like to do' in low
                or low.startswith('alert')):
            return 'ALERT', centre
        bottom = self.ocr(BAND_BOTTOM)
        low = bottom.lower()
        if 'press start' in low:
            return 'TITLE', bottom
        if 'scanning' in low:
            return 'SCANNING', bottom
        if ('online multiplayer' in low or 'single player' in low
                or 'downloadable content' in low):
            return 'MENU', bottom
        if 'loading' in low:
            return 'LOADING', bottom
        if 'exodus in america' in low or 'red dead redemption' in low:
            return 'CHAPTER', bottom
        return 'OTHER', (centre + ' | ' + bottom)[:160]

    def keep(self, name):
        subprocess.run(['convert', self.raw, os.path.join(self.caps, name)],
                       env=self.env, capture_output=True)

    def step(self, elapsed, n):
        if not self.grab():
            return
        state, text = self.classify()
        self.stats[state] = self.stats.get(state, 0) + 1
        if state != self.last:
            self.journal.append({'t': round(elapsed, 2), 'n': n,
                                 'state': state, 'text': text[:200]})
            print('%6.2f STATE %-9s %s' % (elapsed, state, text[:100]), flush=True)
            self.keep('%s-%04d.png' % (state, n))
            self.last_keep = state
        elif n % 12 == 0:
            self.keep('t%05d-%s.png' % (n, state))
        self.act(state, elapsed, n)
        self.last = state

    def act(self, state, elapsed, n):
        # Seeing the screen that FOLLOWS a press is the evidence the press
        # landed: from then on that screen is satisfied and is never re-pressed
        # on this run, however often it is sampled.
        if state in AFTER_ALERT:
            self.satisfied.add('ALERT')
        if state in AFTER_WARNING:
            self.satisfied.add('WARNING')

        if state == 'TITLE' and 'title' not in self.done:
            self.done.add('title')
            self.send('start')
            print('%6.2f -> start' % elapsed, flush=True)
        elif state == 'MENU' and 'menu' not in self.done:
            self.done.add('menu')
            # The cursor starts on Online Multiplayer; Single Player is two rows
            # right. The gaps are longer than one frame: a menu that has not
            # finished drawing eats a press.
            self.send('right')
            time.sleep(0.7)
            self.send('right')
            time.sleep(0.7)
            self.send('cross')
            print('%6.2f -> right,right,cross (Single Player)' % elapsed, flush=True)
        elif state == 'ALERT' and 'ALERT' not in self.satisfied:
            self.press_once('ALERT', self.args.branch, elapsed, n,
                            BRANCHES[self.args.branch][0])
        elif state == 'WARNING' and 'WARNING' not in self.satisfied:
            # Both branches' WARNINGs are dismissed by the same row: Continue [X].
            self.press_once('WARNING', 'cross', elapsed, n, 'Continue')

    def press_once(self, state, button, elapsed, n, label):
        """Press ONCE per screen; re-press only if the screen is still up
        --retry seconds later, because the pad queue can take ~4 s to drain and
        a retry inside that window would double-press."""
        last = self.pressed.get(state)
        if last is not None and elapsed - last < self.args.retry:
            return
        self.pressed[state] = elapsed
        self.send(button)
        attempt = sum(1 for key in self.pressed if key == state)
        print('%6.2f -> %s (%s: %s)%s'
              % (elapsed, button, state, label,
                 '' if attempt == 1 else ' [retry]'), flush=True)
        self.keep('P%d-%s-%s-%04d.png' % (len(self.pressed), state, button, n))

    def run(self):
        self.prepare()
        log = open(os.path.join(self.out, 'run.log'), 'w')
        started = time.time()
        proc = subprocess.Popen([self.args.binary, '--no-launcher'],
                                cwd=self.args.cwd, env=self.env,
                                stdout=log, stderr=subprocess.STDOUT)
        n = 0
        try:
            while proc.poll() is None and time.time() - started < self.args.max_seconds:
                n += 1
                self.step(time.time() - started, n)
                time.sleep(0.15)
        finally:
            elapsed = time.time() - started
            code = proc.poll()
            # One last grab, so the deliverable is the LAST screen reached even
            # when the guest dies between two samples.
            if self.grab():
                self.keep('LAST-%04d.png' % n)
            if code is None:
                proc.send_signal(signal.SIGKILL)
            print('exit %s elapsed %.1f' % (code, elapsed), flush=True)
            print('states %s' % json.dumps(self.stats), flush=True)
            with open(os.path.join(self.out, 'validation.json'), 'w') as f:
                json.dump({'exit': code, 'elapsed': round(elapsed, 1),
                           'branch': self.args.branch,
                           'states': self.stats, 'journal': self.journal}, f, indent=1)
            log.close()
        return 0 if code is not None else 1


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('binary')
    ap.add_argument('display', help='X display, e.g. :145')
    ap.add_argument('vfs', help='VFS root; its dev_hdd0/home/00000001/savedata must be EMPTY')
    ap.add_argument('out', help='output directory')
    ap.add_argument('max_seconds', nargs='?', type=float, default=180.0)
    ap.add_argument('--branch', default='square', choices=sorted(BRANCHES),
                    help='ALERT row to press: square = Continue Without Saving, '
                         'cross = Create A New Save Game (default square)')
    ap.add_argument('--hold', type=int, default=200)
    ap.add_argument('--gap', type=int, default=120)
    ap.add_argument('--retry', type=float, default=10.0,
                    help='seconds a screen must persist after its press before '
                         'the press is repeated (default 10.0)')
    ap.add_argument('--cwd', default=None,
                    help='working directory for the title (default: the checkout holding out-rdr)')
    args = ap.parse_args(argv)
    if not os.path.exists(args.binary):
        print('drive_rdr: no binary at %s' % args.binary, file=sys.stderr)
        return 2
    if not os.path.isdir(args.vfs):
        print('drive_rdr: no vfs at %s' % args.vfs, file=sys.stderr)
        return 2
    savedata = os.path.join(args.vfs, 'dev_hdd0/home/00000001/savedata')
    if os.path.isdir(savedata) and os.listdir(savedata):
        print('drive_rdr: %s is not empty; RDR will load a save instead of prompting.'
              % savedata, file=sys.stderr)
        return 2
    for tool in ('import', 'convert', 'tesseract'):
        if not shutil.which(tool):
            print('drive_rdr: %s is required' % tool, file=sys.stderr)
            return 2
    if args.cwd is None:
        args.cwd = os.environ.get('PS3BASE_RUN_ROOT', '/root/Dev/ps3base2')
    return Driver(args).run()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
