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

"""Any game left running under windowsloader for a while, a capture of the back buffer every 10 s,
then stopped: the first look at a new game (what it opens, asks for and draws is in the log).
The waits are in seconds (the game runs at its own pace).

    python scripts/agent/observe.py GAME.exe [SECONDS] [NAME]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlagent import AgentError, WLAgent  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAUNCHER = os.path.join(ROOT, "bin", "Release", "windowsloader.exe")
GAME = os.path.abspath(sys.argv[1])
seconds = int(sys.argv[2]) if len(sys.argv) > 2 else 60
name = sys.argv[3] if len(sys.argv) > 3 else os.path.splitext(os.path.basename(GAME))[0].lower()
OUT = os.path.join(ROOT, "logs", "observe-" + name)
os.makedirs(OUT, exist_ok=True)

try:
    with WLAgent(LAUNCHER, GAME, log=os.path.join(OUT, "observe.log"), extra_args=("--timeout", str(seconds + 60))) as game:
        print("ready", game.game, game.width, game.height, flush=True)
        for i in range(max(seconds // 10, 1)):
            time.sleep(10)
            try:
                game.shot(os.path.join(OUT, "t%03d.bmp" % ((i + 1) * 10)))
                print("t=%3ds frame %d" % ((i + 1) * 10, game.frame()), flush=True)
            except AgentError as e:
                print("t=%3ds %s" % ((i + 1) * 10, e), flush=True)
except AgentError as e:
    print("stopped:", e, flush=True)
