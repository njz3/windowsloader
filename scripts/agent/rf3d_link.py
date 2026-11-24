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

"""Road Fighters 3D, two cabinets linked on this PC (127.0.0.2 and 127.0.0.3, each its own
write folder and log): SERVICE on the network warning, a coin, the in-store battle (the mode to the
right), the accelerator to confirm, then the race; pictures of both every 10 s. The waits are in seconds.

    python scripts/agent/rf3d_link.py [GAME_DLL] [--set SECTION.KEY=VALUE ...]
"""
import os
import sys
import threading
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
GAME = args[0] if args else os.path.join(ROOT, "games", "wal", "roadfighters3d-full", "jgt.dll")
OUT = os.path.join(ROOT, "logs", "link-rf3d")
os.makedirs(OUT, exist_ok=True)
START = time.time()


def stamp():
    return "%4.0f" % (time.time() - START)


def start(k):
    write = os.path.join(OUT, "write%d" % k)
    os.makedirs(write, exist_ok=True)
    extra = ["--timeout", "1200", "--write-dir", write, "--set", "Game.LINK=127.0.0.2,127.0.0.3",
             "--set", "Game.CABINET=%d" % k] + sets
    return WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "cab%d.log" % k), extra_args=extra)


def shots(games, name):
    for k, g in enumerate(games, 1):
        try:
            g.shot(os.path.join(OUT, "%s-cab%d-%s.bmp" % (stamp().strip().zfill(4), k, name)))
        except AgentError as e:
            print("%s cab%d %s: %s" % (stamp(), k, name, e), flush=True)
    print("%s %s" % (stamp(), name), flush=True)


def both(games, fn):
    threads = [threading.Thread(target=fn, args=(g,)) for g in games]
    for t in threads:
        t.start()
    for t in threads:
        t.join()


def tap(name, seconds=0.3):
    def fn(g):
        g.set(name, 1)
        time.sleep(seconds)
        g.release(name)
    return fn


def wait(games, seconds, name):
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(min(10, max(0, end - time.time())))
        shots(games, name)


games = []
try:
    games.append(start(1))
    games.append(start(2))
    wait(games, 75, "boot")
    for _ in range(2):
        both(games, tap("Service"))
        time.sleep(3)
    wait(games, 20, "attract")
    both(games, tap("Coin"))
    time.sleep(2)
    shots(games, "coin")
    # the 3D warning, the glasses, then the mode: the in-store battle (店内対戦) one to the right
    for i in range(14):
        if i == 3:  # the mode's screen, after the 3D warning and the glasses'
            both(games, lambda g: g.set("Steering", -1))  # -1: right
            time.sleep(0.4)
            both(games, lambda g: g.set("Steering", 0))
            time.sleep(1)
            shots(games, "mode")
        both(games, tap("Accelerator", 0.4))
        time.sleep(3)
        shots(games, "confirm%02d" % i)
    both(games, lambda g: g.set("Accelerator", 1))
    wait(games, 60, "race")
finally:
    for g in games:
        try:
            g.close()
        except Exception:
            pass
    print("%s done" % stamp(), flush=True)
