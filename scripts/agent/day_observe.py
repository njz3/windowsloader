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

"""Daytona Championship USA: the game left in attract mode past the shell's security checks
(the game quits after five unanswered ones, ~54 s), then a coin and Start. Captures of the back
buffer every 10 s; the shell, motor and input lines are in the log. The game runs at its own
pace (not vsynced while its window is hidden): the waits are in seconds.

    python scripts/agent/day_observe.py [nse] [SECONDS]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
nse = "nse" in sys.argv[1:]
seconds = int(next((a for a in sys.argv[1:] if a.isdigit()), "100"))
GAME = os.path.join(ROOT, "games", "daytonausa-nse" if nse else "daytonausa", "Sega", "Daytona", "Daytona.exe")
OUT = os.path.join(ROOT, "logs", "day-nse" if nse else "day")
os.makedirs(OUT, exist_ok=True)

with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "day.log"), extra_args=("--timeout", str(seconds + 120))) as game:
    print("ready", game.game, game.width, game.height, flush=True)
    for i in range(seconds // 10):
        time.sleep(10)
        frame = game.frame()
        game.shot(os.path.join(OUT, "attract-%02d.bmp" % i))
        print("t=%3ds frame %d ffb %s" % ((i + 1) * 10, frame, game.ffb()[-3:]), flush=True)
    game.press("Coin", 10)
    time.sleep(1)
    game.press("Start", 10)
    for i in range(3):
        time.sleep(5)
        game.shot(os.path.join(OUT, "start-%d.bmp" % i))
    print("after start: frame %d ffb %s" % (game.frame(), game.ffb()[-5:]), flush=True)
