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

"""Crazy Speed Arcade: a coin, START through the game card and the choices, then the race with the
accelerator held; pictures every few seconds. The waits are in seconds.

    python scripts/agent/cs_race.py [CLIENT_R_EXE] [--race S] [--set SECTION.KEY=VALUE ...]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
args = sys.argv[1:]
sets, race = [], 60
if "--race" in args:
    i = args.index("--race")
    race = int(args[i + 1])
    del args[i:i + 2]
while "--set" in args:
    i = args.index("--set")
    sets += ["--set", args[i + 1]]
    del args[i:i + 2]
GAME = args[0] if args else os.path.join(ROOT, "games", "wal", "crazyspeed-full", "release", "Client_r.exe")
OUT = os.path.join(ROOT, "logs", "cs-race")
os.makedirs(OUT, exist_ok=True)
START = time.time()


def stamp():
    return "%4.0f" % (time.time() - START)


def shot(game, name):
    try:
        game.shot(os.path.join(OUT, "%s-%s.bmp" % (stamp().strip().zfill(4), name)))
        print("%s %-16s ok" % (stamp(), name), flush=True)
    except AgentError as e:
        print("%s %-16s %s" % (stamp(), name, e), flush=True)


def tap(game, name, seconds=0.3):
    game.set(name, 1)
    time.sleep(seconds)
    game.release(name)


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "cs.log"), extra_args=["--timeout", "900"] + sets) as game:
    for i in range(5):
        time.sleep(8)
        shot(game, "boot%d" % i)
    tap(game, "Coin")
    time.sleep(2)
    shot(game, "coin")
    for i in range(18):
        tap(game, "Start")
        time.sleep(4)
        shot(game, "start%02d" % i)
    # the accelerator only after the countdown (GO about 20 s after the last START)
    for i in range(4):
        time.sleep(6)
        shot(game, "countdown")
    game.set("Accelerator", 1)
    end = time.time() + race
    side = 0.4
    while time.time() < end:
        game.set("Steering", side)  # the wheel to each side in turn
        side = -side
        time.sleep(6)
        shot(game, "race")
    game.release("all")
    print("%s done" % stamp(), flush=True)
