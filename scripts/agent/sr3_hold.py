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

"""Sega Rally 3 held in one aspect ratio and display mode for some seconds, so that the screen
can be looked at or captured from outside the game.

    python scripts/agent/sr3_hold.py ASPECT MODE SECONDS
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe")
OUT = os.path.join(ROOT, "logs", "sr3display")

aspect, mode, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
os.makedirs(OUT, exist_ok=True)
with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "sr3hold.log")) as game:
    game.step(300)
    print("display", game.display(aspect, mode), flush=True)
    print("holding", flush=True)
    time.sleep(seconds)
    game.screen(os.path.join(OUT, "hold-inside.bmp"))
    game.shot(os.path.join(OUT, "hold-backbuffer.bmp"))
