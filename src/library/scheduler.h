// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
//
// Copyright (C) 2021 Matthieu Houdebine (mathoudebine)
// Copyright (C) 2022 Rollbacke
// Copyright (C) 2022 Ebag333
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
 * @file scheduler.h
 * @brief Periodic jobs refreshing the stats, each one in its own thread, and the queue handler thread that sends the requests to the screen one after the other.
 *
 * Each job function starts its thread and returns immediately. A job whose interval is 0 in the theme is not started.
 */

#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "library/lcd/lcd_comm.h"

namespace scheduler {

/** @brief Set when the program is stopping: periodic jobs are not rescheduled, the queue handler empties the queue */
extern std::atomic<bool> STOPPING;

/**
 * @brief Run func every interval_s seconds in a new thread named threadname.
 * Exceptions thrown by func are logged, the job keeps running. Nothing is started if interval_s is 0.
 */
void schedule(const std::string &threadname, double interval_s, std::function<void()> func);

/** @brief Ask all periodic jobs to stop, and wake them up if they are waiting */
void stop();

void CPUPercentage();
void CPUFrequency();
void CPULoad();
void CPUTemperature();
void CPUFanSpeed();
void CPUModel();
void GpuStats();
void GpuModel();
void MemoryStats();
void MemoryModel();
void DiskStats();
void NetStats();
void DateStats();
void SystemUptimeStats();
void CustomStats();
void WeatherStats();
void PingStats();

/** @brief Start the thread executing the requests waiting in a display queue, in order (until stop()) */
void QueueHandler(UpdateQueue &queue);

/** @brief True if no display queue has a pending request. */
bool is_queue_empty();

}  // namespace scheduler
