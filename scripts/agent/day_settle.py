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

"""Daytona Championship USA at a cabinet's wheel, hands off: does the wheel swing back and forth by
itself? The script takes the game into a race as day_race.py does, then gives the steering back to
the real wheel (launcher --controllers) and records, about every 10 ms, the steering the game reads
and the force it sends (> 0 to the left): first while driving (accelerator held), then stopped
(brake held). Nobody touches the wheel during the records.

    python scripts/agent/day_settle.py <Daytona.exe> [out folder] [windowsloader.exe]

The launcher is this repository's bin/Release one unless given (a cabinet's own installation).

Writes <out>/settle-driving.csv and settle-stopped.csv (time s, steering raw, force) and prints for
each: the swing's period and amplitude, the force's, and the delay of the force behind the wheel's
position (a spring that answers late feeds the swing).
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
if len(sys.argv) < 2:
    sys.exit(__doc__)
GAME = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "logs", "day-settle")
if len(sys.argv) > 3:
    LAUNCHER = sys.argv[3]
os.makedirs(OUT, exist_ok=True)


def tap(game, name, seconds=0.3):
    game.set(name, 1)
    time.sleep(seconds)
    game.release(name)


def record(game, seconds):
    """[(t, steering raw, force)] about every 10 ms; the force is the last one the game sent"""
    rows, force, start = [], 0.0, time.perf_counter()
    game.ffb()
    while True:
        t = time.perf_counter() - start
        if t > seconds:
            return rows
        events = game.ffb()
        if events:
            force = events[-1][1]
        rows.append((t, game.get("Steering"), force))
        time.sleep(0.01)


def analyse(name, rows):
    with open(os.path.join(OUT, "settle-%s.csv" % name), "w") as f:
        f.write("t,steering,force\n")
        for t, s, x in rows:
            f.write("%.4f,%d,%.4f\n" % (t, s, x))
    if len(rows) < 50:
        print("%-8s too few samples (%d)" % (name, len(rows)), flush=True)
        return
    pos = [s for _, s, _ in rows]
    mean = sum(pos) / len(pos)
    centred = [p - mean for p in pos]
    # The swing: crossings of the mean position, upward ones a period apart
    ups = [rows[i][0] for i in range(1, len(rows)) if centred[i - 1] < 0 <= centred[i]]
    period = (ups[-1] - ups[0]) / (len(ups) - 1) if len(ups) > 2 else 0.0
    forces = [x for _, _, x in rows]
    # The force's delay behind the position: the shift that best lines the force up with the position
    # (or its opposite, a spring), in samples of ~10 ms
    dt = rows[-1][0] / (len(rows) - 1)
    fmean = sum(forces) / len(forces)
    fc = [x - fmean for x in forces]
    best, lag = 0.0, 0
    for k in range(0, min(100, len(rows) // 2)):
        c = sum(fc[i + k] * centred[i] for i in range(len(rows) - k))
        if abs(c) > abs(best):
            best, lag = c, k
    print("%-8s %d samples, steering %d..%d (mean %.1f), swing period %s, force %+.2f..%+.2f, force %s the position by %d ms"
          % (name, len(rows), min(pos), max(pos), mean, "%.2f s" % period if period else "none",
             min(forces), max(forces), "against" if best < 0 else "with", lag * dt * 1000), flush=True)


with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "settle.log"), extra_args=("--timeout", "300", "--controllers")) as game:
    time.sleep(60)  # loading, then the attract
    tap(game, "Start")
    time.sleep(3)
    for i in range(8):  # mode, transmission, car, track...: the default choice each time
        tap(game, "Accelerator", 0.4)
        time.sleep(2.5)
    time.sleep(10)  # the race loading, the rolling start
    game.release()  # the steering is the real wheel's from now on
    print("hands off the wheel", flush=True)
    game.set("Accelerator", 1)
    time.sleep(2)
    analyse("driving", record(game, 15))
    game.release("Accelerator")
    game.set("Brake", 1)
    time.sleep(4)
    analyse("stopped", record(game, 10))
    game.release()
    try:
        game.quit()
    except AgentError:
        pass
