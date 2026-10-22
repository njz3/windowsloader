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

"""Ford Racing Full Blown: what each switch of the Happ board does. In the attract mode each bit
is pressed alone, a picture taken 3 s later; then the steering and the accelerator on the screen
the start leads to.

    python scripts/agent/fr_bits.py [BIT...]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "fordracing", "FordRacing", "fordracing.exe")
OUT = os.path.join(ROOT, "logs", "frbits")
os.makedirs(OUT, exist_ok=True)
bits = [int(b) for b in sys.argv[1:]] or [0, 1, 2, 3, 8, 9, 10, 11]


def shot(game, name):
    try:
        game.shot(os.path.join(OUT, name + ".bmp"))
    except AgentError:
        print(name, "no frame", flush=True)


def tap(game, name, seconds=0.3, value=1):
    game.set(name, value)
    time.sleep(seconds)
    game.release(name)


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "frbits.log"), extra_args=("--timeout", "400")) as game:
    time.sleep(20)
    shot(game, "00-attract")
    for b in bits:
        tap(game, "Bit%d" % b)
        time.sleep(3)
        shot(game, "bit%02d" % b)
        print("bit", b, "frame", game.frame(), flush=True)
    for name, value in (("Steering", -0.8), ("Steering", 0.8), ("Accelerator", 1)):
        tap(game, name, 0.5, value)
        time.sleep(2)
        shot(game, "%s-%s" % (name, "right" if value < 0 else "left" if name == "Steering" else "on"))
