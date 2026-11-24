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

"""Road Fighters 3D (jgt.dll in wl_avsboot.exe): a game played to its end and back to the
attract, the end where the cabinets crashed (a device Reset, then jgt.dll+0x1d3804). Pictures
every 10 s; the waits are in seconds.

    python scripts/agent/rf3d_gameover.py [GAME_DLL] [--avs-log] [--set SECTION.KEY=VALUE ...]

GAME_DLL: games/wal/roadfighters3d-full/jgt.dll by default. SERVICE on the network warning
(no e-Amusement), a coin, the accelerator to confirm the mode, the car and the gearbox, the race
with the accelerator held, then no coin: the game ends and goes back to the attract.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
args = sys.argv[1:]
sets = []
while "--set" in args:
    i = args.index("--set")
    sets += ["--set", args[i + 1]]
    del args[i:i + 2]
game_args = []
if "--avs-log" in args:  # AVS's own log in windowsloader's, as the cabinets run it
    args.remove("--avs-log")
    game_args.append("--avs-log")
GAME = args[0] if args else os.path.join(ROOT, "games", "wal", "roadfighters3d-full", "jgt.dll")
OUT = os.path.join(ROOT, "logs", "rf3d-gameover")
os.makedirs(OUT, exist_ok=True)
START = time.time()


def stamp():
    return "%4.0f" % (time.time() - START)


def shot(game, name):
    try:
        game.shot(os.path.join(OUT, "%s-%s.bmp" % (stamp().strip().zfill(4), name)))
        print("%s %-20s ok" % (stamp(), name), flush=True)
    except AgentError as e:
        print("%s %-20s %s" % (stamp(), name, e), flush=True)


def tap(game, name, seconds=0.3):
    game.set(name, 1)
    time.sleep(seconds)
    game.release(name)


def wait(game, seconds, every=10, name="wait"):
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(min(every, max(0, end - time.time())))
        shot(game, name)


extra = ["--timeout", "900"] + sets
with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "rf3d.log"), extra_args=extra, game_args=game_args) as game:
    print("ready", game.game, game.width, game.height, flush=True)
    wait(game, 70, name="boot")  # the network warning about 60 s in
    for _ in range(2):
        tap(game, "Service")
        time.sleep(3)
    shot(game, "after-service")
    wait(game, 15, name="attract")
    tap(game, "Coin")
    time.sleep(2)
    shot(game, "coin")
    # the accelerator starts and confirms: the mode, the car, the gearbox, then a few more
    for i in range(12):
        tap(game, "Accelerator", 0.4)
        time.sleep(2.5)
        shot(game, "confirm%02d" % i)
    # the race: the accelerator held, the wheel a little to each side
    game.set("Accelerator", 1)
    for i in range(18):
        game.set("Steering", 0.3 if i % 2 else -0.3)
        time.sleep(10)
        shot(game, "race%02d" % i)
    game.release("all")
    # no coin: the results, the continue countdown, the game's end, the attract
    wait(game, 240, name="end")
    print("%s done" % stamp(), flush=True)
