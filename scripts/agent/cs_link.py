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

"""Crazy Speed Arcade, two cabinets linked on this PC (127.0.0.2 and 127.0.0.3, each its own write
folder and log): a coin, START on each until it shows GAME MODE (its title recognized), there the
wheel to the right to the in-store tournament (its third mode, --down N steps) on both, START, pictures of both
for --watch seconds, then START through what follows and the race with the accelerator held.

    python scripts/agent/cs_link.py [CLIENT_R_EXE] [--down N] [--watch S] [--race S] [--set SECTION.KEY=VALUE ...]
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
opts = {"--down": 2, "--race": 60, "--watch": 40}
for flag in list(opts):
    if flag in args:
        i = args.index(flag)
        opts[flag] = float(args[i + 1])
        del args[i:i + 2]
sets = []
while "--set" in args:
    i = args.index("--set")
    sets += ["--set", args[i + 1]]
    del args[i:i + 2]
GAME = args[0] if args else os.path.join(ROOT, "games", "wal", "crazyspeed-full", "release", "Client_r.exe")
OUT = os.path.join(ROOT, "logs", "link-cs")
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


# GAME MODE's title, white letters at the top left of its 1360x768 picture
TITLE_BOX = (150, 30, 520, 110)
TEMPLATE = os.path.join(ROOT, "logs", "cs-race", "0106-start12.bmp")


def white(path):
    from PIL import Image
    g = Image.open(path).convert("L").crop(TITLE_BOX)
    return [1 if p > 200 else 0 for p in g.getdata()]


def is_game_mode(path, ref=[]):
    if not ref:
        ref.append(white(TEMPLATE))
    m = white(path)
    inter = sum(a & b for a, b in zip(ref[0], m))
    union = sum(a | b for a, b in zip(ref[0], m))
    return inter / max(union, 1) >= 0.7


MODES = (("tournament", (80, 570, 320, 650)), ("time attack", (380, 570, 640, 650)), ("in-store", (670, 570, 980, 670)))


def chosen_mode(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    reds = []
    for name, box in MODES:
        px = list(im.crop(box).getdata())
        reds.append(sum(1 for r, g, b in px if r > 140 and g < 70 and b < 70) / len(px))
    best = max(range(len(MODES)), key=lambda i: reds[i])
    return best if reds[best] > 0.05 else -1


def to_in_store(k):
    def fn(g):
        for i in range(10):
            path = os.path.join(OUT, "probe-cab%d.bmp" % k)
            try:
                g.shot(path)
                mode = chosen_mode(path)
            except AgentError:
                mode = -1
            print("%s cab%d mode %s" % (stamp(), k, MODES[mode][0] if mode >= 0 else "?"), flush=True)
            if mode == 2:
                return
            g.set("Steering", -1 if mode < 2 else 1)  # -1 to the right; held 0.8 s, a shorter turn not taken
            time.sleep(0.8)
            g.set("Steering", 0)
            time.sleep(1.0)
    return fn


def to_game_mode(k):
    def fn(g):
        for i in range(30):
            path = os.path.join(OUT, "probe-cab%d.bmp" % k)
            try:
                g.shot(path)
                if is_game_mode(path):
                    print("%s cab%d at GAME MODE after %d STARTs" % (stamp(), k, i), flush=True)
                    return
            except AgentError:
                pass
            tap("Start")(g)
            time.sleep(4)
        print("%s cab%d never at GAME MODE" % (stamp(), k), flush=True)
    return fn


games = []
try:
    games.append(start(1))
    time.sleep(5)
    games.append(start(2))
    for i in range(5):
        time.sleep(8)
        shots(games, "boot%d" % i)
    both(games, tap("Coin"))
    time.sleep(2)
    shots(games, "coin")
    threads = [threading.Thread(target=to_game_mode(k), args=(g,)) for k, g in enumerate(games, 1)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    shots(games, "gamemode")
    # its modes side by side, chosen with the wheel (one step each time it is turned): each cabinet
    # turned until its in-store tournament is the one shown chosen (its label on red)
    threads = [threading.Thread(target=to_in_store(k), args=(g,)) for k, g in enumerate(games, 1)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    shots(games, "instore")
    both(games, tap("Start"))
    end = time.time() + opts["--watch"]
    while time.time() < end:
        time.sleep(4)
        shots(games, "watch")
    for i in range(8):
        both(games, tap("Start"))
        time.sleep(5)
        shots(games, "after%02d" % i)
    both(games, lambda g: g.set("Accelerator", 1))
    end = time.time() + opts["--race"]
    while time.time() < end:
        time.sleep(8)
        shots(games, "race")
finally:
    for g in games:
        try:
            g.close()
        except Exception:
            pass
    print("%s done" % stamp(), flush=True)
