#!/usr/bin/env python3
"""Capture the title's window on a display. usage: shot.py <display> <out.png>"""
import os
import subprocess
import sys

d, out = sys.argv[1], sys.argv[2]
ids = subprocess.run(["xdotool", "search", "--name", "Minecraft"],
                     capture_output=True, text=True,
                     env={**os.environ, "DISPLAY": d}).stdout.split()
if not ids:
    sys.exit(4)
subprocess.run(["import", "-window", ids[-1], out],
               env={**os.environ, "DISPLAY": d}, capture_output=True)
sys.exit(0 if os.path.exists(out) else 5)
