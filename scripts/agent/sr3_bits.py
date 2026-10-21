# windowsloader - runs Windows arcade games with emulated cabinet hardware
# Copyright (C) 2026 Benjamin Maurin - njz3
# License: GNU General Public License version 3 (GPL v3)
#
# This program is free software: you can redistribute it and/or modify it under the terms of
# the GNU General Public License version 3 as published by the Free Software Foundation.
# It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
# the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
# file for the full text. The author reserves the right to change the license of this code.
#
# Code taken from MAME, linuxloader, sm2-emu, Flycast or segatools stays under the license of
# its own project; such code is marked with its origin.

"""Sega Rally 3: which switch of the emulated I/O board does what. Starts the game, lets the
attract mode run, then presses each of the 24 switches in turn and takes a picture after each.

    python scripts/agent/sr3_bits.py [first_bit] [last_bit] [wait_frames]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe")
OUT = os.path.join(ROOT, "logs", "sr3bits")

first = int(sys.argv[1]) if len(sys.argv) > 1 else 0
last = int(sys.argv[2]) if len(sys.argv) > 2 else 23
wait = int(sys.argv[3]) if len(sys.argv) > 3 else 90

os.makedirs(OUT, exist_ok=True)
with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "sr3bits.log")) as game:
    print("ready", game.game, game.width, game.height, flush=True)
    switches = [n for n, t in game.inputs() if t == "switch"]  # in bit order: Start, ViewChange, Bit2...
    print("switches:", " ".join(switches), flush=True)
    game.step(300)
    game.shot(os.path.join(OUT, "start.bmp"))
    for name in switches[first:last + 1]:
        game.press(name, 15)
        game.step(wait)
        game.shot(os.path.join(OUT, "%s.bmp" % name))
        print(name, "frame", game.frame(), flush=True)
