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

"""Sega Rally 3: the force feedback the game asks for (port 0x1020), with the wheel held to one
side then the other in a race. The centring force must push against the turn: right-turned
wheel, force to the left (> 0 in the agent's convention).

    python scripts/agent/sr3_ffb.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe")
OUT = os.path.join(ROOT, "logs", "sr3ffb")

os.makedirs(OUT, exist_ok=True)


def tap_accel(game, wait):
    game.set("Accelerator", 1)
    game.step(40)
    game.release("Accelerator")
    game.step(wait)


def phase(game, name, steering, frames):
    game.ffb()
    if steering is None:
        game.release("Steering")
    else:
        game.set("Steering", steering)
    game.step(frames)
    events = game.ffb()
    forces = [f for _, f in events]
    mean = sum(forces) / len(forces) if forces else 0.0
    print("%-14s %4d changes, mean %+.3f, min %+.3f, max %+.3f" % (name, len(forces), mean, min(forces, default=0), max(forces, default=0)),
          flush=True)
    game.shot(os.path.join(OUT, name + ".bmp"))
    return events


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "sr3ffb.log")) as game:
    game.step(300)
    print("attract", game.ffb()[:10], flush=True)
    game.press("Start", 15)
    game.step(90)
    tap_accel(game, 120)            # championship
    tap_accel(game, 150)            # car
    tap_accel(game, 150)            # gearbox AT
    game.step(300)                  # countdown
    game.set("Accelerator", 0.8)
    phase(game, "straight", None, 150)
    phase(game, "right", -0.8, 90)
    phase(game, "centre", 0, 90)
    phase(game, "left", 0.8, 90)
    phase(game, "centre2", 0, 90)
    phase(game, "stopped", None, 10)
