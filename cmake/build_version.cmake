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

# Run at each build (cmake -P): writes OUT, the build's version named as the release archives are
# (windowsloader-0.1.0-20261024-4-njz3-windows.zip: "0.1.0-20261024-4-njz3"), the day the build was
# made, NUMBER the archive's number of that day when one is set. The file is rewritten only when
# its text changes, so the sources that include it are compiled again once a day at most.
#   -DOUT=<header> -DVERSION=0.1.0 -DFORK=njz3 -DEDITION="FFB Edition" [-DNUMBER=4]

string(TIMESTAMP day "%Y%m%d")
set(build "${VERSION}-${day}")
if(NUMBER)
    string(APPEND build "-${NUMBER}")
endif()
if(FORK)
    string(APPEND build "-${FORK}")
endif()

set(text "// Written by cmake/build_version.cmake at each build: not to be edited\n#pragma once\n\n#define WL_BUILD_VERSION \"${build}\"\n#define WL_EDITION \"${EDITION}\"\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL text)
    file(WRITE "${OUT}" "${text}")
endif()
