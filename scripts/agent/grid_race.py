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

"""GRID (Sega Europa-R), or DiRT Showdown with "showdown": the title screen, then steps one by
one, a picture and the force feedback of each. The waits are in seconds.

    python scripts/agent/grid_race.py [showdown] [wheel] [STEPS]

wheel: the controllers are opened too (launcher --controllers), the force feedback goes to the
player 1 controller (vJoy on the test PC: warn its other users first).
STEPS: S Start (menus: select), A accelerator held 1 s, H accelerator held 3 s, B brake 1 s,
L / R the wheel left / right 1 s, V view, U / D gear up / down, X reset the car, O boost (Showdown),
M the settings menu
(its pages Controls and Force feedback), w wait 3 s;
the default is "SwSwSwSwSwwwHHH".
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
showdown = "showdown" in sys.argv[1:]
GAME = os.path.join(ROOT, "games", "showdown", "Showdown", "Showdown.exe") if showdown else     os.path.join(ROOT, "games", "grid", "Sega", "Grid", "GRID.exe")
args = [a for a in sys.argv[1:] if a not in ("wheel", "showdown")]
wheel = "wheel" in sys.argv[1:]
steps = args[0] if args else "SwSwSwSwSwwwHHH"
OUT = os.path.join(ROOT, "logs", ("sdnrace" if showdown else "gridrace") + ("-wheel" if wheel else ""))
os.makedirs(OUT, exist_ok=True)


def shot(game, name):
    try:
        game.shot(os.path.join(OUT, name + ".bmp"))
    except AgentError:
        print("%-16s no frame" % name, flush=True)


def tap(game, name, seconds=0.3, value=1):
    game.set(name, value)
    time.sleep(seconds)
    game.release(name)


ACTIONS = {
    "S": ("Start", 0.3, 1), "A": ("Accelerator", 1.0, 1), "H": ("Accelerator", 3.0, 1), "B": ("Brake", 1.0, 1),
    "L": ("Steering", 1.0, 0.8), "R": ("Steering", 1.0, -0.8), "V": ("ViewChange", 0.3, 1), "U": ("GearUp", 0.3, 1),
    "D": ("GearDown", 0.3, 1), "X": ("ResetCar", 0.3, 1), "O": ("Boost", 0.5, 1),
}

extra = ("--timeout", "600") + (("--controllers",) if wheel else ())
with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "gridrace.log"), extra_args=extra) as game:
    time.sleep(25)
    shot(game, "00-title")
    for i, s in enumerate(steps):
        game.ffb()
        if s in ACTIONS:
            name, seconds, value = ACTIONS[s]
            tap(game, name, seconds, value)
        elif s == "M":
            print("menu", game.menu(True), flush=True)
            for tab, page in ((1, "controls"), (3, "ffb")):
                game.menu(tab=tab)
                time.sleep(1)
                shot(game, "1%02d-menu-%s" % (i, page))
            print("menu", game.menu(False), flush=True)
        else:
            time.sleep(3)
        time.sleep(0.7)
        forces = [f for _, f in game.ffb()]
        mean = sum(forces) / len(forces) if forces else 0.0
        print("step %-2d %s: %3d forces, mean %+.2f, min %+.2f, max %+.2f" % (i, s, len(forces), mean, min(forces, default=0),
                                                                              max(forces, default=0)), flush=True)
        shot(game, "1%02d-%s" % (i, s))
