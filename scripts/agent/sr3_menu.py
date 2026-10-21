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

"""The F10 menu over Sega Rally 3: opened during the attract mode, a picture of each page, then
closed; the game must go on from where it stopped.

    python scripts/agent/sr3_menu.py
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe")
OUT = os.path.join(ROOT, "logs", "sr3menu")

os.makedirs(OUT, exist_ok=True)

with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "sr3menu.log")) as game:
    game.step(400)
    game.shot(os.path.join(OUT, "0-before.bmp"))
    before = game.frame()
    print("open:", game.menu(True), flush=True)
    for tab in range(4):
        game.menu(tab=tab)
        time.sleep(1.0)              # the game's frames stand still: wait in real time
        game.shot(os.path.join(OUT, "1-tab%d.bmp" % tab))
    print("frames during the menu:", game.frame() - before, flush=True)
    print("open:", game.menu(False), flush=True)
    game.step(120)
    game.shot(os.path.join(OUT, "2-after.bmp"))
    print("frames after:", game.frame() - before, flush=True)
