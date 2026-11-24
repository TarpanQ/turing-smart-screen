// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
//
// Copyright (C) 2021 Matthieu Houdebine (mathoudebine)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

/**
 * @file list-serial-ports.cpp
 * @brief List the USB serial ports with their serial number, vendor and product ID
 */

#include <cstdio>

#include "library/lcd/serial_port.h"

/** @brief Entry point of the list-serial-ports tool. */
int main()
{
    for (const auto &com_port: comports()) {
        if (com_port.vid && com_port.pid) {
            std::printf("Serial=%s VID=%x PID=%x\n",
                        com_port.serial_number.empty() ? "None" : com_port.serial_number.c_str(), *com_port.vid,
                        *com_port.pid);
        }
    }
    return 0;
}
