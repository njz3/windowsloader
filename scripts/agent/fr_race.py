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

"""Ford Racing Full Blown: Start, the menus taken with the accelerator (manual gearbox chosen with
the wheel), then a race: gear up, gear down and the view pressed one by one (pictures before and
after), the wheel held right then left (the centring force must push against it: > 0 to the
left), the brake. The waits are in seconds.

    python scripts/agent/fr_race.py [wheel]

wheel: the controllers are opened too (launcher --controllers): the force feedback goes to the
player 1 controller (vJoy on the test PC: warn its other users first).
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.join(ROOT, "games", "fordracing", "FordRacing", "fordracing.exe")
wheel = "wheel" in sys.argv[1:]
OUT = os.path.join(ROOT, "logs", "frrace" + ("-wheel" if wheel else ""))
os.makedirs(OUT, exist_ok=True)


def shot(game, name):
    try:
        game.shot(os.path.join(OUT, name + ".bmp"))
    except AgentError:
        print("%-14s no frame" % name, flush=True)


def tap(game, name, seconds=0.3, value=1):
    game.set(name, value)
    time.sleep(seconds)
    game.release(name)


def phase(game, name, seconds, **held):
    game.ffb()
    for k, v in held.items():
        game.set(k, v)
    time.sleep(seconds)
    forces = [f for _, f in game.ffb()]
    mean = sum(forces) / len(forces) if forces else 0.0
    print("%-14s %4d changes, mean %+.3f, min %+.3f, max %+.3f" % (name, len(forces), mean, min(forces, default=0), max(forces, default=0)),
          flush=True)
    shot(game, name)


extra = ("--timeout", "400") + (("--controllers",) if wheel else ())
with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "frrace.log"), extra_args=extra) as game:
    time.sleep(12)
    tap(game, "Start")
    time.sleep(2)
    shot(game, "01-class")
    tap(game, "Accelerator", 0.4)  # vehicle class
    time.sleep(2)
    tap(game, "Accelerator", 0.4)  # vehicle
    time.sleep(2)
    shot(game, "02-transmission")
    game.set("Steering", -0.9)  # manual
    time.sleep(1.2)
    game.release("Steering")
    shot(game, "03-manual")
    tap(game, "Accelerator", 0.4)
    time.sleep(2)
    shot(game, "04-track")
    tap(game, "Accelerator", 0.4)
    time.sleep(2)
    tap(game, "Accelerator", 0.4)
    time.sleep(12)  # loading, countdown
    shot(game, "10-race")
    game.set("Accelerator", 1)
    phase(game, "11-straight", 5)
    for b in ("GearUp", "GearDown", "ViewChange"):
        shot(game, "2-%s-before" % b)
        tap(game, b, 0.4)
        time.sleep(1.5)
        shot(game, "2-%s-after" % b)
    phase(game, "31-right", 3, Steering=-0.7)
    phase(game, "32-left", 3, Steering=0.7)
    game.release("Steering")
    phase(game, "33-centre", 3)
    game.release("Accelerator")
    phase(game, "34-brake", 2, Brake=1)
    game.release()
    # The F10 menu over the race: its Controls and Force feedback pages
    print("menu", game.menu(True), flush=True)
    for tab, name in ((1, "40-menu-controls"), (3, "41-menu-ffb")):
        game.menu(tab=tab)
        time.sleep(1)
        shot(game, name)
    print("menu", game.menu(False), flush=True)
    time.sleep(2)
    shot(game, "42-after-menu")
