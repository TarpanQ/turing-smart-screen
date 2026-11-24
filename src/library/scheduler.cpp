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
 * @file scheduler.cpp
 */

#include "library/scheduler.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#include <pthread.h>

#include "library/config.h"
#include "library/display.h"
#include "library/log.h"
#include "library/stats.h"

namespace scheduler {

std::atomic<bool> STOPPING = false;

namespace {

// Used to wake up the sleeping jobs when the program stops
std::mutex stop_mutex;
std::condition_variable stop_condition;

void set_thread_name(const std::string &threadname)
{
    // Linux thread names are limited to 15 characters
    pthread_setname_np(pthread_self(), threadname.substr(0, 15).c_str());
}

// Interval (seconds) of a stats item, or of a category if no subkey: the INTERVAL of the category in the theme, unless
// the item sets its own (see stats::item_interval). A job runs for every display, so the shortest interval of the
// themes of the displays is used (0 if no display shows the item).
double theme_interval(const std::string &key, const std::string &subkey = "")
{
    return subkey.empty() ? stats::shared_interval({"STATS", key}) : stats::shared_interval({"STATS", key, subkey});
}

}  // namespace

void schedule(const std::string &threadname, double interval_s, std::function<void()> func)
{
    if (interval_s == 0) {
        return;
    }

    std::thread([threadname, interval_s, func = std::move(func)] {
        set_thread_name(threadname);
        const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(interval_s));
        auto next_run = std::chrono::steady_clock::now();
        while (!STOPPING) {
            // The next execution is scheduled before running the job, so the period does not depend on the job duration
            next_run += interval;
            try {
                func();
            } catch (const std::exception &e) {
                logger.error("Error in {}: {}", threadname, e.what());
            }

            // If the job took longer than its interval, it is executed again right now
            const auto now = std::chrono::steady_clock::now();
            if (next_run < now) {
                next_run = now;
            }
            std::unique_lock lock(stop_mutex);
            stop_condition.wait_until(lock, next_run, [] { return STOPPING.load(); });
        }
    }).detach();
}

void stop()
{
    {
        std::lock_guard lock(stop_mutex);
        STOPPING = true;
    }
    stop_condition.notify_all();
}

/** @brief Periodic job updating the CPU usage on the display. */
void CPUPercentage()
{
    // Refresh the CPU Percentage
    schedule("CPU_Percentage", theme_interval("CPU", "PERCENTAGE"), stats::CPU::percentage);
}

/** @brief Periodic job updating the CPU frequency on the display. */
void CPUFrequency()
{
    // Refresh the CPU Frequency
    schedule("CPU_Frequency", theme_interval("CPU", "FREQUENCY"), stats::CPU::frequency);
}

/** @brief Periodic job updating the CPU load on the display. */
void CPULoad()
{
    // Refresh the CPU Load
    schedule("CPU_Load", theme_interval("CPU", "LOAD"), stats::CPU::load);
}

/** @brief Periodic job updating the CPU temperature on the display. */
void CPUTemperature()
{
    // Refresh the CPU Temperature
    schedule("CPU_Temperature", theme_interval("CPU", "TEMPERATURE"), stats::CPU::temperature);
}

/** @brief Periodic job updating the CPU fan speed on the display. */
void CPUFanSpeed()
{
    // Refresh the CPU Fan Speed
    schedule("CPU_FanSpeed", theme_interval("CPU", "FAN_SPEED"), stats::CPU::fan_speed);
}

/** @brief Periodic job updating the CPU model name on the display. */
void CPUModel()
{
    schedule("CPU_Model", theme_interval("CPU", "MODEL"), stats::CPU::model);
}

/** @brief Periodic job updating the GPU statistics on the display. */
void GpuStats()
{
    // Refresh the GPU Stats
    schedule("GPU_Stats", theme_interval("GPU"), stats::Gpu::stats);
}

/** @brief Periodic job updating the GPU model name on the display. */
void GpuModel()
{
    schedule("GPU_Model", theme_interval("GPU", "MODEL"), stats::Gpu::model);
}

/** @brief Periodic job updating the memory statistics on the display. */
void MemoryStats()
{
    schedule("Memory_Stats", theme_interval("MEMORY"), stats::Memory::stats);
}

/** @brief Periodic job updating the RAM module model on the display. */
void MemoryModel()
{
    schedule("Memory_Model", theme_interval("MEMORY", "MODEL"), stats::Memory::model);
}

/** @brief Periodic job updating the disk statistics on the display. */
void DiskStats()
{
    schedule("Disk_Stats", theme_interval("DISK"), stats::Disk::stats);
}

/** @brief Periodic job updating the network statistics on the display. */
void NetStats()
{
    schedule("Net_Stats", theme_interval("NET"), stats::Net::stats);
}

/** @brief Periodic job updating the date and time on the display. */
void DateStats()
{
    schedule("Date_Stats", theme_interval("DATE"), stats::Date::stats);
}

/** @brief Periodic job updating the system uptime on the display. */
void SystemUptimeStats()
{
    schedule("SystemUptime_Stats", theme_interval("UPTIME"), stats::SystemUptime::stats);
}

/** @brief Periodic job updating the custom data sources on the display. */
void CustomStats()
{
    schedule("Custom_Stats", theme_interval("CUSTOM"), stats::Custom::stats);
}

/** @brief Periodic job updating the weather on the display. */
void WeatherStats()
{
    // Weather data is refreshed at most every 5 minutes
    schedule("Weather_Stats", std::max(300.0, theme_interval("WEATHER")), stats::Weather::stats);
}

/** @brief Periodic job updating the ping on the display. */
void PingStats()
{
    schedule("Ping_Stats", theme_interval("PING"), stats::Ping::stats);
}

void QueueHandler(UpdateQueue &queue)
{
    std::thread([&queue] {
        set_thread_name("Queue_Handler");
        auto execute = [](const UpdateQueue::Request &request) {
            if (!request) {
                return;
            }
            try {
                request();
            } catch (const std::exception &e) {
                logger.error("Error sending data to the display: {}", e.what());
            }
        };

        while (true) {
            if (STOPPING) {
                // Empty the action queue to allow program to exit cleanly
                while (const auto request = queue.get(std::chrono::milliseconds(0))) {
                    execute(*request);
                }
                break;
            }
            // Execute first action in the queue
            if (const auto request = queue.get(std::chrono::milliseconds(100))) {
                execute(*request);
            }
        }
    }).detach();
}

/** @brief True if the display queue has no pending request. */
bool is_queue_empty()
{
    return std::all_of(displays().begin(), displays().end(),
                       [](const std::unique_ptr<Display> &display) { return display->update_queue.empty(); });
}

}  // namespace scheduler
