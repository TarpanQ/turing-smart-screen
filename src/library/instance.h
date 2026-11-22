// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
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
 * @file instance.h
 * @brief Single instance of the system monitor: only one process drives the screens at a time.
 */

#pragma once

#include <chrono>
#include <string>

namespace instance {

/** @brief Path of the lock file of the system monitor (in XDG_RUNTIME_DIR, or /tmp) */
std::string lock_path();

/**
 * @brief Take the lock for the whole run of the process; the PID of the process is written in the lock file.
 * Returns the file descriptor of the lock (keep it open), -1 if another instance is already running, or -2 if the lock
 * file cannot be used (the program then runs anyway).
 */
int acquire_lock();

/**
 * @brief Stop the running instance (SIGTERM, so that it turns the screens off) and wait until it has exited.
 * Returns true if no instance is running anymore.
 */
bool stop_running(std::chrono::seconds timeout);

}  // namespace instance
