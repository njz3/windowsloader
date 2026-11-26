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

"""Phases of a controls test in agent mode: inputs held for some frames, a picture (PNG, half size),
the DLL's log lines of the phase (force feedback, the lines matching a pattern)."""
import os
import re


class Phases:
    def __init__(self, out, log, pattern):
        self.out, self.log, self.pattern = out, log, re.compile(pattern)

    def _size(self):
        try:
            return os.path.getsize(self.log)
        except OSError:
            return 0

    def _lines(self, since):
        with open(self.log, "rb") as f:
            f.seek(since)
            return f.read().decode("utf-8", "replace").splitlines()

    def shot(self, game, label):
        bmp = os.path.join(self.out, label + ".bmp")
        game.shot(bmp)
        try:
            from PIL import Image
            im = Image.open(bmp)
            im.resize((im.width // 2, im.height // 2)).save(bmp[:-4] + ".png")
            os.remove(bmp)
        except Exception as e:
            print("  (png: %s)" % e)

    def phase(self, game, label, holds, frames):
        since = self._size()
        game.ffb()
        for n, v in holds:
            game.set(n, v)
        game.step(frames)
        self.shot(game, label)
        for n, _ in holds:
            game.release(n)
        game.step(20)
        forces = [f for _, f in game.ffb()]
        lines = self._lines(since)
        effects = [l for l in lines if "ffb: effects" in l]
        print("%-14s constant: %d changes%s; effects: %d" % (
            label, len(forces), (", mean %+.3f min %+.3f max %+.3f" % (sum(forces) / len(forces), min(forces), max(forces))) if forces else "",
            len(effects)), flush=True)
        for l in [l for l in lines if self.pattern.search(l)][:8]:
            print("    " + l.split("] ", 2)[-1][:150])
