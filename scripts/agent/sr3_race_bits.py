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

"""Sega Rally 3: the switches of the emulated I/O board during a race (view, gears...).
Starts a championship race with the manual gearbox, then presses each switch while driving,
with a picture before and after each press.

    python scripts/agent/sr3_race_bits.py [first_bit] [last_bit]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe")
OUT = os.path.join(ROOT, "logs", "sr3race")

first = int(sys.argv[1]) if len(sys.argv) > 1 else 1
last = int(sys.argv[2]) if len(sys.argv) > 2 else 23

os.makedirs(OUT, exist_ok=True)


def tap_accel(game, wait):
    game.set("Accelerator", 1)
    game.step(40)
    game.release("Accelerator")
    game.step(wait)


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "sr3race.log")) as game:
    game.step(300)
    game.press("Start", 15)
    game.step(90)
    tap_accel(game, 120)            # championship
    tap_accel(game, 150)            # car
    game.set("Steering", -1)        # gearbox: AT -> MT
    game.step(40)
    game.release("Steering")
    game.step(30)
    game.shot(os.path.join(OUT, "gearbox.bmp"))
    tap_accel(game, 150)
    game.step(300)                  # countdown
    game.set("Accelerator", 0.6)
    game.step(200)
    for bit in range(first, last + 1):
        name = "Bit%d" % bit
        game.shot(os.path.join(OUT, "%s-a.bmp" % name))
        game.press(name, 15)
        game.step(30)
        game.shot(os.path.join(OUT, "%s-b.bmp" % name))
        print(name, "frame", game.frame(), flush=True)
