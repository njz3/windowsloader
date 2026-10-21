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

"""Sega Rally 3 in a race: the brake, the view button pressed three times, then each switch whose
role is unknown held while driving (the handbrake?), with a picture before and after each.

    python scripts/agent/sr3_controls.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe")
OUT = os.path.join(ROOT, "logs", "sr3controls")

os.makedirs(OUT, exist_ok=True)


def shot(game, name):
    game.shot(os.path.join(OUT, name + ".bmp"))


def tap_accel(game, wait):
    game.set("Accelerator", 1)
    game.step(40)
    game.release("Accelerator")
    game.step(wait)


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "sr3controls.log")) as game:
    switches = [n for n, t in game.inputs() if t == "switch"]
    game.step(300)
    game.press("Start", 15)
    game.step(90)
    tap_accel(game, 120)            # championship
    tap_accel(game, 150)            # car
    tap_accel(game, 150)            # gearbox AT
    game.step(300)                  # countdown

    # Brake: full speed, then the brake alone for 60 frames
    game.set("Accelerator", 1)
    game.step(300)
    shot(game, "brake-a")
    game.release("Accelerator")
    game.set("Brake", 1)
    game.step(60)
    shot(game, "brake-b")
    game.release("Brake")

    # View: three presses, a picture after each
    game.set("Accelerator", 0.7)
    game.step(60)
    shot(game, "view-0")
    for i in range(1, 4):
        game.press("ViewChange", 10)
        game.step(30)
        shot(game, "view-%d" % i)

    # The unknown switches, each held 40 frames at a steady speed
    for name in switches:
        if not name.startswith("Bit"):
            continue
        game.set("Accelerator", 0.7)
        game.step(90)
        shot(game, name + "-a")
        game.setraw(name, 1)
        game.step(40)
        shot(game, name + "-b")
        game.release(name)
        print(name, "frame", game.frame(), flush=True)
