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

"""Sega Rally 3: is a switch the handbrake? On the straight after the start, full throttle all
along: 30 frames as a control, then 30 frames with the switch held, then 30 frames released.
A handbrake slows the car down while the throttle stays open.

    python scripts/agent/sr3_handbrake.py [switch] (Bit2 by default)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "segarally3", "Rally", "Rally.exe")
OUT = os.path.join(ROOT, "logs", "sr3handbrake")
switch = sys.argv[1] if len(sys.argv) > 1 else "Bit2"

os.makedirs(OUT, exist_ok=True)


def tap_accel(game, wait):
    game.set("Accelerator", 1)
    game.step(40)
    game.release("Accelerator")
    game.step(wait)


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "sr3handbrake.log")) as game:
    game.step(300)
    game.press("Start", 15)
    game.step(90)
    tap_accel(game, 120)            # championship
    tap_accel(game, 150)            # car
    tap_accel(game, 150)            # gearbox AT
    game.step(300)                  # countdown
    game.set("Accelerator", 1)
    game.step(90)
    game.shot(os.path.join(OUT, "0-start.bmp"))
    game.step(30)
    game.shot(os.path.join(OUT, "1-control.bmp"))
    game.setraw(switch, 1)
    game.step(30)
    game.shot(os.path.join(OUT, "2-held.bmp"))
    game.release(switch)
    game.step(30)
    game.shot(os.path.join(OUT, "3-released.bmp"))
    print("done", switch, flush=True)
