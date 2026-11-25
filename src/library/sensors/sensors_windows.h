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
 * @file sensors_windows.h
 * @brief (HW_SENSORS: AUTO on Windows) Reads the same information as psutil on Windows, from the Windows API: system
 * times, power information, PDH counters, memory status, SMBIOS table, disk space and network interfaces. NVIDIA GPUs
 * through NVML (sensors_python::Gpu). Temperatures and fans are not available (NaN).
 */

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "library/sensors/sensors.h"

namespace sensors_windows {

/**
 * @brief Split an SMBIOS table (the structures one after the other) into its structures: each one is its formatted
 * part, its strings and the double NUL ending them, like the raw files of /sys/firmware/dmi/entries on Linux. Stops at
 * the end-of-table structure (type 127) or at the first truncated structure.
 */
std::vector<std::vector<uint8_t>> smbios_structures(const std::vector<uint8_t> &table);

/**
 * @brief Emulation of the 1, 5 and 15 minutes load averages, which Windows does not provide, like psutil: exponentially
 * damped moving averages of the processor queue length.
 */
class LoadAverage
{
public:
    /**
     * @brief Add a sample of the processor queue length.
     * @param queue_length Number of threads waiting for a processor (and running ones).
     * @param elapsed_s Time since the previous sample, in seconds.
     */
    void add_sample(double queue_length, double elapsed_s);

    /** @brief The 1, 5 and 15 minutes load averages. */
    const std::array<double, 3> &values() const { return averages; }

private:
    std::array<double, 3> averages{};
};

#ifdef _WIN32

/** @brief CPU sensors, read from the Windows API. Temperature and fan speed are not available (NaN). */
class Cpu : public sensors::Cpu
{
public:
    /** @brief CPU usage, from the system times. */
    double percentage(std::optional<double> interval) override;
    /** @brief Average current frequency of the processors, from the power information. */
    double frequency() override;
    /** @brief CPU model name, from the registry. */
    std::string model() override;
    /** @brief Emulated load averages (processor queue length). */
    std::array<double, 3> load() override;
    /** @brief Not available on Windows yet: NaN. */
    double temperature() override;
    /** @brief Not available on Windows yet: NaN. */
    double fan_percent(const std::optional<std::string> &fan_name = std::nullopt) override;

private:
    std::mutex mutex;
    bool has_last_times = false;
    double last_total = 0, last_busy = 0;
    LoadAverage load_average;
    std::optional<double> last_load_sample;  ///< Time of the previous load sample (seconds since the system started)
};

/** @brief Memory sensors, read from the Windows API. */
class Memory : public sensors::Memory
{
public:
    /** @brief Paging file usage, in percent. */
    double swap_percent() override;
    /** @brief RAM usage, in percent. */
    double virtual_percent() override;
    /** @brief RAM used, in bytes. */
    int64_t virtual_used() override;
    /** @brief RAM available, in bytes. */
    int64_t virtual_free() override;
    /** @brief Model of the first populated RAM module, from the SMBIOS table. */
    std::string model() override;
};

/** @brief Disk sensors, for the system drive (C:\ usually). */
class Disk : public sensors::Disk
{
public:
    /** @brief Disk usage, in percent. */
    double disk_usage_percent() override;
    /** @brief Disk used, in bytes. */
    int64_t disk_used() override;
    /** @brief Disk free for the user, in bytes. */
    int64_t disk_free() override;
};

/** @brief Network sensors. The interfaces are named like in the Windows settings ("Ethernet", "Wi-Fi"...). */
class Net : public sensors::Net
{
public:
    /** @brief Network statistics of the interface if_name. */
    Stats stats(const std::string &if_name, std::optional<double> interval) override;

private:
    struct Counters
    {
        int64_t bytes_sent;
        int64_t bytes_recv;
    };

    std::mutex mutex;
    std::map<std::string, Counters> pnic_before;
};

/** @brief Create the sensors reading the Windows API. */
sensors::Sensors create();

#endif

}  // namespace sensors_windows
