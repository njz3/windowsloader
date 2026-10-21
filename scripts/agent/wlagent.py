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

"""Client of windowsloader's agent mode (--agent): the game driven over stdin/stdout.

The commands are listed at the top of src/hook/agent.cpp; they are a subset of linuxloader's,
sm2-emu's and Supermodel's agent modes. Example:

    from wlagent import WLAgent
    with WLAgent("bin/Release/windowsloader.exe", "games/segarally3/Rally/Rally.exe") as game:
        game.step(600)
        game.press("Start")
        game.shot("logs/sr3.bmp")
"""
import os
import subprocess


class AgentError(RuntimeError):
    pass


class WLAgent:
    def __init__(self, launcher, game, log=None, extra_args=()):
        """Start the game in agent mode. log: the DLL's log file (windowsloader.log in the current folder by default);
        the launcher's messages go to stderr, left to this process's."""
        args = [os.path.abspath(launcher), "--agent", "--quiet"]
        if log:
            args += ["--log", os.path.abspath(log)]
        args += list(extra_args) + [os.path.abspath(game)]
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
        line = self._line()
        if not line.startswith("ready "):
            raise AgentError("expected ready, got %r" % line)
        _, self.game, width, height = line.split()
        self.width, self.height = int(width), int(height)

    def _line(self):
        line = self.process.stdout.readline()
        if not line:
            raise AgentError("the launcher closed the connection (exit code %s)" % self.process.poll())
        return line.decode("utf-8", "replace").rstrip("\r\n")

    def command(self, command):
        """One command, one answer line; an "err" answer raises AgentError."""
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()
        answer = self._line()
        if answer.startswith("err"):
            raise AgentError("%s: %s" % (command, answer))
        return answer

    def step(self, frames=1):
        return int(self.command("step %d" % frames).split()[1])

    def frame(self):
        return int(self.command("frame").split()[1])

    def set(self, name, value):
        self.command("set %s %g" % (name, value))

    def setraw(self, name, raw):
        self.command("setraw %s %d" % (name, raw))

    def release(self, name="all"):
        self.command("release %s" % name)

    def press(self, name, frames=10):
        """A switch closed for some frames, then released."""
        self.setraw(name, 1)
        self.step(frames)
        self.release(name)

    def get(self, name):
        return int(self.command("get %s" % name).split()[1])

    def inputs(self):
        count = int(self.command("inputs").split()[1])
        return [tuple(self._line().split()) for _ in range(count)]

    def ffb(self):
        """The force feedback changes since the last call: [(frame, force)], force > 0 pushing left."""
        count = int(self.command("ffb").split()[1])
        events = []
        for _ in range(count):
            frame, _, _, force = self._line().split()
            events.append((int(frame), float(force)))
        return events

    def menu(self, state=None, tab=None):
        """Open (True) or close (False) the F10 menu, or show its page tab; returns True when open."""
        if tab is not None:
            return self.command("menu tab %d" % tab).split()[1] == "open"
        if state is None:
            return self.command("menu").split()[1] == "open"
        return self.command("menu %s" % ("on" if state else "off")).split()[1] == "open"

    def shot(self, path):
        self.command("shot %s" % os.path.abspath(path))

    def display(self, *settings):
        """Set the aspect ratio and/or the display mode (windowsloader.ini names); returns both."""
        return tuple(self.command(" ".join(("display",) + settings)).split()[1:])

    def screen(self, path):
        """What the window shows, black bars included."""
        self.command("screen %s" % os.path.abspath(path))

    def quit(self):
        try:
            self.command("quit")
        except AgentError:
            pass
        self.close()

    def close(self):
        try:
            self.process.stdin.close()
        except OSError:
            pass
        try:
            self.process.wait(10)
        except subprocess.TimeoutExpired:
            self.process.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if self.process.poll() is None:
            self.quit()
