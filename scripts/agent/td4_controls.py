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

"""Thrill Drive 4: the boot (its wheel calibration), a coin, START, then each control held in a
race, a picture after each, the force feedback and the lamps from the DLL's log.

    python scripts/agent/td4_controls.py [boot frames]

Pictures and log in logs/td4/ (PNG, half size).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import WLAgent  # noqa: E402
import gvr_controls_lib as lib  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "td4", "td4.exe")
OUT = os.path.join(ROOT, "logs", "td4")
os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(OUT, "game.log")
boot = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
L = lib.Phases(OUT, LOG, r"thrill drive 4: (motor|software|ACIO)|lamp|outputs")

with WLAgent(LAUNCHER, GAME, log=LOG, extra_args=["--set", "Display.PRESENT=DIRECT"]) as game:
    print("ready", game.game, game.width, game.height, flush=True)
    game.step(boot)
    L.shot(game, "00-attract")
    for _ in range(2):
        game.press("Coin", 10)
        game.step(30)
    L.shot(game, "01-coins")
    for i in range(6):
        game.press("Start", 10)
        game.step(150)
        L.shot(game, "02-start%d" % i)
    L.phase(game, "03-gas", [("Accelerator", 1)], 300)
    L.phase(game, "04-gas-left", [("Accelerator", 1), ("Steering", 1)], 120)
    L.phase(game, "05-gas-right", [("Accelerator", 1), ("Steering", -1)], 120)
    L.phase(game, "06-brake", [("Brake", 1)], 90)
    for button in ("View", "ShiftUp", "ShiftDown", "Start"):
        L.phase(game, "07-" + button, [("Accelerator", 1), (button, 1)], 40)
    L.phase(game, "08-gas", [("Accelerator", 1)], 300)
    game.quit()
