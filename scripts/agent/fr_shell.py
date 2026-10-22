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

"""Ford Racing Full Blown's cabinet shell (GameShell\\GameShell.exe) and its service menu: the
shell left alone, then Test pressed, Service pressed a few times (pictures after each), Test
again. The waits are in seconds.

    python scripts/agent/fr_shell.py [STEPS]

STEPS: a list of Test / Service / wait presses after the first look, "T", "S" or "w", e.g. TSSST
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
SHELL = os.path.join(ROOT, "games", "fordracing", "GameShell", "GameShell.exe")
steps = sys.argv[1] if len(sys.argv) > 1 else "TSSST"
OUT = os.path.join(ROOT, "logs", "frshell")
os.makedirs(OUT, exist_ok=True)


def shot(game, name):
    try:
        game.shot(os.path.join(OUT, name + ".bmp"))
        print("%-14s ok" % name, flush=True)
    except AgentError:
        print("%-14s no frame" % name, flush=True)


with WLAgent(LAUNCHER, SHELL, log=os.path.join(OUT, "frshell.log"), extra_args=("--timeout", "200")) as game:
    for i in range(3):
        time.sleep(5)
        shot(game, "0%d-start" % i)
    for i, s in enumerate(steps):
        if s == "w":
            time.sleep(3)
        else:
            name = "Test" if s == "T" else "Service"
            game.set(name, 1)
            time.sleep(0.4)
            game.release(name)
            time.sleep(2)
        shot(game, "%02d-%s" % (10 + i, s))
