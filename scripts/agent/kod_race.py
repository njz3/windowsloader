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

"""K.O. Drive (Sega RingWide): its start and the drive board's power-on test (about 40 s), then
the attract mode, coins and Start, pictures along the way and the force feedback of each phase.
The waits are in seconds.

    python scripts/agent/kod_race.py [wheel] [test] [STEPS]

wheel: the controllers are opened too (launcher --controllers), the force feedback goes to the
player 1 controller (vJoy on the test PC: warn its other users first).
test: the cabinet's test mode instead (TestMode.exe -t, as its gametest.bat does).
STEPS: letters one by one: S Start, C coin, A accelerator held 1 s, H accelerator held 6 s, B brake
held 1 s, L / R the wheel left / right 1 s, V view, T Test, E Service, w wait 3 s; the default is
"CCwSwwSwwSwwHHHH".
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
args = [a for a in sys.argv[1:] if a not in ("wheel", "test")]
wheel = "wheel" in sys.argv[1:]
test = "test" in sys.argv[1:]
GAME = os.path.join(ROOT, "games", "kodrive", "exe", "TestMode.exe" if test else "M-DriveR_RingWide.exe")
steps = args[0] if args else "CCwSwwSwwSwwHHHH"
OUT = os.path.join(ROOT, "logs", "kodrace" + ("-wheel" if wheel else "") + ("-test" if test else ""))
os.makedirs(OUT, exist_ok=True)


def shot(game, name):
    try:
        game.shot(os.path.join(OUT, name + ".bmp"))
        print("%-16s ok" % name, flush=True)
    except AgentError:
        print("%-16s no frame" % name, flush=True)


def tap(game, name, seconds=0.3, value=1):
    game.set(name, value)
    time.sleep(seconds)
    game.release(name)


extra = ("--timeout", "900") + (("--controllers",) if wheel else ())
with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "kodrace.log"), extra_args=extra, game_args=("-t",) if test else ()) as game:
    for t in range(4):
        time.sleep(12)
        shot(game, "0%d-boot" % t)
    for i, s in enumerate(steps):
        game.ffb()
        if s == "S":
            tap(game, "Start")
        elif s == "C":
            tap(game, "Coin", 0.2)
        elif s == "A":
            tap(game, "Accelerator", 1.0)
        elif s == "H":
            tap(game, "Accelerator", 6.0)
        elif s == "B":
            tap(game, "Brake", 1.0)
        elif s == "L":
            tap(game, "Steering", 1.0, 0.8)
        elif s == "R":
            tap(game, "Steering", 1.0, -0.8)
        elif s == "V":
            tap(game, "ViewChange")
        elif s == "T":
            tap(game, "Test")
        elif s == "E":
            tap(game, "Service")
        else:
            time.sleep(3)
        time.sleep(1)
        forces = [f for _, f in game.ffb()]
        mean = sum(forces) / len(forces) if forces else 0.0
        print("step %-2d %s: %3d forces, mean %+.2f" % (i, s, len(forces), mean), flush=True)
        shot(game, "2%02d-%s" % (i, s))
