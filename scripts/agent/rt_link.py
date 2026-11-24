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

"""Two cabinets of a game linked on this PC (127.0.0.2 and 127.0.0.3: some games wait for an address other than 127.0.0.1, each its own write folder
and log): pictures of both every 10 s, a coin and START on both after the boot, then the
accelerator held. The waits are in seconds.

    python scripts/agent/rt_link.py GAME_EXE NAME [--boot S] [--race S] [--calib h2o] [--game-args "..."] [--solo] [--stagger S] [--join | --join2] [--steer] [--gas NAME] [--cabinets N] [--set SECTION.KEY=VALUE ...]

--calib h2o: H2Overdrive's throttle calibration first (a new write folder opens on it): SERVICE on its
welcome, the throttle forward, back and in the middle (BOOST each time), then its full range.

--cabinets N: N cabinets linked on this PC (2 by default; 127.0.0.2 to 127.0.0.(N+1)).

NAME: the folder under logs/link-<NAME> (the pictures, the logs, the write folders).
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
sets, opts = [], {"--boot": 60, "--race": 60, "--stagger": 0, "--cabinets": 2}
for flag in list(opts):
    if flag in args:
        i = args.index(flag)
        opts[flag] = int(args[i + 1])
        del args[i:i + 2]
steer = "--steer" in args  # the wheel turned each 5 s in the race
if steer:
    args.remove("--steer")
join = "--join" in args or "--join2" in args  # one cabinet joins the other's game (SnoCross)
host = 1 if "--join2" in args else 0  # --join2: cabinet 2 starts, cabinet 1 joins
for flag in ("--join", "--join2"):
    if flag in args:
        args.remove(flag)
solo = "--solo" in args  # one cabinet alone, without LINK: the same run, to compare
if solo:
    args.remove("--solo")
game_args = []
if "--game-args" in args:  # the game's own arguments, one string (Dirty Drivin': "-WinPC -Windowed -r 1360x768")
    i = args.index("--game-args")
    game_args = args[i + 1].split()
    del args[i:i + 2]
calib = None
if "--calib" in args:
    i = args.index("--calib")
    calib = args[i + 1]
    del args[i:i + 2]
gas = "Accelerator"  # the game's accelerator's name (SnoCross: Throttle)
if "--gas" in args:
    i = args.index("--gas")
    gas = args[i + 1]
    del args[i:i + 2]
while "--set" in args:
    i = args.index("--set")
    sets += ["--set", args[i + 1]]
    del args[i:i + 2]
GAME, NAME = args[0], args[1]
OUT = os.path.join(ROOT, "logs", "link-" + NAME)
os.makedirs(OUT, exist_ok=True)
START = time.time()


def stamp():
    return "%4.0f" % (time.time() - START)


def start(k):
    write = os.path.join(OUT, "write%d" % k)
    os.makedirs(write, exist_ok=True)
    extra = ["--timeout", "1200", "--write-dir", write] + sets
    if not solo:
        link = ",".join("127.0.0.%d" % (c + 1) for c in range(1, opts["--cabinets"] + 1))
        extra += ["--set", "Game.LINK=" + link, "--set", "Game.CABINET=%d" % k]
    return WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "cab%d.log" % k), extra_args=extra, game_args=game_args)


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


def h2o_calibration(games):
    time.sleep(20)
    shots(games, "calib-welcome")
    both(games, tap("Service"))
    time.sleep(2)
    for axis in ("Accelerator", "Brake", None):  # forward, back, the middle: BOOST each time
        if axis:
            both(games, lambda g, a=axis: g.set(a, 1))
        time.sleep(1)
        both(games, tap("Boost"))
        time.sleep(1)
        both(games, lambda g: g.release("all"))
        time.sleep(1)
        shots(games, "calib-%s" % (axis or "middle"))
    for axis in ("Accelerator", "Brake"):  # its full range, slowly back to the middle
        both(games, lambda g, a=axis: g.set(a, 1))
        time.sleep(1.5)
        for step in range(10, -1, -1):
            both(games, lambda g, a=axis, v=step / 10.0: g.set(a, v))
            time.sleep(0.3)
        both(games, lambda g: g.release("all"))
        time.sleep(1.5)
        shots(games, "calib-%s-range" % axis)
    time.sleep(3)
    shots(games, "calib-done")
    both(games, tap("Service"))
    time.sleep(3)


def wait(games, seconds, name):
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(min(10, max(0, end - time.time())))
        shots(games, name)


games = []
try:
    games.append(start(1))
    if not solo:
        for k in range(2, opts["--cabinets"] + 1):
            time.sleep(8)  # the next once the previous runs
            games.append(start(k))
    print("ready", games[0].game, flush=True)
    if calib == "h2o":
        h2o_calibration(games)
    wait(games, opts["--boot"], "boot")
    if join and len(games) > 1:
        # cabinet 1 starts a game, cabinet 2 joins it while cabinet 1 chooses (SnoCross's
        # "Racer Join"): pictures of both at each step
        first, other = games[host], games[1 - host]
        tap("Coin")(first)
        time.sleep(2)
        for i in range(3):  # its game started, at its track's choice
            tap("Start", 0.5)(first)
            time.sleep(3)
        shots(games, "host-start")
        tap("Coin")(other)
        time.sleep(3)
        shots(games, "join-coin")
        tap("Start", 0.5)(other)  # "LINKED GAME STARTING - JOIN IN NOW!"
        time.sleep(3)
        shots(games, "join-start")
        for i in range(8):
            both(games, tap("Start"))
            time.sleep(5)
            shots(games, "joined%d" % i)
        both(games, lambda g: g.set(gas, 1))
        wait(games, opts["--race"], "race")
        raise SystemExit(0)
    if opts["--stagger"] and len(games) > 1:
        # cabinet 1 starts alone, cabinet 2 joins it some seconds later (a game that offers the
        # others to join a race once one cabinet has started: SnoCross)
        tap("Coin")(games[0])
        time.sleep(2)
        tap("Start")(games[0])
        time.sleep(opts["--stagger"])
        shots(games, "joined")
        both(games[1:], tap("Coin"))  # the others' coins while the first's game waits for them
        time.sleep(2)
    else:
        both(games, tap("Coin"))
        time.sleep(2)
    shots(games, "coin")
    for i in range(6):
        both(games, tap("Start"))
        time.sleep(4)
        shots(games, "start%d" % i)
    both(games, lambda g: g.release(gas))  # a new press after the start (Cars' autopilot)
    time.sleep(1)
    both(games, lambda g: g.set(gas, 1))
    if steer:  # the wheel to each side in turn (a force that follows the wheel: Cars)
        end = time.time() + opts["--race"]
        side = 0.5
        while time.time() < end:
            both(games, lambda g, v=side: g.set("Steering", v))
            side = -side
            time.sleep(5)
            shots(games, "race")
    else:
        wait(games, opts["--race"], "race")
finally:
    for g in games:
        try:
            g.close()
        except Exception:
            pass
    print("%s done" % stamp(), flush=True)
