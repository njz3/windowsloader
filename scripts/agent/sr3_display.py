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

"""Sega Rally 3 shown with each aspect ratio and display mode: a picture of what the window
shows for each pair, then the window mode back. With WIDTH and HEIGHT given, the game is started
at that render resolution (windowsloader.ini is set for the run, then restored).

    python scripts/agent/sr3_display.py [WIDTH HEIGHT]
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe")
INI = os.path.join(ROOT, "bin", "Release", "windowsloader.ini")
OUT = os.path.join(ROOT, "logs", "sr3display")

os.makedirs(OUT, exist_ok=True)
size = sys.argv[1:3]
saved = None
if size and os.path.exists(INI):
    saved = INI + ".saved"
    shutil.copyfile(INI, saved)
    lines = [l for l in open(INI, encoding="utf-8")]
    with open(INI, "w", encoding="utf-8") as f:
        for l in lines:
            if l.startswith("WIDTH"):
                l = "WIDTH = %s\n" % size[0]
            elif l.startswith("HEIGHT"):
                l = "HEIGHT = %s\n" % size[1]
            f.write(l)

try:
    with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "sr3display.log")) as game:
        print("ready", game.width, game.height, flush=True)
        game.step(300)
        if size:
            game.display("ORIGINAL", "WINDOW")
            game.step(20)
            game.screen(os.path.join(OUT, "res-%sx%s.bmp" % tuple(size)))
        else:
            for mode in ("WINDOW", "WINDOW_STRETCH", "FULLSCREEN", "FULLSCREEN_STRETCH"):
                for aspect in ("ORIGINAL", "4:3", "16:9", "4:3_LETTERBOX"):
                    print(game.display(aspect, mode), flush=True)
                    game.step(20)
                    game.screen(os.path.join(OUT, "%s-%s.bmp" % (mode, aspect.replace(":", "x"))))
            game.display("ORIGINAL", "WINDOW")
            game.step(10)
finally:
    if saved:
        shutil.move(saved, INI)
