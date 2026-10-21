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

"""Daytona Championship USA: Start, the steering's direction on the mode screen (right must pick
the right-hand box), the menus taken with the accelerator, then a race: accelerator, the wheel
held right then left (the centring force must push against it: > 0 to the left), brake, gears.
The game runs at its own pace (not vsynced when its window is hidden): the waits are in seconds.

    python scripts/agent/day_race.py [nse]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
nse = "nse" in sys.argv[1:]
GAME = os.path.join(ROOT, "games", "daytonausa-nse" if nse else "daytonausa", "Sega", "Daytona", "Daytona.exe")
OUT = os.path.join(ROOT, "logs", "dayrace-nse" if nse else "dayrace")
os.makedirs(OUT, exist_ok=True)


def shot(game, name):
    """A picture of the next frame; none while the game loads (it draws nothing then)"""
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
    events = game.ffb()
    forces = [f for _, f in events]
    mean = sum(forces) / len(forces) if forces else 0.0
    print("%-14s %4d changes, mean %+.3f, min %+.3f, max %+.3f" % (name, len(forces), mean, min(forces, default=0), max(forces, default=0)),
          flush=True)
    shot(game, name)


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "dayrace.log"), extra_args=("--timeout", "300")) as game:
    time.sleep(60)  # loading (40 to 50 s), then the attract
    shot(game, "00-attract")
    tap(game, "Start")
    time.sleep(3)
    shot(game, "01-mode")
    game.set("Steering", -0.8)  # right
    time.sleep(1)
    shot(game, "02-mode-right")
    game.set("Steering", 0.8)  # left
    time.sleep(1)
    shot(game, "03-mode-left")
    game.release("Steering")
    for i in range(8):  # mode, transmission, car, track...: the default choice each time
        tap(game, "Accelerator", 0.4)
        time.sleep(2.5)
        shot(game, "%02d-menu" % (10 + i))
    time.sleep(10)  # the race loading, the rolling start
    shot(game, "20-race")
    game.set("Accelerator", 1)
    phase(game, "21-straight", 6)
    phase(game, "22-right", 3, Steering=-0.7)
    phase(game, "23-left", 3, Steering=0.7)
    game.release("Steering")
    phase(game, "24-centre", 3)
    game.release("Accelerator")
    phase(game, "25-brake", 2, Brake=1)
    game.release("Brake")
    for g in ("GearUp", "GearDown"):
        tap(game, g)
        time.sleep(1)
    game.set("Accelerator", 1)
    phase(game, "26-again", 4)
    game.release()
    # The F10 menu over the race: its Controls and Force feedback pages
    print("menu", game.menu(True), flush=True)
    for tab, name in ((1, "30-menu-controls"), (3, "31-menu-ffb")):
        game.menu(tab=tab)
        time.sleep(1)
        shot(game, name)
    print("menu", game.menu(False), flush=True)
    time.sleep(2)
    shot(game, "32-after-menu")
