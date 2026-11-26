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

"""Storm Rider: what the control panel's buttons 0x020 and 0x040 (View1, View2) do in the menus
and in the race. Credits, then at each screen View1, View2, Up and Boost pressed alone, a picture
after each; Start to the next screen; in the race the throttle held and the two buttons again.

    python scripts/agent/srider_views.py [MOTOBIKE_EXE] [--set SECTION.KEY=VALUE ...]
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
GAME = args[0] if args else os.path.join(ROOT, "games", "stormrider-full", "Ntfs", "all_test", "game", "Gold", "motobike.exe")
OUT = os.path.join(ROOT, "logs", "srider-views")
os.makedirs(OUT, exist_ok=True)
count = [0]


def shot(game, name):
    count[0] += 1
    path = os.path.join(OUT, "%02d-%s.bmp" % (count[0], name))
    try:
        game.shot(path)
    except AgentError:
        print(name, "no frame", flush=True)
    print("%02d %s frame %d" % (count[0], name, game.frame()), flush=True)


def tap(game, name, seconds=0.3, value=1):
    game.set(name, value)
    time.sleep(seconds)
    game.release(name)


def try_buttons(game, screen):
    for name in ("View1", "View1", "View2", "View2", "Up", "Boost"):
        tap(game, name)
        time.sleep(2)
        shot(game, "%s-%s" % (screen, name))


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "srider.log"), extra_args=["--timeout", "900"] + sets) as game:
    time.sleep(45)
    shot(game, "attract")
    for _ in range(4):
        tap(game, "Coin")
        time.sleep(0.5)
    time.sleep(2)
    shot(game, "credits")
    for screen in range(4):
        tap(game, "Start")
        time.sleep(4)
        shot(game, "screen%d" % screen)
        try_buttons(game, "screen%d" % screen)
    # the race: throttle held, then the two buttons
    tap(game, "Start")
    time.sleep(15)
    game.set("Accelerator", 1)
    time.sleep(5)
    shot(game, "race")
    for name in ("View1", "View1", "View2", "View2"):
        tap(game, name)
        time.sleep(2)
        shot(game, "race-%s" % name)
    game.release("all")
