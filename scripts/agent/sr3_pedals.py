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

"""Sega Rally 3: the steering and the pedals of the emulated I/O board, on the mode select
screen ("select with the wheel, confirm with the accelerator"), then in the race.

    python scripts/agent/sr3_pedals.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe")
OUT = os.path.join(ROOT, "logs", "sr3pedals")

os.makedirs(OUT, exist_ok=True)


def shot(game, name):
    game.shot(os.path.join(OUT, name + ".bmp"))
    print(name, "frame", game.frame(), flush=True)


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "sr3pedals.log")) as game:
    game.step(300)
    game.press("Start", 15)
    game.step(90)
    shot(game, "1-mode")
    game.set("Steering", -1)        # full right
    game.step(40)
    game.release("Steering")
    game.step(30)
    shot(game, "2-right")
    game.set("Steering", 1)         # full left
    game.step(40)
    game.release("Steering")
    game.step(30)
    shot(game, "3-left")
    game.set("Brake", 1)            # should not confirm
    game.step(40)
    game.release("Brake")
    game.step(60)
    shot(game, "4-brake")
    game.set("Accelerator", 1)      # confirm
    game.step(40)
    game.release("Accelerator")
    game.step(120)
    shot(game, "5-accel")
    for i in range(4):              # confirm the next screens (car, gearbox...)
        game.set("Accelerator", 1)
        game.step(40)
        game.release("Accelerator")
        game.step(150)
        shot(game, "6-next%d" % i)
    game.set("Accelerator", 1)      # race: full throttle
    game.step(600)
    shot(game, "7-race")
    game.set("Steering", 0.5)
    game.step(60)
    shot(game, "8-race-left")
